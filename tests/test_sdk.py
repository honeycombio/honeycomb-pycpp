# SPDX-FileCopyrightText: 2026 Honeycomb Authors <support@honeycomb.io>
# SPDX-License-Identifier: Apache-2.0

import pytest
import honeycomb_pycpp as otel


_CONFIG = "./tests/testdata/otel.yaml"


@pytest.fixture(scope="module")
def sdk():
    s = otel.SDK(_CONFIG)
    yield s
    s.shutdown()


# ===========================================================================
# SDKWrapper
# ===========================================================================

class TestSDK:
    def test_init(self):
        s = otel.SDK(_CONFIG)
        s.shutdown()

    def test_bad_path_raises(self):
        with pytest.raises(RuntimeError):
            otel.SDK("/nonexistent/path.yaml")

    def test_tracer_configured(self, sdk):
        assert sdk.tracer_configured is True

    def test_meter_configured(self, sdk):
        assert sdk.meter_configured is True

    def test_logger_configured(self, sdk):
        assert sdk.logger_configured is True

    def test_tracer_provider_not_none(self, sdk):
        assert sdk.tracer_provider is not None

    def test_meter_provider_not_none(self, sdk):
        assert sdk.meter_provider is not None

    def test_logger_provider_not_none(self, sdk):
        assert sdk.logger_provider is not None

    def test_tracer_provider_type(self, sdk):
        assert isinstance(sdk.tracer_provider, otel.SDKTracerProvider)

    def test_meter_provider_type(self, sdk):
        assert isinstance(sdk.meter_provider, otel.SDKMeterProvider)

    def test_logger_provider_type(self, sdk):
        assert isinstance(sdk.logger_provider, otel.SDKLoggerProvider)

    def test_shutdown_idempotent(self):
        s = otel.SDK(_CONFIG)
        s.shutdown()
        s.shutdown()


# ===========================================================================
# SDKTracerProvider
# ===========================================================================

class TestSDKTracerProvider:
    def test_configured(self, sdk):
        assert sdk.tracer_provider.configured is True

    def test_get_tracer_name_only(self, sdk):
        t = sdk.tracer_provider.get_tracer("test-lib")
        assert t is not None
        assert isinstance(t, otel.Tracer)

    def test_get_tracer_with_version(self, sdk):
        t = sdk.tracer_provider.get_tracer("test-lib", version="1.0.0")
        assert t is not None

    def test_get_tracer_with_schema_url(self, sdk):
        t = sdk.tracer_provider.get_tracer("test-lib", schema_url="https://example.com/schema")
        assert t is not None

    def test_tracer_can_start_span(self, sdk):
        tracer = sdk.tracer_provider.get_tracer("test-lib")
        span = tracer.start_span("test-span")
        assert span is not None
        assert isinstance(span, otel.Span)
        span.end()

    def test_tracer_start_as_current_span(self, sdk):
        tracer = sdk.tracer_provider.get_tracer("test-lib")
        with tracer.start_as_current_span("test-span") as span:
            assert span.is_recording()


# ===========================================================================
# SDKMeterProvider
# ===========================================================================

class TestSDKMeterProvider:
    def test_configured(self, sdk):
        assert sdk.meter_provider.configured is True

    def test_get_meter_name_only(self, sdk):
        m = sdk.meter_provider.get_meter("test-lib")
        assert m is not None
        assert isinstance(m, otel.Meter)

    def test_get_meter_with_version(self, sdk):
        m = sdk.meter_provider.get_meter("test-lib", version="1.0.0")
        assert m is not None

    def test_get_meter_with_schema_url(self, sdk):
        m = sdk.meter_provider.get_meter("test-lib", schema_url="https://example.com/schema")
        assert m is not None

    def test_meter_create_counter(self, sdk):
        meter = sdk.meter_provider.get_meter("test-lib")
        counter = meter.create_counter("test.counter")
        assert counter is not None
        counter.add(1.0)

    def test_meter_create_histogram(self, sdk):
        meter = sdk.meter_provider.get_meter("test-lib")
        hist = meter.create_histogram("test.histogram")
        assert hist is not None
        hist.record(42.0)


# ===========================================================================
# SDKLoggerProvider
# ===========================================================================

class TestSDKLoggerProvider:
    def test_configured(self, sdk):
        assert sdk.logger_provider.configured is True

    def test_get_logger_name_only(self, sdk):
        logger = sdk.logger_provider.get_logger("test-lib")
        assert logger is not None
        assert isinstance(logger, otel.Logger)

    def test_get_logger_with_version(self, sdk):
        logger = sdk.logger_provider.get_logger("test-lib", version="1.0.0")
        assert logger is not None

    def test_get_logger_with_schema_url(self, sdk):
        logger = sdk.logger_provider.get_logger("test-lib", schema_url="https://example.com/schema")
        assert logger is not None

    def test_logger_emit(self, sdk):
        logger = sdk.logger_provider.get_logger("test-lib")
        logger.emit(body="hello from SDK test", severity_number=otel.SeverityNumber.INFO)
