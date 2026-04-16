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
#include "opentelemetry/trace/default_span.h"
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

// Static storage for keeping OpenTelemetry Tokens alive
// Maps ContextWrapper token to the real OTel Token
static std::map<uint64_t, nostd::unique_ptr<context::Token>> token_storage;
static std::mutex token_mutex;
static uint64_t next_token_id = 1;

std::shared_ptr<ContextWrapper> ContextWrapper::attach() {
    // Attach new context - CRITICAL: Must keep the Token alive or context will be auto-detached!
    auto otel_token = context::RuntimeContext::Attach(context_);

    // Store the token to keep it alive
    std::lock_guard<std::mutex> lock(token_mutex);
    uint64_t token_id = next_token_id++;
    token_storage[token_id] = std::move(otel_token);

    // Return a wrapper containing the token ID for later detachment
    auto token_ctx = context::Context{}.SetValue("__token_id", token_id);
    return std::make_shared<ContextWrapper>(token_ctx);
}

void ContextWrapper::detach(std::shared_ptr<ContextWrapper> token) {
    if (token) {
        // Extract token ID from the context
        auto token_id_value = token->get_context().GetValue("__token_id");
        if (nostd::holds_alternative<uint64_t>(token_id_value)) {
            uint64_t token_id = nostd::get<uint64_t>(token_id_value);

            std::lock_guard<std::mutex> lock(token_mutex);
            auto it = token_storage.find(token_id);
            if (it != token_storage.end()) {
                // Detach by calling RuntimeContext::Detach with the stored Token
                context::RuntimeContext::Detach(*(it->second));
                token_storage.erase(it);
            }
        }
    }
}

std::shared_ptr<SpanWrapper> ContextWrapper::get_span() const {
    // Extract span from context using OpenTelemetry's GetSpan
    auto span = opentelemetry::trace::GetSpan(context_);

    if (!span || !span->GetContext().IsValid()) {
        return nullptr;
    }

    // Wrap the span in a SpanWrapper
    // Note: We don't create a scope here since we're just reading the span
    return std::make_shared<SpanWrapper>(span);
}

std::shared_ptr<ContextWrapper> ContextWrapper::create_with_span_context(
    const std::string& trace_id_hex,
    const std::string& span_id_hex,
    uint8_t trace_flags,
    bool is_remote) {

    // Convert hex strings to byte arrays
    auto hex_to_bytes = [](const std::string& hex) -> std::vector<uint8_t> {
        std::vector<uint8_t> bytes;
        for (size_t i = 0; i < hex.length(); i += 2) {
            std::string byte_str = hex.substr(i, 2);
            uint8_t byte = static_cast<uint8_t>(std::stoi(byte_str, nullptr, 16));
            bytes.push_back(byte);
        }
        return bytes;
    };

    try {
        // Parse trace ID (32 hex chars = 16 bytes)
        auto trace_id_bytes = hex_to_bytes(trace_id_hex);
        if (trace_id_bytes.size() != 16) {
            throw std::invalid_argument("Invalid trace_id length");
        }

        // Parse span ID (16 hex chars = 8 bytes)
        auto span_id_bytes = hex_to_bytes(span_id_hex);
        if (span_id_bytes.size() != 8) {
            throw std::invalid_argument("Invalid span_id length");
        }

        // Create TraceId and SpanId
        opentelemetry::trace::TraceId trace_id(trace_id_bytes);
        opentelemetry::trace::SpanId span_id(span_id_bytes);

        // Create SpanContext
        opentelemetry::trace::SpanContext span_context(
            trace_id,
            span_id,
            opentelemetry::trace::TraceFlags(trace_flags),
            is_remote
        );

        // Create a DefaultSpan with this context (non-recording span that just holds the context)
        auto span = nostd::shared_ptr<trace_api::Span>(
            new opentelemetry::trace::DefaultSpan(span_context)
        );

        // Get current context and set this span in it
        auto current_ctx = context::RuntimeContext::GetCurrent();
        auto new_ctx = opentelemetry::trace::SetSpan(current_ctx, span);

        return std::make_shared<ContextWrapper>(new_ctx);
    } catch (const std::exception& e) {
        std::cerr << "Failed to create context with span context: " << e.what() << std::endl;
        return nullptr;
    }
}

