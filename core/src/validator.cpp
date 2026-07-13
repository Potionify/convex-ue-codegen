#include <convex_codegen/validator.h>

#include <nlohmann/json.hpp>

#include <algorithm>

namespace convex_codegen {

using json = nlohmann::json;

namespace {

validator_ptr make_any() {
    auto v = std::make_shared<validator>();
    v->kind = validator_kind::any;
    return v;
}

/// Render a JSON scalar for a literal summary. Strings are quoted; everything
/// else uses its compact JSON form.
std::string render_literal(const json& value) {
    if (value.is_string()) return "\"" + value.get<std::string>() + "\"";
    return value.dump();
}

validator_ptr parse_node(const json& node);

validator_ptr parse_object_validator(const json& node) {
    auto v = std::make_shared<validator>();
    v->kind = validator_kind::object;
    v->raw_type = "object";
    if (node.contains("value") && node["value"].is_object()) {
        for (const auto& [name, field_json] : node["value"].items()) {
            validator_field field;
            field.name = name;
            field.optional = field_json.value("optional", false);
            const json& field_type =
                field_json.contains("fieldType") ? field_json["fieldType"] : json(nullptr);
            field.type = parse_node(field_type);
            v->fields.push_back(std::move(field));
        }
        // Keep object fields in a stable, deterministic order.
        std::sort(v->fields.begin(), v->fields.end(),
                  [](const validator_field& a, const validator_field& b) {
                      return a.name < b.name;
                  });
    }
    return v;
}

validator_ptr parse_node(const json& node) {
    if (!node.is_object() || !node.contains("type") || !node["type"].is_string()) {
        return make_any();
    }
    const std::string type = node["type"].get<std::string>();

    auto v = std::make_shared<validator>();
    v->raw_type = type;

    if (type == "string") {
        v->kind = validator_kind::string;
    } else if (type == "number" || type == "float64") {
        v->kind = validator_kind::number;
    } else if (type == "boolean") {
        v->kind = validator_kind::boolean;
    } else if (type == "int64" || type == "bigint") {
        v->kind = validator_kind::int64;
    } else if (type == "null") {
        v->kind = validator_kind::null;
    } else if (type == "bytes") {
        v->kind = validator_kind::bytes;
    } else if (type == "any") {
        v->kind = validator_kind::any;
    } else if (type == "id") {
        v->kind = validator_kind::id;
        v->id_table = node.value("tableName", std::string());
    } else if (type == "literal") {
        v->kind = validator_kind::literal;
        const json& value = node.contains("value") ? node["value"] : json(nullptr);
        v->literal_repr = render_literal(value);
        if (value.is_string()) {
            v->literal_kind = validator::literal_base::string;
        } else if (value.is_boolean()) {
            v->literal_kind = validator::literal_base::boolean;
        } else if (value.is_number()) {
            v->literal_kind = validator::literal_base::number;
        } else {
            v->literal_kind = validator::literal_base::other;
        }
    } else if (type == "array") {
        v->kind = validator_kind::array;
        v->element = parse_node(node.contains("value") ? node["value"] : json(nullptr));
    } else if (type == "record") {
        v->kind = validator_kind::record;
        v->record_values = parse_node(node.contains("values") ? node["values"] : json(nullptr));
    } else if (type == "object") {
        return parse_object_validator(node);
    } else if (type == "union") {
        v->kind = validator_kind::union_;
        if (node.contains("value") && node["value"].is_array()) {
            for (const json& member : node["value"]) v->members.push_back(parse_node(member));
        }
    } else {
        // Unknown validator type: degrade to `any` but remember the name.
        v->kind = validator_kind::any;
    }
    return v;
}

}  // namespace

validator_ptr parse_validator_json(std::string_view json_text) {
    if (json_text.empty()) return make_any();
    json parsed = json::parse(json_text, nullptr, /*allow_exceptions=*/false);
    if (parsed.is_discarded() || parsed.is_null()) return make_any();
    return parse_node(parsed);
}

std::string summarize(const validator& v) {
    switch (v.kind) {
        case validator_kind::any:
            return "any";
        case validator_kind::string:
            return "string";
        case validator_kind::number:
            return v.raw_type.empty() ? "number" : v.raw_type;
        case validator_kind::boolean:
            return "boolean";
        case validator_kind::int64:
            return v.raw_type.empty() ? "int64" : v.raw_type;
        case validator_kind::null:
            return "null";
        case validator_kind::bytes:
            return "bytes";
        case validator_kind::id:
            return "id<" + v.id_table + ">";
        case validator_kind::literal:
            return "literal(" + v.literal_repr + ")";
        case validator_kind::array:
            return "array<" + summarize(v.element) + ">";
        case validator_kind::record:
            return "record<string, " + summarize(v.record_values) + ">";
        case validator_kind::object: {
            std::string out = "object{ ";
            for (std::size_t i = 0; i < v.fields.size(); ++i) {
                const validator_field& f = v.fields[i];
                if (i != 0) out += ", ";
                out += f.name;
                if (f.optional) out += "?";
                out += ": " + summarize(f.type);
            }
            out += v.fields.empty() ? "}" : " }";
            return out;
        }
        case validator_kind::union_: {
            std::string out = "union<";
            for (std::size_t i = 0; i < v.members.size(); ++i) {
                if (i != 0) out += " | ";
                out += summarize(v.members[i]);
            }
            out += ">";
            return out;
        }
    }
    return "any";
}

std::string summarize(const validator_ptr& v) {
    if (!v) return "any";
    return summarize(*v);
}

}  // namespace convex_codegen
