cmake_minimum_required(VERSION 3.21)
project(convex_ue_codegen VERSION 0.1.0 LANGUAGES CXX)

if(PROJECT_IS_TOP_LEVEL)
    set(CODEGEN_DEV_DEFAULT ON)
else()
    set(CODEGEN_DEV_DEFAULT OFF)
endif()

option(CONVEX_UE_CODEGEN_BUILD_TESTS "Build convex-ue-codegen tests" ${CODEGEN_DEV_DEFAULT})

# --- convex-cpp dependency ------------------------------------------------
set(CONVEX_CPP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../convex-cpp" CACHE PATH
    "Path to the convex-cpp source tree")
if(NOT EXISTS "${CONVEX_CPP_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "convex-cpp not found at CONVEX_CPP_DIR='${CONVEX_CPP_DIR}'.\n"
        "Clone https://github.com/Potionify/convex-cpp next to this repo, or pass "
        "-DCONVEX_CPP_DIR=<path> pointing at its source tree.")
endif()

# Build convex-cpp with the bundled desktop HTTP/WebSocket transport, and
# without its own tests (we fetch GoogleTest ourselves below).
set(CONVEX_WITH_IXWEBSOCKET ON CACHE BOOL "" FORCE)
set(CONVEX_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory("${CONVEX_CPP_DIR}" convex-cpp-build)

# --- our targets ----------------------------------------------------------
add_subdirectory(core)
add_subdirectory(cli)

if(CONVEX_UE_CODEGEN_BUILD_TESTS)
    enable_testing()
    include(FetchContent)
    FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.zip
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    # Match the parent project's CRT on Windows.
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
    add_subdirectory(tests)
endif()
