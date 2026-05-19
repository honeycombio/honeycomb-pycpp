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

    def test_tracer_provider_not_none(self, sdk):
        assert sdk.tracer_provider is not None

    def test_meter_provider_not_none(self, sdk):
        assert sdk.meter_provider is not None

    def test_logger_provider_not_none(self, sdk):
        assert sdk.logger_provider is not None

    def test_tracer_provider_type(self, sdk):
        assert isinstance(sdk.tracer_provider, otel.TracerProvider)

    def test_meter_provider_type(self, sdk):
        assert isinstance(sdk.meter_provider, otel.MeterProvider)

    def test_logger_provider_type(self, sdk):
        assert isinstance(sdk.logger_provider, otel.LoggerProvider)

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
        t = sdk.tracer_provider.get_tracer("test-lib", instrumenting_library_version="1.0.0")
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

    def test_get_tracer_with_attributes(self, sdk):
        t = sdk.tracer_provider.get_tracer(
            "test-lib", "1.0.0", "https://example.com/schema",
            {"telemetry.sdk.language": "python"},
        )
        assert t is not None

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

    def test_get_meter_with_attributes(self, sdk):
        m = sdk.meter_provider.get_meter(
            "test-lib", "1.0.0", "https://example.com/schema",
            {"telemetry.sdk.language": "python"},
        )
        assert m is not None

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

    def test_get_logger_with_attributes(self, sdk):
        logger = sdk.logger_provider.get_logger(
            "test-lib", "1.0.0", "https://example.com/schema",
            {"telemetry.sdk.language": "python"},
        )
        assert logger is not None

    def test_logger_emit(self, sdk):
        logger = sdk.logger_provider.get_logger("test-lib")
        logger.emit(body="hello from SDK test", severity_number=otel.SeverityNumber.INFO)


# ===========================================================================
# release_config
# ===========================================================================

class TestReleaseConfig:
    """
    release_config() drops the ConfiguredSdk (model + registry) while keeping
    the signal providers running. Tests verify that providers remain usable and
    that both the normal and post-release shutdown paths are safe.
    """

    def test_providers_still_configured(self):
        s = otel.SDK(_CONFIG)
        s.release_config()
        assert s.tracer_provider.configured
        assert s.meter_provider.configured
        assert s.logger_provider.configured
        s.shutdown()

    def test_tracer_works_after_release(self):
        s = otel.SDK(_CONFIG)
        s.release_config()
        tracer = s.tracer_provider.get_tracer("test")
        span = tracer.start_span("release-span")
        assert span is not None
        span.end()
        s.shutdown()

    def test_meter_works_after_release(self):
        s = otel.SDK(_CONFIG)
        s.release_config()
        meter = s.meter_provider.get_meter("test")
        counter = meter.create_counter("release.counter")
        counter.add(1.0)
        s.shutdown()

    def test_logger_works_after_release(self):
        s = otel.SDK(_CONFIG)
        s.release_config()
        logger = s.logger_provider.get_logger("test")
        logger.emit(body="after release", severity_number=otel.SeverityNumber.INFO)
        s.shutdown()

    def test_release_is_idempotent(self):
        s = otel.SDK(_CONFIG)
        s.release_config()
        s.release_config()
        s.shutdown()

    def test_shutdown_after_release(self):
        s = otel.SDK(_CONFIG)
        s.release_config()
        s.shutdown()

    def test_shutdown_idempotent_after_release(self):
        s = otel.SDK(_CONFIG)
        s.release_config()
        s.shutdown()
        s.shutdown()

    def test_providers_unconfigured_after_shutdown(self):
        s = otel.SDK(_CONFIG)
        tp = s.tracer_provider
        mp = s.meter_provider
        lp = s.logger_provider
        s.release_config()
        s.shutdown()
        assert not tp.configured
        assert not mp.configured
        assert not lp.configured
