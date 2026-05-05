# honeycomb-pycpp changelog

## Unreleased

### New

- feature: add logger provider support. (#) | @codeboten

### Fix

- bugfix: register shutdown for providers with atexit. (#) | @codeboten

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
