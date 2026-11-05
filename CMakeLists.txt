cmake_minimum_required(VERSION 3.20)
project(inertia LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Emscripten turns the same C++ into WebAssembly. Its toolchain sets EMSCRIPTEN.
if(EMSCRIPTEN)
  add_link_options(-sALLOW_MEMORY_GROWTH=1 -sSTACK_SIZE=1048576)
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

# Scenes only touch the physics core, so they are tested headless too.
add_library(scenes STATIC src/scenes/scenes.cpp)
target_link_libraries(scenes PUBLIC physics)
target_compile_options(scenes PRIVATE -Wall -Wextra)

# raylib is fetched into the build folder, so nothing is installed on the system.
include(FetchContent)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_GAMES OFF CACHE BOOL "" FORCE)
if(EMSCRIPTEN)
  # WebGL 2 is OpenGL ES 3 under another name.
  set(PLATFORM Web CACHE STRING "" FORCE)
  set(GRAPHICS GRAPHICS_API_OPENGL_ES3 CACHE STRING "" FORCE)
else()
  set(GRAPHICS GRAPHICS_API_OPENGL_33 CACHE STRING "" FORCE)
endif()
FetchContent_Declare(raylib
  URL https://github.com/raysan5/raylib/archive/refs/tags/5.5.tar.gz
  URL_HASH SHA256=aea98ecf5bc5c5e0b789a76de0083a21a70457050ea4cc2aec7566935f5e258e
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(raylib)

add_executable(inertia
  src/main.cpp
  src/app.cpp
  src/render/canvas.cpp
  src/render/effects.cpp
  src/render/painter.cpp
  src/render/pipeline.cpp
  src/ui/ui.cpp
)
target_link_libraries(inertia PRIVATE scenes raylib)
target_compile_options(inertia PRIVATE -Wall -Wextra)

if(EMSCRIPTEN)
  # site/ is the whole website: index.html, its .js and .wasm, and the fonts in a .data file.
  set_target_properties(inertia PROPERTIES
    OUTPUT_NAME index
    SUFFIX .html
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/site
    LINK_DEPENDS ${CMAKE_SOURCE_DIR}/web/shell.html
  )
  target_link_options(inertia PRIVATE
    -sMIN_WEBGL_VERSION=2
    -sMAX_WEBGL_VERSION=2
    -sENVIRONMENT=web
    "SHELL:--shell-file ${CMAKE_SOURCE_DIR}/web/shell.html"
    "SHELL:--preload-file ${CMAKE_SOURCE_DIR}/assets@/assets"
  )
else()
  # Fonts sit next to the binary, so it runs from any working directory.
  add_custom_command(TARGET inertia POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/assets $<TARGET_FILE_DIR:inertia>/assets
  )
endif()

enable_testing()
add_executable(physics_tests tests/physics_tests.cpp)
target_link_libraries(physics_tests PRIVATE physics)
add_test(NAME physics COMMAND physics_tests)
add_executable(scene_tests tests/scene_tests.cpp)
target_link_libraries(scene_tests PRIVATE scenes)
add_test(NAME scenes COMMAND scene_tests)
if(EMSCRIPTEN)
  # Node runs these, and needs the runtime to shut down to report their exit code.
  target_link_options(physics_tests PRIVATE -sEXIT_RUNTIME=1)
  target_link_options(scene_tests PRIVATE -sEXIT_RUNTIME=1)
endif()
