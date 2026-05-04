// SPDX-FileCopyrightText: 2026 Honeycomb Authors <support@honeycomb.io>
// SPDX-License-Identifier: Apache-2.0

#include "meter_wrapper.h"
#include "py_attribute_iterable.h"

#include "opentelemetry/exporters/ostream/console_push_metric_builder.h"
#include "opentelemetry/exporters/otlp/otlp_grpc_push_metric_builder.h"
#include "opentelemetry/exporters/otlp/otlp_http_push_metric_builder.h"
#include "opentelemetry/metrics/provider.h"
#include "opentelemetry/sdk/configuration/yaml_configuration_parser.h"

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

void CounterWrapper::add(double value, const opentelemetry::common::KeyValueIterable* attributes) {
    if (!counter_) return;
    if (attributes) {
        counter_->Add(value, *attributes);
    } else {
        counter_->Add(value);
    }
}

// ---------------------------------------------------------------------------
// UpDownCounterWrapper
// ---------------------------------------------------------------------------

UpDownCounterWrapper::UpDownCounterWrapper(
    nostd::unique_ptr<metrics_api::UpDownCounter<double>> counter)
    : counter_(std::move(counter)) {}

void UpDownCounterWrapper::add(double value,
                               const opentelemetry::common::KeyValueIterable* attributes) {
    if (!counter_) return;
    if (attributes) {
        counter_->Add(value, *attributes);
    } else {
        counter_->Add(value);
    }
}

// ---------------------------------------------------------------------------
// HistogramWrapper
// ---------------------------------------------------------------------------

HistogramWrapper::HistogramWrapper(nostd::unique_ptr<metrics_api::Histogram<double>> histogram)
    : histogram_(std::move(histogram)) {}

void HistogramWrapper::record(double value,
                              const opentelemetry::common::KeyValueIterable* attributes) {
    if (!histogram_) return;
    // ABI v1: Record(value) and Record(value, attrs) are not available without a context.
    // Pass an empty context as required by the v1 API.
    if (attributes) {
        histogram_->Record(value, *attributes, opentelemetry::context::Context{});
    } else {
        histogram_->Record(value, opentelemetry::context::Context{});
    }
}

// ---------------------------------------------------------------------------
// GaugeWrapper (ABI v2 only; ABI v1 methods are no-ops defined in the header)
// ---------------------------------------------------------------------------

#if OPENTELEMETRY_ABI_VERSION_NO >= 2
GaugeWrapper::GaugeWrapper(nostd::unique_ptr<metrics_api::Gauge<double>> gauge)
    : gauge_(std::move(gauge)) {}

void GaugeWrapper::set(double value, const opentelemetry::common::KeyValueIterable* attributes) {
    if (!gauge_) return;
    if (attributes) {
        gauge_->Record(value, *attributes);
    } else {
        gauge_->Record(value);
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

MeterProviderWrapper::MeterProviderWrapper(const std::string& path) {
    std::shared_ptr<opentelemetry::sdk::configuration::Registry> registry(
        new opentelemetry::sdk::configuration::Registry);

    opentelemetry::exporter::metrics::ConsolePushMetricBuilder::Register(registry.get());
    opentelemetry::exporter::otlp::OtlpHttpPushMetricBuilder::Register(registry.get());
    opentelemetry::exporter::otlp::OtlpGrpcPushMetricBuilder::Register(registry.get());

    auto model = opentelemetry::sdk::configuration::YamlConfigurationParser::ParseFile(path);
    if (!model) throw std::runtime_error("Failed to parse config: " + path);

    // Keep meter_provider; null out the other signals so ConfiguredSdk doesn't
    // attempt to build them without the corresponding exporters registered.
    model->tracer_provider  = nullptr;
    model->logger_provider  = nullptr;

    sdk_ = opentelemetry::sdk::configuration::ConfiguredSdk::Create(registry, model);
    if (!sdk_) throw std::runtime_error("Unsupported configuration: " + path);

    sdk_->Install();

    if (sdk_->meter_provider) {
        auto std_mp = std::static_pointer_cast<metrics_api::MeterProvider>(sdk_->meter_provider);
        nostd::shared_ptr<metrics_api::MeterProvider> mp(std_mp);
        metrics_api::Provider::SetMeterProvider(mp);
    }
}

MeterProviderWrapper::~MeterProviderWrapper() {
    shutdown();
}

std::shared_ptr<MeterWrapper> MeterProviderWrapper::get_meter(const std::string& name,
                                                               py::object version,
                                                               py::object schema_url,
                                                               py::object attributes) {
    if (!sdk_ || !sdk_->meter_provider) return nullptr;

    auto ver_str    = version.is_none()    ? "" : version.cast<std::string>();
    auto schema_str = schema_url.is_none() ? "" : schema_url.cast<std::string>();

    // Attributes are accepted for API compatibility but not forwarded (no ABI v1 support).
    (void)attributes;

    auto meter = sdk_->meter_provider->GetMeter(name, ver_str, schema_str);
    return std::make_shared<MeterWrapper>(meter);
}

void MeterProviderWrapper::shutdown() {
    if (sdk_) {
        sdk_->UnInstall();
        sdk_.reset();
    }
}

}  // namespace otel_wrapper
