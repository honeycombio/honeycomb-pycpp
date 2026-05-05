# SPDX-FileCopyrightText: 2026 Honeycomb Authors <support@honeycomb.io>
# SPDX-License-Identifier: Apache-2.0

"""
Comprehensive tests for the honeycomb_pycpp logging API surface, mirroring the
opentelemetry-api Python spec at:
https://opentelemetry-python.readthedocs.io/en/latest/api/logs.html

One test per method / per optional parameter where applicable.
"""

import time
import pytest
import honeycomb_pycpp


# ---------------------------------------------------------------------------
# Fixtures
# ---------------------------------------------------------------------------

@pytest.fixture(scope="module")
def provider():
    p = honeycomb_pycpp.LoggerProvider("./tests/testdata/otel.yaml")
    yield p
    p.shutdown()


@pytest.fixture(scope="module")
def logger(provider):
    return provider.get_logger("test-logger")


# ===========================================================================
# LoggerProvider
# ===========================================================================

class TestLoggerProvider:
    def test_init(self):
        p = honeycomb_pycpp.LoggerProvider("./tests/testdata/otel.yaml")
        p.shutdown()

    def test_get_logger_name_only(self, provider):
        lg = provider.get_logger("my-lib")
        assert lg is not None

    def test_get_logger_with_version(self, provider):
        lg = provider.get_logger("my-lib", version="1.2.3")
        assert lg is not None

    def test_get_logger_with_schema_url(self, provider):
        lg = provider.get_logger("my-lib", schema_url="https://example.com/schema/1.0.0")
        assert lg is not None

    def test_get_logger_with_attributes(self, provider):
        lg = provider.get_logger("my-lib", attributes={"key": "value"})
        assert lg is not None

    def test_get_logger_with_all_optional_params(self, provider):
        lg = provider.get_logger(
            "my-lib",
            version="2.0.0",
            schema_url="https://example.com/schema",
            attributes={"foo": "bar"},
        )
        assert lg is not None

    def test_shutdown(self):
        p = honeycomb_pycpp.LoggerProvider("./tests/testdata/otel.yaml")
        p.shutdown()


# ===========================================================================
# SeverityNumber enum
# ===========================================================================

class TestSeverityNumber:
    def test_unspecified(self):
        assert honeycomb_pycpp.SeverityNumber.UNSPECIFIED is not None

    def test_trace(self):
        assert honeycomb_pycpp.SeverityNumber.TRACE is not None

    def test_debug(self):
        assert honeycomb_pycpp.SeverityNumber.DEBUG is not None

    def test_info(self):
        assert honeycomb_pycpp.SeverityNumber.INFO is not None

    def test_warn(self):
        assert honeycomb_pycpp.SeverityNumber.WARN is not None

    def test_error(self):
        assert honeycomb_pycpp.SeverityNumber.ERROR is not None

    def test_fatal(self):
        assert honeycomb_pycpp.SeverityNumber.FATAL is not None

    def test_trace_variants(self):
        assert honeycomb_pycpp.SeverityNumber.TRACE2 is not None
        assert honeycomb_pycpp.SeverityNumber.TRACE3 is not None
        assert honeycomb_pycpp.SeverityNumber.TRACE4 is not None

    def test_debug_variants(self):
        assert honeycomb_pycpp.SeverityNumber.DEBUG2 is not None
        assert honeycomb_pycpp.SeverityNumber.DEBUG3 is not None
        assert honeycomb_pycpp.SeverityNumber.DEBUG4 is not None

    def test_info_variants(self):
        assert honeycomb_pycpp.SeverityNumber.INFO2 is not None
        assert honeycomb_pycpp.SeverityNumber.INFO3 is not None
        assert honeycomb_pycpp.SeverityNumber.INFO4 is not None

    def test_warn_variants(self):
        assert honeycomb_pycpp.SeverityNumber.WARN2 is not None
        assert honeycomb_pycpp.SeverityNumber.WARN3 is not None
        assert honeycomb_pycpp.SeverityNumber.WARN4 is not None

    def test_error_variants(self):
        assert honeycomb_pycpp.SeverityNumber.ERROR2 is not None
        assert honeycomb_pycpp.SeverityNumber.ERROR3 is not None
        assert honeycomb_pycpp.SeverityNumber.ERROR4 is not None

    def test_fatal_variants(self):
        assert honeycomb_pycpp.SeverityNumber.FATAL2 is not None
        assert honeycomb_pycpp.SeverityNumber.FATAL3 is not None
        assert honeycomb_pycpp.SeverityNumber.FATAL4 is not None

    def test_values_are_ordered(self):
        assert (
            honeycomb_pycpp.SeverityNumber.TRACE.value
            < honeycomb_pycpp.SeverityNumber.DEBUG.value
            < honeycomb_pycpp.SeverityNumber.INFO.value
            < honeycomb_pycpp.SeverityNumber.WARN.value
            < honeycomb_pycpp.SeverityNumber.ERROR.value
            < honeycomb_pycpp.SeverityNumber.FATAL.value
        )

    def test_unspecified_is_zero(self):
        assert honeycomb_pycpp.SeverityNumber.UNSPECIFIED.value == 0


