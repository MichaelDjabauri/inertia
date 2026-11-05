cmake_minimum_required(VERSION 3.20)
project(inertia LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# The physics core has no graphics dependency, so its tests run headless.
add_library(physics STATIC
  src/physics/shape.cpp
  src/physics/collision.cpp
  src/physics/joint.cpp
  src/physics/world.cpp
)
target_include_directories(physics PUBLIC src)
target_compile_options(physics PRIVATE -Wall -Wextra)

enable_testing()
add_executable(physics_tests tests/physics_tests.cpp)
target_link_libraries(physics_tests PRIVATE physics)
add_test(NAME physics COMMAND physics_tests)
