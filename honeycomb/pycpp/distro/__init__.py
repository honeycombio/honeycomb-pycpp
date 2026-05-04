# SPDX-FileCopyrightText: 2026 Honeycomb Authors <support@honeycomb.io>
# SPDX-License-Identifier: Apache-2.0

import os

from opentelemetry.instrumentation.distro import BaseDistro
from opentelemetry import metrics, trace
from honeycomb.pycpp.distro.patch_api import patch
import honeycomb_pycpp as otel

_DEFAULT_CONFIG = os.path.join(os.path.dirname(os.path.abspath(__file__)), "embedded", "otel.yaml")


class OpenTelemetryConfigurator():
    def configure(self, **kwargs):
        """Configure the SDK"""
        config_file = os.getenv("OTEL_CONFIG_FILE", _DEFAULT_CONFIG)
        tp = otel.TracerProvider(config_file)
        if tp.configured:
            trace.set_tracer_provider(tp)
        mp = otel.MeterProvider(config_file)
        if mp.configured:
            metrics.set_meter_provider(mp)


class OpenTelemetryDistro(BaseDistro):
    """
    The OpenTelemetry provided Distro configures a default set of
    configuration out of the box.
    """

    # pylint: disable=no-self-use
    def _configure(self, **kwargs):
        patch()
