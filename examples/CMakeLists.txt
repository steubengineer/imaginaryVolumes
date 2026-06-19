# Example programs (not part of libiv; not on the test gate). Built with the
# strict warning set so demo code stays clean too.
add_executable(iv_render_demo render_demo.cpp)
target_link_libraries(iv_render_demo PRIVATE iv iv_warnings)
