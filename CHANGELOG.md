# honeycomb-pycpp changelog

## Unreleased

#### Fix

- bugfix: use `opentelemetry.trace.TraceFlags` when returning the span context wrapper's trace flags. (#20) | @codeboten

## [0.1.7] - 2026-05-12

#### Fix

- bugfix: support `links`, `record_exception`, and `set_status_on_exception` arguments in `start_span` and `start_as_current_span`. (#18) | @codeboten

## [0.1.6] - 2026-05-08

#### Fix

- maint: support additional darwin versions. (#17) | @codeboten

## [0.1.5] - 2026-05-06

#### Fix

- maint: build issue preventing all built wheels from being uploaded. | @codeboten

## [0.1.4] - 2026-05-05

### New

- feature: add logger provider support. (#15) | @codeboten

### Fix

- bugfix: register shutdown for providers with atexit. (#15) | @codeboten

## [0.1.3] - 2026-05-04

### New

- feature: add meter provider support. (#14) | @codeboten
- feature: add configured property to TracerProvider to determine if it can be used or not. (#14) | @codeboten

### Fix

- bugfix: clean up output when instrumenting is done. (#14) | @codeboten

## [0.1.2a1] - 2026-05-01

### Fix

- fix: update path from otel_cpp_tracer to honeycomb_pycpp. (#12) | @codeboten

#### Maintenance

- maint: cache build steps. (#7) | @codeboten

## [0.1.1a2] - 2026-04-30

### Fix

- fix: update set_status call to support StatusCode enum. | @codeboten

#### Maintenance

- maint: cache build steps. (#7) | @codeboten
