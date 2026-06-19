# Catch2 v3.7.1, vendored amalgamated distribution (ADR-0002).
# Third-party code: NOT subject to our strict warning set / -Werror. Its headers
# are exposed as SYSTEM includes so they don't trip first-party warnings.
add_library(catch2_amalgamated STATIC catch_amalgamated.cpp)
target_include_directories(catch2_amalgamated SYSTEM PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(catch2_amalgamated PUBLIC cxx_std_17)