# ===========================================================================
# LogRecord
# ===========================================================================

class TestLogRecord:
    def test_default_constructor(self):
        r = honeycomb_pycpp.LogRecord()
        assert r is not None

    def test_with_timestamp(self):
        ts = time.time_ns()
        r = honeycomb_pycpp.LogRecord(timestamp=ts)
        assert r.timestamp == ts

    def test_with_observed_timestamp(self):
        ts = time.time_ns()
        r = honeycomb_pycpp.LogRecord(observed_timestamp=ts)
        assert r.observed_timestamp == ts

    def test_with_severity_number(self):
        r = honeycomb_pycpp.LogRecord(severity_number=honeycomb_pycpp.SeverityNumber.INFO)
        assert r.severity_number == honeycomb_pycpp.SeverityNumber.INFO

    def test_with_severity_text(self):
        r = honeycomb_pycpp.LogRecord(severity_text="INFO")
        assert r.severity_text == "INFO"

    def test_with_body_str(self):
        r = honeycomb_pycpp.LogRecord(body="something happened")
        assert r.body == "something happened"

    def test_with_body_int(self):
        r = honeycomb_pycpp.LogRecord(body=42)
        assert r.body == 42

    def test_with_body_float(self):
        r = honeycomb_pycpp.LogRecord(body=3.14)
        assert r.body == 3.14

    def test_with_body_bool(self):
        r = honeycomb_pycpp.LogRecord(body=True)
        assert r.body is True

    def test_with_attributes_str(self):
        r = honeycomb_pycpp.LogRecord(attributes={"key": "value"})
        assert r.attributes["key"] == "value"

    def test_with_attributes_int(self):
        r = honeycomb_pycpp.LogRecord(attributes={"count": 5})
        assert r.attributes["count"] == 5

    def test_with_attributes_bool(self):
        r = honeycomb_pycpp.LogRecord(attributes={"flag": True})
        assert r.attributes["flag"] is True

    def test_with_attributes_float(self):
        r = honeycomb_pycpp.LogRecord(attributes={"ratio": 0.5})
        assert r.attributes["ratio"] == 0.5

    def test_with_multiple_attributes(self):
        r = honeycomb_pycpp.LogRecord(attributes={"a": "1", "b": 2, "c": True})
        assert r.attributes["a"] == "1"
        assert r.attributes["b"] == 2
        assert r.attributes["c"] is True

    def test_with_event_name(self):
        r = honeycomb_pycpp.LogRecord(event_name="user.login")
        assert r.event_name == "user.login"

    def test_with_exception(self):
        try:
            raise ValueError("something went wrong")
        except ValueError as e:
            r = honeycomb_pycpp.LogRecord(exception=e)
            assert r.exception is e

    def test_with_all_params(self):
        ts = time.time_ns()
        r = honeycomb_pycpp.LogRecord(
            timestamp=ts,
            severity_number=honeycomb_pycpp.SeverityNumber.ERROR,
            severity_text="ERROR",
            body="database connection failed",
            attributes={"db.system": "postgresql"},
            event_name="db.error",
        )
        assert r.timestamp == ts
        assert r.severity_number == honeycomb_pycpp.SeverityNumber.ERROR
        assert r.severity_text == "ERROR"
        assert r.body == "database connection failed"


# ===========================================================================
# Logger — emit via LogRecord
# ===========================================================================

