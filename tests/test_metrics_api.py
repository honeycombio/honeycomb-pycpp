# SPDX-FileCopyrightText: 2026 Honeycomb Authors <support@honeycomb.io>
# SPDX-License-Identifier: Apache-2.0

"""
Comprehensive tests for the honeycomb_pycpp metrics API surface, mirroring the
opentelemetry-api Python spec at:
https://opentelemetry-python.readthedocs.io/en/latest/api/metrics.html

One test per method / per optional parameter where applicable.
"""

import pytest
import honeycomb_pycpp


# ---------------------------------------------------------------------------
# Fixtures
# ---------------------------------------------------------------------------

@pytest.fixture(scope="module")
def provider():
    p = honeycomb_pycpp.MeterProvider("./tests/testdata/otel.yaml")
    yield p
    p.shutdown()


@pytest.fixture(scope="module")
def meter(provider):
    return provider.get_meter("test-meter")


# ===========================================================================
# MeterProvider
# ===========================================================================

class TestMeterProvider:
    def test_init(self):
        p = honeycomb_pycpp.MeterProvider("./tests/testdata/otel.yaml")
        p.shutdown()

    def test_get_meter_name_only(self, provider):
        m = provider.get_meter("my-lib")
        assert m is not None

    def test_get_meter_with_version(self, provider):
        m = provider.get_meter("my-lib", version="1.2.3")
        assert m is not None

    def test_get_meter_with_schema_url(self, provider):
        m = provider.get_meter("my-lib", schema_url="https://example.com/schema/1.0.0")
        assert m is not None

    def test_get_meter_with_attributes(self, provider):
        m = provider.get_meter("my-lib", attributes={"key": "value"})
        assert m is not None

    def test_get_meter_with_all_optional_params(self, provider):
        m = provider.get_meter(
            "my-lib",
            version="2.0.0",
            schema_url="https://example.com/schema",
            attributes={"foo": "bar"},
        )
        assert m is not None

    def test_shutdown(self):
        p = honeycomb_pycpp.MeterProvider("./tests/testdata/otel.yaml")
        p.shutdown()


# ===========================================================================
# Meter — instrument creation
# ===========================================================================

class TestMeterCreateCounter:
    def test_name_only(self, meter):
        c = meter.create_counter("requests")
        assert c is not None

    def test_with_unit(self, meter):
        c = meter.create_counter("requests", unit="1")
        assert c is not None

    def test_with_description(self, meter):
        c = meter.create_counter("requests", description="Total request count")
        assert c is not None

    def test_with_all_params(self, meter):
        c = meter.create_counter("requests", unit="1", description="Total request count")
        assert c is not None


class TestMeterCreateUpDownCounter:
    def test_name_only(self, meter):
        c = meter.create_up_down_counter("active_requests")
        assert c is not None

    def test_with_unit(self, meter):
        c = meter.create_up_down_counter("active_requests", unit="1")
        assert c is not None

    def test_with_description(self, meter):
        c = meter.create_up_down_counter("active_requests", description="Active request count")
        assert c is not None

    def test_with_all_params(self, meter):
        c = meter.create_up_down_counter(
            "active_requests", unit="1", description="Active request count"
        )
        assert c is not None


class TestMeterCreateHistogram:
    def test_name_only(self, meter):
        h = meter.create_histogram("request_duration")
        assert h is not None

    def test_with_unit(self, meter):
        h = meter.create_histogram("request_duration", unit="ms")
        assert h is not None

    def test_with_description(self, meter):
        h = meter.create_histogram("request_duration", description="Request duration in ms")
        assert h is not None

    def test_with_explicit_bucket_boundaries(self, meter):
        h = meter.create_histogram(
            "request_duration",
            explicit_bucket_boundaries_advisory=[0, 5, 10, 25, 50, 75, 100, 250, 500, 1000],
        )
        assert h is not None

    def test_with_all_params(self, meter):
        h = meter.create_histogram(
            "request_duration",
            unit="ms",
            description="Request duration in ms",
            explicit_bucket_boundaries_advisory=[0, 5, 10, 25, 50, 75, 100],
        )
        assert h is not None


class TestMeterCreateGauge:
    def test_name_only(self, meter):
        g = meter.create_gauge("cpu_usage")
        assert g is not None

    def test_with_unit(self, meter):
        g = meter.create_gauge("cpu_usage", unit="1")
        assert g is not None

    def test_with_description(self, meter):
        g = meter.create_gauge("cpu_usage", description="CPU usage ratio")
        assert g is not None

    def test_with_all_params(self, meter):
        g = meter.create_gauge("cpu_usage", unit="1", description="CPU usage ratio")
        assert g is not None


class TestMeterCreateObservableCounter:
    def test_name_only(self, meter):
        c = meter.create_observable_counter("bytes_sent")
        assert c is not None

    def test_with_callback(self, meter):
        def cb(options):
            return [honeycomb_pycpp.Observation(100)]

        c = meter.create_observable_counter("bytes_sent", callbacks=[cb])
        assert c is not None

    def test_with_unit(self, meter):
        c = meter.create_observable_counter("bytes_sent", unit="By")
        assert c is not None

    def test_with_description(self, meter):
        c = meter.create_observable_counter("bytes_sent", description="Total bytes sent")
        assert c is not None

    def test_with_all_params(self, meter):
        def cb(options):
            return [honeycomb_pycpp.Observation(100)]

        c = meter.create_observable_counter(
            "bytes_sent", callbacks=[cb], unit="By", description="Total bytes sent"
        )
        assert c is not None


