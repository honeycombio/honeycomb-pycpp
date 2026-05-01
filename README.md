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

### Programmatic use

```python
import honeycomb_pycpp as otel

# Initialize from config file (or uses embedded default)
provider = otel.TracerProvider("path/to/otel.yaml")

tracer = provider.get_tracer("my-tracer")

with tracer.start_as_current_span("my-span") as span:
    span.set_attribute("key", "value")
    # ... do work ...
```

## Current limitations

- Tracing only — metrics and logs are not yet supported
- Links require OpenTelemetry C++ ABI v2 (not yet enabled)

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
