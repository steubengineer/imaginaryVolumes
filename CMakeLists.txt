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
iv_embed_shader(${CMAKE_CURRENT_SOURCE_DIR}/shaders/overlay.vert overlay_vert)
iv_embed_shader(${CMAKE_CURRENT_SOURCE_DIR}/shaders/overlay.frag overlay_frag)

# --- Text shaping (ADR-0022): vendored HarfBuzz + iv::text::Shaper, gated. ---
# Text lives in the annotation layer, not core `iv`: with IV_BUILD_TEXT=OFF the
# core library, tests, and tools build with NO HarfBuzz present (the text-free
# isolation gate, mirroring IV_BUILD_VIEWER). Default ON.
option(IV_BUILD_TEXT "Build text shaping (vendored HarfBuzz) and the bundled font" ON)
if(IV_BUILD_TEXT)
    add_subdirectory(third_party/harfbuzz)

    # Embed the bundled default face (third_party/fonts) as a byte array so the
    # default font needs no runtime file path (ADR-0022; same pattern as shaders).
    set(IV_FONT_SRC ${CMAKE_CURRENT_SOURCE_DIR}/third_party/fonts/NewCM10-Book.otf)
    set(IV_FONT_GEN ${CMAKE_CURRENT_BINARY_DIR}/ncm_book_otf.cpp)
    add_custom_command(
        OUTPUT ${IV_FONT_GEN}
        COMMAND ${CMAKE_COMMAND} -DIN=${IV_FONT_SRC} -DSYMBOL=ncm_book_otf
                -DNS=iv::text -DHDR=iv/text/bundled_font.hpp -DOUT=${IV_FONT_GEN}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tools/embed_bytes.cmake
        DEPENDS ${IV_FONT_SRC} ${CMAKE_CURRENT_SOURCE_DIR}/tools/embed_bytes.cmake
        COMMENT "embed NewCM10-Book.otf"
        VERBATIM)

    add_library(iv_text STATIC src/text/shaper.cpp src/text/bundled_font.cpp ${IV_FONT_GEN})
    add_library(iv::text ALIAS iv_text)
    # iv is PUBLIC: shaper.hpp includes iv/error.hpp. harfbuzz_core is PRIVATE:
    # HarfBuzz is hidden behind the Shaper (ADR-0004), only shaper.cpp needs it.
    target_link_libraries(iv_text PUBLIC iv)
    target_link_libraries(iv_text PRIVATE harfbuzz_core iv_warnings)
endif()

# --- Tests ---
include(CTest)
if(BUILD_TESTING)
    add_subdirectory(third_party/catch2)
    add_subdirectory(tests)
endif()

# --- Example programs (off the test gate; demonstrate the public API) ---
option(IV_BUILD_EXAMPLES "Build example programs" ON)
if(IV_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# --- Performance benchmark (ADR-0019): headless, no GLFW; opt-in to RUN (not on
# the default ctest gate) but always built so the perf contract has a target. ---
add_executable(iv_bench tools/bench.cpp)
target_link_libraries(iv_bench PRIVATE iv iv_warnings)

# --- Interactive viewer (ADR-0016): GLFW-coupled, isolated in `iv_viewer`. The
# core `iv`, tests, and iv_bench never link GLFW; the viewer is gated by
# IV_BUILD_VIEWER (default ON only when glfw3 is found). ---
find_package(glfw3 QUIET)
if(glfw3_FOUND)
    message(STATUS "glfw3 found (${glfw3_VERSION}): viewer available")
else()
    message(STATUS "glfw3 not found: viewer will not be built")
endif()
option(IV_BUILD_VIEWER "Build the GLFW interactive viewer (requires glfw3)" ${glfw3_FOUND})
if(IV_BUILD_VIEWER)
    if(NOT glfw3_FOUND)
        message(FATAL_ERROR
            "IV_BUILD_VIEWER=ON but glfw3 was not found by find_package(glfw3). "
            "Install libglfw3-dev or configure with -DIV_BUILD_VIEWER=OFF.")
    endif()
    # GLFW links PUBLIC (link-only: the public header forward-declares GLFWwindow
    # and never includes <GLFW/glfw3.h>), so iv_view inherits the link.
    add_library(iv_viewer STATIC src/vk/viewer.cpp)
    add_library(iv::viewer ALIAS iv_viewer)
    target_link_libraries(iv_viewer PUBLIC iv glfw)
    target_link_libraries(iv_viewer PRIVATE iv_warnings)

    add_executable(iv_view examples/view_demo.cpp)
    target_link_libraries(iv_view PRIVATE iv_viewer iv_warnings)
endif()