// Helper function to extract parent span ID from context
static std::string extract_parent_span_id(const context::Context& parent_context) {
    auto parent_span = opentelemetry::trace::GetSpan(parent_context);
    if (parent_span && parent_span->GetContext().IsValid()) {
        auto span_id = parent_span->GetContext().span_id();
        std::stringstream ss;
        ss << std::hex << std::setfill('0');
        for (auto byte : span_id.Id()) {
            ss << std::setw(2) << static_cast<int>(byte);
        }
        return ss.str();
    }
    return "";
}

// SpanWrapper Implementation
SpanWrapper::SpanWrapper(nostd::shared_ptr<trace_api::Span> span, int kind,
                        const context::Context& parent_context)
    : span_(span), scope_(std::nullopt), kind_(kind),
      parent_span_id_(extract_parent_span_id(parent_context)) {}

SpanWrapper::SpanWrapper(nostd::shared_ptr<trace_api::Span> span,
                        trace_api::Scope scope, int kind,
                        const context::Context& parent_context)
    : span_(span), scope_(std::move(scope)), kind_(kind),
      parent_span_id_(extract_parent_span_id(parent_context)) {}

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

void SpanWrapper::set_attribute(const std::string& key, const std::vector<std::string>& value) {
    if (span_) {
        std::vector<nostd::string_view> svs;
        svs.reserve(value.size());
        for (const auto& s : value) svs.push_back(s);
        span_->SetAttribute(key, nostd::span<const nostd::string_view>(svs.data(), svs.size()));
    }
}

void SpanWrapper::set_attribute(const std::string& key, const std::vector<int64_t>& value) {
    if (span_) {
        span_->SetAttribute(key, nostd::span<const int64_t>(value.data(), value.size()));
    }
}

void SpanWrapper::set_attribute(const std::string& key, const std::vector<double>& value) {
    if (span_) {
        span_->SetAttribute(key, nostd::span<const double>(value.data(), value.size()));
    }
}

