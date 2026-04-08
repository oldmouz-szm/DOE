cmake_minimum_required(VERSION 3.16)
project(doe_maxsat_repro CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(doe_maxsat src/doe_maxsat.cpp)

target_compile_options(doe_maxsat PRIVATE -O3 -DNDEBUG -Wall -Wextra -Wpedantic)
