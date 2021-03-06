cmake_minimum_required(VERSION 2.6)
project(InlinedHashTable)
find_package(GTest REQUIRED)
include_directories(${GTEST_INCLUDE_DIRS})
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -std=c++11")
# set(CMAKE_CXX_COMPILER "/opt/clang+llvm-5.0.1-x86_64-linux-gnu-ubuntu-16.04/bin/clang++")
include(ExternalProject)

ExternalProject_Add (
 project_benchmark
 PREFIX ${CMAKE_CURRENT_BINARY_DIR}/benchmark
 GIT_REPOSITORY "https://github.com/google/benchmark.git"
 GIT_TAG "7a74b74856bae690a0998c967c7807dd2272af82"
 UPDATE_COMMAND ""
 INSTALL_COMMAND "")

ExternalProject_Get_Property(project_benchmark install_dir)

add_library(benchmark STATIC IMPORTED)
set_property(
  TARGET benchmark
  PROPERTY IMPORTED_LOCATION
  benchmark/src/project_benchmark-build/src/libbenchmark.a)
add_dependencies(benchmark project_benchmark)

include_directories(benchmark/src/project_benchmark/include)
add_executable(lp_cockoo_hash_test lp_cockoo_hash_test.cc)

target_link_libraries(
  lp_cockoo_hash_test
  benchmark ${GTEST_LIBRARIES} pthread)
