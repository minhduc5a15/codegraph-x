cmake_minimum_required(VERSION 3.26)
project(codegraph-x LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Optimization flags for performance benchmarking
if (MSVC)
    add_compile_options(/O2 /arch:AVX2)
else ()
    add_compile_options(-O3 -march=native)
endif ()

# Core executable
add_executable(codegraph-x src/main.cpp src/InMemoryGraphEngine.cpp)
target_include_directories(codegraph-x PRIVATE src)

# Benchmark executable
find_package(benchmark REQUIRED)
add_executable(graph_engine_benchmark tests/graph_engine_benchmark.cpp src/InMemoryGraphEngine.cpp)
target_include_directories(graph_engine_benchmark PRIVATE src)
target_link_libraries(graph_engine_benchmark PRIVATE benchmark::benchmark)
