add_executable(convex_ue_codegen_tests
    naming_test.cpp
    validator_test.cpp
    api_spec_test.cpp
    env_resolve_test.cpp
    emit_test.cpp
    golden_test.cpp
    live_test.cpp
)

target_link_libraries(convex_ue_codegen_tests
    PRIVATE
        convex_ue_codegen::core
        convex_ue_codegen::cli_support
        convex::convex
        convex::ixwebsocket
        GTest::gtest_main
)

# Paths the tests need, injected as string-literal macros. CMAKE_* paths use
# forward slashes on Windows too, so they are safe inside C string literals.
target_compile_definitions(convex_ue_codegen_tests PRIVATE
    FIXTURE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/apispec_full.json"
    EXPECTED_DIR="${CMAKE_CURRENT_SOURCE_DIR}/expected"
    LOCAL_ENV_PATH="${CONVEX_CPP_DIR}/integration/local.env"
)

if(MSVC)
    target_compile_options(convex_ue_codegen_tests PRIVATE /utf-8)
endif()

include(GoogleTest)
gtest_discover_tests(convex_ue_codegen_tests)
