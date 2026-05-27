cmake_minimum_required(VERSION 3.15)
project(honeycomb_pycpp VERSION 0.1.15 LANGUAGES CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Set position independent code
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Find pybind11 first - it will find Python for us
find_package(pybind11 CONFIG REQUIRED)

# Set Python variables for compatibility with the rest of the script
set(Python_EXECUTABLE ${PYTHON_EXECUTABLE})
set(Python_INCLUDE_DIRS ${PYTHON_INCLUDE_DIRS})
set(Python_VERSION ${PYTHON_VERSION_STRING})

option(WITH_OTLP_GRPC "Build with OTLP gRPC exporter support" ON)

# Find OpenTelemetry C++
find_package(opentelemetry-cpp CONFIG REQUIRED)

# Find required libraries
find_package(CURL REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(ZLIB REQUIRED)
find_package(OpenSSL REQUIRED)

# Include directories
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${Python_INCLUDE_DIRS}
)

# Source files
set(SOURCE_FILES
    src/tracer_wrapper.cpp
    src/meter_wrapper.cpp
    src/logger_wrapper.cpp
    src/sdk_wrapper.cpp
    src/bindings.cpp
)

# Create Python module
pybind11_add_module(${PROJECT_NAME}
    ${SOURCE_FILES}
)

# Link against OpenTelemetry C++ libraries
target_link_libraries(${PROJECT_NAME} PRIVATE
    opentelemetry-cpp::trace
    opentelemetry-cpp::common
    opentelemetry-cpp::configuration
    opentelemetry-cpp::otlp_builder_utils
    opentelemetry-cpp::otlp_http_exporter
    opentelemetry-cpp::otlp_http_exporter_builder
    $<$<BOOL:${WITH_OTLP_GRPC}>:opentelemetry-cpp::otlp_grpc_exporter>
    $<$<BOOL:${WITH_OTLP_GRPC}>:opentelemetry-cpp::otlp_grpc_exporter_builder>
    opentelemetry-cpp::ostream_span_exporter
    opentelemetry-cpp::ostream_span_exporter_builder
    opentelemetry-cpp::metrics
    opentelemetry-cpp::ostream_metrics_exporter
    opentelemetry-cpp::ostream_metrics_exporter_builder
    opentelemetry-cpp::otlp_http_metric_exporter
    opentelemetry-cpp::otlp_http_metric_exporter_builder
    $<$<BOOL:${WITH_OTLP_GRPC}>:opentelemetry-cpp::otlp_grpc_metrics_exporter>
    $<$<BOOL:${WITH_OTLP_GRPC}>:opentelemetry-cpp::otlp_grpc_metric_exporter_builder>
    opentelemetry-cpp::logs
    opentelemetry-cpp::ostream_log_record_exporter
    opentelemetry-cpp::ostream_log_record_exporter_builder
    opentelemetry-cpp::otlp_http_log_record_exporter
    opentelemetry-cpp::otlp_http_log_record_exporter_builder
    $<$<BOOL:${WITH_OTLP_GRPC}>:opentelemetry-cpp::otlp_grpc_log_record_exporter>
    $<$<BOOL:${WITH_OTLP_GRPC}>:opentelemetry-cpp::otlp_grpc_log_record_exporter_builder>
    opentelemetry-cpp::resources
    opentelemetry-cpp::version
    CURL::libcurl
    nlohmann_json::nlohmann_json
    ZLIB::ZLIB
    OpenSSL::SSL
    OpenSSL::Crypto
)

if(WITH_OTLP_GRPC)
    target_compile_definitions(${PROJECT_NAME} PRIVATE WITH_OTLP_GRPC)
endif()

# Set compile options
target_compile_options(${PROJECT_NAME} PRIVATE
    -Wall
    -Wextra
    $<$<CONFIG:Debug>:-g>
    $<$<CONFIG:Release>:-O3>
    -ffunction-sections
    -fdata-sections
)

target_link_options(${PROJECT_NAME} PRIVATE
    $<$<PLATFORM_ID:Linux>:-Wl,--gc-sections>
    $<$<PLATFORM_ID:Darwin>:-Wl,-dead_strip>
)

# Set module properties
set_target_properties(${PROJECT_NAME} PROPERTIES
    PREFIX "${PYTHON_MODULE_PREFIX}"
    SUFFIX "${PYTHON_MODULE_EXTENSION}"
    CXX_VISIBILITY_PRESET hidden
)

# Install target
install(TARGETS ${PROJECT_NAME}
    LIBRARY DESTINATION ${CMAKE_INSTALL_PREFIX}
    RUNTIME DESTINATION ${CMAKE_INSTALL_PREFIX}
)

# Print configuration summary
message(STATUS "")
message(STATUS "Configuration Summary:")
message(STATUS "  Project:           ${PROJECT_NAME} ${PROJECT_VERSION}")
message(STATUS "  Build Type:        ${CMAKE_BUILD_TYPE}")
message(STATUS "  C++ Standard:      ${CMAKE_CXX_STANDARD}")
message(STATUS "  Python Version:    ${Python_VERSION}")
message(STATUS "  Python Executable: ${Python_EXECUTABLE}")
message(STATUS "  pybind11 Version:  ${pybind11_VERSION}")
message(STATUS "  Install Prefix:    ${CMAKE_INSTALL_PREFIX}")
message(STATUS "")
