# Build Instructions

Detailed instructions for building the OpenTelemetry C++ Python bindings.

## Prerequisites

### 1. Install OpenTelemetry C++ SDK

#### macOS (using Homebrew)

```bash
brew install opentelemetry-cpp
```

#### Ubuntu/Debian

```bash
sudo apt-get update
sudo apt-get install -y \
    cmake \
    build-essential \
    libcurl4-openssl-dev \
    libprotobuf-dev \
    protobuf-compiler

# Install OpenTelemetry C++ from source
git clone --recurse-submodules https://github.com/open-telemetry/opentelemetry-cpp.git
cd opentelemetry-cpp
mkdir build && cd build

cmake -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
      -DWITH_OTLP_HTTP=ON \
      -DBUILD_TESTING=OFF \
      -DWITH_EXAMPLES=OFF \
      ..

make -j$(nproc)
sudo make install
```

#### From Source (All Platforms)

```bash
# Clone with submodules
git clone --recurse-submodules https://github.com/open-telemetry/opentelemetry-cpp.git
cd opentelemetry-cpp

# Create build directory
mkdir build && cd build

# Configure
cmake -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
      -DWITH_OTLP_HTTP=ON \
      -DWITH_OTLP_GRPC=OFF \
      -DBUILD_TESTING=OFF \
      -DWITH_EXAMPLES=OFF \
      -DCMAKE_INSTALL_PREFIX=/usr/local \
      ..

# Build (use appropriate number of jobs)
make -j4

# Install
sudo make install

# Update library cache (Linux)
sudo ldconfig
```

### 2. Install Python Dependencies

```bash
# Install dev tools
pip -r requirements-dev.txt
```

### 3. Verify CMake Version

```bash
cmake --version  # Should be >= 3.15
```

## Building the Python Extension

### Option 1: Using Make (Recommended)

```bash
# Build in development mode
make build

# Or install development dependencies first
make dev
```

### Option 2: Using pip

```bash
# Development mode (editable install)
pip install -e .

# Regular install
pip install .

# Verbose build (useful for debugging)
pip install -v .
```

### Option 3: Using setup.py

```bash
# Build extension in place
python setup.py build_ext --inplace

# Build and install
python setup.py install
```

## Testing the Build

### Run the test script

```bash
make test
# or
python test_bindings.py
```

### Run the examples

```bash
make example
# or
python examples/basic_tracing.py
```

## Troubleshooting

### OpenTelemetry C++ not found

**Error:** `Could not find OpenTelemetry C++`

**Solution:**
```bash
# Set CMAKE_PREFIX_PATH to the installation location
export CMAKE_PREFIX_PATH=/usr/local:$CMAKE_PREFIX_PATH
pip install -e .

# Or specify during build
CMAKE_PREFIX_PATH=/usr/local pip install -e .
```

### pybind11 not found

**Error:** `Could not find pybind11`

**Solution:**
```bash
pip install pybind11[global]
# or
pip install "pybind11[global]>=2.10.0"
```

### Compiler errors

**Error:** C++ compilation errors

**Solution:**
```bash
# Ensure C++17 support
# macOS: Update Xcode Command Line Tools
xcode-select --install

# Linux: Install modern g++
sudo apt-get install g++-9
export CXX=g++-9

# Then rebuild
pip install -e . --force-reinstall
```

### Library linking errors (macOS)

**Error:** `Library not loaded: @rpath/libopentelemetry_...`

**Solution:**
```bash
# Set DYLD_LIBRARY_PATH (for testing only)
export DYLD_LIBRARY_PATH=/usr/local/lib:$DYLD_LIBRARY_PATH

# Or fix install names (permanent fix)
install_name_tool -add_rpath /usr/local/lib honeycomb_pycpp.*.so
```

### Library linking errors (Linux)

**Error:** `cannot open shared object file: libopentelemetry_...`

**Solution:**
```bash
# Update library cache
sudo ldconfig

# Or add to LD_LIBRARY_PATH
export LD_LIBRARY_PATH=/usr/local/lib:$LD_LIBRARY_PATH

# Or add to ldconfig
echo "/usr/local/lib" | sudo tee /etc/ld.so.conf.d/opentelemetry.conf
sudo ldconfig
```

## Development Workflow

### 1. Make changes to C++ code

Edit files in `src/` or `include/`

### 2. Rebuild

```bash
# Quick rebuild
pip install -e . --force-reinstall --no-deps

# Or using make
make clean && make build
```

### 3. Test

```bash
python test_bindings.py
```

### 4. Run examples

```bash
python examples/basic_tracing.py
```

## Building for Distribution

### Create a source distribution

```bash
python setup.py sdist
```

### Create a wheel (binary distribution)

```bash
pip install wheel
python setup.py bdist_wheel
```

### Build both

```bash
python setup.py sdist bdist_wheel
```

The built packages will be in the `dist/` directory.

## Docker Build (Optional)

If you want to build in a clean environment:

```dockerfile
FROM python:3.10

RUN apt-get update && apt-get install -y \
    cmake \
    build-essential \
    libcurl4-openssl-dev \
    git

# Install OpenTelemetry C++
RUN git clone --recurse-submodules https://github.com/open-telemetry/opentelemetry-cpp.git && \
    cd opentelemetry-cpp && \
    mkdir build && cd build && \
    cmake -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
          -DWITH_OTLP_HTTP=ON \
          -DBUILD_TESTING=OFF \
          .. && \
    make -j4 && \
    make install && \
    ldconfig

# Install pybind11
RUN pip install pybind11

# Copy and build your project
COPY . /app
WORKDIR /app
RUN pip install -e .

CMD ["python", "examples/basic_tracing.py"]
```

## Platform-Specific Notes

### macOS

- Requires Xcode Command Line Tools
- OpenTelemetry C++ can be installed via Homebrew
- May need to set `MACOSX_DEPLOYMENT_TARGET`

### Linux

- Requires build-essential package
- Remember to run `ldconfig` after installing OpenTelemetry C++
- May need to install libcurl development headers

### Windows

Not officially supported yet, but should work with:
- Visual Studio 2019 or later
- CMake 3.15+
- vcpkg for OpenTelemetry C++

## Clean Build

To start fresh:

```bash
make clean
# or
rm -rf build/ dist/ *.egg-info/ *.so
pip install -e .
```

## Getting Help

If you encounter issues:

1. Check that all prerequisites are installed
2. Verify OpenTelemetry C++ is properly installed: `pkg-config --exists opentelemetry-cpp`
3. Try a verbose build: `pip install -v -e .`
4. Check the CMake output for errors
5. Open an issue with the full error output
