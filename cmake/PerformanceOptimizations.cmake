# Performance Optimization CMake Configuration
# This file configures CMake for QGIS performance optimizations

# Performance optimization flags
if(CMAKE_BUILD_TYPE STREQUAL "Release" OR CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
    message(STATUS "Enabling performance optimizations for build type: ${CMAKE_BUILD_TYPE}")
    
    # Enable O3 optimizations for performance-critical code
    set(PERFORMANCE_OPTIMIZATION_FLAGS "-O3 -DNDEBUG")
    
    # Enable link-time optimization if supported
    include(CheckIPOSupported)
    check_ipo_supported(RESULT ipo_supported OUTPUT ipo_error)
    if(ipo_supported)
        message(STATUS "Enabling link-time optimization for performance")
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
    else()
        message(STATUS "Link-time optimization not supported: ${ipo_error}")
    endif()
    
    # Enable fast math for floating point operations (use carefully)
    if(NOT APPLE)  # Avoid on macOS due to potential precision issues
        set(PERFORMANCE_OPTIMIZATION_FLAGS "${PERFORMANCE_OPTIMIZATION_FLAGS} -ffast-math")
    endif()
endif()

# Performance testing configuration
option(WITH_PERFORMANCE_TESTS "Build performance tests" ON)
option(WITH_BENCHMARKS "Build benchmark tools" ON)

# Performance monitoring configuration
option(WITH_PERFORMANCE_MONITORING "Enable performance monitoring" ON)
if(WITH_PERFORMANCE_MONITORING)
    add_definitions(-DWITH_PERFORMANCE_MONITORING)
endif()

# Benchmark data configuration
set(BENCHMARK_DATA_DIR "${CMAKE_BINARY_DIR}/test_data" CACHE PATH "Directory for benchmark test data")

# Create benchmark data directory
file(MAKE_DIRECTORY ${BENCHMARK_DATA_DIR})

# Performance test targets
if(WITH_PERFORMANCE_TESTS)
    # Enable testing
    enable_testing()
    
    # Add performance test target
    add_custom_target(performance_tests
        COMMAND ${CMAKE_CTEST_COMMAND} -L "performance"
        COMMENT "Running performance tests"
        USES_TERMINAL
    )
    
    # Add benchmark target
    add_custom_target(benchmarks
        COMMAND ${CMAKE_CTEST_COMMAND} -L "benchmark"
        COMMENT "Running benchmarks"
        USES_TERMINAL
    )
endif()

# Performance optimization library
if(WITH_PERFORMANCE_MONITORING)
    # Add include directory for performance headers
    include_directories(${CMAKE_SOURCE_DIR}/src/core/performance)
endif()

message(STATUS "Performance optimization configuration:")
message(STATUS "  - Performance tests: ${WITH_PERFORMANCE_TESTS}")
message(STATUS "  - Benchmarks: ${WITH_BENCHMARKS}")
message(STATUS "  - Performance monitoring: ${WITH_PERFORMANCE_MONITORING}")
message(STATUS "  - Benchmark data directory: ${BENCHMARK_DATA_DIR}")