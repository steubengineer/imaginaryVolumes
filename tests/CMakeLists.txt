# First-party tests (ADR-0002). Built with the strict warning set and linked
# against Catch2's built-in main (from the vendored amalgamated .cpp).
add_executable(iv_tests
    test_error.cpp
    test_assert.cpp
    test_volume.cpp
    test_orbit_camera.cpp
    test_plot_axes.cpp
    test_transfer.cpp
    test_vk_context.cpp
    test_vk_offscreen.cpp
    test_vk_volume.cpp
    test_vk_renderer.cpp
    test_concurrency.cpp)
target_link_libraries(iv_tests PRIVATE iv iv_warnings catch2_amalgamated)

# Text shaping tests (ADR-0022), only when text is built. The rest of the suite
# stays buildable with IV_BUILD_TEXT=OFF (the text-free isolation gate).
if(IV_BUILD_TEXT)
    target_sources(iv_tests PRIVATE test_text_shaper.cpp test_annotations.cpp test_legend.cpp
                                    test_glyph_substrate.cpp test_math_parse.cpp
                                    test_math_layout.cpp test_plot.cpp)
    target_link_libraries(iv_tests PRIVATE iv_text)
endif()

add_test(NAME iv_tests COMMAND iv_tests)
# The Vulkan loader + validation layer make process-exit allocations that never
# free and do not symbolize (not our objects; ADR-0005's validation layer is our
# authoritative Vulkan-object-leak gate). Disable LSan for the full run; ASan
# (use-after-free, overflow) and UBSan stay fully active. See DECISIONS D-0015.
set_tests_properties(iv_tests PROPERTIES ENVIRONMENT "ASAN_OPTIONS=detect_leaks=0")

# Keep LSan coverage on our OWN code by leak-checking the non-Vulkan suite.
add_test(NAME iv_tests_leakcheck COMMAND iv_tests "~[vk]")
set_tests_properties(iv_tests_leakcheck PROPERTIES ENVIRONMENT "ASAN_OPTIONS=detect_leaks=1")
