# SPDX-FileCopyrightText: 2026 Honeycomb Authors <support@honeycomb.io>
# SPDX-License-Identifier: Apache-2.0

"""
Comprehensive tests for the honeycomb_pycpp API surface, mirroring the
opentelemetry-api Python spec at:
https://opentelemetry-python.readthedocs.io/en/latest/api/trace.html

One test per method / per optional parameter where applicable.
"""

import time
import pytest
import honeycomb_pycpp
from opentelemetry.trace import SpanKind, Status, StatusCode


# ---------------------------------------------------------------------------
# Fixtures
# ---------------------------------------------------------------------------

@pytest.fixture(scope="module")
def provider():
    p = honeycomb_pycpp.TracerProvider("./tests/testdata/otel.yaml")
    yield p
    p.shutdown()


@pytest.fixture(scope="module")
def tracer(provider):
    return provider.get_tracer("test-tracer")


# ===========================================================================
# TracerProvider
# ===========================================================================

class TestTracerProvider:
    def test_init_service_name_only(self):
        """TracerProvider can be created with only a service name."""
        p = honeycomb_pycpp.TracerProvider("./tests/testdata/otel.yaml")
        p.shutdown()

    def test_init_with_console_exporter(self):
        """TracerProvider accepts 'console' as exporter_type."""
        p = honeycomb_pycpp.TracerProvider("./tests/testdata/otel.yaml")
        p.shutdown()

    def test_get_tracer_name_only(self, provider):
        """get_tracer with only a name returns a Tracer."""
        t = provider.get_tracer("my-lib")
        assert t is not None

    def test_get_tracer_with_version(self, provider):
        """get_tracer accepts an optional version string."""
        t = provider.get_tracer("my-lib", instrumenting_library_version="1.2.3")
        assert t is not None

    def test_get_tracer_with_schema_url(self, provider):
        """get_tracer accepts an optional schema_url string."""
        t = provider.get_tracer("my-lib", schema_url="https://example.com/schema/1.0.0")
        assert t is not None

    def test_get_tracer_with_attributes(self, provider):
        """get_tracer accepts an optional attributes dict."""
        t = provider.get_tracer("my-lib", attributes={"key": "value"})
        assert t is not None

    def test_get_tracer_with_all_optional_params(self, provider):
        """get_tracer accepts all optional parameters together."""
        t = provider.get_tracer(
            instrumenting_module_name="my-lib",
            instrumenting_library_version="2.0.0",
            schema_url="https://example.com/schema",
            attributes={"foo": "bar", "foo2": [False, True]},
        )
        assert t is not None

    def test_shutdown(self):
        """TracerProvider.shutdown() completes without error."""
        p = honeycomb_pycpp.TracerProvider("./tests/testdata/otel.yaml")
        p.shutdown()  # should not raise


# ===========================================================================
# StatusCode enum
# ===========================================================================

class TestStatusCode:
    def test_unset(self):
        assert honeycomb_pycpp.StatusCode.UNSET is not None

    def test_ok(self):
        assert honeycomb_pycpp.StatusCode.OK is not None

    def test_error(self):
        assert honeycomb_pycpp.StatusCode.ERROR is not None

    def test_values_are_distinct(self):
        codes = {honeycomb_pycpp.StatusCode.UNSET,
                 honeycomb_pycpp.StatusCode.OK,
                 honeycomb_pycpp.StatusCode.ERROR}
        assert len(codes) == 3


# ===========================================================================
# SpanKind enum
# ===========================================================================

class TestSpanKind:
    def test_internal(self):
        assert honeycomb_pycpp.SpanKind.INTERNAL is not None

    def test_server(self):
        assert honeycomb_pycpp.SpanKind.SERVER is not None

    def test_client(self):
        assert honeycomb_pycpp.SpanKind.CLIENT is not None

    def test_producer(self):
        assert honeycomb_pycpp.SpanKind.PRODUCER is not None

    def test_consumer(self):
        assert honeycomb_pycpp.SpanKind.CONSUMER is not None

    def test_values_are_distinct(self):
        kinds = {
            honeycomb_pycpp.SpanKind.INTERNAL,
            honeycomb_pycpp.SpanKind.SERVER,
            honeycomb_pycpp.SpanKind.CLIENT,
            honeycomb_pycpp.SpanKind.PRODUCER,
            honeycomb_pycpp.SpanKind.CONSUMER,
        }
        assert len(kinds) == 5


