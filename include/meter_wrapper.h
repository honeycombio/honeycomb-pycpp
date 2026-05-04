// SPDX-FileCopyrightText: 2026 Honeycomb Authors <support@honeycomb.io>
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <memory>
#include <string>
#include <vector>
#include <mutex>

#include <pybind11/pybind11.h>
#include "opentelemetry/common/attribute_value.h"
#include "opentelemetry/common/key_value_iterable.h"
#include "opentelemetry/context/context.h"
#include "opentelemetry/metrics/meter.h"
#include "opentelemetry/metrics/meter_provider.h"
#include "opentelemetry/metrics/sync_instruments.h"
#include "opentelemetry/metrics/async_instruments.h"
#include "opentelemetry/metrics/observer_result.h"
#include "opentelemetry/nostd/shared_ptr.h"
#include "opentelemetry/nostd/unique_ptr.h"
#include "opentelemetry/sdk/configuration/configuration.h"
#include "opentelemetry/sdk/configuration/configured_sdk.h"

namespace py = pybind11;

namespace otel_wrapper {

namespace metrics_api = opentelemetry::metrics;
namespace nostd = opentelemetry::nostd;

// Simple value holder passed back by Python observable callbacks.
class ObservationWrapper {
public:
    explicit ObservationWrapper(double value) : value_(value) {}
    double get_value() const { return value_; }
private:
    double value_;
};

class CounterWrapper {
public:
    explicit CounterWrapper(nostd::unique_ptr<metrics_api::Counter<double>> counter);
    void add(double value, const opentelemetry::common::KeyValueIterable* attributes = nullptr);
private:
    nostd::unique_ptr<metrics_api::Counter<double>> counter_;
};

class UpDownCounterWrapper {
public:
    explicit UpDownCounterWrapper(nostd::unique_ptr<metrics_api::UpDownCounter<double>> counter);
    void add(double value, const opentelemetry::common::KeyValueIterable* attributes = nullptr);
private:
    nostd::unique_ptr<metrics_api::UpDownCounter<double>> counter_;
};

class HistogramWrapper {
public:
    explicit HistogramWrapper(nostd::unique_ptr<metrics_api::Histogram<double>> histogram);
    void record(double value, const opentelemetry::common::KeyValueIterable* attributes = nullptr);
private:
    nostd::unique_ptr<metrics_api::Histogram<double>> histogram_;
};

// Gauge is only available in ABI v2. On ABI v1 the wrapper is a no-op stub.
class GaugeWrapper {
public:
#if OPENTELEMETRY_ABI_VERSION_NO >= 2
    explicit GaugeWrapper(nostd::unique_ptr<metrics_api::Gauge<double>> gauge);
    void set(double value, const opentelemetry::common::KeyValueIterable* attributes = nullptr);
private:
    nostd::unique_ptr<metrics_api::Gauge<double>> gauge_;
#else
    void set(double, const opentelemetry::common::KeyValueIterable* = nullptr) noexcept {}
#endif
};

// Per-callback state kept alive for the lifetime of each registered callback.
struct CallbackState {
    py::object callback;
};

class ObservableInstrumentWrapper {
public:
    explicit ObservableInstrumentWrapper(
        nostd::shared_ptr<metrics_api::ObservableInstrument> instrument);
    ~ObservableInstrumentWrapper();

    void add_py_callback(py::object callback);

private:
    nostd::shared_ptr<metrics_api::ObservableInstrument> instrument_;
    std::vector<std::unique_ptr<CallbackState>> callback_states_;

    // Static C function pointer required by the OTel C++ AddCallback API.
    static void c_callback(metrics_api::ObserverResult result, void* state) noexcept;
};

class MeterWrapper {
public:
    explicit MeterWrapper(nostd::shared_ptr<metrics_api::Meter> meter);

    std::shared_ptr<CounterWrapper> create_counter(
        const std::string& name,
        const std::string& unit = "",
        const std::string& description = "");

    std::shared_ptr<UpDownCounterWrapper> create_up_down_counter(
        const std::string& name,
        const std::string& unit = "",
        const std::string& description = "");

    std::shared_ptr<HistogramWrapper> create_histogram(
        const std::string& name,
        const std::string& unit = "",
        const std::string& description = "");

    std::shared_ptr<GaugeWrapper> create_gauge(
        const std::string& name,
        const std::string& unit = "",
        const std::string& description = "");

    std::shared_ptr<ObservableInstrumentWrapper> create_observable_counter(
        const std::string& name,
        std::vector<py::object> callbacks = {},
        const std::string& unit = "",
        const std::string& description = "");

    std::shared_ptr<ObservableInstrumentWrapper> create_observable_up_down_counter(
        const std::string& name,
        std::vector<py::object> callbacks = {},
        const std::string& unit = "",
        const std::string& description = "");

    std::shared_ptr<ObservableInstrumentWrapper> create_observable_gauge(
        const std::string& name,
        std::vector<py::object> callbacks = {},
        const std::string& unit = "",
        const std::string& description = "");

private:
    nostd::shared_ptr<metrics_api::Meter> meter_;
    // Keep observable wrappers alive so their CallbackState objects are never
    // freed while the meter is alive, even if Python doesn't store the return value.
    std::vector<std::shared_ptr<ObservableInstrumentWrapper>> observable_instruments_;

    std::shared_ptr<ObservableInstrumentWrapper> make_observable(
        nostd::shared_ptr<metrics_api::ObservableInstrument> instrument,
        const std::vector<py::object>& callbacks);
};

class MeterProviderWrapper {
public:
    explicit MeterProviderWrapper(const std::string& path);
    ~MeterProviderWrapper();

    std::shared_ptr<MeterWrapper> get_meter(
        const std::string& name,
        py::object version = py::none(),
        py::object schema_url = py::none(),
        py::object attributes = py::none());

    void shutdown();
    bool is_configured() const { return sdk_ && sdk_->meter_provider; }

private:
    std::unique_ptr<opentelemetry::sdk::configuration::ConfiguredSdk> sdk_;
};

}  // namespace otel_wrapper
