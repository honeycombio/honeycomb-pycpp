# SPDX-FileCopyrightText: 2026 Honeycomb Authors <support@honeycomb.io>
# SPDX-License-Identifier: Apache-2.0

import os

from opentelemetry.instrumentation.distro import BaseDistro
from opentelemetry import trace
from honeycomb.pycpp.distro.patch_api import patch
import otel_cpp_tracer as otel

_DEFAULT_CONFIG = os.path.join(os.path.dirname(os.path.abspath(__file__)), "embedded", "otel.yaml")


class OpenTelemetryConfigurator():
    def configure(self, **kwargs):
        """Configure the SDK"""
        trace.set_tracer_provider(otel.TracerProvider(os.getenv("OTEL_CONFIG_FILE", _DEFAULT_CONFIG)))


class OpenTelemetryDistro(BaseDistro):
    """
    The OpenTelemetry provided Distro configures a default set of
    configuration out of the box.
    """

    # pylint: disable=no-self-use
    def _configure(self, **kwargs):
        patch()
