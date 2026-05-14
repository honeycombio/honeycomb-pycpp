# SPDX-FileCopyrightText: 2026 Honeycomb Authors <support@honeycomb.io>
# SPDX-License-Identifier: Apache-2.0

"""
Tests for patch_api.py — specifically the context.attach/detach bridging that
allows instrumentations like gRPC to propagate span context into C++ context.
"""

import pytest
import honeycomb_pycpp
import opentelemetry.context as otel_context_api
from opentelemetry import context, trace
from opentelemetry.trace import NonRecordingSpan, SpanContext, TraceFlags
from opentelemetry.trace.propagation import _SPAN_KEY

from honeycomb.pycpp.distro import patch_api


TRACE_ID = 0x4BF92F3577B34DA6A3CE929D0E0E4736
SPAN_ID = 0x00F067AA0BA902B7
TRACE_ID_HEX = format(TRACE_ID, "032x")
SPAN_ID_HEX = format(SPAN_ID, "016x")


@pytest.fixture(scope="module")
def provider():
    sdk = honeycomb_pycpp.SDK("./tests/testdata/otel.yaml")
    yield sdk.tracer_provider
    sdk.shutdown()


@pytest.fixture(scope="module")
def tracer(provider):
    return provider.get_tracer("test-patch-api")


@pytest.fixture(autouse=True)
def apply_patch():
    patch_api.patch()
    yield
    patch_api.unpatch()


def _make_python_context(trace_id=TRACE_ID, span_id=SPAN_ID, is_remote=True, sampled=True):
    """Build a Python OTel Context containing a NonRecordingSpan, as propagation extraction would."""
    flags = TraceFlags(TraceFlags.SAMPLED if sampled else 0)
    span_ctx = SpanContext(trace_id=trace_id, span_id=span_id, is_remote=is_remote, trace_flags=flags)
    span = NonRecordingSpan(span_ctx)
    return trace.set_span_in_context(span)


class TestContextAttachDetachBridging:
    def test_attach_mirrors_span_into_cpp_context(self):
        """After context.attach(ctx), the C++ runtime context contains the span."""
        py_ctx = _make_python_context()
        token = context.attach(py_ctx)
        try:
            cpp_ctx = honeycomb_pycpp.Context.get_current()
            span = cpp_ctx.get_span()
            assert span is not None
            sc = span.get_span_context()
            assert format(sc.trace_id, "032x") == TRACE_ID_HEX
            assert format(sc.span_id, "016x") == SPAN_ID_HEX
        finally:
            context.detach(token)

    def test_detach_removes_span_from_cpp_context(self):
        """After context.detach(), the C++ runtime context no longer has the attached span."""
        # Capture the span id that was active before (if any)
        before = honeycomb_pycpp.Context.get_current().get_span()
        before_id = before.get_span_context().span_id if before else None

        py_ctx = _make_python_context()
        token = context.attach(py_ctx)
        context.detach(token)

        after = honeycomb_pycpp.Context.get_current().get_span()
        after_id = after.get_span_context().span_id if after else None
        assert after_id == before_id

    def test_attach_preserves_trace_flags_sampled(self):
        py_ctx = _make_python_context(sampled=True)
        token = context.attach(py_ctx)
        try:
            sc = honeycomb_pycpp.Context.get_current().get_span().get_span_context()
            assert sc.trace_flags.sampled is True
        finally:
            context.detach(token)

    def test_attach_preserves_trace_flags_not_sampled(self):
        py_ctx = _make_python_context(sampled=False)
        token = context.attach(py_ctx)
        try:
            sc = honeycomb_pycpp.Context.get_current().get_span().get_span_context()
            assert sc.trace_flags.sampled is False
        finally:
            context.detach(token)

    def test_attach_preserves_is_remote(self):
        py_ctx = _make_python_context(is_remote=True)
        token = context.attach(py_ctx)
        try:
            sc = honeycomb_pycpp.Context.get_current().get_span().get_span_context()
            assert sc.is_remote is True
        finally:
            context.detach(token)

    def test_nested_attach_detach(self):
        """Nested attach/detach restores the outer span correctly."""
        outer_span_id = 0xAABBCCDDEEFF0011
        inner_span_id = 0x1122334455667788

        outer_ctx = _make_python_context(span_id=outer_span_id)
        inner_ctx = _make_python_context(span_id=inner_span_id)

        outer_token = context.attach(outer_ctx)
        inner_token = context.attach(inner_ctx)

        sc = honeycomb_pycpp.Context.get_current().get_span().get_span_context()
        assert sc.span_id == inner_span_id

        context.detach(inner_token)

        sc = honeycomb_pycpp.Context.get_current().get_span().get_span_context()
        assert sc.span_id == outer_span_id

        context.detach(outer_token)

    def test_attach_empty_context_does_not_raise(self):
        """Attaching a context with no span should not raise."""
        empty_ctx = context.get_current()  # current context, likely no span
        token = context.attach(empty_ctx)
        context.detach(token)

    def test_child_span_picks_up_propagated_parent(self, tracer):
        """A C++ span started after context.attach() uses the propagated span as parent."""
        py_ctx = _make_python_context()
        token = context.attach(py_ctx)
        try:
            child = tracer.start_span("grpc-child")
            child_sc = child.get_span_context()
            assert format(child_sc.trace_id, "032x") == TRACE_ID_HEX
            child.end()
        finally:
            context.detach(token)