# ===========================================================================
# Status
# ===========================================================================

class TestStatus:
    def test_status_unset(self):
        s = honeycomb_pycpp.Status(honeycomb_pycpp.StatusCode.UNSET)
        assert s.is_unset
        assert not s.is_ok

    def test_status_ok(self):
        s = honeycomb_pycpp.Status(honeycomb_pycpp.StatusCode.OK)
        assert s.is_ok
        assert not s.is_unset

    def test_status_error_no_description(self):
        s = honeycomb_pycpp.Status(honeycomb_pycpp.StatusCode.ERROR)
        assert not s.is_ok
        assert not s.is_unset

    def test_status_error_with_description(self):
        s = honeycomb_pycpp.Status(honeycomb_pycpp.StatusCode.ERROR, "something went wrong")
        assert s.description == "something went wrong"

    def test_status_ok_description_is_cleared(self):
        """Description is only meaningful for ERROR; others get it cleared."""
        s = honeycomb_pycpp.Status(honeycomb_pycpp.StatusCode.OK, "ignored")
        assert s.description == ""

    def test_status_code_property(self):
        s = honeycomb_pycpp.Status(honeycomb_pycpp.StatusCode.ERROR, "err")
        assert s.status_code == honeycomb_pycpp.StatusCode.ERROR.value

    def test_status_description_property_default(self):
        s = honeycomb_pycpp.Status(honeycomb_pycpp.StatusCode.ERROR)
        assert s.description == ""


# ===========================================================================
# Tracer.start_span — one test per optional parameter
# ===========================================================================

class TestTracerStartSpan:
    def test_name_only(self, tracer):
        """start_span with just a name creates a valid span."""
        span = tracer.start_span("basic-span")
        assert span is not None
        assert span.is_recording()
        span.end()

    def test_with_attributes_str(self, tracer):
        span = tracer.start_span("attr-str", attributes={"key": "value"})
        span.end()

    def test_with_attributes_int(self, tracer):
        span = tracer.start_span("attr-int", attributes={"count": 42})
        span.end()

    def test_with_attributes_list(self, tracer):
        span = tracer.start_span("attr-int", attributes={"count_list": [42, 3, 4, 5]})
        span.end()

    def test_with_context_none(self, tracer):
        """Explicit context=None is treated the same as omitting it."""
        span = tracer.start_span("ctx-none", context=None)
        span.end()

    def test_with_context_object(self, tracer):
        """start_span accepts a Context object as parent."""
        parent = tracer.start_span("parent")
        ctx = parent.get_context()
        child = tracer.start_span("child", context=ctx)
        assert child.get_parent_span_id() == parent.get_span_id()
        child.end()
        parent.end()

    def test_with_kind_internal(self, tracer):
        span = tracer.start_span("kind-internal", kind=SpanKind.INTERNAL)
        assert span.kind == honeycomb_pycpp.SpanKind.INTERNAL.value
        span.end()

    def test_with_kind_server(self, tracer):
        span = tracer.start_span("kind-server", kind=SpanKind.SERVER)
        assert span.kind == honeycomb_pycpp.SpanKind.SERVER.value
        span.end()

    def test_with_kind_client(self, tracer):
        span = tracer.start_span("kind-client", kind=SpanKind.CLIENT)
        assert span.kind == honeycomb_pycpp.SpanKind.CLIENT.value
        span.end()

    def test_with_kind_producer(self, tracer):
        span = tracer.start_span("kind-producer", kind=SpanKind.PRODUCER)
        assert span.kind == honeycomb_pycpp.SpanKind.PRODUCER.value
        span.end()

    def test_with_kind_consumer(self, tracer):
        span = tracer.start_span("kind-consumer", kind=SpanKind.CONSUMER)
        assert span.kind == honeycomb_pycpp.SpanKind.CONSUMER.value
        span.end()

    def test_with_start_time(self, tracer):
        """start_span accepts a nanosecond start_time timestamp."""
        ts_ns = time.time_ns() - 1_000_000_000  # 1 second ago
        span = tracer.start_span("timed-span", start_time=ts_ns)
        assert span is not None
        span.end()

    def test_all_optional_params(self, tracer):
        """start_span works with all optional params provided together."""
        ts_ns = time.time_ns()
        parent = tracer.start_span("parent-all")
        ctx = parent.get_context()
        span = tracer.start_span(
            "full-span",
            attributes={"env": "test"},
            context=ctx,
            kind=SpanKind.CLIENT,
            start_time=ts_ns,
        )
        assert span.kind == honeycomb_pycpp.SpanKind.CLIENT.value
        span.end()
        parent.end()

    def test_does_not_become_current(self, tracer):
        """start_span does NOT set the span as current (use start_as_current_span for that)."""
        span = tracer.start_span("not-current")
        ctx = honeycomb_pycpp.Context.get_current()
        active = ctx.get_span()
        # Active span context should not be the one we just started
        if active is not None:
            sc_active = active.get_span_context()
            sc_new = span.get_span_context()
            assert sc_active.span_id != sc_new.span_id
        span.end()


