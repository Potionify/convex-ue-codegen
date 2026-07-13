#pragma once

// A parsed Convex validator ("ValidatorJSON") as an in-memory tree, plus a
// one-line human summary used in generated doc comments and tooltips. The
// parser lives in validator.cpp so nlohmann/json stays a private, impl-only
// dependency (this header pulls in no JSON library, keeping core WASM-friendly).

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace convex_codegen {

/// The validator kinds we distinguish. Unknown / unsupported "type" strings
/// (and null/missing validators) collapse to `any`, which always maps to the
/// pass-through FConvexValue case — the generator never crashes on new types.
enum class validator_kind {
    any,
    string,
    number,   ///< "number" or "float64"
    boolean,
    int64,    ///< "int64" or "bigint"
    null,
    bytes,
    id,
    literal,
    array,
    record,
    object,
    union_,
};

struct validator;
using validator_ptr = std::shared_ptr<const validator>;

/// One field of an object validator.
struct validator_field {
    std::string name;
    validator_ptr type;
    bool optional = false;
};

/// A validator tree node. Only the members relevant to `kind` are populated.
struct validator {
    validator_kind kind = validator_kind::any;

    /// Original "type" string as seen on the wire (e.g. "float64", "bigint"),
    /// used for a faithful summary. Empty for synthesized `any`.
    std::string raw_type;

    /// Table name for an `id` validator.
    std::string id_table;

    /// The base kind a `literal` reduces to, for type mapping.
    enum class literal_base { string, number, boolean, other };
    literal_base literal_kind = literal_base::other;
    /// Rendered literal value for the summary (e.g. "\"foo\"", "3", "true").
    std::string literal_repr;

    /// Element validator of an `array`.
    validator_ptr element;

    /// Member validators of a `union_`.
    std::vector<validator_ptr> members;

    /// Fields of an `object`.
    std::vector<validator_field> fields;

    /// Value validator of a `record` (keys are always strings on the wire).
    validator_ptr record_values;
};

/// Parse a ValidatorJSON document (given as text). A null/empty document, or
/// any unrecognized shape, yields an `any` validator rather than throwing.
validator_ptr parse_validator_json(std::string_view json_text);

/// One-line, deterministic summary of a validator for doc comments, e.g.
/// "object{ name: string, by?: number }" or "array<string>".
std::string summarize(const validator& v);
std::string summarize(const validator_ptr& v);

}  // namespace convex_codegen
