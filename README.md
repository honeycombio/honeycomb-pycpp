# OpenTelemetry C++ Python Bindings

[![OSS Lifecycle](https://img.shields.io/osslifecycle/honeycombio/honeycomb-pycpp?color=success)](https://github.com/honeycombio/home/blob/main/honeycomb-oss-lifecycle-and-practices.md)
[![Build](https://github.com/honeycombio/honeycomb-pycpp/actions/workflows/build-wheels.yml/badge.svg)](https://github.com/honeycombio/honeycomb-pycpp/actions/workflows/build-wheels.yml)

Python bindings for the OpenTelemetry C++ SDK, providing high-performance tracing capabilities through a Pythonic interface. This library is **experimental**.

## Prerequisites

### System Dependencies

1. **OpenTelemetry C++ SDK**
   ```bash
   # macOS
   brew install opentelemetry-cpp

   # Ubuntu/Debian
   sudo apt-get install libopentelemetry-dev

   # From source
   git clone https://github.com/open-telemetry/opentelemetry-cpp.git
   cd opentelemetry-cpp
   mkdir build && cd build
   cmake -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
         -DWITH_OTLP_HTTP=ON \
         -DBUILD_TESTING=OFF \
         ..
   make -j
   sudo make install
   ```

2. **Python dev dependencies**
   ```bash
   pip install -r requirements-dev.txt
   ```

## Installation

### From Source

```bash
# Clone the repository
git clone https://github.com/honeycombio/honeycomb-pycpp
cd honeycomb-pycpp

# Install in development mode
pip install -e .

# Or build and install
pip install .
```

### Building

The build process uses CMake through setuptools:

```bash
python -m pip wheel . --wheel-dir=./build --no-deps -v

# Or use pip
pip install -v .
```

## Quick Start

### Basic Tracing

```python
import otel_cpp_tracer as otel

# Create a tracer provider
provider = otel.TracerProvider("my-service", "console")

# Get a tracer
tracer = provider.get_tracer("my-app", "1.0.0")

# Start a span
span = tracer.start_span("operation")
span.set_attribute("user.id", "123")
span.set_attribute("http.method", "GET")
span.set_status(otel.StatusCode.OK)
span.end()

# Shutdown
provider.shutdown()
```

### Using Context Managers

```python
import otel_cpp_tracer as otel

provider = otel.TracerProvider("my-service", "console")
tracer = provider.get_tracer("my-app")

# Span automatically ends when exiting the context
with tracer.start_span("database-query") as span:
    span.set_attribute("db.system", "postgresql")
    span.set_attribute("db.statement", "SELECT * FROM users")
    span.add_event("Query executed")
    span.set_status(otel.StatusCode.OK)

provider.shutdown()
```

### Error Handling

```python
import otel_cpp_tracer as otel

provider = otel.TracerProvider("my-service", "console")
tracer = provider.get_tracer("my-app")

try:
    with tracer.start_span("risky-operation") as span:
        span.set_attribute("operation.type", "file-processing")
        # Do some work that might fail
        raise ValueError("Something went wrong!")
except ValueError:
    # Span status is automatically set to ERROR
    pass

provider.shutdown()
```

### OTLP Exporter

```python
import otel_cpp_tracer as otel

# Send traces to OTLP collector at localhost:4318
provider = otel.TracerProvider("my-service", "otlp")
tracer = provider.get_tracer("my-app")

with tracer.start_span("api-call") as span:
    span.set_attribute("http.url", "https://api.example.com/data")
    span.set_attribute("http.status_code", 200)
    span.set_status(otel.StatusCode.OK)

# Important: Shutdown to flush buffered spans
provider.shutdown()
```

## API Reference

### TracerProvider

```python
TracerProvider(service_name: str, exporter_type: str = "console")
```

Creates a tracer provider with the specified service name and exporter.

**Parameters:**
- `service_name`: Name of your service
- `exporter_type`: Either "console" or "otlp" (default: "console")

**Methods:**
- `get_tracer(name: str, version: str = "") -> Tracer`: Get a tracer instance
- `shutdown()`: Shutdown the provider and flush all spans

### Tracer

```python
tracer.start_span(name: str, attributes: dict = None) -> Span
```

Start a new span.

**Parameters:**
- `name`: Name of the span
- `attributes`: Optional dictionary of initial attributes (string values only)

### Span

**Methods:**
- `set_attribute(key: str, value: str|int|float|bool)`: Set an attribute
- `add_event(name: str, attributes: dict = None)`: Add an event
- `set_status(status_code: StatusCode, description: str = "")`: Set span status
- `end()`: End the span explicitly
- `is_recording() -> bool`: Check if span is recording
- `get_trace_id() -> str`: Get the trace ID
- `get_span_id() -> str`: Get the span ID

**Context Manager:**
Spans support context manager protocol (`with` statement) for automatic cleanup.

### StatusCode

Enum with values:
- `StatusCode.UNSET`: Default status
- `StatusCode.OK`: Successful operation
- `StatusCode.ERROR`: Failed operation

## Examples

See the `examples/` directory for more comprehensive examples:
- `basic_tracing.py`: Basic span creation and management
- Context manager usage
- Error handling
- OTLP exporter configuration

## Project Structure

```
honeycomb-pycpp/
├── CMakeLists.txt           # CMake build configuration
├── pyproject.toml           # Python project metadata
├── setup.py                 # Build script
├── include/
│   └── tracer_wrapper.h     # C++ wrapper headers
├── src/
│   ├── tracer_wrapper.cpp   # C++ wrapper implementation
│   └── bindings.cpp         # pybind11 bindings
├── examples/
│   └── basic_tracing.py     # Example usage
└── README.md                # This file
```

## Development

### Building for Development

```bash
# Install in editable mode
pip install -e .

# Rebuild after C++ changes
pip install -e . --force-reinstall --no-deps
```

### Requirements

- Python >= 3.10
- CMake >= 3.15
- C++17 compatible compiler
- OpenTelemetry C++ SDK
- pybind11 >= 2.10.0

### Troubleshooting

Running pip install is failing

```bash
# clean up cmake cache files
rm -rf CMakeCache.txt CMakeFiles/ cmake_install.cmake build/ dist/ *.egg-info/ *.so

# Install in editable mode
pip install -e .
```


## Performance Considerations

- The C++ SDK provides better performance than pure Python implementations
- Use the OTLP exporter with batch processing for production workloads
- Console exporter is useful for development and debugging
- Remember to call `provider.shutdown()` to flush buffered spans

## Limitations

- Context propagation between spans is not yet fully implemented
- Only HTTP OTLP exporter is currently supported (not gRPC)
- Metrics and logs are not yet supported (tracing only)

## Future Enhancements

- [ ] Full context propagation support
- [ ] Parent-child span relationships
- [ ] Baggage propagation
- [ ] Metrics and logs support
- [ ] gRPC OTLP exporter
- [ ] Additional exporters (Jaeger, Zipkin)
- [ ] Sampling configuration
- [ ] Custom span processors

## License

Apache License 2.0

## Contributing

Contributions are welcome! Please feel free to submit pull requests or open issues.

## Related Projects

- [OpenTelemetry C++](https://github.com/open-telemetry/opentelemetry-cpp)
- [OpenTelemetry Python](https://github.com/open-telemetry/opentelemetry-python)
- [pybind11](https://github.com/pybind/pybind11)
