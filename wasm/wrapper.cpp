// C bridge over the pure emission core for the WebAssembly build. The web app
// calls convex_ue_codegen_generate() with the apiSpec text and a small options
// JSON, and receives a malloc'd UTF-8 JSON string it must release with
// convex_ue_codegen_free(). No exception ever crosses the C boundary: every
// failure is reported as the {"error": "..."} form.

#include <convex_codegen/emit.h>

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <cstring>
#include <string>

namespace {

using json = nlohmann::json;

/// Copy a std::string into a malloc'd, NUL-terminated buffer the JS side frees
/// via convex_ue_codegen_free().
char* to_c_string(const std::string& s) {
    char* out = static_cast<char*>(std::malloc(s.size() + 1));
    if (out == nullptr) return nullptr;
    std::memcpy(out, s.data(), s.size());
    out[s.size()] = '\0';
    return out;
}

char* error_result(const std::string& message) {
    json doc;
    doc["error"] = message;
    return to_c_string(doc.dump());
}

}  // namespace

extern "C" {

/// Generate the UE wrapper files for an apiSpec document.
///
/// `spec_json`    — the apiSpec text (bare value array or {"status","value"}
///                  envelope), UTF-8.
/// `options_json` — optional (may be null/empty) JSON object:
///                  {"prefix"?: string, "includeInternal"?: bool,
///                   "emitModule"?: string, "stamp"?: string,
///                   "sourceLabel"?: string}.
///
/// Returns a malloc'd UTF-8 JSON string, either
///   {"files": {"<name>": "<content>", ...}}   on success, or
///   {"error": "<message>"}                    on failure.
/// The caller must release it with convex_ue_codegen_free(). Returns nullptr
/// only when the result allocation itself fails.
const char* convex_ue_codegen_generate(const char* spec_json, const char* options_json) {
    try {
        if (spec_json == nullptr) return error_result("spec_json is null");

        convex_codegen::emit_options options;
        if (options_json != nullptr && options_json[0] != '\0') {
            json opts = json::parse(options_json, nullptr, /*allow_exceptions=*/false);
            if (opts.is_discarded() || !opts.is_object()) {
                return error_result("options_json is not a JSON object");
            }
            if (opts.contains("prefix") && opts["prefix"].is_string()) {
                options.prefix = opts["prefix"].get<std::string>();
            }
            if (options.prefix.empty()) return error_result("prefix must not be empty");
            if (opts.contains("includeInternal") && opts["includeInternal"].is_boolean()) {
                options.include_internal = opts["includeInternal"].get<bool>();
            }
            if (opts.contains("emitModule") && opts["emitModule"].is_string() &&
                !opts["emitModule"].get<std::string>().empty()) {
                options.emit_module = opts["emitModule"].get<std::string>();
            }
            if (opts.contains("stamp") && opts["stamp"].is_string()) {
                options.stamp = opts["stamp"].get<std::string>();
            }
            if (opts.contains("sourceLabel") && opts["sourceLabel"].is_string()) {
                options.source_label = opts["sourceLabel"].get<std::string>();
            }
        }

        const std::map<std::string, std::string> files =
            convex_codegen::emit_all(spec_json, options);

        json doc;
        doc["files"] = json::object();
        for (const auto& [name, contents] : files) doc["files"][name] = contents;
        return to_c_string(doc.dump());
    } catch (const std::exception& ex) {
        return error_result(ex.what());
    } catch (...) {
        return error_result("unknown error");
    }
}

/// Release a string returned by convex_ue_codegen_generate().
void convex_ue_codegen_free(char* ptr) { std::free(ptr); }

}  // extern "C"