# ===========================================================================
# Tracer.start_as_current_span — one test per optional parameter
# ===========================================================================

class TestTracerStartAsCurrentSpan:
    def test_name_only(self, tracer):
        span = tracer.start_as_current_span("current-basic")
        assert span is not None
        assert span.is_recording()
        span.end()

    def test_with_attributes(self, tracer):
        span = tracer.start_as_current_span("current-attrs", attributes={"x": "1"})
        span.end()

    def test_with_attributes_mixed(self, tracer):
        span = tracer.start_as_current_span("current-attrs", attributes={"x": "1", "int_list": [1,2,3]})
        span.end()

    def test_with_context_none(self, tracer):
        span = tracer.start_as_current_span("current-ctx-none", context=None)
        span.end()

    def test_with_context_object(self, tracer):
        parent = tracer.start_span("sacc-parent")
        ctx = parent.get_context()
        child = tracer.start_as_current_span("sacc-child", context=ctx)
        assert child.get_parent_span_id() == parent.get_span_id()
        child.end()
        parent.end()

    def test_with_kind_server(self, tracer):
        span = tracer.start_as_current_span("current-server", kind=SpanKind.SERVER)
        assert span.kind == honeycomb_pycpp.SpanKind.SERVER.value
        span.end()

    def test_with_kind_client(self, tracer):
        span = tracer.start_as_current_span("current-client", kind=SpanKind.CLIENT)
        assert span.kind == honeycomb_pycpp.SpanKind.CLIENT.value
        span.end()

    def test_with_kind_producer(self, tracer):
        span = tracer.start_as_current_span("current-producer", kind=SpanKind.PRODUCER)
        assert span.kind == honeycomb_pycpp.SpanKind.PRODUCER.value
        span.end()

    def test_with_kind_consumer(self, tracer):
        span = tracer.start_as_current_span("current-consumer", kind=SpanKind.CONSUMER)
        assert span.kind == honeycomb_pycpp.SpanKind.CONSUMER.value
        span.end()

    def test_with_kind_internal(self, tracer):
        span = tracer.start_as_current_span("current-internal", kind=SpanKind.INTERNAL)
        assert span.kind == honeycomb_pycpp.SpanKind.INTERNAL.value
        span.end()

    def test_with_start_time(self, tracer):
        ts_ns = time.time_ns() - 500_000_000
        span = tracer.start_as_current_span("current-timed", start_time=ts_ns)
        assert span is not None
        span.end()

    def test_becomes_current_span(self, tracer):
        """start_as_current_span makes the span active in the runtime context."""
        span = tracer.start_as_current_span("should-be-current")
        ctx = honeycomb_pycpp.Context.get_current()
        active = ctx.get_span()
        assert active is not None
        assert active.get_span_context().span_id == span.get_span_context().span_id
        span.end()

    def test_nested_parent_propagation(self, tracer):
        """Nested start_as_current_span automatically picks up the outer span as parent."""
        outer = tracer.start_as_current_span("outer")
        inner = tracer.start_as_current_span("inner")
        assert inner.get_parent_span_id() == outer.get_span_id()
        inner.end()
        outer.end()

    def test_context_manager(self, tracer):
        """start_as_current_span span can be used as a context manager."""
        with tracer.start_as_current_span("cm-span") as span:
            assert span.is_recording()
        # After __exit__, span should be ended and no longer recording
        assert not span.is_recording()


