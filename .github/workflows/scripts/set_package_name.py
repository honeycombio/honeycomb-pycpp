#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Honeycomb Authors <support@honeycomb.io>
# SPDX-License-Identifier: Apache-2.0
"""Patch pyproject.toml name before a cibuildwheel build based on exporter flags."""
import os
import re

http = os.environ.get("WITH_OTLP_HTTP", "ON").upper() == "ON"
grpc = os.environ.get("WITH_OTLP_GRPC", "OFF").upper() == "ON"
if http and grpc:
    name = "honeycomb-pycpp"
elif grpc:
    name = "honeycomb-pycpp-otlp-grpc"
else:
    name = "honeycomb-pycpp-otlp-http"

path = "pyproject.toml"
content = open(path).read()
content = re.sub(r'^name = ".+"', f'name = "{name}"', content, flags=re.MULTILINE)
open(path, "w").write(content)
print(f"set package name: {name}")
