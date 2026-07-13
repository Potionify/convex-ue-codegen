#pragma once

// The parsed apiSpec model: the subset of each FunctionSpec the generator
// needs, already canonicalized and filtered. Parsing accepts either the raw
// value array or the {"status","value"} envelope returned by /api/query.

#include <convex_codegen/validator.h>

#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace convex_codegen {

/// Thrown when the apiSpec text cannot be parsed (malformed JSON, or a
/// success envelope reporting an error status).
class codegen_error : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

enum class function_type { query, mutation, action };

/// One deployed Convex function, canonicalized and ready to emit.
struct function_spec {
    std::string original_identifier;   ///< "counters.js:get"
    std::string canonical_identifier;  ///< "counters:get" (used as the call path)
    std::string module_path;           ///< "counters", "admin/tools"
    std::string function_name;         ///< "get"
    function_type type = function_type::query;
    bool internal = false;
    validator_ptr args;     ///< never null (missing -> `any`)
    validator_ptr returns;  ///< never null (missing -> `any`)
};

/// Parse an apiSpec document. HttpAction entries, `_system/*` modules, and
/// (unless `include_internal`) internal-visibility functions are dropped.
/// The result is sorted by canonical identifier for deterministic emission.
std::vector<function_spec> parse_api_spec(std::string_view json_text, bool include_internal);

}  // namespace convex_codegen
