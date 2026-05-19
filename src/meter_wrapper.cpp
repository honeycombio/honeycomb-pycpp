// SPDX-FileCopyrightText: 2026 Honeycomb Authors <support@honeycomb.io>
// SPDX-License-Identifier: Apache-2.0

#include "meter_wrapper.h"
#include "py_attribute_iterable.h"

#include <pybind11/pybind11.h>
#include <iostream>

namespace otel_wrapper {

namespace metrics_api = opentelemetry::metrics;
namespace nostd = opentelemetry::nostd;

// ---------------------------------------------------------------------------
// CounterWrapper
// ---------------------------------------------------------------------------

CounterWrapper::CounterWrapper(nostd::unique_ptr<metrics_api::Counter<double>> counter)
    : counter_(std::move(counter)) {}

void CounterWrapper::add(double value, const opentelemetry::common::KeyValueIterable* attributes,
                         const opentelemetry::context::Context& context) {
    if (!counter_) return;
    if (attributes) {
        counter_->Add(value, *attributes, context);
    } else {
        counter_->Add(value, context);
    }
}

// ---------------------------------------------------------------------------
// UpDownCounterWrapper
// ---------------------------------------------------------------------------

UpDownCounterWrapper::UpDownCounterWrapper(
    nostd::unique_ptr<metrics_api::UpDownCounter<double>> counter)
    : counter_(std::move(counter)) {}

void UpDownCounterWrapper::add(double value,
                               const opentelemetry::common::KeyValueIterable* attributes,
                               const opentelemetry::context::Context& context) {
    if (!counter_) return;
    if (attributes) {
        counter_->Add(value, *attributes, context);
    } else {
        counter_->Add(value, context);
    }
}

// ---------------------------------------------------------------------------
// HistogramWrapper
// ---------------------------------------------------------------------------

HistogramWrapper::HistogramWrapper(nostd::unique_ptr<metrics_api::Histogram<double>> histogram)
    : histogram_(std::move(histogram)) {}

void HistogramWrapper::record(double value,
                              const opentelemetry::common::KeyValueIterable* attributes,
                              const opentelemetry::context::Context& context) {
    if (!histogram_) return;
    if (attributes) {
        histogram_->Record(value, *attributes, context);
    } else {
        histogram_->Record(value, context);
    }
}

// ---------------------------------------------------------------------------
// GaugeWrapper (ABI v2 only; ABI v1 methods are no-ops defined in the header)
// ---------------------------------------------------------------------------

#if OPENTELEMETRY_ABI_VERSION_NO >= 2
GaugeWrapper::GaugeWrapper(nostd::unique_ptr<metrics_api::Gauge<double>> gauge)
    : gauge_(std::move(gauge)) {}

void GaugeWrapper::set(double value, const opentelemetry::common::KeyValueIterable* attributes,
                       const opentelemetry::context::Context& context) {
    if (!gauge_) return;
    if (attributes) {
        gauge_->Record(value, *attributes, context);
    } else {
        gauge_->Record(value, context);
    }
}
#endif

// ---------------------------------------------------------------------------
// ObservableInstrumentWrapper
// ---------------------------------------------------------------------------

ObservableInstrumentWrapper::ObservableInstrumentWrapper(
    nostd::shared_ptr<metrics_api::ObservableInstrument> instrument)
    : instrument_(std::move(instrument)) {}

ObservableInstrumentWrapper::~ObservableInstrumentWrapper() {
    for (auto& state : callback_states_) {
        instrument_->RemoveCallback(c_callback, state.get());
    }
}

void ObservableInstrumentWrapper::add_py_callback(py::object callback) {
    auto state = std::make_unique<CallbackState>();
    state->callback = std::move(callback);
    instrument_->AddCallback(c_callback, state.get());
    callback_states_.push_back(std::move(state));
}

void ObservableInstrumentWrapper::c_callback(metrics_api::ObserverResult result,
                                             void* state) noexcept {
    auto* cb_state = static_cast<CallbackState*>(state);

    py::gil_scoped_acquire gil;
    try {
        // Build a CallbackOptions for the Python instrumentor if available.
        py::object options;
        try {
            options = py::module_::import("opentelemetry.metrics").attr("CallbackOptions")();
        } catch (...) {
            options = py::none();
        }

        py::object observations = cb_state->callback(options);
        if (observations.is_none()) return;

        // Helper: extract double value from any Observation-like object.
        auto get_value = [](py::handle h) -> double {
            py::object obs = py::reinterpret_borrow<py::object>(h);
            // Try our own wrapper first, then duck-type via .value attribute.
            try {
                return obs.cast<ObservationWrapper>().get_value();
            } catch (...) {}
            return obs.attr("value").cast<double>();
        };

        // Helper: forward optional attributes dict to the observer.
        auto observe_double = [&](nostd::shared_ptr<metrics_api::ObserverResultT<double>>& obs,
                                  py::handle h, double value) {
            py::object o = py::reinterpret_borrow<py::object>(h);
            if (py::hasattr(o, "attributes")) {
                py::object attrs = o.attr("attributes");
                if (!attrs.is_none() && py::isinstance<py::dict>(attrs)) {
                    PyAttributeIterable kv(attrs.cast<py::dict>());
                    obs->Observe(value, kv);
                    return;
                }
            }
            obs->Observe(value);
        };

        auto observe_int = [&](nostd::shared_ptr<metrics_api::ObserverResultT<int64_t>>& obs,
                               py::handle h, int64_t value) {
            py::object o = py::reinterpret_borrow<py::object>(h);
            if (py::hasattr(o, "attributes")) {
                py::object attrs = o.attr("attributes");
                if (!attrs.is_none() && py::isinstance<py::dict>(attrs)) {
                    PyAttributeIterable kv(attrs.cast<py::dict>());
                    obs->Observe(value, kv);
                    return;
                }
            }
            obs->Observe(value);
        };

        // Use py::iter so generators (yield) work as well as plain lists.
        if (nostd::holds_alternative<nostd::shared_ptr<metrics_api::ObserverResultT<double>>>(
                result)) {
            auto& observer =
                nostd::get<nostd::shared_ptr<metrics_api::ObserverResultT<double>>>(result);
            for (auto h : py::iter(observations))
                observe_double(observer, h, get_value(h));
        } else if (nostd::holds_alternative<
                       nostd::shared_ptr<metrics_api::ObserverResultT<int64_t>>>(result)) {
            auto& observer =
                nostd::get<nostd::shared_ptr<metrics_api::ObserverResultT<int64_t>>>(result);
            for (auto h : py::iter(observations))
                observe_int(observer, h, static_cast<int64_t>(get_value(h)));
        }
    } catch (const py::error_already_set& e) {
        std::cerr << "[honeycomb_pycpp] observable callback raised a Python exception: "
                  << e.what() << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "[honeycomb_pycpp] observable callback error: " << e.what() << std::endl;
    } catch (...) {
        std::cerr << "[honeycomb_pycpp] observable callback raised an unknown exception" << std::endl;
    }
}

// ---------------------------------------------------------------------------
// MeterWrapper
// ---------------------------------------------------------------------------

MeterWrapper::MeterWrapper(nostd::shared_ptr<metrics_api::Meter> meter)
    : meter_(std::move(meter)) {}

std::shared_ptr<CounterWrapper> MeterWrapper::create_counter(const std::string& name,
                                                              const std::string& unit,
                                                              const std::string& description) {
    if (!meter_) return nullptr;
    return std::make_shared<CounterWrapper>(
        meter_->CreateDoubleCounter(name, description, unit));
}

std::shared_ptr<UpDownCounterWrapper> MeterWrapper::create_up_down_counter(
    const std::string& name,
    const std::string& unit,
    const std::string& description) {
    if (!meter_) return nullptr;
    return std::make_shared<UpDownCounterWrapper>(
        meter_->CreateDoubleUpDownCounter(name, description, unit));
}

std::shared_ptr<HistogramWrapper> MeterWrapper::create_histogram(
    const std::string& name,
    const std::string& unit,
    const std::string& description) {
    if (!meter_) return nullptr;
    return std::make_shared<HistogramWrapper>(
        meter_->CreateDoubleHistogram(name, description, unit));
}

std::shared_ptr<GaugeWrapper> MeterWrapper::create_gauge(const std::string& name,
                                                          const std::string& unit,
                                                          const std::string& description) {
#if OPENTELEMETRY_ABI_VERSION_NO >= 2
    if (!meter_) return nullptr;
    return std::make_shared<GaugeWrapper>(meter_->CreateDoubleGauge(name, description, unit));
#else
    (void)name;
    (void)unit;
    (void)description;
    return std::make_shared<GaugeWrapper>();
#endif
}

std::shared_ptr<ObservableInstrumentWrapper> MeterWrapper::make_observable(
    nostd::shared_ptr<metrics_api::ObservableInstrument> instrument,
    const std::vector<py::object>& callbacks) {
    auto wrapper = std::make_shared<ObservableInstrumentWrapper>(std::move(instrument));
    for (const auto& cb : callbacks) {
        wrapper->add_py_callback(cb);
    }
    observable_instruments_.push_back(wrapper);
    return wrapper;
}

std::shared_ptr<ObservableInstrumentWrapper> MeterWrapper::create_observable_counter(
    const std::string& name,
    std::vector<py::object> callbacks,
    const std::string& unit,
    const std::string& description) {
    if (!meter_) return nullptr;
    return make_observable(meter_->CreateDoubleObservableCounter(name, description, unit),
                           callbacks);
}

std::shared_ptr<ObservableInstrumentWrapper> MeterWrapper::create_observable_up_down_counter(
    const std::string& name,
    std::vector<py::object> callbacks,
    const std::string& unit,
    const std::string& description) {
    if (!meter_) return nullptr;
    return make_observable(
        meter_->CreateDoubleObservableUpDownCounter(name, description, unit), callbacks);
}

std::shared_ptr<ObservableInstrumentWrapper> MeterWrapper::create_observable_gauge(
    const std::string& name,
    std::vector<py::object> callbacks,
    const std::string& unit,
    const std::string& description) {
    if (!meter_) return nullptr;
    return make_observable(meter_->CreateDoubleObservableGauge(name, description, unit),
                           callbacks);
}

// ---------------------------------------------------------------------------
// MeterProviderWrapper
// ---------------------------------------------------------------------------

MeterProviderWrapper::MeterProviderWrapper(
        std::shared_ptr<opentelemetry::sdk::metrics::MeterProvider> provider)
    : provider_(std::move(provider)) {}

MeterProviderWrapper::~MeterProviderWrapper() {
    shutdown();
}

std::shared_ptr<MeterWrapper> MeterProviderWrapper::get_meter(const std::string& name,
                                                               py::object version,
                                                               py::object schema_url,
                                                               py::object attributes) {
    if (!provider_) return nullptr;

    auto ver_str    = version.is_none()    ? "" : version.cast<std::string>();
    auto schema_str = schema_url.is_none() ? "" : schema_url.cast<std::string>();
    (void)attributes;

    auto meter = provider_->GetMeter(name, ver_str, schema_str);
    return std::make_shared<MeterWrapper>(meter);
}

void MeterProviderWrapper::shutdown() {
    if (provider_) {
        provider_->ForceFlush();
        provider_->Shutdown();
        provider_.reset();
    }
}

}  // namespace otel_wrapper
