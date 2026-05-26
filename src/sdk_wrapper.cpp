// SPDX-FileCopyrightText: 2026 Honeycomb Authors <support@honeycomb.io>
// SPDX-License-Identifier: Apache-2.0

#include "sdk_wrapper.h"
#include "opentelemetry/logs/noop.h"
#include "opentelemetry/metrics/noop.h"
#include "opentelemetry/trace/noop.h"

// Factory API — always available
#include "opentelemetry/exporters/otlp/otlp_http_exporter_factory.h"
#include "opentelemetry/exporters/otlp/otlp_http_exporter_options.h"
#include "opentelemetry/exporters/otlp/otlp_http_metric_exporter_factory.h"
#include "opentelemetry/exporters/otlp/otlp_http_metric_exporter_options.h"
#include "opentelemetry/exporters/otlp/otlp_http_log_record_exporter_factory.h"
#include "opentelemetry/exporters/otlp/otlp_http_log_record_exporter_options.h"
#include "opentelemetry/sdk/trace/batch_span_processor_factory.h"
#include "opentelemetry/sdk/trace/batch_span_processor_options.h"
#include "opentelemetry/sdk/trace/samplers/always_on_factory.h"
#include "opentelemetry/sdk/trace/samplers/parent_factory.h"
#include "opentelemetry/sdk/trace/tracer_provider_factory.h"
#include "opentelemetry/sdk/metrics/export/periodic_exporting_metric_reader_factory.h"
#include "opentelemetry/sdk/metrics/export/periodic_exporting_metric_reader_options.h"
#include "opentelemetry/sdk/metrics/meter_provider_factory.h"
#include "opentelemetry/sdk/metrics/view/view_registry_factory.h"
#include "opentelemetry/sdk/logs/batch_log_record_processor_factory.h"
#include "opentelemetry/sdk/logs/batch_log_record_processor_options.h"
#include "opentelemetry/sdk/logs/logger_provider_factory.h"
#include "opentelemetry/sdk/resource/resource.h"
#include "opentelemetry/logs/provider.h"
#include "opentelemetry/metrics/provider.h"
#include "opentelemetry/trace/provider.h"

#ifdef WITH_YAML_SDK_CONFIG
#include "opentelemetry/exporters/ostream/console_span_builder.h"
#include "opentelemetry/exporters/otlp/otlp_http_span_builder.h"
#include "opentelemetry/exporters/ostream/console_push_metric_builder.h"
#include "opentelemetry/exporters/otlp/otlp_http_push_metric_builder.h"
#include "opentelemetry/exporters/ostream/console_log_record_builder.h"
#include "opentelemetry/exporters/otlp/otlp_http_log_record_builder.h"
#ifdef WITH_OTLP_GRPC
#include "opentelemetry/exporters/otlp/otlp_grpc_span_builder.h"
#include "opentelemetry/exporters/otlp/otlp_grpc_push_metric_builder.h"
#include "opentelemetry/exporters/otlp/otlp_grpc_log_record_builder.h"
#endif
#include "opentelemetry/sdk/configuration/always_on_sampler_configuration.h"
#include "opentelemetry/sdk/configuration/parent_based_sampler_configuration.h"
#include "opentelemetry/sdk/configuration/yaml_configuration_parser.h"
#endif  // WITH_YAML_SDK_CONFIG

