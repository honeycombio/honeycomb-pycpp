# honeycomb-pycpp

[![OSS Lifecycle](https://img.shields.io/osslifecycle/honeycombio/honeycomb-pycpp?color=success)](https://github.com/honeycombio/home/blob/main/honeycomb-oss-lifecycle-and-practices.md)
[![Build](https://github.com/honeycombio/honeycomb-pycpp/actions/workflows/build-wheels.yml/badge.svg)](https://github.com/honeycombio/honeycomb-pycpp/actions/workflows/build-wheels.yml)

Python bindings for the OpenTelemetry C++ SDK. Provides high-performance tracing via a Pythonic interface, and ships as an OpenTelemetry [distro](https://opentelemetry.io/docs/concepts/distributions/) for drop-in use with auto-instrumentation.

This library is **experimental**.

## Installation

```bash
pip install honeycomb-pycpp
```

The wheel bundles the OpenTelemetry C++ SDK — no system-level dependencies required.

## Configuration

The SDK is configured via a YAML file following the [OpenTelemetry Configuration File Format](https://opentelemetry.io/docs/specs/otel/configuration/file-configuration/). A default config is embedded in the package and used when no override is provided.

| Environment variable | Description |
|---|---|
| `OTEL_CONFIG_FILE` | Path to a custom configuration YAML. Overrides the embedded default. |
| `OTEL_EXPORTER_OTLP_ENDPOINT` | OTLP endpoint (default: `http://localhost:4318`) |
| `OTEL_EXPORTER_OTLP_HEADERS` | Headers to send with OTLP requests |
| `OTEL_RESOURCE_ATTRIBUTES` | Comma-separated resource attributes |
| `OTEL_SERVICE_NAME` | Service name |

## Usage

### As a distro (auto-instrumentation)

```bash
opentelemetry-instrument --service-name my-service python app.py
```

The distro registers itself automatically via entry points — no code changes required.

### Tracing

```python
import honeycomb_pycpp as otel

# Initialize from config file (or uses embedded default)
provider = otel.TracerProvider("path/to/otel.yaml")

tracer = provider.get_tracer("my-tracer")

with tracer.start_as_current_span("my-span") as span:
    span.set_attribute("key", "value")
    # ... do work ...
```

### Metrics

Install the system metrics instrumentor:

```bash
pip install opentelemetry-instrumentation-system-metrics
```

Then activate it programmatically after initializing the `MeterProvider`:

```python
import honeycomb_pycpp as otel
from opentelemetry import metrics
from opentelemetry.instrumentation.system_metrics import SystemMetricsInstrumentor

provider = otel.MeterProvider("path/to/otel.yaml")
metrics.set_meter_provider(provider)

SystemMetricsInstrumentor().instrument()
```

This collects CPU, memory, network, and other host metrics and exports them via the C++ SDK.

### Logs

```python
import honeycomb_pycpp as otel

provider = otel.LoggerProvider("path/to/otel.yaml")

logger = provider.get_logger("my-logger")

# Emit using keyword arguments
logger.emit(body="something happened", severity_number=otel.SeverityNumber.INFO)

# Emit using a LogRecord object
record = otel.LogRecord()
record.body = "request failed"
record.severity_number = otel.SeverityNumber.ERROR
record.attributes = {"user.id": "u-123", "http.status_code": 500}
logger.emit(record)

# Attach an exception
try:
    1 / 0
except ZeroDivisionError as exc:
    record = otel.LogRecord()
    record.body = "unhandled exception"
    record.severity_number = otel.SeverityNumber.ERROR
    record.exception = exc
    logger.emit(record)

provider.shutdown()
```

The `LoggerProvider` can also be used via bridges such that `opentelemetry-instrumentation-logging` work:

```python
import logging
import honeycomb_pycpp as otel
from opentelemetry.instrumentation.logging.handler import LoggingHandler
from opentelemetry._logs import get_logger_provider

handler = LoggingHandler(logger_provider=otel.LoggerProvider("path/to/otel.yaml"))
logging.getLogger().addHandler(handler)

logging.getLogger("myapp").warning("watch out")
```

### Deploying alongside the standard OpenTelemetry Python distro

Both `honeycomb-pycpp` and the standard `opentelemetry-distro` can be installed at the same time. When both are present, `opentelemetry-instrument` may pick up either distro. Use `OTEL_PYTHON_DISTRO` and `OTEL_PYTHON_CONFIGURATOR` to explicitly select which one runs.

```bash
pip install honeycomb-pycpp opentelemetry-distro
```

To use this C++ distro:

```bash
OTEL_PYTHON_DISTRO=cpp_distro \
OTEL_PYTHON_CONFIGURATOR=cpp_configurator \
opentelemetry-instrument python app.py
```

To use the standard Python SDK distro:

```bash
OTEL_PYTHON_DISTRO=distro \
OTEL_PYTHON_CONFIGURATOR=configurator \
opentelemetry-instrument python app.py
```

If neither variable is set and both distros are installed, the one selected is non-deterministic — always set them explicitly in multi-distro environments.

## Current limitations

- OpenTelemetry C++ ABI v2 not yet enabled, any features relying on it (i.e. links) are not supported

## Building from source

Requirements: Python >= 3.10, CMake >= 3.15, C++17 compiler.

```bash
git clone https://github.com/honeycombio/honeycomb-pycpp
cd honeycomb-pycpp
pip install -r requirements-dev.txt
pip install -e .
```

To rebuild after C++ changes:

```bash
pip install -e . --force-reinstall --no-deps
```

To clean up cmake artifacts:

```bash
rm -rf CMakeCache.txt CMakeFiles/ cmake_install.cmake build/ dist/ *.egg-info/ *.so
```

## License

Apache License 2.0
