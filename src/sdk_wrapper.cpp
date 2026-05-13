// SPDX-FileCopyrightText: 2026 Honeycomb Authors <support@honeycomb.io>
// SPDX-License-Identifier: Apache-2.0

#include "sdk_wrapper.h"

// Trace exporters
#include "opentelemetry/exporters/ostream/console_span_builder.h"
#include "opentelemetry/exporters/otlp/otlp_grpc_span_builder.h"
#include "opentelemetry/exporters/otlp/otlp_http_span_builder.h"
// Metrics exporters
#include "opentelemetry/exporters/ostream/console_push_metric_builder.h"
#include "opentelemetry/exporters/otlp/otlp_grpc_push_metric_builder.h"
#include "opentelemetry/exporters/otlp/otlp_http_push_metric_builder.h"
// Log exporters
#include "opentelemetry/exporters/ostream/console_log_record_builder.h"
#include "opentelemetry/exporters/otlp/otlp_grpc_log_record_builder.h"
#include "opentelemetry/exporters/otlp/otlp_http_log_record_builder.h"

#include "opentelemetry/sdk/configuration/always_on_sampler_configuration.h"
#include "opentelemetry/sdk/configuration/parent_based_sampler_configuration.h"
#include "opentelemetry/sdk/configuration/yaml_configuration_parser.h"
#include "opentelemetry/logs/provider.h"
#include "opentelemetry/metrics/provider.h"
#include "opentelemetry/trace/provider.h"

namespace otel_wrapper {

namespace logs_api    = opentelemetry::logs;
namespace metrics_api = opentelemetry::metrics;
namespace trace_api   = opentelemetry::trace;
namespace nostd       = opentelemetry::nostd;

// SDKTracerProvider

std::shared_ptr<TracerWrapper> SDKTracerProvider::get_tracer(
        const std::string& name, py::object version, py::object schema_url) {
    if (!sdk_ || !sdk_->tracer_provider) return nullptr;
    auto ver_str    = version.is_none()    ? "" : version.cast<std::string>();
    auto schema_str = schema_url.is_none() ? "" : schema_url.cast<std::string>();
    auto tracer = sdk_->tracer_provider->GetTracer(name, ver_str, schema_str);
    return std::make_shared<TracerWrapper>(tracer);
}

// SDKMeterProvider

std::shared_ptr<MeterWrapper> SDKMeterProvider::get_meter(
        const std::string& name, py::object version, py::object schema_url,
        py::object /*attributes*/) {
    if (!sdk_ || !sdk_->meter_provider) return nullptr;
    auto ver_str    = version.is_none()    ? "" : version.cast<std::string>();
    auto schema_str = schema_url.is_none() ? "" : schema_url.cast<std::string>();
    auto meter = sdk_->meter_provider->GetMeter(name, ver_str, schema_str);
    return std::make_shared<MeterWrapper>(meter);
}

// SDKLoggerProvider

std::shared_ptr<LoggerWrapper> SDKLoggerProvider::get_logger(
        const std::string& name, py::object version, py::object schema_url,
        py::object /*attributes*/) {
    if (!sdk_ || !sdk_->logger_provider) return nullptr;
    auto ver_str    = version.is_none()    ? "" : version.cast<std::string>();
    auto schema_str = schema_url.is_none() ? "" : schema_url.cast<std::string>();
    auto logger = sdk_->logger_provider->GetLogger(name, "", ver_str, schema_str);
    return std::make_shared<LoggerWrapper>(logger);
}

// SDKWrapper

SDKWrapper::SDKWrapper(const std::string& path) {
    std::shared_ptr<opentelemetry::sdk::configuration::Registry> registry(
        new opentelemetry::sdk::configuration::Registry);

    opentelemetry::exporter::trace::ConsoleSpanBuilder::Register(registry.get());
    opentelemetry::exporter::otlp::OtlpHttpSpanBuilder::Register(registry.get());
    opentelemetry::exporter::otlp::OtlpGrpcSpanBuilder::Register(registry.get());
    opentelemetry::exporter::metrics::ConsolePushMetricBuilder::Register(registry.get());
    opentelemetry::exporter::otlp::OtlpHttpPushMetricBuilder::Register(registry.get());
    opentelemetry::exporter::otlp::OtlpGrpcPushMetricBuilder::Register(registry.get());
    opentelemetry::exporter::logs::ConsoleLogRecordBuilder::Register(registry.get());
    opentelemetry::exporter::otlp::OtlpHttpLogRecordBuilder::Register(registry.get());
    opentelemetry::exporter::otlp::OtlpGrpcLogRecordBuilder::Register(registry.get());

    auto model = opentelemetry::sdk::configuration::YamlConfigurationParser::ParseFile(path);
    if (!model) throw std::runtime_error("Failed to parse config: " + path);

    if (model->tracer_provider && !model->tracer_provider->sampler) {
        auto root = std::make_unique<opentelemetry::sdk::configuration::AlwaysOnSamplerConfiguration>();
        auto parent_based = std::make_unique<opentelemetry::sdk::configuration::ParentBasedSamplerConfiguration>();
        parent_based->root = std::move(root);
        model->tracer_provider->sampler = std::move(parent_based);
    }

    sdk_ = opentelemetry::sdk::configuration::ConfiguredSdk::Create(registry, model);
    if (!sdk_) throw std::runtime_error("Unsupported configuration: " + path);

    sdk_->Install();

    if (sdk_->tracer_provider) {
        auto std_tp = std::static_pointer_cast<trace_api::TracerProvider>(sdk_->tracer_provider);
        nostd::shared_ptr<trace_api::TracerProvider> tp(std_tp);
        trace_api::Provider::SetTracerProvider(tp);
        tracer_ = std::make_shared<SDKTracerProvider>(sdk_);
    }
    if (sdk_->meter_provider) {
        auto std_mp = std::static_pointer_cast<metrics_api::MeterProvider>(sdk_->meter_provider);
        nostd::shared_ptr<metrics_api::MeterProvider> mp(std_mp);
        metrics_api::Provider::SetMeterProvider(mp);
        meter_ = std::make_shared<SDKMeterProvider>(sdk_);
    }
    if (sdk_->logger_provider) {
        auto std_lp = std::static_pointer_cast<logs_api::LoggerProvider>(sdk_->logger_provider);
        nostd::shared_ptr<logs_api::LoggerProvider> lp(std_lp);
        logs_api::Provider::SetLoggerProvider(lp);
        logger_ = std::make_shared<SDKLoggerProvider>(sdk_);
    }
}

SDKWrapper::~SDKWrapper() {
    shutdown();
}

void SDKWrapper::shutdown() {
    if (sdk_) {
        sdk_->UnInstall();
        sdk_.reset();
    }
}

} // namespace otel_wrapper
