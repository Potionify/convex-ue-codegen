#pragma once

// The public entry point of the pure emission core: apiSpec text in, a map of
// filename -> file content out. No networking, no filesystem — the same core
// compiles to WASM for a browser version. The CLI does the I/O around it.

#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace convex_codegen {

struct emit_options {
    /// File/name prefix for the generated API (default "ConvexApi").
    std::string prefix = "ConvexApi";

    /// Include internal-visibility functions when true.
    bool include_internal = false;

    /// Provenance comment line. When set, it is emitted verbatim as the second
    /// line of every file (tests pass a fixed value for determinism). When
    /// unset, the second line is "// Source: <source_label>".
    std::optional<std::string> stamp;

    /// Human description of where the spec came from (deployment URL or file
    /// path); used to build the default "// Source: ..." provenance line.
    std::string source_label = "unknown";

    /// When set, additionally emit "<name>.Build.cs" and "<name>Module.cpp" so
    /// the output folder drops into a UE project as a complete module.
    std::optional<std::string> emit_module;
};

/// Emit every generated file. Keyed by filename, values are full file
/// contents. Deterministic: identical (spec, options) -> identical output.
/// Throws codegen_error on malformed input.
std::map<std::string, std::string> emit_all(std::string_view api_spec_json,
                                            const emit_options& options);

/// The number of functions the spec contributes under `options` (for the CLI
/// summary line). Parses the spec the same way emit_all does.
std::size_t count_functions(std::string_view api_spec_json, const emit_options& options);

}  // namespace convex_codegen
