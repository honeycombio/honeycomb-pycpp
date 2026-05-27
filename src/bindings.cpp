#include <pybind11/pybind11.h>
#include <pybind11/eval.h>
#include <pybind11/stl.h>
#include <pybind11/functional.h>

#include "tracer_wrapper.h"
#include "meter_wrapper.h"
#include "logger_wrapper.h"
#include "sdk_wrapper.h"
#include "py_attribute_iterable.h"

#include "opentelemetry/context/runtime_context.h"

namespace py = pybind11;

namespace {

// Extract a C++ context from a Python opentelemetry.context.Context object.
// If the Python context holds a SpanWrapper, build the C++ context from its
// span context so the SDK can record exemplars against the right span.
// Falls back to the current C++ runtime context if nothing useful is found.
opentelemetry::context::Context cpp_context_from_py(py::object py_ctx) {
    if (py_ctx.is_none()) {
        return opentelemetry::context::RuntimeContext::GetCurrent();
    }
    if (py::isinstance<otel_wrapper::ContextWrapper>(py_ctx)) {
        return py_ctx.cast<std::shared_ptr<otel_wrapper::ContextWrapper>>()->get_context();
    }
    try {
        auto otel_ctx = py::module_::import("opentelemetry.context");
        auto span_key = py::module_::import("opentelemetry.trace.propagation").attr("_SPAN_KEY");
        py::object span = otel_ctx.attr("get_value")(span_key, py_ctx);
        if (!span.is_none() && py::isinstance<otel_wrapper::SpanWrapper>(span)) {
            auto ctx_wrapper = span.cast<std::shared_ptr<otel_wrapper::SpanWrapper>>()->get_context();
            if (ctx_wrapper) return ctx_wrapper->get_context();
        }
    } catch (...) {}
    return opentelemetry::context::RuntimeContext::GetCurrent();
}

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

// Lazy span context manager: defers span creation to __enter__ so the object
// can also act as a decorator (@tracer.start_as_current_span("name")).
struct SpanContextManager {
    std::shared_ptr<otel_wrapper::TracerWrapper> tracer;
    std::string name;
    py::object context;
    py::object kind;
    py::object attributes;
    py::object links;
    py::object start_time;
    bool record_exception        = true;
    bool set_status_on_exception = true;
    bool end_on_exit             = true;
    std::shared_ptr<otel_wrapper::SpanWrapper> span;
};

}  // namespace

