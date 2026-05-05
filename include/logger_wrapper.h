// SPDX-FileCopyrightText: 2026 Honeycomb Authors <support@honeycomb.io>
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <memory>
#include <string>

#include <pybind11/pybind11.h>
#include "opentelemetry/common/attribute_value.h"
#include "opentelemetry/common/key_value_iterable.h"
#include "opentelemetry/logs/logger.h"
#include "opentelemetry/logs/logger_provider.h"
#include "opentelemetry/logs/provider.h"
#include "opentelemetry/logs/severity.h"
#include "opentelemetry/nostd/shared_ptr.h"
#include "opentelemetry/sdk/configuration/configuration.h"
#include "opentelemetry/sdk/configuration/configured_sdk.h"

namespace py = pybind11;

namespace otel_wrapper {

namespace logs_api = opentelemetry::logs;
namespace nostd = opentelemetry::nostd;

// Python-accessible data bag for a log record. Fields are stored as Python
// objects and populated into a real C++ LogRecord at emit() time.
class LogRecordWrapper {
public:
    uint64_t timestamp = 0;           // nanoseconds since epoch; 0 = unset
    uint64_t observed_timestamp = 0;  // nanoseconds since epoch; 0 = unset
    int severity_number = 0;          // maps to logs_api::Severity; 0 = kInvalid
    std::string severity_text;        // stored for API parity; no C++ setter exists
    py::object body = py::none();     // str | int | float | bool
    py::dict attributes;
    std::string event_name;
    py::object exception = py::none();
};

class LoggerWrapper {
public:
    explicit LoggerWrapper(nostd::shared_ptr<logs_api::Logger> logger);
    void emit(const LogRecordWrapper& record);

private:
    nostd::shared_ptr<logs_api::Logger> logger_;
};

class LoggerProviderWrapper {
public:
    explicit LoggerProviderWrapper(const std::string& path);
    ~LoggerProviderWrapper();

    std::shared_ptr<LoggerWrapper> get_logger(
        const std::string& name,
        py::object version    = py::none(),
        py::object schema_url = py::none(),
        py::object attributes = py::none());

    void shutdown();
    bool is_configured() const { return sdk_ && sdk_->logger_provider; }

private:
    std::unique_ptr<opentelemetry::sdk::configuration::ConfiguredSdk> sdk_;
};

}  // namespace otel_wrapper