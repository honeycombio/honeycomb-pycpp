# SPDX-FileCopyrightText: 2026 Honeycomb Authors <support@honeycomb.io>
# SPDX-License-Identifier: Apache-2.0

"""Parse an OTel YAML config file into a dict for SDK(config)."""

import os
import re

import yaml


def _expand_env_vars(text: str) -> str:
    """Expand ${VAR:-default} and ${VAR} patterns.

    Matches bash semantics: use default when VAR is unset or empty.
    """
    def _replace(m):
        var, default = m.group(1), m.group(2)
        value = os.environ.get(var, "")
        if not value and default is not None:
            return default
        return value

    return re.sub(r'\$\{([^}:]+)(?::-(.*?))?\}', _replace, text)


def _parse_headers_list(headers_list: str) -> list:
    """Parse a comma-separated key=value headers string."""
    if not headers_list:
        return []
    result = []
    for item in headers_list.split(","):
        item = item.strip()
        if "=" in item:
            key, _, value = item.partition("=")
            result.append((key.strip(), value.strip()))
    return result


def _extract_signal(exporter_cfg: dict) -> dict:
    otlp = (exporter_cfg or {}).get("otlp_http") or {}
    tls = otlp.get("tls") or {}
    return {
        "endpoint": otlp.get("endpoint") or "",
        "headers": _parse_headers_list(otlp.get("headers_list") or ""),
        "ca_file": tls.get("ca_file") or "",
        "key_file": tls.get("key_file") or "",
        "cert_file": tls.get("cert_file") or "",
    }


def load_config(path: str) -> dict:
    """Load an OTel YAML config file and return a dict for SDK(config)."""
    with open(path, encoding="utf-8") as f:
        raw = f.read()
    data = yaml.safe_load(_expand_env_vars(raw)) or {}

    # Resource attributes from "key=value,..." string
    resource = data.get("resource") or {}
    attrs_str = resource.get("attributes_list") or ""
    resource_attributes = []
    for item in attrs_str.split(","):
        item = item.strip()
        if "=" in item:
            k, _, v = item.partition("=")
            resource_attributes.append((k.strip(), v.strip()))

    # Tracer provider
    tp_cfg = data.get("tracer_provider") or {}
    tp_processors = tp_cfg.get("processors") or []
    if tp_processors:
        batch = (tp_processors[0] or {}).get("batch") or {}
        traces = _extract_signal(batch.get("exporter") or {})
    else:
        traces = _extract_signal({})

    # Meter provider
    mp_cfg = data.get("meter_provider") or {}
    mp_readers = mp_cfg.get("readers") or []
    if mp_readers:
        periodic = (mp_readers[0] or {}).get("periodic") or {}
        metric_interval_ms = int(periodic.get("interval") or 60000)
        metric_timeout_ms = int(periodic.get("timeout") or 30000)
        metrics = _extract_signal(periodic.get("exporter") or {})
    else:
        metric_interval_ms = 60000
        metric_timeout_ms = 30000
        metrics = _extract_signal({})

    # Logger provider
    lp_cfg = data.get("logger_provider") or {}
    lp_processors = lp_cfg.get("processors") or []
    if lp_processors:
        batch = (lp_processors[0] or {}).get("batch") or {}
        logs = _extract_signal(batch.get("exporter") or {})
    else:
        logs = _extract_signal({})

    return {
        "resource_attributes": resource_attributes,
        "traces": traces,
        "metrics": metrics,
        "metric_interval_ms": metric_interval_ms,
        "metric_timeout_ms": metric_timeout_ms,
        "logs": logs,
    }
