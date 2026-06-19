# HarfBuzz, vendored at a pinned commit (ADR-0022; see VENDORING.md for the pin).
# Third-party code: NOT subject to the first-party strict warning set / -Werror;
# its headers are exposed as SYSTEM includes so they don't trip our warnings.
#
# Minimal build (ADR-0022): compile only the amalgamated core translation unit
# src/harfbuzz.cc. With no HAVE_* configuration defines HarfBuzz uses its own
# built-in OpenType + Unicode (UCD) implementations — no ICU / GLib / Cairo /
# FreeType. (libharfbuzz-gpu — hb-gpu*.cc — is wired in ADR-0023.)

add_library(harfbuzz_core STATIC src/harfbuzz.cc)
add_library(harfbuzz::core ALIAS harfbuzz_core)

target_include_directories(harfbuzz_core SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

# HarfBuzz uses atomic refcounts (threads) and a little libm. Link both so
# consumers (iv_text) inherit them.
find_package(Threads REQUIRED)
target_link_libraries(harfbuzz_core PUBLIC Threads::Threads)
if(NOT WIN32)
    target_link_libraries(harfbuzz_core PUBLIC m)
endif()

# libharfbuzz-gpu (ADR-0023): the experimental Slug glyph-outline encoder. Its
# hb-static.cc is already in the core amalgamation, so it is NOT recompiled here;
# the GPU lib links the core for it. The .glsl shaders ship with these sources and
# are compiled separately by our glslc toolchain (not here).
add_library(harfbuzz_gpu STATIC
    src/hb-gpu.cc
    src/hb-gpu-draw.cc
    src/hb-gpu-paint.cc)
add_library(harfbuzz::gpu ALIAS harfbuzz_gpu)
target_include_directories(harfbuzz_gpu SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(harfbuzz_gpu PUBLIC harfbuzz_core)
