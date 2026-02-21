cmake_minimum_required(VERSION 3.16)
project(NYC311Analysis VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Release build flags (default to Release if not set)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native -DNDEBUG")
set(CMAKE_CXX_FLAGS_DEBUG   "-g -O0 -Wall -Wextra -Wpedantic")

# Common include directory shared by all phases
include_directories(${PROJECT_SOURCE_DIR}/include)

# OpenMP (required for Phase 2 and Phase 3)
find_package(OpenMP)

add_subdirectory(src/phase1)
add_subdirectory(src/phase2)
add_subdirectory(src/phase3)
