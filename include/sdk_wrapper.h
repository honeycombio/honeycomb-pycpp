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

/**
 * Configures all three OTel signals from a single YAML file in one
 * ConfiguredSdk::Create call and exposes a provider view for each signal.
 */
class SDKWrapper {
public:
    explicit SDKWrapper(const std::string& path);
    ~SDKWrapper();

    void shutdown();
    void release_config();

    std::shared_ptr<TracerProviderWrapper> tracer_provider() const { return tracer_; }
    std::shared_ptr<MeterProviderWrapper>  meter_provider()  const { return meter_;  }
    std::shared_ptr<LoggerProviderWrapper> logger_provider() const { return logger_; }

private:
    std::shared_ptr<ConfiguredSdk>         sdk_;
    std::shared_ptr<TracerProviderWrapper> tracer_;
    std::shared_ptr<MeterProviderWrapper>  meter_;
    std::shared_ptr<LoggerProviderWrapper> logger_;
};

} // namespace otel_wrapper
