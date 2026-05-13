#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Honeycomb Authors <support@honeycomb.io>
# SPDX-License-Identifier: Apache-2.0

"""
Monkey patch for OpenTelemetry Python API to use C++ context.

This lighter approach patches just the span-related context methods,
allowing Python instrumentations to work with C++ spans while keeping
the implementation simple.
"""

import threading

try:
    from opentelemetry import trace
    import opentelemetry.context as _otel_context_api
    from opentelemetry.trace.propagation import _SPAN_KEY
    OTEL_AVAILABLE = True
except ImportError:
    OTEL_AVAILABLE = False
    _SPAN_KEY = None
    print("Warning: opentelemetry-api not installed. Monkey patching will have no effect.")

import honeycomb_pycpp


# Store original implementations
_original_implementations = {}
_patched = False
_patch_lock = threading.Lock()

# Maps Python context token id -> C++ token, for context.attach/detach bridging
_token_to_cpp_token = {}
_token_lock = threading.Lock()


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
        cpp_context = honeycomb_pycpp.Context.get_current()
        span = cpp_context.get_span()
        if span and span.is_recording():
            return span
    except Exception as e:
        pass

    # Return non-recording span if nothing active
    return trace.INVALID_SPAN if OTEL_AVAILABLE else None


def _context_get_current_cpp():
    """
    Wraps opentelemetry.context.get_current() to inject the active C++ span.

    Without this, code that builds new contexts from get_current() (e.g.
    set_value calls for baggage) would produce a stale base when a C++ span
    was activated via start_as_current_span, which bypasses Python's
    context.attach.
    """
    ctx = _original_implementations['context_get_current']()
    try:
        cpp_span = honeycomb_pycpp.Context.get_current().get_span()
        if cpp_span and cpp_span.is_recording():
            ctx = _original_implementations['context_set_value'](_SPAN_KEY, cpp_span, ctx)
    except Exception:
        pass
    return ctx


def _context_get_value_cpp(key, context=None):
    """
    Wraps opentelemetry.context.get_value() to read from a C++-aware current context.
    """
    if context is None:
        context = _context_get_current_cpp()
    return _original_implementations['context_get_value'](key, context)


def _context_set_value_cpp(key, value, context=None):
    """
    Wraps opentelemetry.context.set_value() to use a C++-aware base when none is given.
    """
    if context is None:
        context = _context_get_current_cpp()
    return _original_implementations['context_set_value'](key, value, context)


def _context_attach_cpp(ctx):
    """
    Wraps opentelemetry.context.attach() to mirror the attached span into C++ context.

    Handles three cases:
    - C++ recording span (has get_context()): attach via its own context so the
      recording span itself lands in C++ runtime context, not a non-recording copy.
      If the span is already the active C++ span (e.g. because get_current() injected
      it), skip the C++ attach to avoid double-attach.
    - Python OTel span (has get_span_context() only): bridge via create_with_span_context.
    - No span / invalid span: no-op on the C++ side.
    """
    token = _original_implementations['context_attach'](ctx)

    try:
        # Use get_value directly so C++ spans aren't filtered out by the
        # isinstance(span, Span) check inside the Python get_current_span.
        span = _original_implementations['context_get_value'](_SPAN_KEY, ctx) if _SPAN_KEY else None

        if span is not None:
            cpp_ctx = None
            if hasattr(span, 'get_context'):
                current_cpp_span = honeycomb_pycpp.Context.get_current().get_span()
                already_active = (
                    current_cpp_span is not None
                    and current_cpp_span.is_recording()
                    and current_cpp_span.get_span_context().span_id
                        == span.get_span_context().span_id
                )
                if not already_active:
                    cpp_ctx = span.get_context()
            elif hasattr(span, 'get_span_context'):
                span_ctx = span.get_span_context()
                if span_ctx and span_ctx.is_valid:
                    cpp_ctx = honeycomb_pycpp.Context.create_with_span_context(
                        format(span_ctx.trace_id, '032x'),
                        format(span_ctx.span_id, '016x'),
                        int(span_ctx.trace_flags),
                        is_remote=span_ctx.is_remote,
                    )

            if cpp_ctx:
                cpp_token = cpp_ctx.attach()
                with _token_lock:
                    _token_to_cpp_token[id(token)] = cpp_token
    except Exception:
        pass

    return token


def _context_detach_cpp(token):
    """
    Wraps opentelemetry.context.detach() to also detach the mirrored C++ context.
    """
    cpp_token = None
    with _token_lock:
        cpp_token = _token_to_cpp_token.pop(id(token), None)

    if cpp_token is not None:
        try:
            honeycomb_pycpp.Context.detach(cpp_token)
        except Exception:
            pass

    _original_implementations['context_detach'](token)


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
        return False

    with _patch_lock:
        if _patched:
            return True

        try:
            # Patch get_current_span
            _original_implementations['get_current_span'] = trace.get_current_span
            trace.get_current_span = _get_current_span_from_cpp

            # Patch context.get_current / get_value / set_value so that Python code
            # which reads or builds from the current context sees the active C++ span,
            # even when it was activated via start_as_current_span (which bypasses
            # Python's context.attach).
            # Save set_value first: _context_get_current_cpp calls it internally.
            _original_implementations['context_set_value'] = _otel_context_api.set_value
            _otel_context_api.set_value = _context_set_value_cpp
            _original_implementations['context_get_value'] = _otel_context_api.get_value
            _otel_context_api.get_value = _context_get_value_cpp
            _original_implementations['context_get_current'] = _otel_context_api.get_current
            _otel_context_api.get_current = _context_get_current_cpp

            # Patch context.attach / context.detach so instrumentations that call
            # these directly (e.g. gRPC) also propagate spans into C++ context.
            _original_implementations['context_attach'] = _otel_context_api.attach
            _otel_context_api.attach = _context_attach_cpp
            _original_implementations['context_detach'] = _otel_context_api.detach
            _otel_context_api.detach = _context_detach_cpp

            _patched = True
            return True

        except Exception as e:
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

            if 'context_get_current' in _original_implementations:
                _otel_context_api.get_current = _original_implementations['context_get_current']

            if 'context_get_value' in _original_implementations:
                _otel_context_api.get_value = _original_implementations['context_get_value']

            if 'context_set_value' in _original_implementations:
                _otel_context_api.set_value = _original_implementations['context_set_value']

            if 'context_attach' in _original_implementations:
                _otel_context_api.attach = _original_implementations['context_attach']

            if 'context_detach' in _original_implementations:
                _otel_context_api.detach = _original_implementations['context_detach']

            _original_implementations.clear()
            _patched = False

        except Exception as e:
            pass


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
