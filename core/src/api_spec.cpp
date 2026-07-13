#include <convex_codegen/api_spec.h>

#include <convex_codegen/naming.h>

#include <nlohmann/json.hpp>

#include <algorithm>

namespace convex_codegen {

using json = nlohmann::json;

namespace {

validator_ptr parse_validator_field(const json& node) {
    // A missing or explicit-null args/returns validator means `any`.
    if (node.is_null()) return parse_validator_json({});
    return parse_validator_json(node.dump());
}

}  // namespace

std::vector<function_spec> parse_api_spec(std::string_view json_text, bool include_internal) {
    json doc = json::parse(json_text, nullptr, /*allow_exceptions=*/false);
    if (doc.is_discarded()) {
        throw codegen_error("apiSpec is not valid JSON");
    }

    // Accept either the raw value array or the {"status","value"} envelope.
    const json* value_array = nullptr;
    if (doc.is_array()) {
        value_array = &doc;
    } else if (doc.is_object() && doc.contains("value")) {
        const std::string status = doc.value("status", std::string("success"));
        if (status != "success") {
            std::string message = doc.value("errorMessage", std::string());
            throw codegen_error("apiSpec query returned status '" + status + "'" +
                                (message.empty() ? "" : ": " + message));
        }
        if (!doc["value"].is_array()) {
            throw codegen_error("apiSpec envelope 'value' is not an array");
        }
        value_array = &doc["value"];
    } else {
        throw codegen_error(
            "apiSpec must be a value array or a {\"status\",\"value\"} envelope");
    }

    std::vector<function_spec> specs;
    for (const json& entry : *value_array) {
        if (!entry.is_object()) continue;

        const std::string ftype = entry.value("functionType", std::string());
        if (ftype == "HttpAction") continue;  // HTTP routes have no typed wrapper.

        const std::string identifier = entry.value("identifier", std::string());
        if (identifier.empty()) continue;

        function_spec spec;
        spec.original_identifier = identifier;
        spec.canonical_identifier = canonicalize_identifier(identifier);
        const split_identifier split = split_module_function(identifier);
        spec.module_path = split.module_path;
        spec.function_name = split.function_name;

        // Skip reserved system modules.
        if (spec.module_path.rfind("_system/", 0) == 0 || spec.module_path == "_system") {
            continue;
        }

        if (ftype == "Query") {
            spec.type = function_type::query;
        } else if (ftype == "Mutation") {
            spec.type = function_type::mutation;
        } else if (ftype == "Action") {
            spec.type = function_type::action;
        } else {
            continue;  // Unknown function type: skip rather than mis-emit.
        }

        spec.internal = false;
        if (entry.contains("visibility") && entry["visibility"].is_object()) {
            spec.internal = entry["visibility"].value("kind", std::string("public")) == "internal";
        }
        if (spec.internal && !include_internal) continue;

        spec.args = parse_validator_field(entry.contains("args") ? entry["args"] : json(nullptr));
        spec.returns =
            parse_validator_field(entry.contains("returns") ? entry["returns"] : json(nullptr));

        specs.push_back(std::move(spec));
    }

    std::sort(specs.begin(), specs.end(), [](const function_spec& a, const function_spec& b) {
        return a.canonical_identifier < b.canonical_identifier;
    });
    return specs;
}

}  // namespace convex_codegen
