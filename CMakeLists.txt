cmake_minimum_required(VERSION 3.25)

project(imaginaryVolumes
    VERSION 0.1.0
    DESCRIPTION "Volumetric 3D plotting of complex numbers (Vulkan)"
    LANGUAGES CXX)

# --- Standard: strict ISO C++23 (ADR-0001) ---
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Default to Debug for single-config generators.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo)
endif()

# --- Vulkan dependency (ADR-0001 pre-authorized; used from M2 / ADR-0004) ---
# Imported target Vulkan::Vulkan carries the loader + headers; its include dirs
# are SYSTEM by default, so vulkan.hpp's own diagnostics don't trip -Werror.
find_package(Vulkan REQUIRED)

# --- Compiler contract: system GCC >= 13 (ADR-0001) ---
if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    message(FATAL_ERROR
        "ADR-0001: this project requires the system GCC. Found: ${CMAKE_CXX_COMPILER_ID}")
endif()
if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS 13)
    message(FATAL_ERROR
        "ADR-0001: GCC >= 13 required for C++23. Found: ${CMAKE_CXX_COMPILER_VERSION}")
endif()

# --- Mandatory warning set, warnings-as-errors (ADR-0001) ---
# Applied to FIRST-PARTY targets only (linked PRIVATE); never imposed on
# third-party code or library consumers.
add_library(iv_warnings INTERFACE)
target_compile_options(iv_warnings INTERFACE
    -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion
    -Wnon-virtual-dtor -Wcast-align -Wunused -Woverloaded-virtual
    -Wdouble-promotion -Wformat=2 -Werror)

# --- Sanitizer selection (ADR-0001) ---
# Applied tree-wide (all targets, incl. vendored deps) so instrumentation is
# uniform. Empty by default => no effect. Examples:
#   -D IV_SANITIZE=address,undefined   (the test gate)
#   -D IV_SANITIZE=thread              (for threaded code, later milestones)
set(IV_SANITIZE "" CACHE STRING "Sanitizers, e.g. 'address,undefined' or 'thread'")
if(IV_SANITIZE)
    message(STATUS "Sanitizers enabled: ${IV_SANITIZE}")
    add_compile_options(-fsanitize=${IV_SANITIZE} -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=${IV_SANITIZE})
endif()

# --- Library ---
add_library(iv
    src/error.cpp
    src/assert.cpp
    src/volume.cpp
    src/vk/result.cpp
    src/vk/memory.cpp
    src/vk/context.cpp
    src/vk/offscreen.cpp
    src/vk/volume.cpp
    src/vk/shaders.cpp
    src/vk/renderer.cpp)
add_library(iv::iv ALIAS iv)
target_include_directories(iv PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
# Vulkan is PUBLIC: our public headers include <vulkan/vulkan.hpp> via
# iv/vk/vulkan.hpp, so consumers need its include path and the loader.
target_link_libraries(iv PUBLIC Vulkan::Vulkan)
target_link_libraries(iv PRIVATE iv_warnings)

# --- Shaders: glslc -> SPIR-V -> embedded byte array (ADR-0011, D-0022) ---
# glslc (Vulkan SDK) is a REQUIRED build tool; this extends ADR-0001's dependency
# policy. It is a build tool, not a linked/runtime dependency.
find_program(IV_GLSLC glslc)
if(NOT IV_GLSLC)
    message(FATAL_ERROR
        "ADR-0011: glslc (from the Vulkan SDK) is required to compile shaders but "
        "was not found on PATH. Install the Vulkan SDK or add glslc to PATH.")
endif()
message(STATUS "Shader compiler (glslc): ${IV_GLSLC}")

set(IV_EMBED_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/tools/embed_spirv.cmake)

# Compile a GLSL shader to SPIR-V and embed it as a generated .cpp linked into iv.
function(iv_embed_shader SRC SYMBOL)
    set(spv ${CMAKE_CURRENT_BINARY_DIR}/${SYMBOL}.spv)
    set(gen ${CMAKE_CURRENT_BINARY_DIR}/${SYMBOL}.spv.cpp)
    add_custom_command(
        OUTPUT ${spv}
        COMMAND ${IV_GLSLC} -O --target-env=vulkan1.3 -o ${spv} ${SRC}
        DEPENDS ${SRC}
        COMMENT "glslc ${SYMBOL}"
        VERBATIM)
    add_custom_command(
        OUTPUT ${gen}
        COMMAND ${CMAKE_COMMAND} -DSPV=${spv} -DSYMBOL=${SYMBOL} -DOUT=${gen}
                -P ${IV_EMBED_SCRIPT}
        DEPENDS ${spv} ${IV_EMBED_SCRIPT}
        COMMENT "embed ${SYMBOL}"
        VERBATIM)
    target_sources(iv PRIVATE ${gen})
endfunction()

iv_embed_shader(${CMAKE_CURRENT_SOURCE_DIR}/shaders/ray_march.comp ray_march_comp)

# --- Tests ---
include(CTest)
if(BUILD_TESTING)
    add_subdirectory(third_party/catch2)
    add_subdirectory(tests)
endif()
