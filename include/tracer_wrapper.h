#pragma once

#include <memory>
#include <string>
#include <map>
#include <vector>
#include <optional>

#include <pybind11/pybind11.h>
#include "opentelemetry/common/attribute_value.h"
#include "opentelemetry/common/key_value_iterable.h"
#include "opentelemetry/trace/provider.h"
#include "opentelemetry/trace/tracer.h"
#include "opentelemetry/trace/scope.h"
#include "opentelemetry/context/context.h"
#include "opentelemetry/sdk/trace/tracer_provider.h"
#include "opentelemetry/sdk/trace/simple_processor.h"
#include "opentelemetry/sdk/trace/batch_span_processor.h"
#include "opentelemetry/sdk/configuration/configuration.h"
#include "opentelemetry/sdk/configuration/configured_sdk.h"

namespace py = pybind11;
namespace otel_wrapper {

namespace trace_api = opentelemetry::trace;
namespace trace_sdk = opentelemetry::sdk::trace;
namespace nostd = opentelemetry::nostd;
namespace context = opentelemetry::context;

// Forward declaration
class SpanWrapper;

// SpanContext wrapper to expose span context fields (mirrors opentelemetry.trace.SpanContext)
class SpanContextWrapper {
public:
    SpanContextWrapper(const std::string& trace_id,
                       const std::string& span_id,
                       uint8_t trace_flags,
                       bool is_remote,
                       bool is_valid,
                       const std::string& trace_state = "")
        : trace_id_(trace_id), span_id_(span_id), trace_flags_(trace_flags),
          is_remote_(is_remote), is_valid_(is_valid), trace_state_(trace_state) {}

    std::string get_trace_id() const { return trace_id_; }
    std::string get_span_id() const { return span_id_; }
    uint8_t get_trace_flags() const { return trace_flags_; }
    bool get_is_remote() const { return is_remote_; }
    bool get_is_valid() const { return is_valid_; }
    std::string get_trace_state() const { return trace_state_; }

private:
    std::string trace_id_;
    std::string span_id_;
    uint8_t trace_flags_;
    bool is_remote_;
    bool is_valid_;
    std::string trace_state_;
};

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
    void set_attribute(const std::string& key, const std::vector<std::string>& value);
    void set_attribute(const std::string& key, const std::vector<int64_t>& value);
    void set_attribute(const std::string& key, const std::vector<double>& value);
    void set_attribute(const std::string& key, const std::vector<bool>& value);

    void add_event(const std::string& name);
    void add_event(const std::string& name, uint64_t timestamp_ns);
    void add_event(const std::string& name, const opentelemetry::common::KeyValueIterable& attributes);
    void add_event(const std::string& name, const opentelemetry::common::KeyValueIterable& attributes, uint64_t timestamp_ns);

    // add_link requires OpenTelemetry C++ ABI v2. On ABI v1 the call is a no-op.
    void add_link(const SpanContextWrapper& link_context,
                  const opentelemetry::common::KeyValueIterable& attributes);

    void set_status(const Status& status);
    void update_name(const std::string& name);
    void end(std::optional<uint64_t> end_time_ns = std::nullopt);

    bool is_recording() const;
    std::string get_span_context_trace_id() const;
    std::string get_span_context_span_id() const;
    std::string get_parent_span_id() const;
    std::shared_ptr<SpanContextWrapper> get_span_context() const;

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
        const opentelemetry::common::KeyValueIterable* attributes = nullptr,
        std::shared_ptr<ContextWrapper> context = nullptr,
        int kind = 0,  // SpanKind::kInternal
        uint64_t start_time = 0);  // 0 means use current time

    // Start a span and make it the current active span
    std::shared_ptr<SpanWrapper> start_as_current_span(
        const std::string& name,
        const opentelemetry::common::KeyValueIterable* attributes = nullptr,
        std::shared_ptr<ContextWrapper> context = nullptr,
        int kind = 0,  // SpanKind::kInternal
        uint64_t start_time = 0);  // 0 means use current time

private:
    nostd::shared_ptr<trace_api::Tracer> tracer_;
};

class TracerProviderWrapper {
public:
    TracerProviderWrapper(const std::string& path);
    ~TracerProviderWrapper();

    std::shared_ptr<TracerWrapper> get_tracer(
        const std::string& name,
        py::object version = py::none(),
        py::object schema_url = py::none(),
        const opentelemetry::common::KeyValueIterable* attributes = nullptr,
        TracerProviderWrapper* provider = nullptr);

    void shutdown();

private:
    std::unique_ptr<opentelemetry::sdk::configuration::ConfiguredSdk> sdk_;
    std::string service_name_;
};

} // namespace otel_wrapper
