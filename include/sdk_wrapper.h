// SPDX-FileCopyrightText: 2026 Honeycomb Authors <support@honeycomb.io>
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "tracer_wrapper.h"
#include "meter_wrapper.h"
#include "logger_wrapper.h"

#include "opentelemetry/sdk/configuration/configured_sdk.h"

#include <memory>
#include <string>

namespace otel_wrapper {

using ConfiguredSdk = opentelemetry::sdk::configuration::ConfiguredSdk;

// Thin provider views returned by SDKWrapper.  They hold a reference to the
// shared ConfiguredSdk so the underlying providers stay alive as long as any
// view is live.  Shutdown is intentionally a no-op: lifecycle is owned by
// SDKWrapper.

class SDKTracerProvider {
public:
    explicit SDKTracerProvider(std::shared_ptr<ConfiguredSdk> sdk) : sdk_(std::move(sdk)) {}

    std::shared_ptr<TracerWrapper> get_tracer(
        const std::string& name,
        py::object version    = py::none(),
        py::object schema_url = py::none());

    bool is_configured() const { return sdk_ && sdk_->tracer_provider; }

private:
    std::shared_ptr<ConfiguredSdk> sdk_;
};

class SDKMeterProvider {
public:
    explicit SDKMeterProvider(std::shared_ptr<ConfiguredSdk> sdk) : sdk_(std::move(sdk)) {}

    std::shared_ptr<MeterWrapper> get_meter(
        const std::string& name,
        py::object version    = py::none(),
        py::object schema_url = py::none(),
        py::object attributes = py::none());

    bool is_configured() const { return sdk_ && sdk_->meter_provider; }

private:
    std::shared_ptr<ConfiguredSdk> sdk_;
};

class SDKLoggerProvider {
public:
    explicit SDKLoggerProvider(std::shared_ptr<ConfiguredSdk> sdk) : sdk_(std::move(sdk)) {}

    std::shared_ptr<LoggerWrapper> get_logger(
        const std::string& name,
        py::object version    = py::none(),
        py::object schema_url = py::none(),
        py::object attributes = py::none());

    bool is_configured() const { return sdk_ && sdk_->logger_provider; }

private:
    std::shared_ptr<ConfiguredSdk> sdk_;
};

/**
 * Configures all three OTel signals from a single YAML file in one
 * ConfiguredSdk::Create call and exposes a provider view for each signal.
 */
class SDKWrapper {
public:
    explicit SDKWrapper(const std::string& path);
    ~SDKWrapper();

    void shutdown();

    bool tracer_configured() const { return sdk_ && sdk_->tracer_provider; }
    bool meter_configured()  const { return sdk_ && sdk_->meter_provider;  }
    bool logger_configured() const { return sdk_ && sdk_->logger_provider; }

    std::shared_ptr<SDKTracerProvider> tracer_provider() const { return tracer_; }
    std::shared_ptr<SDKMeterProvider>  meter_provider()  const { return meter_;  }
    std::shared_ptr<SDKLoggerProvider> logger_provider() const { return logger_; }

private:
    std::shared_ptr<ConfiguredSdk>     sdk_;
    std::shared_ptr<SDKTracerProvider> tracer_;
    std::shared_ptr<SDKMeterProvider>  meter_;
    std::shared_ptr<SDKLoggerProvider> logger_;
};

} // namespace otel_wrapper
