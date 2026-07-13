#include <convex_codegen/type_map.h>

namespace convex_codegen {

namespace {

mapped_type scalar(arg_class cls, std::string by_value, std::string param) {
    mapped_type m;
    m.cls = cls;
    m.by_value = std::move(by_value);
    m.param = std::move(param);
    return m;
}

/// The array typing for a scalar element class, or nullptr-equivalent when the
/// element is not one of the four scalar TArray element types.
bool array_of_scalar(arg_class element, mapped_type& out) {
    switch (element) {
        case arg_class::str:
            out = scalar(arg_class::arr_str, "TArray<FString>", "const TArray<FString>&");
            return true;
        case arg_class::flt:
            out = scalar(arg_class::arr_flt, "TArray<double>", "const TArray<double>&");
            return true;
        case arg_class::i64:
            out = scalar(arg_class::arr_i64, "TArray<int64>", "const TArray<int64>&");
            return true;
        case arg_class::boolean:
            out = scalar(arg_class::arr_bool, "TArray<bool>", "const TArray<bool>&");
            return true;
        default:
            return false;
    }
}

}  // namespace

mapped_type map_type(const validator& v) {
    switch (v.kind) {
        case validator_kind::string:
            return scalar(arg_class::str, "FString", "const FString&");
        case validator_kind::id: {
            mapped_type m = scalar(arg_class::str, "FString", "const FString&");
            m.id_table = v.id_table;
            return m;
        }
        case validator_kind::number:
            return scalar(arg_class::flt, "double", "double");
        case validator_kind::int64:
            return scalar(arg_class::i64, "int64", "int64");
        case validator_kind::boolean:
            return scalar(arg_class::boolean, "bool", "bool");
        case validator_kind::bytes:
            return scalar(arg_class::bytes, "TArray<uint8>", "const TArray<uint8>&");
        case validator_kind::literal:
            switch (v.literal_kind) {
                case validator::literal_base::string:
                    return scalar(arg_class::str, "FString", "const FString&");
                case validator::literal_base::number:
                    return scalar(arg_class::flt, "double", "double");
                case validator::literal_base::boolean:
                    return scalar(arg_class::boolean, "bool", "bool");
                case validator::literal_base::other:
                    break;
            }
            return scalar(arg_class::convex, "FConvexValue", "const FConvexValue&");
        case validator_kind::array: {
            const mapped_type element =
                v.element ? map_type(*v.element) : scalar(arg_class::convex, "FConvexValue", "const FConvexValue&");
            mapped_type arr;
            if (array_of_scalar(element.cls, arr)) return arr;
            return scalar(arg_class::arr_convex, "TArray<FConvexValue>",
                          "const TArray<FConvexValue>&");
        }
        case validator_kind::any:
        case validator_kind::null:
        case validator_kind::record:
        case validator_kind::object:
        case validator_kind::union_:
            return scalar(arg_class::convex, "FConvexValue", "const FConvexValue&");
    }
    return scalar(arg_class::convex, "FConvexValue", "const FConvexValue&");
}

mapped_type map_type(const validator_ptr& v) {
    if (!v) return scalar(arg_class::convex, "FConvexValue", "const FConvexValue&");
    return map_type(*v);
}

}  // namespace convex_codegen