class TestUseSpan:
    def test_use_span_activates_cpp_span(self, tracer):
        """use_span makes the C++ span visible via get_current_span."""
        span = tracer.start_span("use-span-test")
        with trace.use_span(span, end_on_exit=True):
            current = trace.get_current_span()
            assert current is not None
            assert current.is_recording()
            assert current.get_span_context().span_id == span.get_span_context().span_id

    def test_use_span_cpp_context_contains_span(self, tracer):
        """After use_span entry the C++ runtime context holds the recording span."""
        span = tracer.start_span("use-span-cpp-ctx")
        with trace.use_span(span, end_on_exit=True):
            cpp_span = honeycomb_pycpp.Context.get_current().get_span()
            assert cpp_span is not None
            assert cpp_span.is_recording()
            assert cpp_span.get_span_context().span_id == span.get_span_context().span_id

    def test_use_span_clears_cpp_context_on_exit(self, tracer):
        before = honeycomb_pycpp.Context.get_current().get_span()
        before_id = before.get_span_context().span_id if before else None

        span = tracer.start_span("use-span-cleanup")
        with trace.use_span(span, end_on_exit=True):
            pass

        after = honeycomb_pycpp.Context.get_current().get_span()
        after_id = after.get_span_context().span_id if after else None
        assert after_id == before_id

    def test_child_span_inside_use_span_has_correct_parent(self, tracer):
        parent = tracer.start_span("use-span-parent")
        with trace.use_span(parent, end_on_exit=True):
            child = tracer.start_span("use-span-child")
            child_sc = child.get_span_context()
            parent_sc = parent.get_span_context()
            assert format(child_sc.trace_id, "032x") == format(parent_sc.trace_id, "032x")
            child.end()


class TestContextAPIStaleness:
    """
    Tests that context.get_current / get_value / set_value return C++-aware state
    even when the active span was set via start_as_current_span (which bypasses
    Python's context.attach and leaves the Python thread-local stale).
    """

    def test_get_current_includes_cpp_span(self, tracer):
        with tracer.start_as_current_span("stale-get-current") as span:
            ctx = otel_context_api.get_current()
            stored = otel_context_api.get_value(_SPAN_KEY, ctx)
            assert stored is not None
            assert stored.get_span_context().span_id == span.get_span_context().span_id

    def test_get_value_returns_cpp_span(self, tracer):
        with tracer.start_as_current_span("stale-get-value") as span:
            stored = otel_context_api.get_value(_SPAN_KEY)
            assert stored is not None
            assert stored.get_span_context().span_id == span.get_span_context().span_id

    def test_set_value_preserves_cpp_span(self, tracer):
        """set_value(key, val) with no explicit context should include the active C++ span."""
        with tracer.start_as_current_span("stale-set-value") as span:
            new_ctx = otel_context_api.set_value("some_key", "some_val")
            stored = otel_context_api.get_value(_SPAN_KEY, new_ctx)
            assert stored is not None
            assert stored.get_span_context().span_id == span.get_span_context().span_id

    def test_attach_set_value_context_does_not_break_cpp_context(self, tracer):
        """Attaching a context built with set_value (no explicit base) should not displace
        the active C++ span, and child spans should still be parented correctly."""
        with tracer.start_as_current_span("outer") as outer:
            # Simulate instrumentation adding baggage
            baggage_ctx = otel_context_api.set_value("baggage_key", "baggage_val")
            token = otel_context_api.attach(baggage_ctx)
            try:
                child = tracer.start_span("child-under-baggage")
                child_sc = child.get_span_context()
                outer_sc = outer.get_span_context()
                assert format(child_sc.trace_id, "032x") == format(outer_sc.trace_id, "032x")
                child.end()
            finally:
                otel_context_api.detach(token)

    def test_get_current_returns_no_span_when_none_active(self):
        """When no C++ span is active, get_current should not inject a stale span."""
        ctx = otel_context_api.get_current()
        stored = otel_context_api.get_value(_SPAN_KEY, ctx)
        assert stored is None or not stored.get_span_context().is_valid