class TestMeterCreateObservableUpDownCounter:
    def test_name_only(self, meter):
        c = meter.create_observable_up_down_counter("queue_depth")
        assert c is not None

    def test_with_callback(self, meter):
        def cb(options):
            return [honeycomb_pycpp.Observation(42)]

        c = meter.create_observable_up_down_counter("queue_depth", callbacks=[cb])
        assert c is not None

    def test_with_all_params(self, meter):
        def cb(options):
            return [honeycomb_pycpp.Observation(42)]

        c = meter.create_observable_up_down_counter(
            "queue_depth", callbacks=[cb], unit="1", description="Queue depth"
        )
        assert c is not None


class TestMeterCreateObservableGauge:
    def test_name_only(self, meter):
        g = meter.create_observable_gauge("temperature")
        assert g is not None

    def test_with_callback(self, meter):
        def cb(options):
            return [honeycomb_pycpp.Observation(21.5)]

        g = meter.create_observable_gauge("temperature", callbacks=[cb])
        assert g is not None

    def test_with_all_params(self, meter):
        def cb(options):
            return [honeycomb_pycpp.Observation(21.5)]

        g = meter.create_observable_gauge(
            "temperature", callbacks=[cb], unit="Cel", description="Ambient temperature"
        )
        assert g is not None


# ===========================================================================
# Counter
# ===========================================================================

class TestCounter:
    def test_add_int(self, meter):
        c = meter.create_counter("test.counter.int")
        c.add(1)

    def test_add_float(self, meter):
        c = meter.create_counter("test.counter.float")
        c.add(1.5)

    def test_add_zero(self, meter):
        c = meter.create_counter("test.counter.zero")
        c.add(0)

    def test_add_with_str_attributes(self, meter):
        c = meter.create_counter("test.counter.attrs.str")
        c.add(1, attributes={"endpoint": "/api/v1"})

    def test_add_with_int_attributes(self, meter):
        c = meter.create_counter("test.counter.attrs.int")
        c.add(1, attributes={"status_code": 200})

    def test_add_with_bool_attributes(self, meter):
        c = meter.create_counter("test.counter.attrs.bool")
        c.add(1, attributes={"success": True})

    def test_add_with_multiple_attributes(self, meter):
        c = meter.create_counter("test.counter.attrs.multi")
        c.add(1, attributes={"endpoint": "/api", "method": "GET", "status_code": 200})

    def test_add_multiple_times(self, meter):
        c = meter.create_counter("test.counter.multi")
        c.add(1)
        c.add(2)
        c.add(3)


# ===========================================================================
# UpDownCounter
# ===========================================================================

class TestUpDownCounter:
    def test_add_positive(self, meter):
        c = meter.create_up_down_counter("test.updown.positive")
        c.add(5)

    def test_add_negative(self, meter):
        c = meter.create_up_down_counter("test.updown.negative")
        c.add(-3)

    def test_add_zero(self, meter):
        c = meter.create_up_down_counter("test.updown.zero")
        c.add(0)

    def test_add_float(self, meter):
        c = meter.create_up_down_counter("test.updown.float")
        c.add(1.5)

    def test_add_with_attributes(self, meter):
        c = meter.create_up_down_counter("test.updown.attrs")
        c.add(1, attributes={"pool": "primary"})

    def test_add_positive_then_negative(self, meter):
        c = meter.create_up_down_counter("test.updown.sequence")
        c.add(10)
        c.add(-4)


# ===========================================================================
# Histogram
# ===========================================================================

class TestHistogram:
    def test_record_int(self, meter):
        h = meter.create_histogram("test.histogram.int")
        h.record(100)

    def test_record_float(self, meter):
        h = meter.create_histogram("test.histogram.float")
        h.record(99.9)

    def test_record_zero(self, meter):
        h = meter.create_histogram("test.histogram.zero")
        h.record(0)

    def test_record_with_str_attributes(self, meter):
        h = meter.create_histogram("test.histogram.attrs.str")
        h.record(150, attributes={"endpoint": "/api/v1"})

    def test_record_with_int_attributes(self, meter):
        h = meter.create_histogram("test.histogram.attrs.int")
        h.record(150, attributes={"status_code": 200})

    def test_record_with_multiple_attributes(self, meter):
        h = meter.create_histogram("test.histogram.attrs.multi")
        h.record(150, attributes={"endpoint": "/api", "method": "POST"})

    def test_record_multiple_values(self, meter):
        h = meter.create_histogram("test.histogram.multi")
        for val in [10, 50, 100, 200, 500]:
            h.record(val)


# ===========================================================================
# Gauge
# ===========================================================================

class TestGauge:
    def test_set_int(self, meter):
        g = meter.create_gauge("test.gauge.int")
        g.set(42)

    def test_set_float(self, meter):
        g = meter.create_gauge("test.gauge.float")
        g.set(3.14)

    def test_set_zero(self, meter):
        g = meter.create_gauge("test.gauge.zero")
        g.set(0)

    def test_set_negative(self, meter):
        g = meter.create_gauge("test.gauge.negative")
        g.set(-10)

    def test_set_with_attributes(self, meter):
        g = meter.create_gauge("test.gauge.attrs")
        g.set(0.75, attributes={"cpu": "0"})

    def test_set_multiple_times(self, meter):
        g = meter.create_gauge("test.gauge.multi")
        g.set(10)
        g.set(20)
        g.set(15)
