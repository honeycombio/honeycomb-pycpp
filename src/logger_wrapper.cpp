// SPDX-FileCopyrightText: 2026 Honeycomb Authors <support@honeycomb.io>
// SPDX-License-Identifier: Apache-2.0

#include "logger_wrapper.h"
#include "py_attribute_iterable.h"

#include "opentelemetry/common/timestamp.h"
#include "opentelemetry/exporters/ostream/console_log_record_builder.h"
#include "opentelemetry/exporters/otlp/otlp_grpc_log_record_builder.h"
#include "opentelemetry/exporters/otlp/otlp_http_log_record_builder.h"
#include "opentelemetry/sdk/configuration/yaml_configuration_parser.h"

#include <chrono>
#include <iostream>

namespace otel_wrapper {

namespace logs_api = opentelemetry::logs;
namespace nostd = opentelemetry::nostd;

// ---------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------

static opentelemetry::common::SystemTimestamp ns_to_timestamp(uint64_t ns) {
    return opentelemetry::common::SystemTimestamp(std::chrono::nanoseconds(ns));
}

// Serialize a Python exception into attribute key/value pairs on a log record.
static void set_exception_attributes(opentelemetry::logs::LogRecord* rec, py::object exc) {
    auto exc_type = py::type::of(exc);
    std::string type_str;
    if (py::hasattr(exc_type, "__qualname__")) {
        type_str = exc_type.attr("__qualname__").cast<std::string>();
    } else {
        type_str = exc_type.attr("__name__").cast<std::string>();
    }

    std::string message_str = py::str(exc).cast<std::string>();

    std::string stacktrace_str;
    try {
        auto tb_mod = py::module_::import("traceback");
        py::object lines = tb_mod.attr("format_exception")(
            exc_type, exc, exc.attr("__traceback__"));
        stacktrace_str = py::str("").attr("join")(lines).cast<std::string>();
    } catch (...) {}

    rec->SetAttribute("exception.type",    nostd::string_view(type_str));
    rec->SetAttribute("exception.message", nostd::string_view(message_str));
    if (!stacktrace_str.empty()) {
        rec->SetAttribute("exception.stacktrace", nostd::string_view(stacktrace_str));
    }
}

// ---------------------------------------------------------------------------
// LoggerWrapper
// ---------------------------------------------------------------------------

LoggerWrapper::LoggerWrapper(nostd::shared_ptr<logs_api::Logger> logger)
    : logger_(std::move(logger)) {}

void LoggerWrapper::emit(const LogRecordWrapper& rec) {
    if (!logger_) return;

    auto log_record = logger_->CreateLogRecord();
    if (!log_record) return;

    if (rec.timestamp != 0) {
        log_record->SetTimestamp(ns_to_timestamp(rec.timestamp));
    }
    if (rec.observed_timestamp != 0) {
        log_record->SetObservedTimestamp(ns_to_timestamp(rec.observed_timestamp));
    }
    if (rec.severity_number != 0) {
        log_record->SetSeverity(static_cast<logs_api::Severity>(rec.severity_number));
    }
    if (!rec.body.is_none()) {
        if (py::isinstance<py::bool_>(rec.body)) {
            log_record->SetBody(rec.body.cast<bool>());
        } else if (py::isinstance<py::int_>(rec.body)) {
            log_record->SetBody(rec.body.cast<int64_t>());
        } else if (py::isinstance<py::float_>(rec.body)) {
            log_record->SetBody(rec.body.cast<double>());
        } else if (py::isinstance<py::str>(rec.body)) {
            std::string s = rec.body.cast<std::string>();
            log_record->SetBody(nostd::string_view(s));
        }
    }

    // User attributes
    if (!rec.attributes.empty()) {
        PyAttributeIterable kv(rec.attributes);
        kv.ForEachKeyValue([&](nostd::string_view key,
                               opentelemetry::common::AttributeValue val) {
            log_record->SetAttribute(key, val);
            return true;
        });
    }

    if (!rec.event_name.empty()) {
        log_record->SetEventId(0, rec.event_name);
    }

    if (!rec.exception.is_none()) {
        set_exception_attributes(log_record.get(), rec.exception);
    }

    logger_->EmitLogRecord(std::move(log_record));
}

// ---------------------------------------------------------------------------
// LoggerProviderWrapper
// ---------------------------------------------------------------------------

LoggerProviderWrapper::LoggerProviderWrapper(const std::string& path) {
    std::shared_ptr<opentelemetry::sdk::configuration::Registry> registry(
        new opentelemetry::sdk::configuration::Registry);

    opentelemetry::exporter::logs::ConsoleLogRecordBuilder::Register(registry.get());
    opentelemetry::exporter::otlp::OtlpHttpLogRecordBuilder::Register(registry.get());
    opentelemetry::exporter::otlp::OtlpGrpcLogRecordBuilder::Register(registry.get());

    auto model = opentelemetry::sdk::configuration::YamlConfigurationParser::ParseFile(path);
    if (!model) throw std::runtime_error("Failed to parse config: " + path);

    // Keep logger_provider; null out the other signals so ConfiguredSdk doesn't
    // attempt to build them without the corresponding exporters registered.
    model->tracer_provider = nullptr;
    model->meter_provider  = nullptr;

    sdk_ = opentelemetry::sdk::configuration::ConfiguredSdk::Create(registry, model);
    if (!sdk_) throw std::runtime_error("Unsupported configuration: " + path);

    sdk_->Install();

    if (sdk_->logger_provider) {
        auto std_lp = std::static_pointer_cast<logs_api::LoggerProvider>(sdk_->logger_provider);
        nostd::shared_ptr<logs_api::LoggerProvider> lp(std_lp);
        logs_api::Provider::SetLoggerProvider(lp);
    }
}

LoggerProviderWrapper::~LoggerProviderWrapper() {
    shutdown();
}

std::shared_ptr<LoggerWrapper> LoggerProviderWrapper::get_logger(const std::string& name,
                                                                  py::object version,
                                                                  py::object schema_url,
                                                                  py::object attributes) {
    if (!sdk_ || !sdk_->logger_provider) return nullptr;

    auto ver_str    = version.is_none()    ? "" : version.cast<std::string>();
    auto schema_str = schema_url.is_none() ? "" : schema_url.cast<std::string>();

    // Attributes accepted for API compatibility but not forwarded (no v1 support).
    (void)attributes;

    auto logger = sdk_->logger_provider->GetLogger(name, "", ver_str, schema_str);
    return std::make_shared<LoggerWrapper>(logger);
}

void LoggerProviderWrapper::shutdown() {
    if (sdk_) {
        sdk_->UnInstall();
        sdk_.reset();
    }
}

}  // namespace otel_wrapper
