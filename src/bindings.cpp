#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/functional.h>

#include "tracer_wrapper.h"

namespace py = pybind11;

namespace {
void set_span_attribute(otel_wrapper::SpanWrapper& span,
                        const std::string& key,
                        py::object value) {
    if (py::isinstance<py::bool_>(value)) {
        span.set_attribute(key, value.cast<bool>());
    } else if (py::isinstance<py::int_>(value)) {
        span.set_attribute(key, value.cast<int64_t>());
    } else if (py::isinstance<py::float_>(value)) {
        span.set_attribute(key, value.cast<double>());
    } else if (py::isinstance<py::str>(value)) {
        span.set_attribute(key, value.cast<std::string>());
    } else if (py::isinstance<py::sequence>(value)) {
        auto seq = value.cast<py::sequence>();
        if (seq.size() == 0) return;
        py::object first = seq[0];
        if (py::isinstance<py::bool_>(first)) {
            std::vector<bool> v;
            v.reserve(seq.size());
            for (auto item : seq) v.push_back(item.cast<bool>());
            span.set_attribute(key, v);
        } else if (py::isinstance<py::int_>(first)) {
            std::vector<int64_t> v;
            v.reserve(seq.size());
            for (auto item : seq) v.push_back(item.cast<int64_t>());
            span.set_attribute(key, v);
        } else if (py::isinstance<py::float_>(first)) {
            std::vector<double> v;
            v.reserve(seq.size());
            for (auto item : seq) v.push_back(item.cast<double>());
            span.set_attribute(key, v);
        } else if (py::isinstance<py::str>(first)) {
            std::vector<std::string> v;
            v.reserve(seq.size());
            for (auto item : seq) v.push_back(item.cast<std::string>());
            span.set_attribute(key, v);
        } else {
            throw py::type_error("Sequence elements must be str, bool, int, or float");
        }
    } else {
        throw py::type_error(
            "Attribute value must be str, bool, int, float, or a homogeneous sequence thereof");
    }
}
}  // namespace

