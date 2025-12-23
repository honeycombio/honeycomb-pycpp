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

private:
    context::Context context_;
};

class SpanWrapper {
public:
    explicit SpanWrapper(nostd::shared_ptr<trace_api::Span> span);
    explicit SpanWrapper(nostd::shared_ptr<trace_api::Span> span,
                        trace_api::Scope scope);
    ~SpanWrapper();

    void set_attribute(const std::string& key, const std::string& value);
    void set_attribute(const std::string& key, int64_t value);
    void set_attribute(const std::string& key, double value);
    void set_attribute(const std::string& key, bool value);

    void add_event(const std::string& name);
    void add_event(const std::string& name, const std::map<std::string, std::string>& attributes);

    void set_status(int status_code, const std::string& description = "");
    void end();

    bool is_recording() const;
    std::string get_span_context_trace_id() const;
    std::string get_span_context_span_id() const;

    // Get the context that contains this span
    std::shared_ptr<ContextWrapper> get_context() const;

private:
    nostd::shared_ptr<trace_api::Span> span_;
    std::optional<trace_api::Scope> scope_;  // Manages active span context
};

class TracerWrapper {
public:
    explicit TracerWrapper(nostd::shared_ptr<trace_api::Tracer> tracer);

    // Start a span without making it current
    std::shared_ptr<SpanWrapper> start_span(
        const std::string& name,
        const std::map<std::string, std::string>& attributes = {},
        std::shared_ptr<ContextWrapper> context = nullptr);

    // Start a span and make it the current active span
    std::shared_ptr<SpanWrapper> start_as_current_span(
        const std::string& name,
        const std::map<std::string, std::string>& attributes = {},
        std::shared_ptr<ContextWrapper> context = nullptr);

private:
    nostd::shared_ptr<trace_api::Tracer> tracer_;
};

class TracerProviderWrapper {
public:
    TracerProviderWrapper(const std::string& service_name,
                         const std::string& exporter_type = "console");
    ~TracerProviderWrapper();

    std::shared_ptr<TracerWrapper> get_tracer(const std::string& name,
                                              const std::string& version = "");

    void shutdown();

private:
    void initialize_console_exporter();
    void initialize_otlp_exporter(const std::string& endpoint = "");

    std::shared_ptr<trace_sdk::TracerProvider> provider_;
    std::string service_name_;
};

} // namespace otel_wrapper
