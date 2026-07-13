#include <convex_codegen/emit.h>

#include <convex_codegen/api_spec.h>
#include <convex_codegen/naming.h>
#include <convex_codegen/type_map.h>
#include <convex_codegen/validator.h>

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace convex_codegen {

namespace {

// --------------------------------------------------------------------------
// Argument planning
// --------------------------------------------------------------------------

struct planned_arg {
    std::string field_name;  ///< wire key (original field name)
    std::string param_name;  ///< PascalCased C++/BP parameter identifier
    mapped_type type;
    bool optional = false;
};

struct arg_plan {
    /// True when the args validator is not an object: a single pass-through
    /// TMap<FString, FConvexValue> parameter named "Args" carries everything.
    bool passthrough = false;
    std::vector<planned_arg> args;  ///< required (alpha) then optional (alpha)
};

arg_plan plan_args(const function_spec& spec) {
    arg_plan plan;
    if (!spec.args || spec.args->kind != validator_kind::object) {
        plan.passthrough = true;
        return plan;
    }
    std::vector<planned_arg> required;
    std::vector<planned_arg> optional;
    for (const validator_field& field : spec.args->fields) {
        planned_arg a;
        a.field_name = field.name;
        a.param_name = pascal_case(field.name);
        a.type = map_type(field.type);
        a.optional = field.optional;
        (field.optional ? optional : required).push_back(std::move(a));
    }
    // Fields arrive already sorted by name from the validator parser; keep the
    // required-first, each-group-alphabetical ordering explicit for clarity.
    auto by_field = [](const planned_arg& a, const planned_arg& b) {
        return a.field_name < b.field_name;
    };
    std::stable_sort(required.begin(), required.end(), by_field);
    std::stable_sort(optional.begin(), optional.end(), by_field);
    plan.args = std::move(required);
    plan.args.insert(plan.args.end(), optional.begin(), optional.end());
    return plan;
}

// --------------------------------------------------------------------------
// Name deduplication (per namespace / per class)
// --------------------------------------------------------------------------

class name_pool {
public:
    std::string take(const std::string& desired) {
        if (used_.insert(desired).second) return desired;
        for (int n = 2;; ++n) {
            std::string candidate = desired + "_" + std::to_string(n);
            if (used_.insert(candidate).second) return candidate;
        }
    }

private:
    std::set<std::string> used_;
};

// --------------------------------------------------------------------------
// Small text helpers
// --------------------------------------------------------------------------

const char* factory_for(arg_class cls) {
    switch (cls) {
        case arg_class::str:
        case arg_class::arr_str:
            return "String";
        case arg_class::flt:
        case arg_class::arr_flt:
            return "Float";
        case arg_class::i64:
        case arg_class::arr_i64:
            return "Int64";
        case arg_class::boolean:
        case arg_class::arr_bool:
            return "Bool";
        case arg_class::bytes:
            return "Bytes";
        default:
            return "";
    }
}

const char* scalar_elem_type(arg_class array_cls) {
    switch (array_cls) {
        case arg_class::arr_str:
            return "FString";
        case arg_class::arr_flt:
            return "double";
        case arg_class::arr_i64:
            return "int64";
        case arg_class::arr_bool:
            return "bool";
        default:
            return "FConvexValue";
    }
}

/// Emit the statement(s) that add one argument to a local `Args` map. `expr`
/// is the C++ expression yielding the by-value argument (e.g. "Name" or
/// "Name.GetValue()"). `indent` prefixes every emitted line.
std::string emit_add(const planned_arg& a, const std::string& expr, const std::string& indent) {
    const std::string key = "TEXT(\"" + a.field_name + "\")";
    switch (a.type.cls) {
        case arg_class::convex:
            return indent + "Args.Add(" + key + ", " + expr + ");\n";
        case arg_class::str:
        case arg_class::flt:
        case arg_class::i64:
        case arg_class::boolean:
        case arg_class::bytes:
            return indent + "Args.Add(" + key + ", FConvexValue::" + factory_for(a.type.cls) +
                   "(" + expr + "));\n";
        case arg_class::arr_convex:
            return indent + "Args.Add(" + key + ", FConvexValue::Array(" + expr + "));\n";
        case arg_class::arr_str:
        case arg_class::arr_flt:
        case arg_class::arr_i64:
        case arg_class::arr_bool: {
            const std::string tmp = "_" + a.param_name + "Items";
            const std::string elem = scalar_elem_type(a.type.cls);
            std::string out;
            out += indent + "{\n";
            out += indent + "\tTArray<FConvexValue> " + tmp + ";\n";
            out += indent + "\tfor (const " + elem + "& _Item : " + expr + ")\n";
            out += indent + "\t{\n";
            out += indent + "\t\t" + tmp + ".Add(FConvexValue::" + factory_for(a.type.cls) +
                   "(_Item));\n";
            out += indent + "\t}\n";
            out += indent + "\tArgs.Add(" + key + ", FConvexValue::Array(" + tmp + "));\n";
            out += indent + "}\n";
            return out;
        }
    }
    return {};
}

/// Build the body statements that populate a local `Args` map from a plan.
/// When `include_optional` is false (the Blueprint wrappers, which drop
/// optional parameters from their signature), optional fields are skipped so
/// the body only references parameters that actually exist.
std::string emit_args_build(const arg_plan& plan, bool include_optional) {
    std::string out;
    out += "\tTMap<FString, FConvexValue> Args;\n";
    for (const planned_arg& a : plan.args) {
        if (!a.optional) {
            out += emit_add(a, a.param_name, "\t");
        } else if (include_optional) {
            out += "\tif (" + a.param_name + ".IsSet())\n\t{\n";
            out += emit_add(a, a.param_name + ".GetValue()", "\t\t");
            out += "\t}\n";
        }
    }
    return out;
}

const char* type_word(function_type t) {
    switch (t) {
        case function_type::query:
            return "Query";
        case function_type::mutation:
            return "Mutation";
        case function_type::action:
            return "Action";
    }
    return "Query";
}

// --------------------------------------------------------------------------
// Grouping
// --------------------------------------------------------------------------

struct group {
    std::string key;                    ///< sort/identity key
    std::vector<std::string> segments;  ///< namespace segments (native)
    std::string flat;                   ///< flattened class fragment (bp)
    std::string module_path;            ///< representative canonical module path
    std::vector<const function_spec*> functions;
};

/// Group by native namespace identity (PascalCased segments joined by "::").
std::vector<group> group_by_namespace(const std::vector<function_spec>& specs) {
    std::map<std::string, group> groups;
    for (const function_spec& spec : specs) {
        std::vector<std::string> segments = module_namespace_segments(spec.module_path);
        std::string key;
        for (const std::string& s : segments) {
            if (!key.empty()) key += "::";
            key += s;
        }
        group& g = groups[key];
        if (g.key.empty()) {
            g.key = key;
            g.segments = segments;
            g.module_path = spec.module_path;
        }
        g.functions.push_back(&spec);
    }
    std::vector<group> out;
    for (auto& [k, g] : groups) out.push_back(std::move(g));
    return out;
}

/// Group by Blueprint class identity (flattened module name).
std::vector<group> group_by_class(const std::vector<function_spec>& specs) {
    std::map<std::string, group> groups;
    for (const function_spec& spec : specs) {
        std::string flat = module_flat(spec.module_path);
        group& g = groups[flat];
        if (g.key.empty()) {
            g.key = flat;
            g.flat = flat;
            g.module_path = spec.module_path;
        }
        g.functions.push_back(&spec);
    }
    std::vector<group> out;
    for (auto& [k, g] : groups) out.push_back(std::move(g));
    return out;
}

// --------------------------------------------------------------------------
// Per-function resolved names
// --------------------------------------------------------------------------

struct resolved_fn {
    const function_spec* spec = nullptr;
    arg_plan plan;
    std::string call_name;   ///< one-shot wrapper name
    std::string watch_name;  ///< subscription wrapper name (queries only)
};

std::vector<resolved_fn> resolve_names(const group& g) {
    std::vector<resolved_fn> out;
    name_pool pool;
    for (const function_spec* spec : g.functions) {
        resolved_fn r;
        r.spec = spec;
        r.plan = plan_args(*spec);
        r.call_name = pool.take(pascal_case(spec->function_name));
        if (spec->type == function_type::query) {
            r.watch_name = pool.take("Watch" + pascal_case(spec->function_name));
        }
        out.push_back(std::move(r));
    }
    return out;
}

// --------------------------------------------------------------------------
// Signature fragments
// --------------------------------------------------------------------------

std::string id_comment(const mapped_type& t) {
    if (t.id_table.empty()) return {};
    return " /* Id<" + t.id_table + "> */";
}

/// Native parameter list fragment (the args part, without the leading Client
/// or trailing callback). Empty string when there are no args.
std::string native_arg_params(const arg_plan& plan) {
    if (plan.passthrough) return "const TMap<FString, FConvexValue>& Args";
    std::string out;
    for (const planned_arg& a : plan.args) {
        if (!out.empty()) out += ", ";
        if (a.optional) {
            out += "const TOptional<" + a.type.by_value + ">&" + id_comment(a.type) + " " +
                   a.param_name;
        } else {
            out += a.type.param + id_comment(a.type) + " " + a.param_name;
        }
    }
    return out;
}

/// Blueprint parameter list fragment: required args only, no id comments.
std::string bp_arg_params(const arg_plan& plan) {
    if (plan.passthrough) return "const TMap<FString, FConvexValue>& Args";
    std::string out;
    for (const planned_arg& a : plan.args) {
        if (a.optional) continue;
        if (!out.empty()) out += ", ";
        out += a.type.param + " " + a.param_name;
    }
    return out;
}

bool has_optional(const arg_plan& plan) {
    for (const planned_arg& a : plan.args)
        if (a.optional) return true;
    return false;
}

std::string join_params(const std::string& lead, const std::string& mid, const std::string& tail) {
    std::string out = lead;
    if (!mid.empty()) out += ", " + mid;
    out += ", " + tail;
    return out;
}

std::string doc_comment(const function_spec& spec) {
    std::string out;
    out += "\t/// " + spec.original_identifier + " (" + type_word(spec.type) + ")\n";
    out += "\t/// args:    " + summarize(spec.args) + "\n";
    out += "\t/// returns: " + summarize(spec.returns) + "\n";
    return out;
}

// --------------------------------------------------------------------------
// File header
// --------------------------------------------------------------------------

std::string file_header(const emit_options& opt) {
    std::string prov = opt.stamp ? *opt.stamp : ("// Source: " + opt.source_label);
    return "// Generated by convex-ue-codegen. Do not edit by hand.\n" + prov + "\n";
}

// --------------------------------------------------------------------------
// Native header / source
// --------------------------------------------------------------------------

void emit_native(const emit_options& opt, const std::vector<function_spec>& specs,
                 std::map<std::string, std::string>& files) {
    const std::string& prefix = opt.prefix;
    const std::vector<group> groups = group_by_namespace(specs);

    // ---- Header ----
    std::string h = file_header(opt);
    h += "\n#pragma once\n\n";
    h += "// Typed native C++ wrappers for the deployed Convex functions.\n";
    h += "// Compiles inside a UE module (depends on the ConvexClient module).\n\n";
    h += "#include \"CoreMinimal.h\"\n";
    h += "#include \"ConvexValue.h\"\n";
    h += "#include \"ConvexDelegates.h\"\n\n";
    h += "class UConvexClient;\n";
    h += "class UConvexSubscription;\n\n";
    h += "namespace " + prefix + "\n{\n\n";

    // ---- Source ----
    std::string c = file_header(opt);
    c += "\n#include \"" + prefix + ".h\"\n\n";
    c += "#include \"ConvexClient.h\"\n";
    c += "#include \"ConvexSubscription.h\"\n\n";

    for (const group& g : groups) {
        const std::vector<resolved_fn> fns = resolve_names(g);

        std::string ns_open;
        std::string ns_close;
        std::string ns_qualified;
        for (const std::string& seg : g.segments) {
            ns_open += "namespace " + seg + "\n{\n";
            ns_qualified += seg + "::";
        }
        for (auto it = g.segments.rbegin(); it != g.segments.rend(); ++it) {
            ns_close += "}  // namespace " + *it + "\n";
        }

        h += ns_open + "\n";

        for (std::size_t i = 0; i < fns.size(); ++i) {
            const resolved_fn& r = fns[i];
            const function_spec& spec = *r.spec;
            const std::string args = native_arg_params(r.plan);
            const char* native_call = spec.type == function_type::query   ? "QueryNative"
                                      : spec.type == function_type::mutation ? "MutationNative"
                                                                             : "ActionNative";

            // --- Header declarations ---
            h += doc_comment(spec);
            h += "\tvoid " + r.call_name + "(" +
                 join_params("UConvexClient& Client", args, "FConvexResultNative OnResult") +
                 ");\n";
            if (spec.type == function_type::query) {
                h += "\tUConvexSubscription* " + r.watch_name + "(" +
                     join_params("UConvexClient& Client", args, "FConvexResultNative OnUpdate") +
                     ");\n";
            }
            h += "\n";

            // --- Source definitions ---
            const std::string qual = prefix + "::" + ns_qualified;
            c += "void " + qual + r.call_name + "(" +
                 join_params("UConvexClient& Client", args, "FConvexResultNative OnResult") +
                 ")\n{\n";
            if (r.plan.passthrough) {
                c += "\tClient." + std::string(native_call) + "(TEXT(\"" +
                     spec.canonical_identifier + "\"), Args, MoveTemp(OnResult));\n";
            } else {
                c += emit_args_build(r.plan, /*include_optional=*/true);
                c += "\tClient." + std::string(native_call) + "(TEXT(\"" +
                     spec.canonical_identifier + "\"), Args, MoveTemp(OnResult));\n";
            }
            c += "}\n\n";

            if (spec.type == function_type::query) {
                c += "UConvexSubscription* " + qual + r.watch_name + "(" +
                     join_params("UConvexClient& Client", args, "FConvexResultNative OnUpdate") +
                     ")\n{\n";
                if (r.plan.passthrough) {
                    c += "\treturn Client.SubscribeNative(TEXT(\"" + spec.canonical_identifier +
                         "\"), Args, MoveTemp(OnUpdate));\n";
                } else {
                    c += emit_args_build(r.plan, /*include_optional=*/true);
                    c += "\treturn Client.SubscribeNative(TEXT(\"" + spec.canonical_identifier +
                         "\"), Args, MoveTemp(OnUpdate));\n";
                }
                c += "}\n\n";
            }
        }

        h += ns_close + "\n";
    }

    h += "}  // namespace " + prefix + "\n";

    files[prefix + ".h"] = h;
    files[prefix + ".cpp"] = c;
}

// --------------------------------------------------------------------------
// Blueprint header / source
// --------------------------------------------------------------------------

std::string bp_tooltip(const function_spec& spec, const arg_plan& plan, bool watch) {
    std::string tip = spec.canonical_identifier + " (" + type_word(spec.type) + ")";
    if (watch) tip += " live subscription";
    tip += "\\nargs: " + summarize(spec.args);
    if (!plan.passthrough && has_optional(plan)) {
        tip += "\\noptional args omitted; use the C++ wrapper";
    }
    return tip;
}

void emit_blueprint(const emit_options& opt, const std::vector<function_spec>& specs,
                    std::map<std::string, std::string>& files) {
    const std::string& prefix = opt.prefix;
    const std::vector<group> groups = group_by_class(specs);

    // ---- Header ----
    std::string h = file_header(opt);
    h += "\n#pragma once\n\n";
    h += "// Blueprint-callable wrappers for the deployed Convex functions.\n";
    h += "// These files only compile inside a UE module (UHT-processed).\n\n";
    h += "#include \"CoreMinimal.h\"\n";
    h += "#include \"Kismet/BlueprintFunctionLibrary.h\"\n";
    h += "#include \"ConvexValue.h\"\n";
    h += "#include \"ConvexDelegates.h\"\n\n";
    h += "#include \"" + prefix + "BP.generated.h\"\n\n";
    h += "class UConvexClient;\n";
    h += "class UConvexSubscription;\n\n";

    // ---- Source ----
    std::string c = file_header(opt);
    c += "\n#include \"" + prefix + "BP.h\"\n\n";
    c += "#include \"ConvexClient.h\"\n";
    c += "#include \"ConvexSubscription.h\"\n\n";
    c += "DEFINE_LOG_CATEGORY_STATIC(Log" + prefix + "BP, Log, All);\n\n";

    for (const group& g : groups) {
        const std::vector<resolved_fn> fns = resolve_names(g);
        const std::string class_name = "U" + prefix + g.flat + "Library";
        const std::string category = "Convex API|" + g.module_path;

        h += "UCLASS()\n";
        h += "class " + class_name + " : public UBlueprintFunctionLibrary\n{\n";
        h += "\tGENERATED_BODY()\n\npublic:\n";

        for (const resolved_fn& r : fns) {
            const function_spec& spec = *r.spec;
            const std::string args = bp_arg_params(r.plan);
            const char* dyn_call = spec.type == function_type::query      ? "Query"
                                   : spec.type == function_type::mutation ? "Mutation"
                                                                          : "Action";

            // --- One-shot UFUNCTION ---
            h += "\tUFUNCTION(BlueprintCallable, Category = \"" + category +
                 "\", meta = (DisplayName = \"Convex " + spec.canonical_identifier +
                 "\", ToolTip = \"" + bp_tooltip(spec, r.plan, false) + "\"))\n";
            h += "\tstatic void " + r.call_name + "(" +
                 join_params("UConvexClient* Client", args, "FConvexResultDelegate OnResult") +
                 ");\n\n";

            c += "void " + class_name + "::" + r.call_name + "(" +
                 join_params("UConvexClient* Client", args, "FConvexResultDelegate OnResult") +
                 ")\n{\n";
            c += "\tif (Client == nullptr)\n\t{\n";
            c += "\t\tUE_LOG(Log" + prefix + "BP, Warning, TEXT(\"Convex " + r.call_name +
                 ": Client is null\"));\n";
            c += "\t\treturn;\n\t}\n";
            if (r.plan.passthrough) {
                c += "\tClient->" + std::string(dyn_call) + "(TEXT(\"" +
                     spec.canonical_identifier + "\"), Args, OnResult);\n";
            } else {
                c += emit_args_build(r.plan, /*include_optional=*/false);
                c += "\tClient->" + std::string(dyn_call) + "(TEXT(\"" +
                     spec.canonical_identifier + "\"), Args, OnResult);\n";
            }
            c += "}\n\n";

            // --- Subscription UFUNCTION (queries only) ---
            if (spec.type == function_type::query) {
                h += "\tUFUNCTION(BlueprintCallable, Category = \"" + category +
                     "\", meta = (DisplayName = \"Convex Watch " + spec.canonical_identifier +
                     "\", ToolTip = \"" + bp_tooltip(spec, r.plan, true) + "\"))\n";
                h += "\tstatic UConvexSubscription* " + r.watch_name + "(" +
                     join_params("UConvexClient* Client", args, "FConvexResultDelegate OnUpdate") +
                     ");\n\n";

                c += "UConvexSubscription* " + class_name + "::" + r.watch_name + "(" +
                     join_params("UConvexClient* Client", args, "FConvexResultDelegate OnUpdate") +
                     ")\n{\n";
                c += "\tif (Client == nullptr)\n\t{\n";
                c += "\t\tUE_LOG(Log" + prefix + "BP, Warning, TEXT(\"Convex " + r.watch_name +
                     ": Client is null\"));\n";
                c += "\t\treturn nullptr;\n\t}\n";
                if (r.plan.passthrough) {
                    c += "\treturn Client->Subscribe(TEXT(\"" + spec.canonical_identifier +
                         "\"), Args, OnUpdate);\n";
                } else {
                    c += emit_args_build(r.plan, /*include_optional=*/false);
                    c += "\treturn Client->Subscribe(TEXT(\"" + spec.canonical_identifier +
                         "\"), Args, OnUpdate);\n";
                }
                c += "}\n\n";
            }
        }

        h += "};\n\n";
    }

    files[prefix + "BP.h"] = h;
    files[prefix + "BP.cpp"] = c;
}

// --------------------------------------------------------------------------
// Optional UE module scaffolding
// --------------------------------------------------------------------------

void emit_module_files(const emit_options& opt, std::map<std::string, std::string>& files) {
    if (!opt.emit_module) return;
    const std::string& name = *opt.emit_module;

    std::string build = file_header(opt);
    build += "\nusing UnrealBuildTool;\n\n";
    build += "public class " + name + " : ModuleRules\n{\n";
    build += "\tpublic " + name + "(ReadOnlyTargetRules Target) : base(Target)\n\t{\n";
    build += "\t\tPCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;\n\n";
    build += "\t\tPublicDependencyModuleNames.AddRange(new string[]\n\t\t{\n";
    build += "\t\t\t\"Core\",\n";
    build += "\t\t\t\"CoreUObject\",\n";
    build += "\t\t\t\"Engine\",\n";
    build += "\t\t\t\"ConvexClient\",\n";
    build += "\t\t\t\"ConvexCore\",\n";
    build += "\t\t});\n\t}\n}\n";
    files[name + ".Build.cs"] = build;

    std::string mod = file_header(opt);
    mod += "\n#include \"Modules/ModuleManager.h\"\n\n";
    mod += "IMPLEMENT_MODULE(FDefaultModuleImpl, " + name + ");\n";
    files[name + "Module.cpp"] = mod;
}

}  // namespace

std::map<std::string, std::string> emit_all(std::string_view api_spec_json,
                                            const emit_options& options) {
    const std::vector<function_spec> specs =
        parse_api_spec(api_spec_json, options.include_internal);

    std::map<std::string, std::string> files;
    emit_native(options, specs, files);
    emit_blueprint(options, specs, files);
    emit_module_files(options, files);
    return files;
}

std::size_t count_functions(std::string_view api_spec_json, const emit_options& options) {
    return parse_api_spec(api_spec_json, options.include_internal).size();
}

}  // namespace convex_codegen