PYBIND11_MODULE(honeycomb_pycpp, m) {
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
        .def_property_readonly("trace_flags", [](const otel_wrapper::SpanContextWrapper& self) -> py::object {
            auto trace_mod = py::module_::import("opentelemetry.trace");
            return trace_mod.attr("TraceFlags")(self.get_trace_flags());
        }, "Trace flags as opentelemetry.trace.TraceFlags")
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
                 uint64_t ts_ns = 0;
                 if (!timestamp.is_none()) {
                     ts_ns = timestamp.cast<uint64_t>();
                 }
                 if (!attributes.is_none()) {
                     PyAttributeIterable attrs(attributes.cast<py::dict>());
                     if (ts_ns != 0) {
                         self.add_event(name, attrs, ts_ns);
                     } else {
                         self.add_event(name, attrs);
                     }
                 } else if (ts_ns != 0) {
                     self.add_event(name, ts_ns);
                 } else {
                     self.add_event(name);
                 }
             },
             py::arg("name"),
             py::arg("attributes") = py::none(),
             py::arg("timestamp") = py::none(),
             "Add an event to the span with optional attributes dict and optional timestamp (nanoseconds since UNIX epoch)")

        .def("record_exception",
             [](otel_wrapper::SpanWrapper& self,
                py::object exception,
                py::object attributes,
                py::object timestamp,
                bool escaped) {
                 // Local strings must outlive attrs and the add_event call
                 std::string type_str, message_str, stacktrace_str;

                 // exception.type: use __qualname__ if available, else __name__
                 auto exc_type = py::type::of(exception);
                 if (py::hasattr(exc_type, "__qualname__")) {
                     type_str = exc_type.attr("__qualname__").cast<std::string>();
                 } else {
                     type_str = exc_type.attr("__name__").cast<std::string>();
                 }

                 // exception.message
                 message_str = py::str(exception).cast<std::string>();

                 // exception.stacktrace
                 try {
                     auto tb_mod = py::module_::import("traceback");
                     py::object lines = tb_mod.attr("format_exception")(
                         exc_type, exception, exception.attr("__traceback__"));
                     stacktrace_str = py::str("").attr("join")(lines).cast<std::string>();
                 } catch (...) {}

                 // Build a Python dict with semconv attrs; user attrs are merged on top
                 // (user attrs override semconv attrs for duplicate keys).
                 py::dict attrs;
                 attrs["exception.type"]    = type_str;
                 attrs["exception.message"] = message_str;
                 if (!stacktrace_str.empty()) {
                     attrs["exception.stacktrace"] = stacktrace_str;
                 }
                 if (escaped) {
                     attrs["exception.escaped"] = true;
                 }
                 if (!attributes.is_none()) {
                     for (auto item : attributes.cast<py::dict>()) {
                         attrs[item.first] = item.second;
                     }
                 }

                 PyAttributeIterable event_attrs(attrs);
                 uint64_t ts_ns = 0;
                 if (!timestamp.is_none()) {
                     ts_ns = timestamp.cast<uint64_t>();
                 }
                 if (ts_ns != 0) {
                     self.add_event("exception", event_attrs, ts_ns);
                 } else {
                     self.add_event("exception", event_attrs);
                 }
             },
             py::arg("exception"),
             py::arg("attributes") = py::none(),
             py::arg("timestamp") = py::none(),
             py::arg("escaped") = false,
             "Record an exception as a span event per OTel semconv. "
             "Adds an 'exception' event with exception.type, exception.message, "
             "and exception.stacktrace attributes.")

        .def("set_status", [](otel_wrapper::SpanWrapper& self, py::object status_obj, py::object description_override) {
            // Support three call forms matching the Python OTel API:
            //   span.set_status(honeycomb_pycpp.Status(...))
            //   span.set_status(opentelemetry.trace.status.Status(...))
            //   span.set_status(StatusCode.ERROR, "description")
            if (py::isinstance<otel_wrapper::Status>(status_obj)) {
                auto status = status_obj.cast<otel_wrapper::Status>();
                if (!description_override.is_none()) {
                    otel_wrapper::Status overridden(status.get_status_code(), description_override.cast<std::string>());
                    self.set_status(overridden);
                } else {
                    self.set_status(status);
                }
            } else if (py::hasattr(status_obj, "status_code") && py::hasattr(status_obj, "description")) {
                // opentelemetry.trace.status.Status or compatible object
                auto status_code = status_obj.attr("status_code");
                auto description = status_obj.attr("description");
                int code_value;
                if (py::hasattr(status_code, "value")) {
                    code_value = status_code.attr("value").cast<int>();
                } else {
                    code_value = status_code.cast<int>();
                }
                std::string desc_str;
                if (!description_override.is_none()) {
                    desc_str = description_override.cast<std::string>();
                } else if (!description.is_none()) {
                    desc_str = description.cast<std::string>();
                }
                otel_wrapper::Status status(code_value, desc_str);
                self.set_status(status);
            } else if (py::isinstance<py::int_>(status_obj) || py::hasattr(status_obj, "value")) {
                // Bare StatusCode enum: span.set_status(StatusCode.ERROR, "description")
                int code_value;
                if (py::hasattr(status_obj, "value")) {
                    code_value = status_obj.attr("value").cast<int>();
                } else {
                    code_value = status_obj.cast<int>();
                }
                std::string desc_str;
                if (!description_override.is_none()) {
                    desc_str = description_override.cast<std::string>();
                }
                otel_wrapper::Status status(code_value, desc_str);
                self.set_status(status);
            } else {
                throw py::type_error("set_status expects a Status object or a StatusCode enum value");
            }
        }, py::arg("status"), py::arg("description") = py::none(),
           "Set the status of the span. Accepts honeycomb_pycpp.Status, "
           "opentelemetry.trace.status.Status, or a bare StatusCode enum with an optional description.")

        .def("update_name", &otel_wrapper::SpanWrapper::update_name,
             py::arg("name"),
             "Update the span name, overriding the name set at creation.")

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

        .def("add_link",
             [](otel_wrapper::SpanWrapper& self,
                const otel_wrapper::SpanContextWrapper& context,
                py::object attributes) {
                 py::dict attr_dict = attributes.is_none() ? py::dict() : attributes.cast<py::dict>();
                 PyAttributeIterable attrs(attr_dict);
                 self.add_link(context, attrs);
             },
             py::arg("context"),
             py::arg("attributes") = py::none(),
             "Add a link to another span. Requires OpenTelemetry C++ ABI v2; no-op on ABI v1.")

        .def("__enter__", [](std::shared_ptr<otel_wrapper::SpanWrapper> self) {
            return self;
        })

        .def("__exit__", [](std::shared_ptr<otel_wrapper::SpanWrapper> self,
                           py::object exc_type, py::object exc_value, py::object traceback) {
            if (exc_type.ptr() != Py_None) {
                if (self->get_record_exception_flag()) {
                    std::string type_str, message_str, stacktrace_str;
                    if (py::hasattr(exc_type, "__qualname__"))
                        type_str = exc_type.attr("__qualname__").cast<std::string>();
                    else
                        type_str = exc_type.attr("__name__").cast<std::string>();
                    message_str = py::str(exc_value).cast<std::string>();
                    try {
                        auto tb_mod = py::module_::import("traceback");
                        py::object lines = tb_mod.attr("format_exception")(exc_type, exc_value, traceback);
                        stacktrace_str = py::str("").attr("join")(lines).cast<std::string>();
                    } catch (...) {}
                    py::dict attrs;
                    attrs["exception.type"]    = type_str;
                    attrs["exception.message"] = message_str;
                    if (!stacktrace_str.empty()) attrs["exception.stacktrace"] = stacktrace_str;
                    attrs["exception.escaped"] = true;
                    PyAttributeIterable event_attrs(attrs);
                    self->add_event("exception", event_attrs);
                }
                if (self->get_set_status_on_exception_flag()) {
                    otel_wrapper::Status error_status(
                        static_cast<int>(opentelemetry::trace::StatusCode::kError),
                        py::str(exc_value).cast<std::string>());
                    self->set_status(error_status);
                }
            }
            if (self->get_end_on_exit_flag())
                self->end();
            return false;
        });

    // SpanContextManager — returned by start_as_current_span; lazy so it can also
    // be used as a decorator (@tracer.start_as_current_span("name")).
    py::class_<SpanContextManager, std::shared_ptr<SpanContextManager>>(m, "_SpanContextManager")
        .def("__enter__",
             [](std::shared_ptr<SpanContextManager> self) {
                 std::shared_ptr<otel_wrapper::ContextWrapper> ctx_ptr = nullptr;
                 if (!self->context.is_none() && !py::isinstance<py::dict>(self->context))
                     ctx_ptr = self->context.cast<std::shared_ptr<otel_wrapper::ContextWrapper>>();

                 int kind_value = 0;
                 if (!self->kind.is_none()) {
                     kind_value = py::hasattr(self->kind, "value")
                         ? self->kind.attr("value").cast<int>()
                         : self->kind.cast<int>();
                 }

                 uint64_t start_time_ns = 0;
                 if (!self->start_time.is_none())
                     start_time_ns = self->start_time.cast<uint64_t>();

                 std::shared_ptr<otel_wrapper::SpanWrapper> span;
                 if (!self->attributes.is_none()) {
                     PyAttributeIterable attrs(self->attributes.cast<py::dict>());
                     span = self->tracer->start_as_current_span(
                         self->name, &attrs, ctx_ptr, kind_value, start_time_ns);
                 } else {
                     span = self->tracer->start_as_current_span(
                         self->name, nullptr, ctx_ptr, kind_value, start_time_ns);
                 }

                 if (!self->links.is_none()) {
                     for (auto link_handle : self->links) {
                         py::object lobj = py::reinterpret_borrow<py::object>(link_handle);
                         if (!py::hasattr(lobj, "context")) continue;
                         auto sc = lobj.attr("context").cast<otel_wrapper::SpanContextWrapper>();
                         py::dict attr_dict;
                         if (py::hasattr(lobj, "attributes")) {
                             py::object av = lobj.attr("attributes");
                             if (av.ptr() != Py_None && PyDict_Check(av.ptr()))
                                 attr_dict = py::reinterpret_borrow<py::dict>(av);
                         }
                         PyAttributeIterable attr_iter(attr_dict);
                         span->add_link(sc, attr_iter);
                     }
                 }

                 span->set_record_exception_flag(self->record_exception);
                 span->set_status_on_exception_flag(self->set_status_on_exception);
                 span->set_end_on_exit_flag(self->end_on_exit);
                 self->span = span;
                 return span;
             })
        .def("__exit__",
             [](std::shared_ptr<SpanContextManager> self,
                py::object exc_type, py::object exc_val, py::object tb) -> py::object {
                 if (!self->span) return py::bool_(false);
                 return py::cast(self->span).attr("__exit__")(exc_type, exc_val, tb);
             })
        .def("_clone", [](std::shared_ptr<SpanContextManager> self) {
                 auto c = std::make_shared<SpanContextManager>(*self);
                 c->span.reset();
                 return c;
             });

    // Generator function that makes _SpanContextManager usable as both a
    // context manager and a decorator via contextlib.contextmanager.
    py::object span_cm_fn;
    {
        py::dict ns;
        py::exec(R"(
import contextlib

@contextlib.contextmanager
def _span_cm_fn(cm):
    with cm._clone() as span:
        yield span
)", py::globals(), ns);
        span_cm_fn = ns["_span_cm_fn"];
    }

    // TracerWrapper class
    py::class_<otel_wrapper::TracerWrapper, std::shared_ptr<otel_wrapper::TracerWrapper>>(m, "Tracer")
        .def("start_span",
             [](otel_wrapper::TracerWrapper& self,
                const std::string& name,
                py::object context,
                py::object kind,
                py::object attributes,
                py::object links,
                py::object start_time,
                bool record_exception,
                bool set_status_on_exception) {
                 std::shared_ptr<otel_wrapper::ContextWrapper> ctx_ptr = nullptr;
                 if (!context.is_none() && !py::isinstance<py::dict>(context)) {
                     ctx_ptr = context.cast<std::shared_ptr<otel_wrapper::ContextWrapper>>();
                 }

                 int kind_value = 0;
                 if (!kind.is_none()) {
                     if (py::hasattr(kind, "value")) {
                         kind_value = kind.attr("value").cast<int>();
                     } else {
                         kind_value = kind.cast<int>();
                     }
                 }

                 uint64_t start_time_value = 0;
                 if (!start_time.is_none()) {
                     start_time_value = start_time.cast<uint64_t>();
                 }

                 std::shared_ptr<otel_wrapper::SpanWrapper> span;
                 if (!attributes.is_none()) {
                     PyAttributeIterable attrs(attributes.cast<py::dict>());
                     span = self.start_span(name, &attrs, ctx_ptr, kind_value, start_time_value);
                 } else {
                     span = self.start_span(name, nullptr, ctx_ptr, kind_value, start_time_value);
                 }

                 if (!links.is_none()) {
                     for (auto link_handle : links) {
                         py::object lobj = py::reinterpret_borrow<py::object>(link_handle);
                         if (!py::hasattr(lobj, "context")) continue;
                         auto sc = lobj.attr("context").cast<otel_wrapper::SpanContextWrapper>();
                         py::dict attr_dict;
                         if (py::hasattr(lobj, "attributes")) {
                             py::object av = lobj.attr("attributes");
                             if (av.ptr() != Py_None && PyDict_Check(av.ptr()))
                                 attr_dict = py::reinterpret_borrow<py::dict>(av);
                         }
                         PyAttributeIterable attr_iter(attr_dict);
                         span->add_link(sc, attr_iter);
                     }
                 }
                 span->set_record_exception_flag(record_exception);
                 span->set_status_on_exception_flag(set_status_on_exception);
                 return span;
             },
             py::arg("name"),
             py::arg("context") = py::none(),
             py::arg("kind") = py::none(),
             py::arg("attributes") = py::none(),
             py::arg("links") = py::none(),
             py::arg("start_time") = py::none(),
             py::arg("record_exception") = true,
             py::arg("set_status_on_exception") = true,
             "Start a new span with optional context, kind, attributes, links, and start_time")

        .def("start_as_current_span",
             [span_cm_fn](std::shared_ptr<otel_wrapper::TracerWrapper> self,
                const std::string& name,
                py::object context,
                py::object kind,
                py::object attributes,
                py::object links,
                py::object start_time,
                bool record_exception,
                bool set_status_on_exception,
                bool end_on_exit) {
                 auto cm = std::make_shared<SpanContextManager>();
                 cm->tracer             = self;
                 cm->name               = name;
                 cm->context            = context;
                 cm->kind               = kind;
                 cm->attributes         = attributes;
                 cm->links              = links;
                 cm->start_time         = start_time;
                 cm->record_exception        = record_exception;
                 cm->set_status_on_exception = set_status_on_exception;
                 cm->end_on_exit             = end_on_exit;
                 return span_cm_fn(py::cast(cm));
             },
             py::arg("name"),
             py::arg("context") = py::none(),
             py::arg("kind") = py::none(),
             py::arg("attributes") = py::none(),
             py::arg("links") = py::none(),
             py::arg("start_time") = py::none(),
             py::arg("record_exception") = true,
             py::arg("set_status_on_exception") = true,
             py::arg("end_on_exit") = true,
             "Start a span as the current active span. Usable as a context manager "
             "(with tracer.start_as_current_span(...)) or decorator "
             "(@tracer.start_as_current_span(...)).");

    // TracerProviderWrapper class
    py::class_<otel_wrapper::TracerProviderWrapper, std::shared_ptr<otel_wrapper::TracerProviderWrapper>>(
        m, "TracerProvider")
        .def("get_tracer",
             [](otel_wrapper::TracerProviderWrapper& self,
                const std::string& name,
                py::object instrumenting_library_version,
                py::object schema_url,
                py::object attributes,
                otel_wrapper::TracerProviderWrapper* provider) {

                 if (!attributes.is_none()) {
                     PyAttributeIterable attrs(attributes.cast<py::dict>());
                     return self.get_tracer(name, instrumenting_library_version, schema_url, &attrs, provider);
                 }
                 return self.get_tracer(name, instrumenting_library_version, schema_url, nullptr, provider);
             },
             py::arg("instrumenting_module_name"),
             py::arg("instrumenting_library_version") = py::none(),
             py::arg("schema_url") = py::none(),
             py::arg("attributes") = py::none(),
             py::arg("provider") = nullptr,
             "Get a tracer with the given instrumenting_module_name, optional instrumenting_library_version, schema URL, attributes, and provider")

        .def("shutdown", &otel_wrapper::TracerProviderWrapper::shutdown,
             "Shutdown the tracer provider")

        .def_property_readonly("configured", &otel_wrapper::TracerProviderWrapper::is_configured,
             "True if a tracer provider was built from the config file.");

    // -----------------------------------------------------------------------
    // Metrics API
    // -----------------------------------------------------------------------

    py::class_<otel_wrapper::ObservationWrapper>(m, "Observation")
        .def(py::init<double>(),
             py::arg("value"),
             "Create an Observation with a numeric value for use in observable callbacks.")
        .def_property_readonly("value", &otel_wrapper::ObservationWrapper::get_value,
                               "The observation value.");

    py::class_<otel_wrapper::CounterWrapper,
               std::shared_ptr<otel_wrapper::CounterWrapper>>(m, "Counter")
        .def("add",
             [](otel_wrapper::CounterWrapper& self, double amount, py::object attributes,
                py::object context) {
                 auto ctx = cpp_context_from_py(context);
                 if (!attributes.is_none()) {
                     PyAttributeIterable attrs(attributes.cast<py::dict>());
                     self.add(amount, &attrs, ctx);
                 } else {
                     self.add(amount, nullptr, ctx);
                 }
             },
             py::arg("amount"),
             py::arg("attributes") = py::none(),
             py::arg("context") = py::none(),
             "Increment the counter by amount. amount must be non-negative.");

    py::class_<otel_wrapper::UpDownCounterWrapper,
               std::shared_ptr<otel_wrapper::UpDownCounterWrapper>>(m, "UpDownCounter")
        .def("add",
             [](otel_wrapper::UpDownCounterWrapper& self, double amount, py::object attributes,
                py::object context) {
                 auto ctx = cpp_context_from_py(context);
                 if (!attributes.is_none()) {
                     PyAttributeIterable attrs(attributes.cast<py::dict>());
                     self.add(amount, &attrs, ctx);
                 } else {
                     self.add(amount, nullptr, ctx);
                 }
             },
             py::arg("amount"),
             py::arg("attributes") = py::none(),
             py::arg("context") = py::none(),
             "Add amount to the up-down counter. May be positive, negative, or zero.");

    py::class_<otel_wrapper::HistogramWrapper,
               std::shared_ptr<otel_wrapper::HistogramWrapper>>(m, "Histogram")
        .def("record",
             [](otel_wrapper::HistogramWrapper& self, double amount, py::object attributes,
                py::object context) {
                 auto ctx = cpp_context_from_py(context);
                 if (!attributes.is_none()) {
                     PyAttributeIterable attrs(attributes.cast<py::dict>());
                     self.record(amount, &attrs, ctx);
                 } else {
                     self.record(amount, nullptr, ctx);
                 }
             },
             py::arg("amount"),
             py::arg("attributes") = py::none(),
             py::arg("context") = py::none(),
             "Record a measurement in the histogram.");

    py::class_<otel_wrapper::GaugeWrapper,
               std::shared_ptr<otel_wrapper::GaugeWrapper>>(m, "Gauge")
        .def("set",
             [](otel_wrapper::GaugeWrapper& self, double amount, py::object attributes,
                py::object context) {
                 auto ctx = cpp_context_from_py(context);
                 if (!attributes.is_none()) {
                     PyAttributeIterable attrs(attributes.cast<py::dict>());
                     self.set(amount, &attrs, ctx);
                 } else {
                     self.set(amount, nullptr, ctx);
                 }
             },
             py::arg("amount"),
             py::arg("attributes") = py::none(),
             py::arg("context") = py::none(),
             "Set the gauge to amount. On ABI v1 this is a no-op.");

    py::class_<otel_wrapper::ObservableInstrumentWrapper,
               std::shared_ptr<otel_wrapper::ObservableInstrumentWrapper>>(
        m, "ObservableInstrument");

    py::class_<otel_wrapper::MeterWrapper,
               std::shared_ptr<otel_wrapper::MeterWrapper>>(m, "Meter")
        .def("create_counter",
             [](otel_wrapper::MeterWrapper& self, const std::string& name,
                py::object unit, py::object description) {
                 return self.create_counter(
                     name,
                     unit.is_none()        ? "" : unit.cast<std::string>(),
                     description.is_none() ? "" : description.cast<std::string>());
             },
             py::arg("name"),
             py::arg("unit") = py::none(),
             py::arg("description") = py::none(),
             "Create a monotonically increasing double counter.")

        .def("create_up_down_counter",
             [](otel_wrapper::MeterWrapper& self, const std::string& name,
                py::object unit, py::object description) {
                 return self.create_up_down_counter(
                     name,
                     unit.is_none()        ? "" : unit.cast<std::string>(),
                     description.is_none() ? "" : description.cast<std::string>());
             },
             py::arg("name"),
             py::arg("unit") = py::none(),
             py::arg("description") = py::none(),
             "Create a double up-down counter.")

        .def("create_histogram",
             [](otel_wrapper::MeterWrapper& self, const std::string& name,
                py::object unit, py::object description,
                py::object explicit_bucket_boundaries_advisory) {
                 // explicit_bucket_boundaries_advisory is accepted for API compatibility
                 // but not forwarded; bucket boundaries are configured via SDK views.
                 (void)explicit_bucket_boundaries_advisory;
                 return self.create_histogram(
                     name,
                     unit.is_none()        ? "" : unit.cast<std::string>(),
                     description.is_none() ? "" : description.cast<std::string>());
             },
             py::arg("name"),
             py::arg("unit") = py::none(),
             py::arg("description") = py::none(),
             py::arg("explicit_bucket_boundaries_advisory") = py::none(),
             "Create a double histogram.")

        .def("create_gauge",
             [](otel_wrapper::MeterWrapper& self, const std::string& name,
                py::object unit, py::object description) {
                 return self.create_gauge(
                     name,
                     unit.is_none()        ? "" : unit.cast<std::string>(),
                     description.is_none() ? "" : description.cast<std::string>());
             },
             py::arg("name"),
             py::arg("unit") = py::none(),
             py::arg("description") = py::none(),
             "Create a double gauge. Returns a no-op wrapper on ABI v1.")

        .def("create_observable_counter",
             [](otel_wrapper::MeterWrapper& self, const std::string& name,
                py::object callbacks, py::object unit, py::object description) {
                 std::vector<py::object> cbs;
                 if (!callbacks.is_none()) {
                     for (auto cb : callbacks.cast<py::list>())
                         cbs.push_back(py::reinterpret_borrow<py::object>(cb));
                 }
                 return self.create_observable_counter(
                     name, std::move(cbs),
                     unit.is_none()        ? "" : unit.cast<std::string>(),
                     description.is_none() ? "" : description.cast<std::string>());
             },
             py::arg("name"),
             py::arg("callbacks") = py::none(),
             py::arg("unit") = py::none(),
             py::arg("description") = py::none(),
             "Create an observable (asynchronous) counter.")

        .def("create_observable_up_down_counter",
             [](otel_wrapper::MeterWrapper& self, const std::string& name,
                py::object callbacks, py::object unit, py::object description) {
                 std::vector<py::object> cbs;
                 if (!callbacks.is_none()) {
                     for (auto cb : callbacks.cast<py::list>())
                         cbs.push_back(py::reinterpret_borrow<py::object>(cb));
                 }
                 return self.create_observable_up_down_counter(
                     name, std::move(cbs),
                     unit.is_none()        ? "" : unit.cast<std::string>(),
                     description.is_none() ? "" : description.cast<std::string>());
             },
             py::arg("name"),
             py::arg("callbacks") = py::none(),
             py::arg("unit") = py::none(),
             py::arg("description") = py::none(),
             "Create an observable (asynchronous) up-down counter.")

        .def("create_observable_gauge",
             [](otel_wrapper::MeterWrapper& self, const std::string& name,
                py::object callbacks, py::object unit, py::object description) {
                 std::vector<py::object> cbs;
                 if (!callbacks.is_none()) {
                     for (auto cb : callbacks.cast<py::list>())
                         cbs.push_back(py::reinterpret_borrow<py::object>(cb));
                 }
                 return self.create_observable_gauge(
                     name, std::move(cbs),
                     unit.is_none()        ? "" : unit.cast<std::string>(),
                     description.is_none() ? "" : description.cast<std::string>());
             },
             py::arg("name"),
             py::arg("callbacks") = py::none(),
             py::arg("unit") = py::none(),
             py::arg("description") = py::none(),
             "Create an observable (asynchronous) gauge.");

    py::class_<otel_wrapper::MeterProviderWrapper,
               std::shared_ptr<otel_wrapper::MeterProviderWrapper>>(m, "MeterProvider")
        .def("get_meter",
             [](otel_wrapper::MeterProviderWrapper& self, const std::string& name,
                py::object version, py::object schema_url, py::object attributes) {
                 return self.get_meter(name, version, schema_url, attributes);
             },
             py::arg("name"),
             py::arg("version") = py::none(),
             py::arg("schema_url") = py::none(),
             py::arg("attributes") = py::none(),
             "Get (or create) a Meter for the given instrumentation scope.")

        .def("shutdown", &otel_wrapper::MeterProviderWrapper::shutdown,
             "Flush and shut down the meter provider.")

        .def_property_readonly("configured", &otel_wrapper::MeterProviderWrapper::is_configured,
             "True if a meter provider was built from the config file.");

    // -----------------------------------------------------------------------
    // Logging API
    // -----------------------------------------------------------------------

    py::enum_<opentelemetry::logs::Severity>(m, "SeverityNumber")
        .value("UNSPECIFIED", opentelemetry::logs::Severity::kInvalid)
        .value("TRACE",  opentelemetry::logs::Severity::kTrace)
        .value("TRACE2", opentelemetry::logs::Severity::kTrace2)
        .value("TRACE3", opentelemetry::logs::Severity::kTrace3)
        .value("TRACE4", opentelemetry::logs::Severity::kTrace4)
        .value("DEBUG",  opentelemetry::logs::Severity::kDebug)
        .value("DEBUG2", opentelemetry::logs::Severity::kDebug2)
        .value("DEBUG3", opentelemetry::logs::Severity::kDebug3)
        .value("DEBUG4", opentelemetry::logs::Severity::kDebug4)
        .value("INFO",   opentelemetry::logs::Severity::kInfo)
        .value("INFO2",  opentelemetry::logs::Severity::kInfo2)
        .value("INFO3",  opentelemetry::logs::Severity::kInfo3)
        .value("INFO4",  opentelemetry::logs::Severity::kInfo4)
        .value("WARN",   opentelemetry::logs::Severity::kWarn)
        .value("WARN2",  opentelemetry::logs::Severity::kWarn2)
        .value("WARN3",  opentelemetry::logs::Severity::kWarn3)
        .value("WARN4",  opentelemetry::logs::Severity::kWarn4)
        .value("ERROR",  opentelemetry::logs::Severity::kError)
        .value("ERROR2", opentelemetry::logs::Severity::kError2)
        .value("ERROR3", opentelemetry::logs::Severity::kError3)
        .value("ERROR4", opentelemetry::logs::Severity::kError4)
        .value("FATAL",  opentelemetry::logs::Severity::kFatal)
        .value("FATAL2", opentelemetry::logs::Severity::kFatal2)
        .value("FATAL3", opentelemetry::logs::Severity::kFatal3)
        .value("FATAL4", opentelemetry::logs::Severity::kFatal4)
        .export_values();

    py::class_<otel_wrapper::LogRecordWrapper>(m, "LogRecord")
        .def(py::init([](py::object timestamp,
                         py::object observed_timestamp,
                         py::object severity_number,
                         py::object severity_text,
                         py::object body,
                         py::object attributes,
                         py::object event_name,
                         py::object exception) {
                 otel_wrapper::LogRecordWrapper r;
                 if (!timestamp.is_none())
                     r.timestamp = timestamp.cast<uint64_t>();
                 if (!observed_timestamp.is_none())
                     r.observed_timestamp = observed_timestamp.cast<uint64_t>();
                 if (!severity_number.is_none()) {
                     if (py::hasattr(severity_number, "value")) {
                         r.severity_number = severity_number.attr("value").cast<int>();
                     } else {
                         r.severity_number = severity_number.cast<int>();
                     }
                 }
                 if (!severity_text.is_none())
                     r.severity_text = severity_text.cast<std::string>();
                 if (!body.is_none())
                     r.body = body;
                 if (!attributes.is_none())
                     r.attributes = attributes.cast<py::dict>();
                 if (!event_name.is_none())
                     r.event_name = event_name.cast<std::string>();
                 if (!exception.is_none())
                     r.exception = exception;
                 return r;
             }),
             py::arg("timestamp")          = py::none(),
             py::arg("observed_timestamp") = py::none(),
             py::arg("severity_number")    = py::none(),
             py::arg("severity_text")      = py::none(),
             py::arg("body")               = py::none(),
             py::arg("attributes")         = py::none(),
             py::arg("event_name")         = py::none(),
             py::arg("exception")          = py::none())
        .def_readwrite("timestamp",          &otel_wrapper::LogRecordWrapper::timestamp)
        .def_readwrite("observed_timestamp", &otel_wrapper::LogRecordWrapper::observed_timestamp)
        .def_property(
            "severity_number",
            [](const otel_wrapper::LogRecordWrapper& self) -> py::object {
                return py::cast(static_cast<opentelemetry::logs::Severity>(self.severity_number));
            },
            [](otel_wrapper::LogRecordWrapper& self, py::object val) {
                if (py::hasattr(val, "value")) {
                    self.severity_number = val.attr("value").cast<int>();
                } else {
                    self.severity_number = val.cast<int>();
                }
            })
        .def_readwrite("severity_text", &otel_wrapper::LogRecordWrapper::severity_text)
        .def_readwrite("body",          &otel_wrapper::LogRecordWrapper::body)
        .def_readwrite("attributes",    &otel_wrapper::LogRecordWrapper::attributes)
        .def_readwrite("event_name",    &otel_wrapper::LogRecordWrapper::event_name)
        .def_readwrite("exception",     &otel_wrapper::LogRecordWrapper::exception);

    py::class_<otel_wrapper::LoggerWrapper,
               std::shared_ptr<otel_wrapper::LoggerWrapper>>(m, "Logger")
        .def("emit",
             [](otel_wrapper::LoggerWrapper& self,
                py::object record_or_none,
                py::object timestamp,
                py::object observed_timestamp,
                py::object severity_number,
                py::object severity_text,
                py::object body,
                py::object attributes,
                py::object event_name,
                py::object exception) {
                 if (!record_or_none.is_none() &&
                     py::isinstance<otel_wrapper::LogRecordWrapper>(record_or_none)) {
                     self.emit(record_or_none.cast<otel_wrapper::LogRecordWrapper>());
                     return;
                 }
                 // Handle a Python OTel LogRecord (duck-typed): extract its fields
                 // so LoggingHandler and other Python bridges work transparently.
                 if (!record_or_none.is_none() && py::hasattr(record_or_none, "severity_number")) {
                     otel_wrapper::LogRecordWrapper r;
                     auto get_opt = [&](const char* attr) -> py::object {
                         if (!py::hasattr(record_or_none, attr)) return py::none();
                         return record_or_none.attr(attr);
                     };
                     auto ts = get_opt("timestamp");
                     if (!ts.is_none()) r.timestamp = ts.cast<uint64_t>();
                     auto ots = get_opt("observed_timestamp");
                     if (!ots.is_none()) r.observed_timestamp = ots.cast<uint64_t>();
                     auto sn = get_opt("severity_number");
                     if (!sn.is_none()) {
                         if (py::hasattr(sn, "value")) {
                             r.severity_number = sn.attr("value").cast<int>();
                         } else {
                             r.severity_number = sn.cast<int>();
                         }
                     }
                     auto st = get_opt("severity_text");
                     if (!st.is_none()) r.severity_text = st.cast<std::string>();
                     auto b = get_opt("body");
                     if (!b.is_none()) r.body = b;
                     auto attrs = get_opt("attributes");
                     if (!attrs.is_none() && py::isinstance<py::dict>(attrs))
                         r.attributes = attrs.cast<py::dict>();
                     auto en = get_opt("event_name");
                     if (!en.is_none()) r.event_name = en.cast<std::string>();
                     self.emit(r);
                     return;
                 }
                 // Build a record from keyword arguments.
                 otel_wrapper::LogRecordWrapper r;
                 if (!timestamp.is_none())
                     r.timestamp = timestamp.cast<uint64_t>();
                 if (!observed_timestamp.is_none())
                     r.observed_timestamp = observed_timestamp.cast<uint64_t>();
                 if (!severity_number.is_none()) {
                     if (py::hasattr(severity_number, "value")) {
                         r.severity_number = severity_number.attr("value").cast<int>();
                     } else {
                         r.severity_number = severity_number.cast<int>();
                     }
                 }
                 if (!severity_text.is_none())
                     r.severity_text = severity_text.cast<std::string>();
                 if (!body.is_none())
                     r.body = body;
                 if (!attributes.is_none())
                     r.attributes = attributes.cast<py::dict>();
                 if (!event_name.is_none())
                     r.event_name = event_name.cast<std::string>();
                 if (!exception.is_none())
                     r.exception = exception;
                 self.emit(r);
             },
             py::arg("record")             = py::none(),
             py::arg("timestamp")          = py::none(),
             py::arg("observed_timestamp") = py::none(),
             py::arg("severity_number")    = py::none(),
             py::arg("severity_text")      = py::none(),
             py::arg("body")               = py::none(),
             py::arg("attributes")         = py::none(),
             py::arg("event_name")         = py::none(),
             py::arg("exception")          = py::none(),
             "Emit a log record. Pass a LogRecord object as the first argument, "
             "or supply fields as keyword arguments.");

    py::class_<otel_wrapper::LoggerProviderWrapper,
               std::shared_ptr<otel_wrapper::LoggerProviderWrapper>>(m, "LoggerProvider")
        .def("get_logger",
             [](otel_wrapper::LoggerProviderWrapper& self,
                const std::string& name,
                py::object version,
                py::object schema_url,
                py::object attributes) {
                 return self.get_logger(name, version, schema_url, attributes);
             },
             py::arg("name"),
             py::arg("version")    = py::none(),
             py::arg("schema_url") = py::none(),
             py::arg("attributes") = py::none(),
             "Get (or create) a Logger for the given instrumentation scope.")
        .def("shutdown", &otel_wrapper::LoggerProviderWrapper::shutdown,
             "Flush and shut down the logger provider.")
        .def_property_readonly("configured", &otel_wrapper::LoggerProviderWrapper::is_configured,
             "True if a logger provider was built from the config file.");

    // -----------------------------------------------------------------------
    // SDK — combined single-parse provider
    // -----------------------------------------------------------------------

    py::class_<otel_wrapper::SDKWrapper>(m, "SDK")
