# The pure emission core: apiSpec text in, filename -> content map out. No
# networking, no filesystem. nlohmann/json is a private, implementation-only
# dependency (no public header includes it), keeping the core WASM-friendly.

add_library(convex_ue_codegen_core STATIC
    src/naming.cpp
    src/validator.cpp
    src/type_map.cpp
    src/api_spec.cpp
    src/emit.cpp
)
add_library(convex_ue_codegen::core ALIAS convex_ue_codegen_core)

target_compile_features(convex_ue_codegen_core PUBLIC cxx_std_20)
set_target_properties(convex_ue_codegen_core PROPERTIES CXX_EXTENSIONS OFF)

target_include_directories(convex_ue_codegen_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    PRIVATE
        ${CMAKE_SOURCE_DIR}/third_party
)

if(MSVC)
    target_compile_options(convex_ue_codegen_core PRIVATE /W4 /utf-8 /permissive-)
else()
    target_compile_options(convex_ue_codegen_core PRIVATE -Wall -Wextra -Wpedantic)
endif()
