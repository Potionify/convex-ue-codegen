# Argument parsing and env resolution are compiled into a small support library
# (no networking, no side effects) so the tests can exercise them directly. The
# executable adds the I/O: HTTP fetch via convex-cpp, filesystem writes.

add_library(convex_ue_codegen_cli_support STATIC
    cli_support.cpp
)
add_library(convex_ue_codegen::cli_support ALIAS convex_ue_codegen_cli_support)

target_compile_features(convex_ue_codegen_cli_support PUBLIC cxx_std_20)
set_target_properties(convex_ue_codegen_cli_support PROPERTIES CXX_EXTENSIONS OFF)
target_include_directories(convex_ue_codegen_cli_support
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)
target_link_libraries(convex_ue_codegen_cli_support PUBLIC convex_ue_codegen::core)

if(MSVC)
    target_compile_options(convex_ue_codegen_cli_support PRIVATE /W4 /utf-8 /permissive-)
else()
    target_compile_options(convex_ue_codegen_cli_support PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(convex-ue-codegen main.cpp)
target_link_libraries(convex-ue-codegen
    PRIVATE
        convex_ue_codegen::core
        convex_ue_codegen::cli_support
        convex::convex
        convex::ixwebsocket
)
if(MSVC)
    target_compile_options(convex-ue-codegen PRIVATE /utf-8)
endif()