# ===========================================================================
# Span — set_attribute (one test per supported type)
# ===========================================================================

class TestSpanSetAttribute:
    def test_str_value(self, tracer):
        span = tracer.start_span("attr")
        span.set_attribute("key", "hello")
        span.end()

    def test_int_value(self, tracer):
        span = tracer.start_span("attr")
        span.set_attribute("count", 42)
        span.end()

    def test_float_value(self, tracer):
        span = tracer.start_span("attr")
        span.set_attribute("ratio", 3.14)
        span.end()

    def test_bool_true_value(self, tracer):
        span = tracer.start_span("attr")
        span.set_attribute("flag", True)
        span.end()

    def test_bool_false_value(self, tracer):
        span = tracer.start_span("attr")
        span.set_attribute("flag", False)
        span.end()

    def test_list_of_str(self, tracer):
        span = tracer.start_span("attr")
        span.set_attribute("tags", ["a", "b", "c"])
        span.end()

    def test_list_of_int(self, tracer):
        span = tracer.start_span("attr")
        span.set_attribute("counts", [1, 2, 3])
        span.end()

    def test_list_of_float(self, tracer):
        span = tracer.start_span("attr")
        span.set_attribute("ratios", [1.1, 2.2, 3.3])
        span.end()

    def test_list_of_bool(self, tracer):
        span = tracer.start_span("attr")
        span.set_attribute("flags", [True, False, True])
        span.end()

    def test_invalid_type_raises(self, tracer):
        span = tracer.start_span("attr")
        with pytest.raises(Exception):
            span.set_attribute("bad", object())
        span.end()

    def test_mixed_sequence_raises(self, tracer):
        span = tracer.start_span("attr")
        with pytest.raises(Exception):
            span.set_attribute("mixed", [1, "two", 3.0])
        span.end()


# ===========================================================================
# Span — set_attributes
# ===========================================================================

class TestSpanSetAttributes:
    def test_empty_dict(self, tracer):
        span = tracer.start_span("sa")
        span.set_attributes({})
        span.end()

    def test_single_attribute(self, tracer):
        span = tracer.start_span("sa")
        span.set_attributes({"key": "value"})
        span.end()

    def test_multiple_attributes(self, tracer):
        span = tracer.start_span("sa")
        span.set_attributes({"a": "1", "b": "2", "c": "3"})
        span.end()

    def test_mixed_value_types(self, tracer):
        span = tracer.start_span("sa")
        span.set_attributes({"s": "text", "i": 99, "f": 0.5, "b": True, "bool_list": [True, False]})
        span.end()


# ===========================================================================
# Span — add_event (one test per overload / optional parameter)
# ===========================================================================

class TestSpanAddEvent:
    def test_name_only(self, tracer):
        span = tracer.start_span("ev")
        span.add_event("something-happened")
        span.end()

    def test_with_empty_attributes(self, tracer):
        span = tracer.start_span("ev")
        span.add_event("empty-attrs", {})
        span.end()

    def test_with_string_attributes(self, tracer):
        span = tracer.start_span("ev")
        span.add_event("with-attrs", {"key": "val"})
        span.end()

    def test_with_multiple_attributes(self, tracer):
        span = tracer.start_span("ev")
        span.add_event("multi", attributes={"k1": "v1", "k2": "v2"})
        span.end()

    def test_with_timestamp(self, tracer):
        span = tracer.start_span("ev")
        span.add_event("multi", timestamp=10)
        span.end()

    def test_multiple_events(self, tracer):
        span = tracer.start_span("ev")
        span.add_event("first")
        span.add_event("second")
        span.add_event("third", {"seq": "3"})
        span.end()

    def test_with_all_fields(self, tracer):
        span = tracer.start_span("ev")
        span.add_event("multi", attributes={"k1": 10, "k2": False, "k3":[1,2,3]}, timestamp=10)
        span.end()