#ifdef WITH_YAML_SDK_CONFIG
        .def(py::init<const std::string&>(),
             py::arg("path"),
             "Configure all OTel signals from a single YAML file.")
#endif
        .def(py::init([](py::dict cfg) {
                otel_wrapper::ProgrammaticConfig c;

                auto str_val = [](py::dict d, const char* key) -> std::string {
                    if (!d.contains(key)) return "";
                    auto v = d[key];
                    return v.is_none() ? "" : v.cast<std::string>();
                };
                auto pairs_val = [](py::dict d, const char* key)
                        -> std::vector<std::pair<std::string, std::string>> {
                    std::vector<std::pair<std::string, std::string>> out;
                    if (!d.contains(key) || d[key].is_none()) return out;
                    for (auto item : d[key].cast<py::list>()) {
                        auto t = item.cast<py::tuple>();
                        out.emplace_back(t[0].cast<std::string>(), t[1].cast<std::string>());
                    }
                    return out;
                };
                auto extract_signal = [&](const char* key) -> otel_wrapper::OtlpSignalConfig {
                    otel_wrapper::OtlpSignalConfig s;
                    if (!cfg.contains(key) || cfg[key].is_none()) return s;
                    auto d = cfg[key].cast<py::dict>();
                    s.endpoint = str_val(d, "endpoint");
                    s.headers  = pairs_val(d, "headers");
                    s.ca_file  = str_val(d, "ca_file");
                    s.key_file = str_val(d, "key_file");
                    s.cert_file = str_val(d, "cert_file");
                    return s;
                };

                c.resource_attributes = pairs_val(cfg, "resource_attributes");
                c.traces   = extract_signal("traces");
                c.metrics  = extract_signal("metrics");
                c.logs     = extract_signal("logs");
                if (cfg.contains("metric_interval_ms") && !cfg["metric_interval_ms"].is_none())
                    c.metric_interval_ms = cfg["metric_interval_ms"].cast<int>();
                if (cfg.contains("metric_timeout_ms") && !cfg["metric_timeout_ms"].is_none())
                    c.metric_timeout_ms = cfg["metric_timeout_ms"].cast<int>();
                return std::make_unique<otel_wrapper::SDKWrapper>(c);
             }),
             py::arg("config"),
             "Configure all OTel signals from a pre-parsed config dict.")
        .def("shutdown", &otel_wrapper::SDKWrapper::shutdown,
             "Flush and shut down all configured providers.")
        .def("release_config", &otel_wrapper::SDKWrapper::release_config,
             "Free the config scaffolding (model + registry) while keeping providers running. "
             "Call shutdown() later to stop the providers.")
        .def_property_readonly("tracer_provider", &otel_wrapper::SDKWrapper::tracer_provider,
             "The TracerProvider, or None if not configured.")
        .def_property_readonly("meter_provider", &otel_wrapper::SDKWrapper::meter_provider,
             "The MeterProvider, or None if not configured.")
        .def_property_readonly("logger_provider", &otel_wrapper::SDKWrapper::logger_provider,
             "The LoggerProvider, or None if not configured.");
}
