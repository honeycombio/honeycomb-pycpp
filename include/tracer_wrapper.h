#pragma once

#include <memory>
#include <string>
#include <map>
#include <vector>
#include <optional>

#include "opentelemetry/trace/provider.h"
#include "opentelemetry/trace/tracer.h"
#include "opentelemetry/trace/scope.h"
#include "opentelemetry/context/context.h"
#include "opentelemetry/sdk/trace/tracer_provider.h"
#include "opentelemetry/sdk/trace/simple_processor.h"
#include "opentelemetry/sdk/trace/batch_span_processor.h"

namespace otel_wrapper {

namespace trace_api = opentelemetry::trace;
namespace trace_sdk = opentelemetry::sdk::trace;
namespace nostd = opentelemetry::nostd;
namespace context = opentelemetry::context;

// Forward declaration
class SpanWrapper;

// Status class to wrap and match Python OpenTelemetry API (opentelemetry.trace.status.Status)
class Status {
public:
    Status(int status_code, const std::string& description = "")
        : status_code_(status_code), description_(description) {
        // Validate that description is only set for ERROR status
        // In Python OpenTelemetry, description should only be used with StatusCode.ERROR
        if (!description.empty() && status_code != static_cast<int>(trace_api::StatusCode::kError)) {
            // Clear description if status is not ERROR
            description_ = "";
        }
    }

    int get_status_code() const { return status_code_; }
    std::string get_description() const { return description_; }

    // Helper methods to match Python OpenTelemetry Status API
    bool is_ok() const { return status_code_ == static_cast<int>(trace_api::StatusCode::kOk); }
    bool is_unset() const { return status_code_ == static_cast<int>(trace_api::StatusCode::kUnset); }

private:
    int status_code_;
    std::string description_;
};

// Context wrapper to manage OpenTelemetry context
class ContextWrapper {
public:
    ContextWrapper();
    explicit ContextWrapper(const context::Context& ctx);

    context::Context get_context() const { return context_; }

    // Get the current context
    static std::shared_ptr<ContextWrapper> get_current();

    // Set this context as current and return a token for restoring
    std::shared_ptr<ContextWrapper> attach();

    // Detach and restore previous context
    static void detach(std::shared_ptr<ContextWrapper> token);

    // Get the active span from this context (if any)
    std::shared_ptr<SpanWrapper> get_span() const;

    // Create a context with a span context (for bridging Python spans)
    static std::shared_ptr<ContextWrapper> create_with_span_context(
        const std::string& trace_id_hex,
        const std::string& span_id_hex,
        uint8_t trace_flags = 1,
        bool is_remote = true);

private:
    context::Context context_;
};

class SpanWrapper {
public:
    explicit SpanWrapper(nostd::shared_ptr<trace_api::Span> span, int kind = 0,
                        const context::Context& parent_context = context::Context{});
    explicit SpanWrapper(nostd::shared_ptr<trace_api::Span> span,
                        trace_api::Scope scope, int kind = 0,
                        const context::Context& parent_context = context::Context{});
    ~SpanWrapper();

    void set_attribute(const std::string& key, const std::string& value);
    void set_attribute(const std::string& key, int64_t value);
    void set_attribute(const std::string& key, double value);
    void set_attribute(const std::string& key, bool value);

    void add_event(const std::string& name);
    void add_event(const std::string& name, const std::map<std::string, std::string>& attributes);

    void set_status(const Status& status);
    void end();

    bool is_recording() const;
    std::string get_span_context_trace_id() const;
    std::string get_span_context_span_id() const;
    std::string get_parent_span_id() const;

    // Get the span kind
    int get_kind() const { return kind_; }

    // Get the context that contains this span
    std::shared_ptr<ContextWrapper> get_context() const;

private:
    nostd::shared_ptr<trace_api::Span> span_;
    std::optional<trace_api::Scope> scope_;  // Manages active span context
    int kind_;  // SpanKind value
    std::string parent_span_id_;  // Parent span ID (extracted from parent context)
};

class TracerWrapper {
public:
    explicit TracerWrapper(nostd::shared_ptr<trace_api::Tracer> tracer);

    // Start a span without making it current
    std::shared_ptr<SpanWrapper> start_span(
        const std::string& name,
        const std::map<std::string, std::string>& attributes = {},
        std::shared_ptr<ContextWrapper> context = nullptr,
        int kind = 0,  // SpanKind::kInternal
        uint64_t start_time = 0);  // 0 means use current time

    // Start a span and make it the current active span
    std::shared_ptr<SpanWrapper> start_as_current_span(
        const std::string& name,
        const std::map<std::string, std::string>& attributes = {},
        std::shared_ptr<ContextWrapper> context = nullptr,
        int kind = 0,  // SpanKind::kInternal
        uint64_t start_time = 0);  // 0 means use current time

private:
    nostd::shared_ptr<trace_api::Tracer> tracer_;
};

class TracerProviderWrapper {
public:
    TracerProviderWrapper(const std::string& service_name,
                         const std::string& exporter_type = "console");
    ~TracerProviderWrapper();

    std::shared_ptr<TracerWrapper> get_tracer(
        const std::string& name,
        const std::string& version = "",
        const std::string& schema_url = "",
        const std::map<std::string, std::string>& attributes = {},
        TracerProviderWrapper* provider = nullptr);

    void shutdown();

private:
    void initialize_console_exporter();
    void initialize_otlp_exporter(const std::string& endpoint = "");

    std::shared_ptr<trace_sdk::TracerProvider> provider_;
    std::string service_name_;
};

} // namespace otel_wrapper