namespace otel_wrapper {

namespace logs_api    = opentelemetry::logs;
namespace metrics_api = opentelemetry::metrics;
namespace trace_api   = opentelemetry::trace;
namespace nostd       = opentelemetry::nostd;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static opentelemetry::sdk::resource::Resource make_resource(
    const std::vector<std::pair<std::string, std::string>>& attrs)
{
    opentelemetry::sdk::resource::ResourceAttributes ra;
    for (const auto& kv : attrs) ra[kv.first] = kv.second;
    return opentelemetry::sdk::resource::Resource::Create(ra);
}

static opentelemetry::exporter::otlp::OtlpHeaders make_headers(
    const std::vector<std::pair<std::string, std::string>>& pairs)
{
    opentelemetry::exporter::otlp::OtlpHeaders h;
    for (const auto& kv : pairs) h.emplace(kv.first, kv.second);
    return h;
}

// ---------------------------------------------------------------------------
// Programmatic constructor
// ---------------------------------------------------------------------------

SDKWrapper::SDKWrapper(const ProgrammaticConfig& cfg) {
    auto resource = make_resource(cfg.resource_attributes);

    namespace trace_sdk   = opentelemetry::sdk::trace;
    namespace metrics_sdk = opentelemetry::sdk::metrics;
    namespace logs_sdk    = opentelemetry::sdk::logs;

    // Traces
    if (!cfg.traces.endpoint.empty()) {
        opentelemetry::exporter::otlp::OtlpHttpExporterOptions opts;
        opts.url              = cfg.traces.endpoint;
        opts.http_headers     = make_headers(cfg.traces.headers);
        opts.ssl_ca_cert_path     = cfg.traces.ca_file;
        opts.ssl_client_key_path  = cfg.traces.key_file;
        opts.ssl_client_cert_path = cfg.traces.cert_file;

        auto exporter     = opentelemetry::exporter::otlp::OtlpHttpExporterFactory::Create(opts);
        auto processor    = trace_sdk::BatchSpanProcessorFactory::Create(
                                std::move(exporter), trace_sdk::BatchSpanProcessorOptions{});
        auto root_sampler = trace_sdk::AlwaysOnSamplerFactory::Create();
        auto sampler      = trace_sdk::ParentBasedSamplerFactory::Create(std::move(root_sampler));
        auto sdk_tp       = std::shared_ptr<trace_sdk::TracerProvider>(
                                trace_sdk::TracerProviderFactory::Create(
                                    std::move(processor), resource, std::move(sampler)));
        trace_api::Provider::SetTracerProvider(
            nostd::shared_ptr<trace_api::TracerProvider>(
                std::static_pointer_cast<trace_api::TracerProvider>(sdk_tp)));
        tracer_ = std::make_shared<TracerProviderWrapper>(sdk_tp);
    }

    // Metrics
    if (!cfg.metrics.endpoint.empty()) {
        opentelemetry::exporter::otlp::OtlpHttpMetricExporterOptions opts;
        opts.url              = cfg.metrics.endpoint;
        opts.http_headers     = make_headers(cfg.metrics.headers);
        opts.ssl_ca_cert_path     = cfg.metrics.ca_file;
        opts.ssl_client_key_path  = cfg.metrics.key_file;
        opts.ssl_client_cert_path = cfg.metrics.cert_file;

        auto exporter = opentelemetry::exporter::otlp::OtlpHttpMetricExporterFactory::Create(opts);
        opentelemetry::sdk::metrics::PeriodicExportingMetricReaderOptions reader_opts;
        reader_opts.export_interval_millis = std::chrono::milliseconds(cfg.metric_interval_ms);
        reader_opts.export_timeout_millis  = std::chrono::milliseconds(cfg.metric_timeout_ms);
        auto reader  = opentelemetry::sdk::metrics::PeriodicExportingMetricReaderFactory::Create(
                           std::move(exporter), reader_opts);
        auto sdk_mp_unique = metrics_sdk::MeterProviderFactory::Create(
                                 metrics_sdk::ViewRegistryFactory::Create(), resource);
        sdk_mp_unique->AddMetricReader(
            std::shared_ptr<metrics_sdk::MetricReader>(std::move(reader)));
        auto sdk_mp = std::shared_ptr<metrics_sdk::MeterProvider>(std::move(sdk_mp_unique));
        metrics_api::Provider::SetMeterProvider(
            nostd::shared_ptr<metrics_api::MeterProvider>(
                std::static_pointer_cast<metrics_api::MeterProvider>(sdk_mp)));
        meter_ = std::make_shared<MeterProviderWrapper>(sdk_mp);
    }

    // Logs
    if (!cfg.logs.endpoint.empty()) {
        opentelemetry::exporter::otlp::OtlpHttpLogRecordExporterOptions opts;
        opts.url              = cfg.logs.endpoint;
        opts.http_headers     = make_headers(cfg.logs.headers);
        opts.ssl_ca_cert_path     = cfg.logs.ca_file;
        opts.ssl_client_key_path  = cfg.logs.key_file;
        opts.ssl_client_cert_path = cfg.logs.cert_file;

        auto exporter  = opentelemetry::exporter::otlp::OtlpHttpLogRecordExporterFactory::Create(opts);
        auto processor = logs_sdk::BatchLogRecordProcessorFactory::Create(
                             std::move(exporter), logs_sdk::BatchLogRecordProcessorOptions{});
        auto sdk_lp    = std::shared_ptr<logs_sdk::LoggerProvider>(
                             logs_sdk::LoggerProviderFactory::Create(std::move(processor), resource));
        logs_api::Provider::SetLoggerProvider(
            nostd::shared_ptr<logs_api::LoggerProvider>(
                std::static_pointer_cast<logs_api::LoggerProvider>(sdk_lp)));
        logger_ = std::make_shared<LoggerProviderWrapper>(sdk_lp);
    }
}

// ---------------------------------------------------------------------------
// YAML constructor (optional)
// ---------------------------------------------------------------------------

#ifdef WITH_YAML_SDK_CONFIG

SDKWrapper::SDKWrapper(const std::string& path) {
    std::shared_ptr<opentelemetry::sdk::configuration::Registry> registry(
        new opentelemetry::sdk::configuration::Registry);

    opentelemetry::exporter::trace::ConsoleSpanBuilder::Register(registry.get());
    opentelemetry::exporter::otlp::OtlpHttpSpanBuilder::Register(registry.get());
    opentelemetry::exporter::metrics::ConsolePushMetricBuilder::Register(registry.get());
    opentelemetry::exporter::otlp::OtlpHttpPushMetricBuilder::Register(registry.get());
    opentelemetry::exporter::logs::ConsoleLogRecordBuilder::Register(registry.get());
    opentelemetry::exporter::otlp::OtlpHttpLogRecordBuilder::Register(registry.get());
#ifdef WITH_OTLP_GRPC
    opentelemetry::exporter::otlp::OtlpGrpcSpanBuilder::Register(registry.get());
    opentelemetry::exporter::otlp::OtlpGrpcPushMetricBuilder::Register(registry.get());
    opentelemetry::exporter::otlp::OtlpGrpcLogRecordBuilder::Register(registry.get());
#endif

    auto model = opentelemetry::sdk::configuration::YamlConfigurationParser::ParseFile(path);
    if (!model) throw std::runtime_error("Failed to parse config: " + path);

    if (model->tracer_provider && !model->tracer_provider->sampler) {
        auto root = std::make_unique<opentelemetry::sdk::configuration::AlwaysOnSamplerConfiguration>();
        auto parent_based = std::make_unique<opentelemetry::sdk::configuration::ParentBasedSamplerConfiguration>();
        parent_based->root = std::move(root);
        model->tracer_provider->sampler = std::move(parent_based);
    }

    auto configured_sdk_unique = opentelemetry::sdk::configuration::ConfiguredSdk::Create(registry, model);
    if (!configured_sdk_unique) throw std::runtime_error("Unsupported configuration: " + path);

    auto configured_sdk = std::shared_ptr<opentelemetry::sdk::configuration::ConfiguredSdk>(
        std::move(configured_sdk_unique));
    configured_sdk->Install();
    sdk_ = configured_sdk;

    if (configured_sdk->tracer_provider) {
        auto std_tp = std::static_pointer_cast<trace_api::TracerProvider>(configured_sdk->tracer_provider);
        nostd::shared_ptr<trace_api::TracerProvider> tp(std_tp);
        trace_api::Provider::SetTracerProvider(tp);
        tracer_ = std::make_shared<TracerProviderWrapper>(configured_sdk->tracer_provider);
    }
    if (configured_sdk->meter_provider) {
        auto std_mp = std::static_pointer_cast<metrics_api::MeterProvider>(configured_sdk->meter_provider);
        nostd::shared_ptr<metrics_api::MeterProvider> mp(std_mp);
        metrics_api::Provider::SetMeterProvider(mp);
        meter_ = std::make_shared<MeterProviderWrapper>(configured_sdk->meter_provider);
    }
    if (configured_sdk->logger_provider) {
        auto std_lp = std::static_pointer_cast<logs_api::LoggerProvider>(configured_sdk->logger_provider);
        nostd::shared_ptr<logs_api::LoggerProvider> lp(std_lp);
        logs_api::Provider::SetLoggerProvider(lp);
        logger_ = std::make_shared<LoggerProviderWrapper>(configured_sdk->logger_provider);
    }
}

#endif  // WITH_YAML_SDK_CONFIG

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

SDKWrapper::~SDKWrapper() {
    shutdown();
}

void SDKWrapper::release_config() {
#ifdef WITH_YAML_SDK_CONFIG
    sdk_.reset();
#endif
}

void SDKWrapper::shutdown() {
#ifdef WITH_YAML_SDK_CONFIG
    if (sdk_) {
        std::static_pointer_cast<opentelemetry::sdk::configuration::ConfiguredSdk>(sdk_)->UnInstall();
        sdk_.reset();
    } else {
#endif
        if (tracer_) tracer_->shutdown();
        if (meter_)  meter_->shutdown();
        if (logger_) logger_->shutdown();
        trace_api::Provider::SetTracerProvider(
            nostd::shared_ptr<trace_api::TracerProvider>(new trace_api::NoopTracerProvider()));
        metrics_api::Provider::SetMeterProvider(
            nostd::shared_ptr<metrics_api::MeterProvider>(new metrics_api::NoopMeterProvider()));
        logs_api::Provider::SetLoggerProvider(
            nostd::shared_ptr<logs_api::LoggerProvider>(new logs_api::NoopLoggerProvider()));
#ifdef WITH_YAML_SDK_CONFIG
    }
#endif
    tracer_.reset();
    meter_.reset();
    logger_.reset();
}

} // namespace otel_wrapper
