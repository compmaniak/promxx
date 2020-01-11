cmake_minimum_required(VERSION 3.2)

project(promxx LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(include)

add_library(promxx STATIC src/registry.cpp)

add_executable(registry_test src/registry_test.cpp)
target_link_libraries(registry_test promxx)

enable_testing()

add_test(NAME registry COMMAND registry_test)