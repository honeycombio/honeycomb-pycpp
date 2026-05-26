// SPDX-FileCopyrightText: 2026 Honeycomb Authors <support@honeycomb.io>
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "tracer_wrapper.h"
#include "meter_wrapper.h"
#include "logger_wrapper.h"

#ifdef WITH_YAML_SDK_CONFIG
#include "opentelemetry/sdk/configuration/configured_sdk.h"
#endif

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace otel_wrapper {

struct OtlpSignalConfig {
    std::string endpoint;
    std::vector<std::pair<std::string, std::string>> headers;
    std::string ca_file, key_file, cert_file;
};

struct ProgrammaticConfig {
    std::vector<std::pair<std::string, std::string>> resource_attributes;
    OtlpSignalConfig traces, metrics, logs;
    int metric_interval_ms = 60000;
    int metric_timeout_ms  = 30000;
};

class SDKWrapper {
public:
#ifdef WITH_YAML_SDK_CONFIG
    explicit SDKWrapper(const std::string& path);
#endif
    explicit SDKWrapper(const ProgrammaticConfig& config);
    ~SDKWrapper();

    void shutdown();
    void release_config();

    std::shared_ptr<TracerProviderWrapper> tracer_provider() const { return tracer_; }
    std::shared_ptr<MeterProviderWrapper>  meter_provider()  const { return meter_;  }
    std::shared_ptr<LoggerProviderWrapper> logger_provider() const { return logger_; }

private:
#ifdef WITH_YAML_SDK_CONFIG
    std::shared_ptr<void> sdk_;  // ConfiguredSdk, type-erased
#endif
    std::shared_ptr<TracerProviderWrapper> tracer_;
    std::shared_ptr<MeterProviderWrapper>  meter_;
    std::shared_ptr<LoggerProviderWrapper> logger_;
};

} // namespace otel_wrapper
