#pragma once

// Maps a Convex validator to the Unreal C++ / Blueprint parameter type and to
// the FConvexValue factory used to marshal an argument into the wire args map.

#include <convex_codegen/validator.h>

#include <string>

namespace convex_codegen {

/// How an argument is turned into an FConvexValue in a generated body. The
/// arr_* classes carry a scalar element type and are built by looping into a
/// TArray<FConvexValue>; arr_convex passes an already-FConvexValue array
/// straight to FConvexValue::Array.
enum class arg_class {
    convex,      ///< FConvexValue, added directly
    str,         ///< FConvexValue::String
    flt,         ///< FConvexValue::Float
    i64,         ///< FConvexValue::Int64
    boolean,     ///< FConvexValue::Bool
    bytes,       ///< FConvexValue::Bytes
    arr_str,     ///< TArray<FString>  -> Array of String
    arr_flt,     ///< TArray<double>   -> Array of Float
    arr_i64,     ///< TArray<int64>    -> Array of Int64
    arr_bool,    ///< TArray<bool>     -> Array of Bool
    arr_convex,  ///< TArray<FConvexValue> -> Array (elements already values)
};

/// The resolved Unreal typing for a single validator.
struct mapped_type {
    arg_class cls = arg_class::convex;
    std::string by_value;   ///< value form, e.g. "FString", "double", "TArray<FString>"
    std::string param;      ///< parameter form, e.g. "const FString&", "double"
    std::string id_table;   ///< non-empty when the source validator was an `id`
};

/// Map a validator to its Unreal typing (used for a required parameter and,
/// via `by_value`, for TOptional<> parameters).
mapped_type map_type(const validator& v);
mapped_type map_type(const validator_ptr& v);

}  // namespace convex_codegen