PYBIND11_MODULE(otel_cpp_tracer, m) {
    m.doc() = "Python bindings for OpenTelemetry C++ SDK tracing";

    // SpanStatusCode enum
    py::enum_<opentelemetry::trace::StatusCode>(m, "StatusCode")
        .value("UNSET", opentelemetry::trace::StatusCode::kUnset)
        .value("OK", opentelemetry::trace::StatusCode::kOk)
        .value("ERROR", opentelemetry::trace::StatusCode::kError)
        .export_values();

    // Status class - wrapper for opentelemetry.trace.status.Status
    py::class_<otel_wrapper::Status>(m, "Status")
        .def(py::init<int, const std::string&>(),
             py::arg("status_code"),
             py::arg("description") = "",
             "Create a Status object with status code and optional description.\n"
             "Note: description should only be set when status_code is StatusCode.ERROR")
        .def_property_readonly("status_code", &otel_wrapper::Status::get_status_code,
                              "Get the status code")
        .def_property_readonly("description", &otel_wrapper::Status::get_description,
                              "Get the status description")
        .def_property_readonly("is_ok", &otel_wrapper::Status::is_ok,
                              "Returns True if status code is OK")
        .def_property_readonly("is_unset", &otel_wrapper::Status::is_unset,
                              "Returns True if status code is UNSET");

    // SpanKind enum
    py::enum_<opentelemetry::trace::SpanKind>(m, "SpanKind")
        .value("INTERNAL", opentelemetry::trace::SpanKind::kInternal)
        .value("SERVER", opentelemetry::trace::SpanKind::kServer)
        .value("CLIENT", opentelemetry::trace::SpanKind::kClient)
        .value("PRODUCER", opentelemetry::trace::SpanKind::kProducer)
        .value("CONSUMER", opentelemetry::trace::SpanKind::kConsumer)
        .export_values();

    // SpanContextWrapper class
    py::class_<otel_wrapper::SpanContextWrapper, std::shared_ptr<otel_wrapper::SpanContextWrapper>>(m, "SpanContext")
        .def_property_readonly("trace_id", [](const otel_wrapper::SpanContextWrapper& self) -> py::object {
            auto builtins = py::module_::import("builtins");
            return builtins.attr("int")(self.get_trace_id(), 16);
        }, "Trace ID as an integer (128-bit), matching opentelemetry.trace.SpanContext")
        .def_property_readonly("span_id", [](const otel_wrapper::SpanContextWrapper& self) -> py::object {
            auto builtins = py::module_::import("builtins");
            return builtins.attr("int")(self.get_span_id(), 16);
        }, "Span ID as an integer (64-bit), matching opentelemetry.trace.SpanContext")
        .def_property_readonly("trace_flags", &otel_wrapper::SpanContextWrapper::get_trace_flags,
                              "Trace flags byte")
        .def_property_readonly("is_remote", &otel_wrapper::SpanContextWrapper::get_is_remote,
                              "True if the span context was propagated from a remote parent")
        .def_property_readonly("is_valid", &otel_wrapper::SpanContextWrapper::get_is_valid,
                              "True if the span context has a valid trace ID and span ID")
        .def_property_readonly("trace_state", &otel_wrapper::SpanContextWrapper::get_trace_state,
                              "Trace state as a W3C tracestate header string");

    // ContextWrapper class
    py::class_<otel_wrapper::ContextWrapper, std::shared_ptr<otel_wrapper::ContextWrapper>>(m, "Context")
        .def(py::init<>(), "Create a context from the current runtime context")
        .def_static("get_current", &otel_wrapper::ContextWrapper::get_current,
                   "Get the current runtime context")
        .def("attach", &otel_wrapper::ContextWrapper::attach,
             "Set this context as current and return a token for restoring")
        .def_static("detach", &otel_wrapper::ContextWrapper::detach,
                   py::arg("token"),
                   "Detach and restore the previous context from token")
        .def("get_span", &otel_wrapper::ContextWrapper::get_span,
             "Get the active span from this context (returns None if no span is active)")
        .def_static("create_with_span_context", &otel_wrapper::ContextWrapper::create_with_span_context,
                   py::arg("trace_id_hex"),
                   py::arg("span_id_hex"),
                   py::arg("trace_flags") = 1,
                   py::arg("is_remote") = true,
                   "Create a context with a span context from trace/span IDs (for bridging Python spans)");

    // SpanWrapper class
    py::class_<otel_wrapper::SpanWrapper, std::shared_ptr<otel_wrapper::SpanWrapper>>(m, "Span")
        .def("set_attribute",
             [](otel_wrapper::SpanWrapper& self, const std::string& key, py::object value) {
                 set_span_attribute(self, key, value);
             },
             py::arg("key"), py::arg("value"),
             "Set an attribute on the span. Accepts str, bool, int, float, or a homogeneous "
             "sequence of any of those types, matching opentelemetry.trace.types.AttributeValue.")

        .def("set_attributes",
             [](otel_wrapper::SpanWrapper& self, py::dict attributes) {
                 for (auto item : attributes) {
                     std::string key = py::str(item.first).cast<std::string>();
                     py::object value = py::reinterpret_borrow<py::object>(item.second);
                     set_span_attribute(self, key, value);
                 }
             },
             py::arg("attributes"),
             "Set multiple attributes on the span from a dict, matching opentelemetry.trace.Span.set_attributes.")

        .def("add_event",
             [](otel_wrapper::SpanWrapper& self,
                const std::string& name,
                py::object attributes,
                py::object timestamp) {
                 std::map<std::string, std::string> attrs;
                 if (!attributes.is_none()) {
                     attrs = attributes.cast<std::map<std::string, std::string>>();
                 }
                 uint64_t ts_ns = 0;
                 if (!timestamp.is_none()) {
                     ts_ns = timestamp.cast<uint64_t>();
                 }
                 if (ts_ns != 0) {
                     self.add_event(name, attrs, ts_ns);
                 } else {
                     if (!attrs.empty()) {
                         self.add_event(name, attrs);
                     } else {
                         self.add_event(name);
                     }
                 }
             },
             py::arg("name"),
             py::arg("attributes") = py::none(),
             py::arg("timestamp") = py::none(),
             "Add an event to the span with optional attributes dict and optional timestamp (nanoseconds since UNIX epoch)")

        .def("set_status", [](otel_wrapper::SpanWrapper& self, py::object status_obj) {
            // Support both our Status class and Python's opentelemetry.trace.status.Status
            if (py::isinstance<otel_wrapper::Status>(status_obj)) {
                // Our Status class
                auto status = status_obj.cast<otel_wrapper::Status>();
                self.set_status(status);
            } else if (py::hasattr(status_obj, "status_code") && py::hasattr(status_obj, "description")) {
                // Python opentelemetry.trace.status.Status or compatible object
                auto status_code = status_obj.attr("status_code");
                auto description = status_obj.attr("description");

                // Extract status code value (handle both enum and int)
                int code_value;
                if (py::hasattr(status_code, "value")) {
                    code_value = status_code.attr("value").cast<int>();
                } else {
                    code_value = status_code.cast<int>();
                }

                // Extract description (handle None)
                std::string desc_str;
                if (!description.is_none()) {
                    desc_str = description.cast<std::string>();
                }

                otel_wrapper::Status status(code_value, desc_str);
                self.set_status(status);
            } else {
                throw py::type_error("set_status expects a Status object with status_code and description attributes");
            }
        }, py::arg("status"),
           "Set the status of the span. Accepts either otel_cpp_tracer.Status or opentelemetry.trace.status.Status")

        .def("end",
             [](otel_wrapper::SpanWrapper& self, py::object end_time) {
                 if (end_time.is_none()) {
                     self.end();
                 } else {
                     self.end(end_time.cast<uint64_t>());
                 }
             },
             py::arg("end_time") = py::none(),
             "End the span, with an optional end_time (nanoseconds since UNIX epoch)")

        .def("is_recording", &otel_wrapper::SpanWrapper::is_recording,
             "Check if the span is recording")

        .def("get_trace_id", &otel_wrapper::SpanWrapper::get_span_context_trace_id,
             "Get the trace ID of the span")

        .def("get_span_id", &otel_wrapper::SpanWrapper::get_span_context_span_id,
             "Get the span ID")

        .def("get_parent_span_id", &otel_wrapper::SpanWrapper::get_parent_span_id,
             "Get the parent span ID (empty string if no parent)")

        .def_property_readonly("kind", &otel_wrapper::SpanWrapper::get_kind,
                              "Get the span kind")

        .def("get_context", &otel_wrapper::SpanWrapper::get_context,
             "Get the context containing this span")

        .def("get_span_context", &otel_wrapper::SpanWrapper::get_span_context,
             "Get the SpanContext for this span (trace_id, span_id, trace_flags, is_remote, is_valid)")

        .def("__enter__", [](std::shared_ptr<otel_wrapper::SpanWrapper> self) {
            return self;
        })

        .def("__exit__", [](std::shared_ptr<otel_wrapper::SpanWrapper> self,
                           py::object exc_type, py::object exc_value, py::object traceback) {
            if (exc_type.ptr() != Py_None) {
                // Exception occurred, set error status
                otel_wrapper::Status error_status(
                    static_cast<int>(opentelemetry::trace::StatusCode::kError),
                    "Exception occurred");
                self->set_status(error_status);
            }
            self->end();
            return false;  // Don't suppress exceptions
        });

    // TracerWrapper class
    py::class_<otel_wrapper::TracerWrapper, std::shared_ptr<otel_wrapper::TracerWrapper>>(m, "Tracer")
        .def("start_span",
             [](otel_wrapper::TracerWrapper& self,
                const std::string& name,
                py::object attributes,
                py::object context,
                py::object kind,
                py::object start_time) {
                 // Convert context (None or empty dict or Context to ContextWrapper)
                 std::shared_ptr<otel_wrapper::ContextWrapper> ctx_ptr = nullptr;
                 if (!context.is_none() && !py::isinstance<py::dict>(context)) {
                     ctx_ptr = context.cast<std::shared_ptr<otel_wrapper::ContextWrapper>>();
                 }

                 // Convert kind (supports SpanKind enum or int)
                 int kind_value = 0;
                 if (!kind.is_none()) {
                     if (py::hasattr(kind, "value")) {
                         kind_value = kind.attr("value").cast<int>();
                     } else {
                         kind_value = kind.cast<int>();
                     }
                 }

                 // Convert start_time (None to 0, otherwise to uint64_t)
                 uint64_t start_time_value = 0;
                 if (!start_time.is_none()) {
                     start_time_value = start_time.cast<uint64_t>();
                 }

                 // Create span without attributes first
                 auto span = self.start_span(name, {}, ctx_ptr, kind_value, start_time_value);

                 // Set attributes with proper types
                 if (!attributes.is_none() && py::isinstance<py::dict>(attributes)) {
                     py::dict attrs_dict = attributes.cast<py::dict>();
                     for (auto item : attrs_dict) {
                         std::string key = py::str(item.first).cast<std::string>();
                         py::object value = py::reinterpret_borrow<py::object>(item.second);
                         set_span_attribute(*span, key, value);
                     }
                 }

                 return span;
             },
             py::arg("name"),
             py::arg("attributes") = py::none(),
             py::arg("context") = py::none(),
             py::arg("kind") = py::none(),
             py::arg("start_time") = py::none(),
             "Start a new span with optional attributes, context, kind, and start_time")

        .def("start_as_current_span",
             [](otel_wrapper::TracerWrapper& self,
                const std::string& name,
                py::object attributes,
                py::object context,
                py::object kind,
                py::object start_time) {
                 // Convert context (None or empty dict or Context to ContextWrapper)
                 std::shared_ptr<otel_wrapper::ContextWrapper> ctx_ptr = nullptr;
                 if (!context.is_none() && !py::isinstance<py::dict>(context)) {
                     ctx_ptr = context.cast<std::shared_ptr<otel_wrapper::ContextWrapper>>();
                 }

                 // Convert kind (supports SpanKind enum or int)
                 int kind_value = 0;
                 if (!kind.is_none()) {
                     if (py::hasattr(kind, "value")) {
                         kind_value = kind.attr("value").cast<int>();
                     } else {
                         kind_value = kind.cast<int>();
                     }
                 }

                 // Convert start_time (None to 0, otherwise to uint64_t)
                 uint64_t start_time_value = 0;
                 if (!start_time.is_none()) {
                     start_time_value = start_time.cast<uint64_t>();
                 }

                 // Create span without attributes first
                 auto span = self.start_as_current_span(name, {}, ctx_ptr, kind_value, start_time_value);

                 // Set attributes with proper types
                 if (!attributes.is_none() && py::isinstance<py::dict>(attributes)) {
                     py::dict attrs_dict = attributes.cast<py::dict>();
                     for (auto item : attrs_dict) {
                         std::string key = py::str(item.first).cast<std::string>();
                         py::object value = py::reinterpret_borrow<py::object>(item.second);
                         set_span_attribute(*span, key, value);
                     }
                 }

                 return span;
             },
             py::arg("name"),
             py::arg("attributes") = py::none(),
             py::arg("context") = py::none(),
             py::arg("kind") = py::none(),
             py::arg("start_time") = py::none(),
             "Start a new span as the current active span with optional attributes, context, kind, and start_time");

    // TracerProviderWrapper class
    py::class_<otel_wrapper::TracerProviderWrapper, std::shared_ptr<otel_wrapper::TracerProviderWrapper>>(
        m, "TracerProvider")
        .def(py::init<const std::string&, const std::string&>(),
             py::arg("service_name"),
             py::arg("exporter_type") = "otlp",
             "Create a new tracer provider with the given service name and exporter type.\n"
             "Supported exporter types: 'console', 'otlp'")

        .def("get_tracer",
             [](otel_wrapper::TracerProviderWrapper& self,
                const std::string& name,
                py::object version,
                py::object schema_url,
                py::object attributes,
                otel_wrapper::TracerProviderWrapper* provider) {
                 // Convert None to empty map
                 std::map<std::string, std::string> attrs_map;
                 if (!attributes.is_none()) {
                     attrs_map = attributes.cast<std::map<std::string, std::string>>();
                 }
                 return self.get_tracer(name, version, schema_url, attrs_map, provider);
             },
             py::arg("name"),
             py::arg("version") = py::none(),
             py::arg("schema_url") = py::none(),
             py::arg("attributes") = py::none(),
             py::arg("provider") = nullptr,
             "Get a tracer with the given name, optional version, schema URL, attributes, and provider")

        .def("shutdown", &otel_wrapper::TracerProviderWrapper::shutdown,
             "Shutdown the tracer provider");
}
