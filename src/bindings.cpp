#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/functional.h>

#include "tracer_wrapper.h"

namespace py = pybind11;

PYBIND11_MODULE(otel_cpp_tracer, m) {
    m.doc() = "Python bindings for OpenTelemetry C++ SDK tracing";

    // SpanStatusCode enum
    py::enum_<opentelemetry::trace::StatusCode>(m, "StatusCode")
        .value("UNSET", opentelemetry::trace::StatusCode::kUnset)
        .value("OK", opentelemetry::trace::StatusCode::kOk)
        .value("ERROR", opentelemetry::trace::StatusCode::kError)
        .export_values();

    // ContextWrapper class
    py::class_<otel_wrapper::ContextWrapper, std::shared_ptr<otel_wrapper::ContextWrapper>>(m, "Context")
        .def(py::init<>(), "Create a context from the current runtime context")
        .def_static("get_current", &otel_wrapper::ContextWrapper::get_current,
                   "Get the current runtime context")
        .def("attach", &otel_wrapper::ContextWrapper::attach,
             "Set this context as current and return a token for restoring")
        .def_static("detach", &otel_wrapper::ContextWrapper::detach,
                   py::arg("token"),
                   "Detach and restore the previous context from token");

    // SpanWrapper class
    py::class_<otel_wrapper::SpanWrapper, std::shared_ptr<otel_wrapper::SpanWrapper>>(m, "Span")
        .def("set_attribute", py::overload_cast<const std::string&, const std::string&>(
                 &otel_wrapper::SpanWrapper::set_attribute),
             py::arg("key"), py::arg("value"),
             "Set a string attribute on the span")

        .def("set_attribute", py::overload_cast<const std::string&, int64_t>(
                 &otel_wrapper::SpanWrapper::set_attribute),
             py::arg("key"), py::arg("value"),
             "Set an integer attribute on the span")

        .def("set_attribute", py::overload_cast<const std::string&, double>(
                 &otel_wrapper::SpanWrapper::set_attribute),
             py::arg("key"), py::arg("value"),
             "Set a float attribute on the span")

        .def("set_attribute", py::overload_cast<const std::string&, bool>(
                 &otel_wrapper::SpanWrapper::set_attribute),
             py::arg("key"), py::arg("value"),
             "Set a boolean attribute on the span")

        .def("add_event", py::overload_cast<const std::string&>(
                 &otel_wrapper::SpanWrapper::add_event),
             py::arg("name"),
             "Add an event to the span")

        .def("add_event", py::overload_cast<const std::string&,
                 const std::map<std::string, std::string>&>(
                 &otel_wrapper::SpanWrapper::add_event),
             py::arg("name"), py::arg("attributes"),
             "Add an event with attributes to the span")

        .def("set_status", &otel_wrapper::SpanWrapper::set_status,
             py::arg("status_code"), py::arg("description") = "",
             "Set the status of the span")

        .def("end", &otel_wrapper::SpanWrapper::end,
             "End the span explicitly")

        .def("is_recording", &otel_wrapper::SpanWrapper::is_recording,
             "Check if the span is recording")

        .def("get_trace_id", &otel_wrapper::SpanWrapper::get_span_context_trace_id,
             "Get the trace ID of the span")

        .def("get_span_id", &otel_wrapper::SpanWrapper::get_span_context_span_id,
             "Get the span ID")

        .def("get_context", &otel_wrapper::SpanWrapper::get_context,
             "Get the context containing this span")

        .def("__enter__", [](std::shared_ptr<otel_wrapper::SpanWrapper> self) {
            return self;
        })

        .def("__exit__", [](std::shared_ptr<otel_wrapper::SpanWrapper> self,
                           py::object exc_type, py::object exc_value, py::object traceback) {
            if (exc_type.ptr() != Py_None) {
                // Exception occurred, set error status
                self->set_status(static_cast<int>(opentelemetry::trace::StatusCode::kError),
                               "Exception occurred");
            }
            self->end();
            return false;  // Don't suppress exceptions
        });

    // TracerWrapper class
    py::class_<otel_wrapper::TracerWrapper, std::shared_ptr<otel_wrapper::TracerWrapper>>(m, "Tracer")
        .def("start_span", &otel_wrapper::TracerWrapper::start_span,
             py::arg("name"),
             py::arg("attributes") = std::map<std::string, std::string>(),
             py::arg("context") = nullptr,
             "Start a new span with optional attributes and context")

        .def("start_as_current_span", &otel_wrapper::TracerWrapper::start_as_current_span,
             py::arg("name"),
             py::arg("attributes") = std::map<std::string, std::string>(),
             py::arg("context") = nullptr,
             "Start a new span as the current active span with optional attributes and context");

    // TracerProviderWrapper class
    py::class_<otel_wrapper::TracerProviderWrapper, std::shared_ptr<otel_wrapper::TracerProviderWrapper>>(
        m, "TracerProvider")
        .def(py::init<const std::string&, const std::string&>(),
             py::arg("service_name"),
             py::arg("exporter_type") = "console",
             "Create a new tracer provider with the given service name and exporter type.\n"
             "Supported exporter types: 'console', 'otlp'")

        .def("get_tracer", &otel_wrapper::TracerProviderWrapper::get_tracer,
             py::arg("name"),
             py::arg("version") = "",
             "Get a tracer with the given name and optional version")

        .def("shutdown", &otel_wrapper::TracerProviderWrapper::shutdown,
             "Shutdown the tracer provider");
}
