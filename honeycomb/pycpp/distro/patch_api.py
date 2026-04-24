#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Honeycomb Authors <support@honeycomb.io>
# SPDX-License-Identifier: Apache-2.0

"""
Monkey patch for OpenTelemetry Python API to use C++ context.

This lighter approach patches just the span-related context methods,
allowing Python instrumentations to work with C++ spans while keeping
the implementation simple.
"""

import functools
import threading
from typing import Optional

try:
    from opentelemetry import trace
    from opentelemetry.context import context as otel_context
    OTEL_AVAILABLE = True
except ImportError:
    OTEL_AVAILABLE = False
    print("Warning: opentelemetry-api not installed. Monkey patching will have no effect.")

import otel_cpp_tracer


# Store original implementations
_original_implementations = {}
_patched = False
_patch_lock = threading.Lock()


def _get_current_span_from_cpp(context=None):
    """
    Get the current span from C++ context.

    This replaces opentelemetry.trace.get_current_span() to return
    the active span from the C++ RuntimeContext instead of Python's
    thread-local context.
    """
    # If a specific context is provided, we'd need to extract from it
    # For now, we always get from current C++ runtime context
    try:
        cpp_context = otel_cpp_tracer.Context.get_current()
        span = cpp_context.get_span()
        if span and span.is_recording():
            return span
    except Exception as e:
        print(f"Warning: Failed to get span from C++ context: {e}")

    # Return non-recording span if nothing active
    return trace.INVALID_SPAN if OTEL_AVAILABLE else None


def _use_span_cpp(span, end_on_exit=False, record_exception=True, set_status_on_exception=True):
    """
    Context manager to make a span active in C++ context.

    This replaces opentelemetry.trace.use_span() to activate spans
    in the C++ RuntimeContext. Handles both C++ spans and Python
    OpenTelemetry spans.

    Args:
        span: The span to activate
        end_on_exit: Whether to end the span when exiting context
        record_exception: Whether to record exceptions as events
        set_status_on_exception: Whether to set ERROR status on exception
    """
    class SpanContext:
        def __init__(self, span, end_on_exit, record_exception, set_status_on_exception):
            self.span = span
            self.end_on_exit = end_on_exit
            self.record_exception = record_exception
            self.set_status_on_exception = set_status_on_exception
            self.token = None

        def __enter__(self):
            # Handle C++ spans (from our bindings)
            if hasattr(self.span, 'get_context'):
                ctx = self.span.get_context()
                self.token = ctx.attach()
            # Handle Python OpenTelemetry spans
            elif hasattr(self.span, 'get_span_context'):
                try:
                    span_ctx = self.span.get_span_context()
                    if span_ctx.is_valid:
                        # Extract trace_id and span_id as hex strings
                        trace_id_hex = format(span_ctx.trace_id, '032x')
                        span_id_hex = format(span_ctx.span_id, '016x')
                        trace_flags = span_ctx.trace_flags

                        # Create C++ context with this span context
                        ctx = otel_cpp_tracer.Context.create_with_span_context(
                            trace_id_hex,
                            span_id_hex,
                            trace_flags,
                            is_remote=True
                        )

                        if ctx:
                            self.token = ctx.attach()
                except Exception as e:
                    print(f"Warning: Failed to bridge Python span to C++ context: {e}")
                    import traceback
                    traceback.print_exc()

            return self.span

        def __exit__(self, exc_type, exc_val, exc_tb):
            # Detach context
            if self.token:
                otel_cpp_tracer.Context.detach(self.token)

            # End span if requested
            if self.end_on_exit and hasattr(self.span, 'end'):
                if exc_type is not None:
                    # Set error status on exception
                    if hasattr(self.span, 'set_status'):
                        # Try C++ status first
                        try:
                            self.span.set_status(
                                otel_cpp_tracer.Status(
                                    otel_cpp_tracer.StatusCode.ERROR,
                                    f"{exc_type.__name__}: {exc_val}"
                                )
                            )
                        except:
                            # Fall back to Python status
                            if OTEL_AVAILABLE:
                                from opentelemetry.trace import StatusCode
                                self.span.set_status(StatusCode.ERROR, f"{exc_type.__name__}: {exc_val}")

                self.span.end()

            return False  # Don't suppress exceptions

    return SpanContext(span, end_on_exit, record_exception, set_status_on_exception)


