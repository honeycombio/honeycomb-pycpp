# SPDX-FileCopyrightText: 2026 Honeycomb Authors <support@honeycomb.io>
# SPDX-License-Identifier: Apache-2.0

import atexit
import os

from opentelemetry.instrumentation.distro import BaseDistro
from opentelemetry import metrics, trace
import opentelemetry._logs as logs
from honeycomb.pycpp.distro.patch_api import patch
from honeycomb.pycpp.distro._config import load_config
import honeycomb_pycpp as otel

_DEFAULT_CONFIG = os.path.join(os.path.dirname(os.path.abspath(__file__)), "embedded", "otel.yaml")


class OpenTelemetryConfigurator():
    def configure(self, **kwargs):
        """Configure the SDK"""
        config_file = os.getenv("OTEL_CONFIG_FILE", _DEFAULT_CONFIG)
        sdk = otel.SDK(load_config(config_file))
        if sdk.tracer_provider is not None:
            trace.set_tracer_provider(sdk.tracer_provider)
        if sdk.meter_provider is not None:
            metrics.set_meter_provider(sdk.meter_provider)
        if sdk.logger_provider is not None:
            logs.set_logger_provider(sdk.logger_provider)
        atexit.register(sdk.shutdown)


class OpenTelemetryDistro(BaseDistro):
    """
    The OpenTelemetry provided Distro configures a default set of
    configuration out of the box.
    """

    # pylint: disable=no-self-use
    def _configure(self, **kwargs):
        patch()
