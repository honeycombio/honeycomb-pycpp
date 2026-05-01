# SPDX-FileCopyrightText: 2026 Honeycomb Authors <support@honeycomb.io>
# SPDX-License-Identifier: Apache-2.0

import os
import sys
import subprocess

from setuptools import setup, Extension
from setuptools.command.build_ext import build_ext


class CMakeExtension(Extension):
    def __init__(self, name, sourcedir=""):
        Extension.__init__(self, name, sources=[])
        self.sourcedir = os.path.abspath(sourcedir)


class CMakeBuild(build_ext):
    def build_extension(self, ext):
        extdir = os.path.abspath(os.path.dirname(self.get_ext_fullpath(ext.name)))

        # Required for auto-detection & inclusion of auxiliary "native" libs
        if not extdir.endswith(os.path.sep):
            extdir += os.path.sep

        debug = int(os.environ.get("DEBUG", 0)) if self.debug is None else self.debug
        cfg = "Debug" if debug else "Release"

        # Get Python paths for CMake
        import sysconfig
        python_include_dir = sysconfig.get_path('include')
        python_library = sysconfig.get_config_var('LIBDIR')

        cmake_args = [
            f"-DCMAKE_LIBRARY_OUTPUT_DIRECTORY={extdir}",
            f"-DPYTHON_EXECUTABLE={sys.executable}",
            f"-DPython_EXECUTABLE={sys.executable}",
            f"-DPython_ROOT_DIR={sys.prefix}",
            f"-DPython_INCLUDE_DIR={python_include_dir}",
            f"-DCMAKE_BUILD_TYPE={cfg}",
        ]

        build_args = []

        if sys.platform.startswith("darwin"):
            # ARCHFLAGS is set by cibuildwheel as "-arch arm64" (compiler flags),
            # but CMAKE_OSX_ARCHITECTURES expects just "arm64" or "arm64;x86_64".
            import re
            archs = os.environ.get("ARCHFLAGS", "")
            if archs:
                arch_names = re.findall(r"-arch\s+(\S+)", archs)
                cmake_args.append(
                    f"-DCMAKE_OSX_ARCHITECTURES={';'.join(arch_names) if arch_names else archs}"
                )

            deployment_target = os.environ.get("MACOSX_DEPLOYMENT_TARGET", "")
            if deployment_target:
                cmake_args.append(f"-DCMAKE_OSX_DEPLOYMENT_TARGET={deployment_target}")

        # Set CMAKE_BUILD_PARALLEL_LEVEL to control the parallel build level
        if "CMAKE_BUILD_PARALLEL_LEVEL" not in os.environ:
            build_args += ["-j4"]

        if not os.path.exists(self.build_temp):
            os.makedirs(self.build_temp)

        subprocess.check_call(
            ["cmake", ext.sourcedir] + cmake_args, cwd=self.build_temp
        )
        subprocess.check_call(
            ["cmake", "--build", "."] + build_args, cwd=self.build_temp
        )


setup(
    ext_modules=[CMakeExtension("honeycomb_pycpp")],
    cmdclass={"build_ext": CMakeBuild},
)