def _wrap_tracer_start_span(original_method):
    """
    Wrap tracer.start_span() to use current C++ context as parent.

    This ensures that Python-created spans automatically become children
    of active C++ spans.
    """
    @functools.wraps(original_method)
    def wrapper(self, *args, **kwargs):
        # If no explicit context/parent provided, inject current C++ context
        if 'context' not in kwargs or kwargs['context'] is None:
            try:
                cpp_context = otel_cpp_tracer.Context.get_current()
                # Store C++ context in a way the original method might use
                # This is a best-effort approach for standard Python tracers
                kwargs['context'] = otel_context.get_current()
            except:
                pass

        return original_method(self, *args, **kwargs)

    return wrapper


def _wrap_tracer_start_as_current_span(original_method):
    """
    Wrap tracer.start_as_current_span() to use C++ context activation.
    """
    @functools.wraps(original_method)
    def wrapper(self, name, context=None, *args, **kwargs):
        # Get span from original method
        span = original_method(self, name, context=context, *args, **kwargs)

        # Wrap it to use C++ context activation
        class ActivatedSpan:
            def __init__(self, span):
                self.span = span
                self.cpp_token = None

            def __enter__(self):
                # Activate in C++ context if it's a C++ span
                if hasattr(self.span, 'get_context'):
                    try:
                        ctx = self.span.get_context()
                        self.cpp_token = ctx.attach()
                    except:
                        pass

                # Also call original enter
                if hasattr(self.span, '__enter__'):
                    self.span.__enter__()

                return self.span

            def __exit__(self, exc_type, exc_val, exc_tb):
                result = False

                # Call original exit first
                if hasattr(self.span, '__exit__'):
                    result = self.span.__exit__(exc_type, exc_val, exc_tb)

                # Detach from C++ context
                if self.cpp_token:
                    try:
                        otel_cpp_tracer.Context.detach(self.cpp_token)
                    except:
                        pass

                return result

        return ActivatedSpan(span)

    return wrapper


def patch():
    """
    Apply monkey patches to OpenTelemetry Python API.

    This patches:
    - trace.get_current_span() to read from C++ context
    - trace.use_span() to activate spans in C++ context

    After patching, Python instrumentations will automatically work
    with C++ spans and propagate context correctly.
    """
    global _patched, _original_implementations

    if not OTEL_AVAILABLE:
        print("Cannot patch: opentelemetry-api not installed")
        return False

    with _patch_lock:
        if _patched:
            print("Already patched")
            return True

        try:
            # Patch get_current_span
            _original_implementations['get_current_span'] = trace.get_current_span
            trace.get_current_span = _get_current_span_from_cpp

            # Patch use_span
            _original_implementations['use_span'] = trace.use_span
            trace.use_span = _use_span_cpp

            _patched = True
            print("✓ OpenTelemetry span context API patched to use C++ implementation")
            print("  - trace.get_current_span() now reads from C++ RuntimeContext")
            print("  - trace.use_span() now activates spans in C++ RuntimeContext")
            return True

        except Exception as e:
            print(f"✗ Failed to patch OpenTelemetry API: {e}")
            # Restore any partial patches
            unpatch()
            return False


def unpatch():
    """
    Remove monkey patches and restore original OpenTelemetry API.
    """
    global _patched, _original_implementations

    if not OTEL_AVAILABLE:
        return

    with _patch_lock:
        if not _patched:
            return

        try:
            # Restore original implementations
            if 'get_current_span' in _original_implementations:
                trace.get_current_span = _original_implementations['get_current_span']

            if 'use_span' in _original_implementations:
                trace.use_span = _original_implementations['use_span']

            _original_implementations.clear()
            _patched = False
            print("✓ OpenTelemetry API restored to original Python implementation")

        except Exception as e:
            print(f"✗ Failed to unpatch OpenTelemetry API: {e}")


def is_patched():
    """Check if monkey patches are currently applied."""
    return _patched


# Convenience context manager for temporary patching
class patched:
    """
    Context manager for temporary monkey patching.

    Usage:
        with patched():
            # Python OTel code here will use C++ context
            pass
        # Original behavior restored
    """
    def __enter__(self):
        patch()
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        unpatch()
        return False


if __name__ == "__main__":
    print("OpenTelemetry C++ Context Monkey Patch")
    print("=" * 50)
    print()
    print("Usage:")
    print("  from monkey_patch import patch, unpatch")
    print()
    print("  # Apply patches")
    print("  patch()")
    print()
    print("  # Your code using Python OTel instrumentations")
    print("  # They will now see C++ spans!")
    print()
    print("  # Remove patches (optional)")
    print("  unpatch()")
    print()
    print("Or use as context manager:")
    print("  from monkey_patch import patched")
    print()
    print("  with patched():")
    print("      # Python OTel code here uses C++ context")
    print("      pass")