class TestLoggerEmitRecord:
    def test_emit_empty_record(self, logger):
        r = honeycomb_pycpp.LogRecord()
        logger.emit(r)

    def test_emit_with_severity_info(self, logger):
        r = honeycomb_pycpp.LogRecord(
            severity_number=honeycomb_pycpp.SeverityNumber.INFO,
            body="info message",
        )
        logger.emit(r)

    def test_emit_with_severity_warn(self, logger):
        r = honeycomb_pycpp.LogRecord(
            severity_number=honeycomb_pycpp.SeverityNumber.WARN,
            body="warn message",
        )
        logger.emit(r)

    def test_emit_with_severity_error(self, logger):
        r = honeycomb_pycpp.LogRecord(
            severity_number=honeycomb_pycpp.SeverityNumber.ERROR,
            body="error message",
        )
        logger.emit(r)

    def test_emit_with_severity_debug(self, logger):
        r = honeycomb_pycpp.LogRecord(
            severity_number=honeycomb_pycpp.SeverityNumber.DEBUG,
            body="debug message",
        )
        logger.emit(r)

    def test_emit_with_severity_fatal(self, logger):
        r = honeycomb_pycpp.LogRecord(
            severity_number=honeycomb_pycpp.SeverityNumber.FATAL,
            body="fatal message",
        )
        logger.emit(r)

    def test_emit_with_attributes(self, logger):
        r = honeycomb_pycpp.LogRecord(
            severity_number=honeycomb_pycpp.SeverityNumber.INFO,
            body="user logged in",
            attributes={"user.id": "abc123", "user.role": "admin"},
        )
        logger.emit(r)

    def test_emit_with_timestamp(self, logger):
        r = honeycomb_pycpp.LogRecord(
            timestamp=time.time_ns(),
            severity_number=honeycomb_pycpp.SeverityNumber.INFO,
            body="timed message",
        )
        logger.emit(r)

    def test_emit_with_event_name(self, logger):
        r = honeycomb_pycpp.LogRecord(
            severity_number=honeycomb_pycpp.SeverityNumber.INFO,
            body="user action",
            event_name="user.action",
        )
        logger.emit(r)

    def test_emit_with_exception(self, logger):
        try:
            raise RuntimeError("db connection refused")
        except RuntimeError as e:
            r = honeycomb_pycpp.LogRecord(
                severity_number=honeycomb_pycpp.SeverityNumber.ERROR,
                body="database error",
                exception=e,
            )
            logger.emit(r)

    def test_emit_with_severity_text(self, logger):
        r = honeycomb_pycpp.LogRecord(
            severity_number=honeycomb_pycpp.SeverityNumber.WARN,
            severity_text="WARN",
            body="low disk space",
        )
        logger.emit(r)


# ===========================================================================
# Logger — emit via keyword arguments
# ===========================================================================

class TestLoggerEmitKeywords:
    def test_emit_body_only(self, logger):
        logger.emit(body="simple log message")

    def test_emit_severity_info(self, logger):
        logger.emit(
            severity_number=honeycomb_pycpp.SeverityNumber.INFO,
            body="info message",
        )

    def test_emit_severity_warn(self, logger):
        logger.emit(
            severity_number=honeycomb_pycpp.SeverityNumber.WARN,
            body="warn message",
        )

    def test_emit_severity_error(self, logger):
        logger.emit(
            severity_number=honeycomb_pycpp.SeverityNumber.ERROR,
            body="error message",
        )

    def test_emit_with_severity_text(self, logger):
        logger.emit(
            severity_number=honeycomb_pycpp.SeverityNumber.INFO,
            severity_text="INFO",
            body="message with severity text",
        )

    def test_emit_with_timestamp(self, logger):
        logger.emit(
            timestamp=time.time_ns(),
            severity_number=honeycomb_pycpp.SeverityNumber.INFO,
            body="timed message",
        )

    def test_emit_with_observed_timestamp(self, logger):
        logger.emit(
            observed_timestamp=time.time_ns(),
            severity_number=honeycomb_pycpp.SeverityNumber.INFO,
            body="observed timed message",
        )

    def test_emit_with_str_attributes(self, logger):
        logger.emit(
            severity_number=honeycomb_pycpp.SeverityNumber.INFO,
            body="attributed message",
            attributes={"service.name": "my-service", "env": "production"},
        )

    def test_emit_with_int_attributes(self, logger):
        logger.emit(
            severity_number=honeycomb_pycpp.SeverityNumber.INFO,
            body="attributed message",
            attributes={"http.status_code": 500},
        )

    def test_emit_with_bool_attributes(self, logger):
        logger.emit(
            severity_number=honeycomb_pycpp.SeverityNumber.INFO,
            body="attributed message",
            attributes={"cache.hit": False},
        )

    def test_emit_with_event_name(self, logger):
        logger.emit(
            severity_number=honeycomb_pycpp.SeverityNumber.INFO,
            body="user signed up",
            event_name="user.signup",
        )

    def test_emit_with_exception(self, logger):
        try:
            raise ValueError("invalid input")
        except ValueError as e:
            logger.emit(
                severity_number=honeycomb_pycpp.SeverityNumber.ERROR,
                body="validation error",
                exception=e,
            )

    def test_emit_with_all_params(self, logger):
        ts = time.time_ns()
        logger.emit(
            timestamp=ts,
            observed_timestamp=ts,
            severity_number=honeycomb_pycpp.SeverityNumber.ERROR,
            severity_text="ERROR",
            body="complete log record",
            attributes={"service.name": "my-service", "http.status_code": 500},
            event_name="http.error",
        )
