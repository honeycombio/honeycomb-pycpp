// SPDX-FileCopyrightText: 2026 Honeycomb Authors <support@honeycomb.io>
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <memory>
#include <string>
#include <vector>

#include <pybind11/pybind11.h>
#include "opentelemetry/common/attribute_value.h"
#include "opentelemetry/common/key_value_iterable.h"
#include "opentelemetry/nostd/function_ref.h"
#include "opentelemetry/nostd/span.h"
#include "opentelemetry/nostd/string_view.h"

namespace py = pybind11;

// Wraps a Python dict as a KeyValueIterable, lazily converting values during
// ForEachKeyValue. Sequence values are converted into temporary stack-allocated
// arrays that live for the duration of each synchronous callback invocation.
class PyAttributeIterable final : public opentelemetry::common::KeyValueIterable {
    py::dict dict_;
public:
    explicit PyAttributeIterable(py::dict d) : dict_(std::move(d)) {}

    size_t size() const noexcept override { return dict_.size(); }

    bool ForEachKeyValue(opentelemetry::nostd::function_ref<
                             bool(opentelemetry::nostd::string_view,
                                  opentelemetry::common::AttributeValue)>
                             callback) const noexcept override {
        namespace nostd = opentelemetry::nostd;
        for (auto item : dict_) {
            try {
                std::string key = item.first.cast<std::string>();
                py::object val  = py::reinterpret_borrow<py::object>(item.second);
                bool cont = true;
                if (py::isinstance<py::bool_>(val)) {
                    cont = callback(key, val.cast<bool>());
                } else if (py::isinstance<py::int_>(val)) {
                    cont = callback(key, val.cast<int64_t>());
                } else if (py::isinstance<py::float_>(val)) {
                    cont = callback(key, val.cast<double>());
                } else if (py::isinstance<py::str>(val)) {
                    std::string s = val.cast<std::string>();
                    cont = callback(key, nostd::string_view(s));
                } else if (py::isinstance<py::sequence>(val)) {
                    auto seq = val.cast<py::sequence>();
                    if (seq.size() == 0) continue;
                    py::object first = seq[0];
                    if (py::isinstance<py::bool_>(first)) {
                        auto arr = std::make_unique<bool[]>(seq.size());
                        for (size_t i = 0; i < seq.size(); ++i)
                            arr[i] = seq[i].cast<bool>();
                        cont = callback(key, nostd::span<const bool>(arr.get(), seq.size()));
                    } else if (py::isinstance<py::int_>(first)) {
                        std::vector<int64_t> v;
                        v.reserve(seq.size());
                        for (auto el : seq) v.push_back(el.cast<int64_t>());
                        cont = callback(key, nostd::span<const int64_t>(v.data(), v.size()));
                    } else if (py::isinstance<py::float_>(first)) {
                        std::vector<double> v;
                        v.reserve(seq.size());
                        for (auto el : seq) v.push_back(el.cast<double>());
                        cont = callback(key, nostd::span<const double>(v.data(), v.size()));
                    } else if (py::isinstance<py::str>(first)) {
                        std::vector<std::string> strs;
                        strs.reserve(seq.size());
                        for (auto el : seq) strs.push_back(el.cast<std::string>());
                        std::vector<nostd::string_view> views;
                        views.reserve(strs.size());
                        for (const auto& s : strs) views.push_back(s);
                        cont = callback(key, nostd::span<const nostd::string_view>(
                                                 views.data(), views.size()));
                    }
                }
                if (!cont) return false;
            } catch (...) {}
        }
        return true;
    }
};