void SpanWrapper::set_attribute(const std::string& key, const std::vector<bool>& value) {
    if (span_) {
        // std::vector<bool> is a bitset; copy to a contiguous bool array for nostd::span
        auto bool_arr = std::make_unique<bool[]>(value.size());
        for (size_t i = 0; i < value.size(); ++i) bool_arr[i] = value[i];
        span_->SetAttribute(key, nostd::span<const bool>(bool_arr.get(), value.size()));
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

void SpanWrapper::set_status(const Status& status) {
    if (span_) {
        auto code = static_cast<trace_api::StatusCode>(status.get_status_code());
        span_->SetStatus(code, status.get_description());
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

std::string SpanWrapper::get_parent_span_id() const {
    return parent_span_id_;
}

std::shared_ptr<SpanContextWrapper> SpanWrapper::get_span_context() const {
    if (!span_) return nullptr;

    auto sc = span_->GetContext();

    auto bytes_to_hex = [](auto id_bytes) -> std::string {
        std::stringstream ss;
        ss << std::hex << std::setfill('0');
        for (auto byte : id_bytes) {
            ss << std::setw(2) << static_cast<int>(byte);
        }
        return ss.str();
    };

    return std::make_shared<SpanContextWrapper>(
        bytes_to_hex(sc.trace_id().Id()),
        bytes_to_hex(sc.span_id().Id()),
        sc.trace_flags().flags(),
        sc.IsRemote(),
        sc.IsValid(),
        sc.trace_state()->ToHeader()
    );
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
    std::shared_ptr<ContextWrapper> context,
    int kind,
    uint64_t start_time) {

    if (!tracer_) return nullptr;

    trace_api::StartSpanOptions options;

    // Store the parent context for passing to SpanWrapper
    context::Context parent_ctx;

    // Use provided context as parent, or no specific parent
    if (context) {
        parent_ctx = context->get_context();
        options.parent = parent_ctx;
    }

    // Set span kind
    options.kind = static_cast<trace_api::SpanKind>(kind);

    // Set start time if provided (0 means use current time)
    if (start_time != 0) {
        options.start_steady_time = opentelemetry::common::SteadyTimestamp(
            std::chrono::nanoseconds(start_time));
        options.start_system_time = opentelemetry::common::SystemTimestamp(
            std::chrono::nanoseconds(start_time));
    }

    auto span = tracer_->StartSpan(name, options);

    // Set attributes after span creation
    for (const auto& [key, value] : attributes) {
        span->SetAttribute(key, value);
    }

    return std::make_shared<SpanWrapper>(span, kind, parent_ctx);
}

std::shared_ptr<SpanWrapper> TracerWrapper::start_as_current_span(
    const std::string& name,
    const std::map<std::string, std::string>& attributes,
    std::shared_ptr<ContextWrapper> context,
    int kind,
    uint64_t start_time) {

    if (!tracer_) return nullptr;

    trace_api::StartSpanOptions options;

    // Store the parent context for passing to SpanWrapper
    context::Context parent_ctx;

    // Use provided context as parent if specified, otherwise use current RuntimeContext
    // which contains the active span (if any)
    if (context) {
        parent_ctx = context->get_context();
        options.parent = parent_ctx;
    } else {
        // Explicitly get the current runtime context to pick up any active span
        parent_ctx = context::RuntimeContext::GetCurrent();
        options.parent = parent_ctx;
    }

    // Set span kind
    options.kind = static_cast<trace_api::SpanKind>(kind);

    // Set start time if provided (0 means use current time)
    if (start_time != 0) {
        options.start_steady_time = opentelemetry::common::SteadyTimestamp(
            std::chrono::nanoseconds(start_time));
        options.start_system_time = opentelemetry::common::SystemTimestamp(
            std::chrono::nanoseconds(start_time));
    }

    auto span = tracer_->StartSpan(name, options);

    // Set attributes after span creation
    for (const auto& [key, value] : attributes) {
        span->SetAttribute(key, value);
    }

    // Make this span the current span by creating a scope
    auto scope = tracer_->WithActiveSpan(span);

    return std::make_shared<SpanWrapper>(span, std::move(scope), kind, parent_ctx);
}

// TracerProviderWrapper Implementation
TracerProviderWrapper::TracerProviderWrapper(const std::string& service_name,
                                             const std::string& exporter_type)
    : service_name_(service_name) {

    if (exporter_type == "console" || exporter_type == "ostream") {
        initialize_console_exporter();
    } else {
        // Default to OTLP
        initialize_otlp_exporter();
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

void TracerProviderWrapper::initialize_otlp_exporter() {
    // Create OTLP HTTP exporter using env variables
    opentelemetry::exporter::otlp::OtlpHttpExporterOptions opts;

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
    py::object version,
    py::object schema_url,
    const std::map<std::string, std::string>& attributes,
    TracerProviderWrapper* provider) {

    // Use the provided provider or fall back to this instance's provider
    auto target_provider = (provider && provider->provider_) ? provider->provider_ : provider_;

    if (!target_provider) return nullptr;

    auto ver_str = (!version.is_none()) ? version.cast<std::string>() : "";
    auto schema_str = (!schema_url.is_none()) ? schema_url.cast<std::string>() : "";

    // TODO: Apply attributes to the InstrumentationScope when creating the tracer
    // For now, attributes are accepted but not used (requires OpenTelemetry C++ ABI v2 or manual Tracer construction)
    (void)attributes;  // Suppress unused parameter warning

    auto tracer = target_provider->GetTracer(name, ver_str, schema_str);
    return std::make_shared<TracerWrapper>(tracer);
}

void TracerProviderWrapper::shutdown() {
    if (provider_) {
        provider_->Shutdown();
    }
}

} // namespace otel_wrapper
