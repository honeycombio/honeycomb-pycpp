#include "tracer_wrapper.h"

#include "opentelemetry/exporters/ostream/span_exporter.h"
#include "opentelemetry/exporters/otlp/otlp_http_exporter.h"
#include "opentelemetry/sdk/trace/tracer_provider.h"
#include "opentelemetry/sdk/trace/simple_processor.h"
#include "opentelemetry/sdk/trace/batch_span_processor.h"
#include "opentelemetry/sdk/trace/tracer_provider_factory.h"
#include "opentelemetry/sdk/resource/resource.h"
#include "opentelemetry/semconv/service_attributes.h"
#include "opentelemetry/trace/span_startoptions.h"
#include "opentelemetry/trace/scope.h"
#include "opentelemetry/trace/context.h"
#include "opentelemetry/context/context.h"

#include <iostream>
#include <sstream>
#include <iomanip>

namespace otel_wrapper {

namespace trace_api = opentelemetry::trace;
namespace trace_sdk = opentelemetry::sdk::trace;
namespace resource = opentelemetry::sdk::resource;
namespace nostd = opentelemetry::nostd;
namespace context = opentelemetry::context;

// ContextWrapper Implementation
ContextWrapper::ContextWrapper()
    : context_(context::RuntimeContext::GetCurrent()) {}

ContextWrapper::ContextWrapper(const context::Context& ctx)
    : context_(ctx) {}

std::shared_ptr<ContextWrapper> ContextWrapper::get_current() {
    return std::make_shared<ContextWrapper>(context::RuntimeContext::GetCurrent());
}

std::shared_ptr<ContextWrapper> ContextWrapper::attach() {
    auto token = context::RuntimeContext::GetCurrent();
    context::RuntimeContext::Attach(context_);
    return std::make_shared<ContextWrapper>(token);
}

void ContextWrapper::detach(std::shared_ptr<ContextWrapper> token) {
    if (token) {
        context::RuntimeContext::Attach(token->get_context());
    }
}

// SpanWrapper Implementation
SpanWrapper::SpanWrapper(nostd::shared_ptr<trace_api::Span> span)
    : span_(span), scope_(std::nullopt) {}

SpanWrapper::SpanWrapper(nostd::shared_ptr<trace_api::Span> span,
                        trace_api::Scope scope)
    : span_(span), scope_(std::move(scope)) {}

SpanWrapper::~SpanWrapper() {
    // Scope is automatically detached when destroyed
    if (span_ && is_recording()) {
        span_->End();
    }
}

void SpanWrapper::set_attribute(const std::string& key, const std::string& value) {
    if (span_) {
        span_->SetAttribute(key, value);
    }
}

void SpanWrapper::set_attribute(const std::string& key, int64_t value) {
    if (span_) {
        span_->SetAttribute(key, value);
    }
}

void SpanWrapper::set_attribute(const std::string& key, double value) {
    if (span_) {
        span_->SetAttribute(key, value);
    }
}

void SpanWrapper::set_attribute(const std::string& key, bool value) {
    if (span_) {
        span_->SetAttribute(key, value);
    }
}

void SpanWrapper::add_event(const std::string& name) {
    if (span_) {
        span_->AddEvent(name);
    }
}

void SpanWrapper::add_event(const std::string& name,
                            const std::map<std::string, std::string>& attributes) {
    if (span_) {
        std::map<std::string, opentelemetry::common::AttributeValue> attrs;
        for (const auto& [key, value] : attributes) {
            attrs[key] = value;
        }
        span_->AddEvent(name, attrs);
    }
}

void SpanWrapper::set_status(int status_code, const std::string& description) {
    if (span_) {
        auto code = static_cast<trace_api::StatusCode>(status_code);
        span_->SetStatus(code, description);
    }
}

void SpanWrapper::end() {
    if (span_) {
        span_->End();
    }
}

bool SpanWrapper::is_recording() const {
    return span_ && span_->IsRecording();
}

std::string SpanWrapper::get_span_context_trace_id() const {
    if (!span_) return "";

    auto span_context = span_->GetContext();
    auto trace_id = span_context.trace_id();

    std::stringstream ss;
    ss << std::hex << std::setfill('0');
    for (auto byte : trace_id.Id()) {
        ss << std::setw(2) << static_cast<int>(byte);
    }
    return ss.str();
}

std::string SpanWrapper::get_span_context_span_id() const {
    if (!span_) return "";

    auto span_context = span_->GetContext();
    auto span_id = span_context.span_id();

    std::stringstream ss;
    ss << std::hex << std::setfill('0');
    for (auto byte : span_id.Id()) {
        ss << std::setw(2) << static_cast<int>(byte);
    }
    return ss.str();
}

std::shared_ptr<ContextWrapper> SpanWrapper::get_context() const {
    if (!span_) return nullptr;

    // Get current context and set this span as active in it
    auto ctx = context::RuntimeContext::GetCurrent();
    auto span_ctx = opentelemetry::trace::SetSpan(ctx, span_);
    return std::make_shared<ContextWrapper>(span_ctx);
}

// TracerWrapper Implementation
TracerWrapper::TracerWrapper(nostd::shared_ptr<trace_api::Tracer> tracer)
    : tracer_(tracer) {}

std::shared_ptr<SpanWrapper> TracerWrapper::start_span(
    const std::string& name,
    const std::map<std::string, std::string>& attributes,
    std::shared_ptr<ContextWrapper> context) {

    if (!tracer_) return nullptr;

    trace_api::StartSpanOptions options;

    // Use provided context as parent, or no specific parent
    if (context) {
        options.parent = context->get_context();
    }

    auto span = tracer_->StartSpan(name, options);

    // Set attributes after span creation
    for (const auto& [key, value] : attributes) {
        span->SetAttribute(key, value);
    }

    return std::make_shared<SpanWrapper>(span);
}

std::shared_ptr<SpanWrapper> TracerWrapper::start_as_current_span(
    const std::string& name,
    const std::map<std::string, std::string>& attributes,
    std::shared_ptr<ContextWrapper> context) {

    if (!tracer_) return nullptr;

    trace_api::StartSpanOptions options;

    // Use provided context as parent, or current context
    if (context) {
        options.parent = context->get_context();
    } else {
        options.parent = context::RuntimeContext::GetCurrent();
    }

    auto span = tracer_->StartSpan(name, options);

    // Set attributes after span creation
    for (const auto& [key, value] : attributes) {
        span->SetAttribute(key, value);
    }

    // Make this span the current span by creating a scope
    auto scope = tracer_->WithActiveSpan(span);

    return std::make_shared<SpanWrapper>(span, std::move(scope));
}

// TracerProviderWrapper Implementation
TracerProviderWrapper::TracerProviderWrapper(const std::string& service_name,
                                             const std::string& exporter_type)
    : service_name_(service_name) {

    if (exporter_type == "console" || exporter_type == "ostream") {
        initialize_console_exporter();
    } else if (exporter_type == "otlp" || exporter_type == "otlp_http") {
        initialize_otlp_exporter();
    } else {
        // Default to console
        initialize_console_exporter();
    }
}

TracerProviderWrapper::~TracerProviderWrapper() {
    shutdown();
}

void TracerProviderWrapper::initialize_console_exporter() {
    // Create an ostream exporter
    auto exporter = std::unique_ptr<trace_sdk::SpanExporter>(
        new opentelemetry::exporter::trace::OStreamSpanExporter);

    // Create a simple processor
    auto processor = std::unique_ptr<trace_sdk::SpanProcessor>(
        new trace_sdk::SimpleSpanProcessor(std::move(exporter)));

    // Create resource with service name
    auto resource_attributes = resource::ResourceAttributes{
        {opentelemetry::semconv::service::kServiceName, service_name_}
    };
    auto resource_ = resource::Resource::Create(resource_attributes);

    // Create tracer provider
    provider_ = std::shared_ptr<trace_sdk::TracerProvider>(
        new trace_sdk::TracerProvider(std::move(processor), resource_));

    // Set as global provider
    std::shared_ptr<trace_api::TracerProvider> api_provider = provider_;
    trace_api::Provider::SetTracerProvider(api_provider);
}

void TracerProviderWrapper::initialize_otlp_exporter(const std::string& endpoint) {
    // Create OTLP HTTP exporter
    opentelemetry::exporter::otlp::OtlpHttpExporterOptions opts;
    if (!endpoint.empty()) {
        opts.url = endpoint;
    } else {
        opts.url = "http://localhost:4318/v1/traces";
    }

    auto exporter = std::unique_ptr<trace_sdk::SpanExporter>(
        new opentelemetry::exporter::otlp::OtlpHttpExporter(opts));

    // Create a batch processor for better performance
    trace_sdk::BatchSpanProcessorOptions batch_opts;
    batch_opts.max_queue_size = 1000000;
    auto processor = std::unique_ptr<trace_sdk::SpanProcessor>(
        new trace_sdk::BatchSpanProcessor(std::move(exporter), batch_opts));

    // Create resource with service name
    auto resource_attributes = resource::ResourceAttributes{
        {opentelemetry::semconv::service::kServiceName, service_name_}
    };
    auto resource_ = resource::Resource::Create(resource_attributes);

    // Create tracer provider
    provider_ = std::shared_ptr<trace_sdk::TracerProvider>(
        new trace_sdk::TracerProvider(std::move(processor), resource_));

    // Set as global provider
    std::shared_ptr<trace_api::TracerProvider> api_provider = provider_;
    // std::shared_ptr<trace_api::TracerProvider> provider = trace_sdk::TracerProviderFactory::Create(std::move(processor));
    trace_api::Provider::SetTracerProvider(api_provider);
}

std::shared_ptr<TracerWrapper> TracerProviderWrapper::get_tracer(
    const std::string& name,
    const std::string& version) {

    if (!provider_) return nullptr;

    auto tracer = provider_->GetTracer(name, version);
    return std::make_shared<TracerWrapper>(tracer);
}

void TracerProviderWrapper::shutdown() {
    if (provider_) {
        provider_->Shutdown();
    }
}

} // namespace otel_wrapper
