# SPDX-FileCopyrightText: 2026 Honeycomb Authors <support@honeycomb.io>
# SPDX-License-Identifier: Apache-2.0

import os

from opentelemetry.instrumentation.distro import BaseDistro
from opentelemetry import trace
from opentelemetry.cpp.distro.patch_api import patch
import otel_cpp_tracer as otel


class OpenTelemetryConfigurator():
    def configure(self, **kwargs):
        """Configure the SDK"""
        # Set the tracer provider
        trace.set_tracer_provider(otel.TracerProvider(os.getenv("OTEL_SERVICE_NAME", "unknown_service")))


class OpenTelemetryDistro(BaseDistro):
    """
    The OpenTelemetry provided Distro configures a default set of
    configuration out of the box.
    """

    # pylint: disable=no-self-use
    def _configure(self, **kwargs):
        patch()