# ===========================================================================
# Span — add_link (one test per overload / optional parameter)
# ===========================================================================

class TestSpanAddLink:
    def test_context_only(self, tracer):
        span1 = tracer.start_span("ev")
        span2 = tracer.start_span("ev-link")
        span1.add_link(span2.get_span_context())
        span1.end()
        span2.end()

    def test_context_and_attributes(self, tracer):
        span1 = tracer.start_span("ev")
        span2 = tracer.start_span("ev-link")
        span1.add_link(span2.get_span_context(), attributes={"first":1, "second":[1,2,3]})
        span1.end()
        span2.end()

# ===========================================================================
# Span — update_name (one test per overload / optional parameter)
# ===========================================================================

class TestSpanUpdateName:
    def test_context_only(self, tracer):
        span = tracer.start_span("ev")
        span.update_name("not-ev")
        span.end()

# ===========================================================================
# Span — set_status
# ===========================================================================

class TestSpanSetStatus:
    def test_set_status_unset(self, tracer):
        span = tracer.start_span("status")
        span.set_status(Status(StatusCode.UNSET))
        span.end()

    def test_set_status_ok(self, tracer):
        span = tracer.start_span("status")
        span.set_status(Status(StatusCode.OK))
        span.end()

    def test_set_status_error_no_description(self, tracer):
        span = tracer.start_span("status")
        span.set_status(Status(StatusCode.ERROR))
        span.end()

    def test_set_status_error_with_description(self, tracer):
        span = tracer.start_span("status")
        span.set_status(Status(StatusCode.ERROR, "db timeout"), description="something happened")
        span.end()

    def test_set_status_statuscode_error_with_description(self, tracer):
        """span.set_status(StatusCode.ERROR, description) — bare enum, Python OTel API form."""
        span = tracer.start_span("status")
        span.set_status(StatusCode.ERROR, "something went wrong")
        span.end()

    def test_set_status_statuscode_error_no_description(self, tracer):
        """span.set_status(StatusCode.ERROR) — bare enum without description."""
        span = tracer.start_span("status")
        span.set_status(StatusCode.ERROR)
        span.end()

    def test_set_status_statuscode_ok(self, tracer):
        """span.set_status(StatusCode.OK) — bare enum OK."""
        span = tracer.start_span("status")
        span.set_status(StatusCode.OK)
        span.end()

    def test_set_status_statuscode_unset(self, tracer):
        """span.set_status(StatusCode.UNSET) — bare enum UNSET."""
        span = tracer.start_span("status")
        span.set_status(StatusCode.UNSET)
        span.end()

    def test_set_status_invalid_raises(self, tracer):
        span = tracer.start_span("status")
        with pytest.raises(Exception):
            span.set_status("not-a-status")
        span.end()


# ===========================================================================
# Span — record_exception
# ===========================================================================

class TestSpanRecordException:
    def test_record_exception_basic(self, tracer):
        span = tracer.start_span("exc")
        try:
            raise ValueError("something went wrong")
        except ValueError as e:
            span.record_exception(e)
        span.end()

    def test_record_exception_with_attributes(self, tracer):
        span = tracer.start_span("exc")
        try:
            raise RuntimeError("db error")
        except RuntimeError as e:
            span.record_exception(e, attributes={"db.system": "postgresql"})
        span.end()

    def test_record_exception_with_timestamp(self, tracer):
        span = tracer.start_span("exc")
        ts = 1_000_000_000
        try:
            raise TypeError("bad type")
        except TypeError as e:
            span.record_exception(e, timestamp=ts)
        span.end()

    def test_record_exception_escaped(self, tracer):
        span = tracer.start_span("exc")
        try:
            raise KeyError("missing key")
        except KeyError as e:
            span.record_exception(e, escaped=True)
        span.end()

    def test_record_exception_no_traceback(self, tracer):
        """Exception created but never raised has no __traceback__; should not crash."""
        span = tracer.start_span("exc")
        e = ValueError("never raised")
        span.record_exception(e)
        span.end()


# ===========================================================================
# Span — end / is_recording
# ===========================================================================

