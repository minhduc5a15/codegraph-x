cmake_minimum_required(VERSION 3.26)
project(codegraph-x LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Optimization flags for performance benchmarking
if (MSVC)
    add_compile_options(/O2 /arch:AVX2)
else ()
    add_compile_options(-O3 -march=native)
endif ()

# Thread support
find_package(Threads REQUIRED)

# --- Vendored Tree-Sitter ---

# Tree-Sitter core (Amalgamated build)
add_library(tree-sitter STATIC vendor/tree-sitter/lib/src/lib.c)
target_include_directories(tree-sitter PUBLIC vendor/tree-sitter/lib/include)
target_include_directories(tree-sitter PRIVATE vendor/tree-sitter/lib/src)

# Tree-Sitter CPP grammar
add_library(tree-sitter-cpp STATIC 
    vendor/tree-sitter-cpp/src/parser.c
    vendor/tree-sitter-cpp/src/scanner.c
)
target_include_directories(tree-sitter-cpp PUBLIC vendor/tree-sitter-cpp/src)
target_link_libraries(tree-sitter-cpp PRIVATE tree-sitter)

# --- Targets ---

# Core executable
add_executable(codegraph-x 
    src/main.cpp 
    src/InMemoryGraphEngine.cpp 
    src/parser.cpp
    src/parallel_parser.cpp
)
target_include_directories(codegraph-x PRIVATE src)
target_link_libraries(codegraph-x PRIVATE tree-sitter tree-sitter-cpp Threads::Threads)

# Benchmark executable
find_package(benchmark REQUIRED)
add_executable(graph_engine_benchmark 
    tests/graph_engine_benchmark.cpp 
    src/InMemoryGraphEngine.cpp
)
target_include_directories(graph_engine_benchmark PRIVATE src)
target_link_libraries(graph_engine_benchmark PRIVATE benchmark::benchmark Threads::Threads)