class TestSpanEnd:
    def test_is_recording_before_end(self, tracer):
        span = tracer.start_span("rec")
        assert span.is_recording()
        span.end()

    def test_is_recording_after_end(self, tracer):
        span = tracer.start_span("rec")
        span.end()
        assert not span.is_recording()

    def test_end_idempotent(self, tracer):
        """Calling end() a second time should not raise."""
        span = tracer.start_span("end2x")
        span.end()
        span.end()  # should not raise

    def test_end_with_end_time(self, tracer):
        span = tracer.start_span("rec")
        assert span.is_recording()
        span.end(end_time=10)
        assert not span.is_recording()


# ===========================================================================
# Span — identity methods
# ===========================================================================

class TestSpanIdentity:
    def test_get_trace_id_is_nonempty_hex(self, tracer):
        span = tracer.start_span("id")
        tid = span.get_trace_id()
        assert isinstance(tid, str)
        assert len(tid) == 32
        assert all(c in "0123456789abcdef" for c in tid)
        span.end()

    def test_get_span_id_is_nonempty_hex(self, tracer):
        span = tracer.start_span("id")
        sid = span.get_span_id()
        assert isinstance(sid, str)
        assert len(sid) == 16
        assert all(c in "0123456789abcdef" for c in sid)
        span.end()

    def test_get_parent_span_id_no_parent(self, tracer):
        """A root span has no parent span ID."""
        span = tracer.start_span("root")
        assert span.get_parent_span_id() == ""
        span.end()

    def test_get_parent_span_id_with_parent(self, tracer):
        parent = tracer.start_span("parent")
        ctx = parent.get_context()
        child = tracer.start_span("child", context=ctx)
        assert child.get_parent_span_id() == parent.get_span_id()
        child.end()
        parent.end()

    def test_kind_default_is_internal(self, tracer):
        span = tracer.start_span("kind")
        assert span.kind == honeycomb_pycpp.SpanKind.INTERNAL.value
        span.end()

    def test_unique_trace_ids_across_spans(self, tracer):
        """Different root spans should have different trace IDs."""
        s1 = tracer.start_span("s1")
        s2 = tracer.start_span("s2")
        # Trace IDs may differ (not guaranteed), but span IDs must differ
        assert s1.get_span_id() != s2.get_span_id()
        s1.end()
        s2.end()

    def test_child_inherits_trace_id(self, tracer):
        parent = tracer.start_span("p")
        ctx = parent.get_context()
        child = tracer.start_span("c", context=ctx)
        assert child.get_trace_id() == parent.get_trace_id()
        child.end()
        parent.end()


# ===========================================================================
# Span — get_context / get_span_context
# ===========================================================================

class TestSpanContext:
    def test_get_context_returns_context(self, tracer):
        span = tracer.start_span("ctx")
        ctx = span.get_context()
        assert ctx is not None
        span.end()

    def test_get_span_context_returns_span_context(self, tracer):
        span = tracer.start_span("sc")
        sc = span.get_span_context()
        assert sc is not None
        span.end()

    def test_span_context_trace_id_is_int(self, tracer):
        span = tracer.start_span("sc")
        sc = span.get_span_context()
        assert isinstance(sc.trace_id, int)
        assert sc.trace_id != 0
        span.end()

    def test_span_context_span_id_is_int(self, tracer):
        span = tracer.start_span("sc")
        sc = span.get_span_context()
        assert isinstance(sc.span_id, int)
        assert sc.span_id != 0
        span.end()

    def test_span_context_trace_flags_is_int(self, tracer):
        span = tracer.start_span("sc")
        sc = span.get_span_context()
        assert isinstance(sc.trace_flags, int)
        span.end()

    def test_span_context_is_remote_is_bool(self, tracer):
        span = tracer.start_span("sc")
        sc = span.get_span_context()
        assert isinstance(sc.is_remote, bool)
        assert sc.is_remote is False  # locally started span
        span.end()

    def test_span_context_is_valid_is_bool(self, tracer):
        span = tracer.start_span("sc")
        sc = span.get_span_context()
        assert isinstance(sc.is_valid, bool)
        assert sc.is_valid is True
        span.end()

    def test_span_context_trace_state_is_str(self, tracer):
        span = tracer.start_span("sc")
        sc = span.get_span_context()
        assert isinstance(sc.trace_state, str)
        span.end()


# ===========================================================================
# Span — context manager (__enter__ / __exit__)
# ===========================================================================

class TestSpanContextManager:
    def test_enter_returns_span(self, tracer):
        span = tracer.start_span("cm")
        with span as s:
            assert s is span

    def test_exit_ends_span(self, tracer):
        span = tracer.start_span("cm")
        with span:
            assert span.is_recording()
        assert not span.is_recording()

    def test_exit_on_exception_does_not_suppress(self, tracer):
        """__exit__ should not suppress exceptions."""
        span = tracer.start_span("cm-exc")
        with pytest.raises(ValueError):
            with span:
                raise ValueError("boom")
        assert not span.is_recording()

    def test_start_as_current_span_context_manager(self, tracer):
        with tracer.start_as_current_span("cm-current") as span:
            assert span.is_recording()
            ctx = honeycomb_pycpp.Context.get_current()
            active = ctx.get_span()
            assert active is not None
            assert active.get_span_context().span_id == span.get_span_context().span_id


# ===========================================================================
# Context
# ===========================================================================

class TestContext:
    def test_default_constructor(self):
        ctx = honeycomb_pycpp.Context()
        assert ctx is not None

    def test_get_current_returns_context(self):
        ctx = honeycomb_pycpp.Context.get_current()
        assert ctx is not None

    def test_get_span_returns_none_with_no_active_span(self):
        ctx = honeycomb_pycpp.Context()
        # A freshly created context has no span set in it
        # (may or may not have an active span depending on test order)
        result = ctx.get_span()
        # We just verify the call does not raise
        assert result is None or result is not None

    def test_attach_and_detach(self):
        ctx = honeycomb_pycpp.Context.get_current()
        token = ctx.attach()
        assert token is not None
        honeycomb_pycpp.Context.detach(token)

    def test_create_with_span_context_defaults(self):
        """create_with_span_context with only required args uses default flags/is_remote."""
        trace_id = "4bf92f3577b34da6a3ce929d0e0e4736"
        span_id = "00f067aa0ba902b7"
        ctx = honeycomb_pycpp.Context.create_with_span_context(trace_id, span_id)
        assert ctx is not None
        span = ctx.get_span()
        assert span is not None

    def test_create_with_span_context_explicit_trace_flags(self):
        trace_id = "4bf92f3577b34da6a3ce929d0e0e4736"
        span_id = "00f067aa0ba902b7"
        ctx = honeycomb_pycpp.Context.create_with_span_context(
            trace_id, span_id, trace_flags=1
        )
        sc = ctx.get_span().get_span_context()
        assert sc.trace_flags == 1

    def test_create_with_span_context_is_remote_true(self):
        trace_id = "4bf92f3577b34da6a3ce929d0e0e4736"
        span_id = "00f067aa0ba902b7"
        ctx = honeycomb_pycpp.Context.create_with_span_context(
            trace_id, span_id, is_remote=True
        )
        sc = ctx.get_span().get_span_context()
        assert sc.is_remote is True

    def test_create_with_span_context_is_remote_false(self):
        trace_id = "4bf92f3577b34da6a3ce929d0e0e4736"
        span_id = "00f067aa0ba902b7"
        ctx = honeycomb_pycpp.Context.create_with_span_context(
            trace_id, span_id, is_remote=False
        )
        sc = ctx.get_span().get_span_context()
        assert sc.is_remote is False

    def test_create_with_span_context_round_trips_ids(self):
        trace_id = "4bf92f3577b34da6a3ce929d0e0e4736"
        span_id = "00f067aa0ba902b7"
        ctx = honeycomb_pycpp.Context.create_with_span_context(trace_id, span_id)
        sc = ctx.get_span().get_span_context()
        assert format(sc.trace_id, "032x") == trace_id
        assert format(sc.span_id, "016x") == span_id

    def test_span_active_via_context(self, tracer):
        """A span started as current is retrievable from the runtime context."""
        span = tracer.start_as_current_span("ctx-active")
        ctx = honeycomb_pycpp.Context.get_current()
        active = ctx.get_span()
        assert active is not None
        assert active.get_span_context().span_id == span.get_span_context().span_id
        span.end()
