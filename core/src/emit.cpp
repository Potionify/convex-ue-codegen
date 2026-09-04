#include <convex_codegen/emit.h>

#include <convex_codegen/api_spec.h>
#include <convex_codegen/naming.h>
#include <convex_codegen/type_map.h>
#include <convex_codegen/validator.h>

#include <algorithm>
#include <map>
#include <optional>
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
    validator_ptr validator;  ///< source validator (the script emitter types nested objects)
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
        a.validator = field.type;
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
    bool paginated = false;  ///< query matches the paginationOpts shape
    std::string watch_paginated_name;  ///< live paginated-list wrapper name
    arg_plan paginated_plan;  ///< args with paginationOpts removed (client injects it)
};

/// Copy `plan` dropping the named field. Used for the paginated wrapper, whose
/// paginationOpts argument is injected by the client rather than the caller.
arg_plan plan_without_field(const arg_plan& plan, const std::string& field_name) {
    arg_plan out;
    out.passthrough = plan.passthrough;
    for (const planned_arg& a : plan.args) {
        if (a.field_name == field_name) continue;
        out.args.push_back(a);
    }
    return out;
}

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
            if (is_paginated_query(*spec)) {
                r.paginated = true;
                r.watch_paginated_name =
                    pool.take("Watch" + pascal_case(spec->function_name) + "Paginated");
                r.paginated_plan = plan_without_field(r.plan, "paginationOpts");
            }
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

    // Paginated query wrappers reference UConvexPaginatedSubscription and the
    // FConvexPaginatedUpdateNativeFn alias, both declared in the plugin header
    // ConvexPaginatedSubscription.h — pull it in only when we actually emit one.
    const bool any_paginated =
        std::any_of(specs.begin(), specs.end(), is_paginated_query);

    // ---- Header ----
    std::string h = file_header(opt);
    h += "\n#pragma once\n\n";
    h += "// Typed native C++ wrappers for the deployed Convex functions.\n";
    h += "// Compiles inside a UE module (depends on the ConvexClient module).\n\n";
    h += "#include \"CoreMinimal.h\"\n";
    h += "#include \"ConvexValue.h\"\n";
    h += "#include \"ConvexDelegates.h\"\n";
    if (any_paginated) {
        h += "#include \"ConvexPaginatedSubscription.h\"\n";
    }
    h += "\n";
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
                if (r.paginated) {
                    const std::string pag_args = native_arg_params(r.paginated_plan);
                    h += "\t/// Subscribes " + spec.canonical_identifier +
                         " as a growing live list (the usePaginatedQuery\n";
                    h += "\t/// pattern); paginationOpts is injected by the client.\n";
                    h += "\tUConvexPaginatedSubscription* " + r.watch_paginated_name + "(" +
                         join_params("UConvexClient& Client", pag_args,
                                     "int32 InitialNumItems, FConvexPaginatedUpdateNativeFn "
                                     "OnUpdate") +
                         ");\n";
                }
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

                if (r.paginated) {
                    const std::string pag_args = native_arg_params(r.paginated_plan);
                    c += "UConvexPaginatedSubscription* " + qual + r.watch_paginated_name + "(" +
                         join_params("UConvexClient& Client", pag_args,
                                     "int32 InitialNumItems, FConvexPaginatedUpdateNativeFn "
                                     "OnUpdate") +
                         ")\n{\n";
                    c += emit_args_build(r.paginated_plan, /*include_optional=*/true);
                    c += "\treturn Client.SubscribePaginatedNative(TEXT(\"" +
                         spec.canonical_identifier +
                         "\"), Args, InitialNumItems, MoveTemp(OnUpdate));\n";
                    c += "}\n\n";
                }
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
// AngelScript (Hazelight UnrealEngine-Angelscript fork)
// --------------------------------------------------------------------------
//
// One file. In order: a script struct for every object shape the deployed
// functions declare (deduplicated by shape), a `<Prefix>::Types` namespace
// that decodes and encodes those structs, a delegate and an adapter class per
// function with a typed return, and one namespace of wrappers per Convex
// module. The wrappers build the args map with the plugin's value library
// (UConvexBlueprintLibrary, bound as the `Convex::` namespace), read values
// through the plugin's script mixins (`Value.Get("x").AsString()`), and call
// the ScriptCallable dynamic-delegate methods on UConvexClient.
//
// Typed callbacks: the fork has no lambdas, so a typed delegate cannot be
// bound to the client directly. Each typed wrapper creates an adapter object
// whose UFUNCTION receives the raw FConvexResult, decodes it, and executes
// the caller's typed delegate. One-shot adapters are kept alive by the
// client until the callback fires; subscription adapters are attached to the
// returned subscription handle.
//
// Optional fields: the fork's TOptional cannot hold containers and does not
// convert from integer literals, so instead of TOptional parameters each
// function with optional fields gets two overloads, one with the required
// arguments only and one with every argument. The overloads always differ in
// arity, so resolution is unambiguous. Optional struct fields get a bHas
// companion instead.
//
// Containers cannot nest in the fork, so an array of arrays and an array of
// bytes stay TArray<FConvexValue>.

/// How a validator is spelled and marshalled in script.
struct script_ty {
    enum class base { str, flt, i64, boolean, bytes, value, strct };
    base b = base::value;
    bool is_array = false;    ///< TArray<...> of `b`
    std::string struct_name;  ///< when b == strct

    bool is_plain_value() const { return b == base::value && !is_array; }
};

/// The element spelling of a script type. `float` is 64-bit in the fork, so
/// C++ double maps to plain float.
std::string script_elem(const script_ty& t) {
    switch (t.b) {
        case script_ty::base::str:
            return "FString";
        case script_ty::base::flt:
            return "float";
        case script_ty::base::i64:
            return "int64";
        case script_ty::base::boolean:
            return "bool";
        case script_ty::base::bytes:
            return "TArray<uint8>";
        case script_ty::base::value:
            return "FConvexValue";
        case script_ty::base::strct:
            return t.struct_name;
    }
    return "FConvexValue";
}

std::string script_spelling(const script_ty& t) {
    if (t.is_array) return "TArray<" + script_elem(t) + ">";
    return script_elem(t);
}

/// The default initializer suffix for a struct member of this type.
std::string script_default(const script_ty& t) {
    if (t.is_array) return "";
    switch (t.b) {
        case script_ty::base::flt:
            return " = 0.0";
        case script_ty::base::i64:
            return " = 0";
        case script_ty::base::boolean:
            return " = false";
        default:
            return "";
    }
}

struct script_struct_field {
    std::string wire;  ///< wire field name
    std::string name;  ///< PascalCased script member
    std::string has;   ///< bHas companion member; empty unless optional
    script_ty type;
};

struct script_struct {
    std::string name;        ///< "FConvexApiMessagesListElement"
    std::string short_name;  ///< "MessagesListElement" (Decode<short_name>)
    std::string shape;       ///< summarize() of the source validator
    std::vector<script_struct_field> fields;
};

/// Deduplicates object validators by shape into named script structs. The
/// name comes from the first place a shape is seen, walking functions in
/// sorted order, so identical documents returned by several functions share
/// one struct named after the first of them.
class script_type_registry {
public:
    explicit script_type_registry(std::string prefix) : prefix_(std::move(prefix)) {}

    /// Resolve a validator seen at position `hint` (e.g. "Result", "Nested")
    /// of the context `base` (e.g. "ConvexApiSinkKitchenSink").
    script_ty resolve(const validator_ptr& v, const std::string& base, const std::string& hint) {
        script_ty t;
        if (!v) return t;
        switch (v->kind) {
            case validator_kind::string:
            case validator_kind::id:
                t.b = script_ty::base::str;
                return t;
            case validator_kind::number:
                t.b = script_ty::base::flt;
                return t;
            case validator_kind::int64:
                t.b = script_ty::base::i64;
                return t;
            case validator_kind::boolean:
                t.b = script_ty::base::boolean;
                return t;
            case validator_kind::bytes:
                t.b = script_ty::base::bytes;
                return t;
            case validator_kind::literal:
                switch (v->literal_kind) {
                    case validator::literal_base::string:
                        t.b = script_ty::base::str;
                        return t;
                    case validator::literal_base::number:
                        t.b = script_ty::base::flt;
                        return t;
                    case validator::literal_base::boolean:
                        t.b = script_ty::base::boolean;
                        return t;
                    case validator::literal_base::other:
                        return t;
                }
                return t;
            case validator_kind::object:
                if (v->fields.empty()) return t;
                t.b = script_ty::base::strct;
                t.struct_name = register_object(*v, base, hint);
                return t;
            case validator_kind::array: {
                const script_ty elem = resolve(v->element, base, hint + "Element");
                if (elem.is_array || elem.b == script_ty::base::bytes) {
                    t.is_array = true;  // TArray<FConvexValue>: containers cannot nest
                    return t;
                }
                t = elem;
                t.is_array = true;
                return t;
            }
            case validator_kind::any:
            case validator_kind::null:
            case validator_kind::record:
            case validator_kind::union_:
                return t;
        }
        return t;
    }

    /// Take a global name (delegate or adapter class) from the same pool as
    /// the structs, so nothing in the file collides.
    std::string take_global(const std::string& desired) { return pool_.take(desired); }

    const std::vector<script_struct>& structs() const { return structs_; }

private:
    std::string register_object(const validator& v, const std::string& base,
                                const std::string& hint) {
        const std::string shape = summarize(v);
        if (auto it = by_shape_.find(shape); it != by_shape_.end()) return it->second;

        const std::string name = pool_.take("F" + base + hint);
        by_shape_[shape] = name;

        // Reserve the slot before recursing so a parent precedes its nested
        // structs in the file.
        const std::size_t index = structs_.size();
        structs_.push_back(script_struct{});
        structs_[index].name = name;
        structs_[index].short_name = name.substr(1 + prefix_.size());
        structs_[index].shape = shape;

        std::vector<script_struct_field> fields;
        name_pool members;
        for (const validator_field& f : v.fields) {
            script_struct_field sf;
            sf.wire = f.name;
            sf.name = members.take(pascal_case(f.name));
            const std::string nested_base = base + hint;
            sf.type = resolve(f.type, nested_base, pascal_case(f.name));
            if (f.optional) sf.has = members.take("bHas" + sf.name);
            fields.push_back(std::move(sf));
        }
        structs_[index].fields = std::move(fields);
        return name;
    }

    std::string prefix_;
    std::map<std::string, std::string> by_shape_;
    std::vector<script_struct> structs_;
    name_pool pool_;
};

/// Per-function script typing, computed in a first pass over every function
/// so struct names are settled before any wrapper is written.
struct script_fn {
    std::string base;                     ///< "ConvexApiSinkKitchenSink"
    std::vector<script_ty> arg_types;     ///< aligned with resolved_fn::plan.args
    std::vector<script_ty> pag_arg_types; ///< aligned with resolved_fn::paginated_plan.args
    std::optional<script_ty> ret;         ///< set when the return is typed
    std::string delegate_name;            ///< "FConvexApiSinkKitchenSinkDelegate"
    std::string adapter_name;             ///< "UConvexApiSinkKitchenSinkAdapter"
    std::optional<script_ty> page_item;   ///< set when a paginated page is typed
    std::string page_delegate_name;
    std::string page_adapter_name;
};

/// The `page` element validator of a paginated query's declared return, if
/// it declares one in the PaginationResult shape.
validator_ptr page_element(const function_spec& spec) {
    if (!spec.returns || spec.returns->kind != validator_kind::object) return nullptr;
    for (const validator_field& f : spec.returns->fields) {
        if (f.name != "page") continue;
        if (f.type && f.type->kind == validator_kind::array) return f.type->element;
        return nullptr;
    }
    return nullptr;
}

script_fn plan_script_fn(script_type_registry& reg, const std::string& prefix, const group& g,
                         const resolved_fn& r) {
    script_fn s;
    s.base = prefix;
    for (const std::string& seg : g.segments) s.base += seg;
    s.base += r.call_name;

    for (const planned_arg& a : r.plan.args) {
        s.arg_types.push_back(reg.resolve(a.validator, s.base, pascal_case(a.field_name)));
    }
    for (const planned_arg& a : r.paginated_plan.args) {
        s.pag_arg_types.push_back(reg.resolve(a.validator, s.base, pascal_case(a.field_name)));
    }

    // An object return is "<Base>Result"; an array return names its element
    // "<Base>Element" (resolve appends "Element" to the hint for elements).
    const bool array_return = r.spec->returns && r.spec->returns->kind == validator_kind::array;
    const script_ty ret = reg.resolve(r.spec->returns, s.base, array_return ? "" : "Result");
    if (!ret.is_plain_value()) {
        s.ret = ret;
        s.delegate_name = reg.take_global("F" + s.base + "Delegate");
        s.adapter_name = reg.take_global("U" + s.base + "Adapter");
    }
    if (r.paginated) {
        const script_ty item = reg.resolve(page_element(*r.spec), s.base, "Element");
        if (!item.is_plain_value() && !item.is_array) {
            s.page_item = item;
            s.page_delegate_name = reg.take_global("F" + s.base + "PageDelegate");
            s.page_adapter_name = reg.take_global("U" + s.base + "PageAdapter");
        }
    }
    return s;
}

/// Script expression reading one non-array value of type `t` from the
/// FConvexValue expression `v`.
std::string script_read_expr(const std::string& prefix, const script_ty& t, const std::string& v) {
    switch (t.b) {
        case script_ty::base::str:
            return v + ".AsString()";
        case script_ty::base::flt:
            return v + ".AsFloat()";
        case script_ty::base::i64:
            return v + ".AsInt()";
        case script_ty::base::boolean:
            return v + ".AsBool()";
        case script_ty::base::bytes:
            return v + ".AsBytes()";
        case script_ty::base::value:
            return v;
        case script_ty::base::strct:
            return prefix + "::Types::Decode" + t.struct_name.substr(1 + prefix.size()) + "(" + v +
                   ")";
    }
    return v;
}

/// Script statements assigning the value expression `v` to `target`.
std::string script_decode(const std::string& prefix, const script_ty& t, const std::string& target,
                          const std::string& v, const std::string& indent) {
    if (!t.is_array) return indent + target + " = " + script_read_expr(prefix, t, v) + ";\n";
    if (t.b == script_ty::base::value) return indent + target + " = " + v + ".AsArray();\n";
    script_ty elem = t;
    elem.is_array = false;
    std::string out;
    out += indent + "for (const FConvexValue& _Item : " + v + ".AsArray())\n";
    out += indent + "{\n";
    out += indent + "\t" + target + ".Add(" + script_read_expr(prefix, elem, "_Item") + ");\n";
    out += indent + "}\n";
    return out;
}

/// Script expression building a FConvexValue from one non-array `src` of
/// type `t`.
std::string script_make_expr(const std::string& prefix, const script_ty& t, const std::string& src) {
    switch (t.b) {
        case script_ty::base::str:
            return "Convex::MakeConvexString(" + src + ")";
        case script_ty::base::flt:
            return "Convex::MakeConvexFloat(" + src + ")";
        case script_ty::base::i64:
            return "Convex::MakeConvexInt(" + src + ")";
        case script_ty::base::boolean:
            return "Convex::MakeConvexBool(" + src + ")";
        case script_ty::base::bytes:
            return "Convex::MakeConvexBytes(" + src + ")";
        case script_ty::base::value:
            return src;
        case script_ty::base::strct:
            return prefix + "::Types::Encode(" + src + ")";
    }
    return src;
}

/// Script statements adding `src` of type `t` to the map `map` under `key`.
std::string script_encode_add(const std::string& prefix, const script_ty& t, const std::string& map,
                              const std::string& key, const std::string& src,
                              const std::string& tmp_name, const std::string& indent) {
    const std::string k = "\"" + key + "\"";
    if (!t.is_array) {
        return indent + map + ".Add(" + k + ", " + script_make_expr(prefix, t, src) + ");\n";
    }
    if (t.b == script_ty::base::value) {
        return indent + map + ".Add(" + k + ", Convex::MakeConvexArray(" + src + "));\n";
    }
    script_ty elem = t;
    elem.is_array = false;
    const std::string tmp = "_" + tmp_name + "Items";
    std::string out;
    out += indent + "{\n";
    out += indent + "\tTArray<FConvexValue> " + tmp + ";\n";
    out += indent + "\tfor (const " + script_elem(elem) + "& _Item : " + src + ")\n";
    out += indent + "\t{\n";
    out += indent + "\t\t" + tmp + ".Add(" + script_make_expr(prefix, elem, "_Item") + ");\n";
    out += indent + "\t}\n";
    out += indent + "\t" + map + ".Add(" + k + ", Convex::MakeConvexArray(" + tmp + "));\n";
    out += indent + "}\n";
    return out;
}

// --- Structs and the Types namespace ---------------------------------------

void emit_script_structs(std::string& s, const script_type_registry& reg) {
    for (const script_struct& st : reg.structs()) {
        s += "/// " + st.shape + "\n";
        s += "struct " + st.name + "\n{\n";
        for (const script_struct_field& f : st.fields) {
            s += "\t" + script_spelling(f.type) + " " + f.name + script_default(f.type) + ";\n";
            if (!f.has.empty()) {
                s += "\tbool " + f.has + " = false; // " + f.name + " was present\n";
            }
        }
        s += "};\n\n";
    }
}

void emit_script_types_namespace(std::string& s, const std::string& prefix,
                                 const script_type_registry& reg) {
    if (reg.structs().empty()) return;
    s += "namespace " + prefix + "::Types\n{\n";
    bool first = true;
    for (const script_struct& st : reg.structs()) {
        if (!first) s += "\n";
        first = false;

        s += "\t" + st.name + " Decode" + st.short_name + "(FConvexValue Value)\n\t{\n";
        s += "\t\t" + st.name + " Out;\n";
        for (const script_struct_field& f : st.fields) {
            const std::string v = "Value.Get(\"" + f.wire + "\")";
            if (f.has.empty()) {
                s += script_decode(prefix, f.type, "Out." + f.name, v, "\t\t");
            } else {
                s += "\t\tif (Value.HasField(\"" + f.wire + "\"))\n\t\t{\n";
                s += "\t\t\tOut." + f.has + " = true;\n";
                s += script_decode(prefix, f.type, "Out." + f.name, v, "\t\t\t");
                s += "\t\t}\n";
            }
        }
        s += "\t\treturn Out;\n\t}\n\n";

        s += "\tFConvexValue Encode(" + st.name + " In)\n\t{\n";
        s += "\t\tTMap<FString, FConvexValue> Fields;\n";
        for (const script_struct_field& f : st.fields) {
            if (f.has.empty()) {
                s += script_encode_add(prefix, f.type, "Fields", f.wire, "In." + f.name, f.name,
                                       "\t\t");
            } else {
                s += "\t\tif (In." + f.has + ")\n\t\t{\n";
                s += script_encode_add(prefix, f.type, "Fields", f.wire, "In." + f.name, f.name,
                                       "\t\t\t");
                s += "\t\t}\n";
            }
        }
        s += "\t\treturn Convex::MakeConvexObject(Fields);\n\t}\n";
    }
    s += "}\n\n";
}

// --- Delegates and adapters -------------------------------------------------

void emit_script_adapters(std::string& s, const std::string& prefix, const function_spec& spec,
                          const script_fn& sf) {
    if (sf.ret) {
        const script_ty& t = *sf.ret;
        s += "/// " + spec.original_identifier + " returns " + script_spelling(t) + "\n";
        s += "delegate void " + sf.delegate_name + "(" + script_spelling(t) +
             " Value, FConvexResult Result);\n\n";
        s += "class " + sf.adapter_name + " : UObject\n{\n";
        s += "\t" + sf.delegate_name + " Typed;\n\n";
        s += "\tUFUNCTION()\n\tvoid OnResult(FConvexResult Result)\n\t{\n";
        s += "\t\t" + script_spelling(t) + " Value" + script_default(t) + ";\n";
        s += "\t\tif (Result.bSuccess)\n\t\t{\n";
        s += script_decode(prefix, t, "Value", "Result.Value", "\t\t\t");
        s += "\t\t}\n";
        s += "\t\tTyped.ExecuteIfBound(Value, Result);\n\t}\n}\n\n";
    }
    if (sf.page_item) {
        const script_ty& item = *sf.page_item;
        script_ty list = item;
        list.is_array = true;
        s += "/// " + spec.original_identifier + " pages of " + script_elem(item) + "\n";
        s += "delegate void " + sf.page_delegate_name + "(" + script_spelling(list) +
             " Results, FConvexPaginatedSnapshot Snapshot);\n\n";
        s += "class " + sf.page_adapter_name + " : UObject\n{\n";
        s += "\t" + sf.page_delegate_name + " Typed;\n\n";
        s += "\tUFUNCTION()\n\tvoid OnSnapshot(FConvexPaginatedSnapshot Snapshot)\n\t{\n";
        s += "\t\t" + script_spelling(list) + " Results;\n";
        s += "\t\tfor (const FConvexValue& _Item : Snapshot.Results)\n\t\t{\n";
        s += "\t\t\tResults.Add(" + script_read_expr(prefix, item, "_Item") + ");\n";
        s += "\t\t}\n";
        s += "\t\tTyped.ExecuteIfBound(Results, Snapshot);\n\t}\n}\n\n";
    }
}

// --- Wrappers ---------------------------------------------------------------

/// Body statements populating a local `Args` map. `include_optional` selects
/// the all-arguments overload, whose optional parameters are plain values.
std::string script_args_build(const std::string& prefix, const arg_plan& plan,
                              const std::vector<script_ty>& types, bool include_optional) {
    std::string out;
    out += "\t\tTMap<FString, FConvexValue> Args;\n";
    for (std::size_t i = 0; i < plan.args.size(); ++i) {
        const planned_arg& a = plan.args[i];
        if (a.optional && !include_optional) continue;
        out += script_encode_add(prefix, types[i], "Args", a.field_name, a.param_name,
                                 a.param_name, "\t\t");
    }
    return out;
}

/// Script argument parameters, by value: required only, or every field.
std::string script_arg_params(const arg_plan& plan, const std::vector<script_ty>& types,
                              bool include_optional) {
    if (plan.passthrough) return "TMap<FString, FConvexValue> Args";
    std::string out;
    for (std::size_t i = 0; i < plan.args.size(); ++i) {
        const planned_arg& a = plan.args[i];
        if (a.optional && !include_optional) continue;
        if (!out.empty()) out += ", ";
        out += script_spelling(types[i]) + " " + a.param_name;
    }
    return out;
}

/// Full script parameter list: Client, args, extras, callback.
std::string script_params(const arg_plan& plan, const std::vector<script_ty>& types,
                          bool include_optional, const std::string& before_callback,
                          const std::string& callback) {
    std::string out = "UConvexClient Client";
    const std::string args = script_arg_params(plan, types, include_optional);
    if (!args.empty()) out += ", " + args;
    if (!before_callback.empty()) out += ", " + before_callback;
    out += ", " + callback;
    return out;
}

enum class script_call { one_shot, watch, paginated };

/// Emit one script function, and its all-arguments overload when the plan has
/// optional fields.
void script_function(std::string& s, const std::string& prefix, script_call mode,
                     const std::string& name, const arg_plan& plan,
                     const std::vector<script_ty>& types, const std::string& path,
                     const std::string& dyn_call, const script_fn& sf) {
    const bool overload = !plan.passthrough && has_optional(plan);

    // What the caller passes and what the client receives.
    std::string ret = "void";
    std::string callback_type = "FConvexResultDelegate";
    std::string callback_name = "OnResult";
    std::string extra;
    std::string adapter;   // adapter class when the callback is typed
    std::string handler_fn; // the adapter UFUNCTION bound to the client
    std::string client_delegate = "FConvexResultDelegate";
    switch (mode) {
        case script_call::one_shot:
            if (sf.ret) {
                callback_type = sf.delegate_name;
                adapter = sf.adapter_name;
                handler_fn = "OnResult";
            }
            break;
        case script_call::watch:
            ret = "UConvexSubscription";
            callback_name = "OnUpdate";
            if (sf.ret) {
                callback_type = sf.delegate_name;
                adapter = sf.adapter_name;
                handler_fn = "OnResult";
            }
            break;
        case script_call::paginated:
            ret = "UConvexPaginatedSubscription";
            callback_name = "OnUpdate";
            callback_type = "FConvexPaginatedSnapshotDelegate";
            client_delegate = "FConvexPaginatedSnapshotDelegate";
            extra = "int InitialNumItems";
            if (sf.page_item) {
                callback_type = sf.page_delegate_name;
                adapter = sf.page_adapter_name;
                handler_fn = "OnSnapshot";
            }
            break;
    }

    for (int pass = 0; pass < (overload ? 2 : 1); ++pass) {
        const bool include_optional = pass == 1;
        if (include_optional) s += "\t/// All arguments, including the optional ones.\n";
        s += "\t" + ret + " " + name + "(" +
             script_params(plan, types, include_optional, extra,
                           callback_type + " " + callback_name) +
             ")\n\t{\n";
        if (!plan.passthrough) s += script_args_build(prefix, plan, types, include_optional);

        std::string handler = callback_name;
        if (!adapter.empty()) {
            s += "\t\t" + adapter + " Adapter = Cast<" + adapter + ">(NewObject(Client, " +
                 adapter + "));\n";
            s += "\t\tAdapter.Typed = " + callback_name + ";\n";
            s += "\t\t" + client_delegate + " Handler;\n";
            s += "\t\tHandler.BindUFunction(Adapter, n\"" + handler_fn + "\");\n";
            handler = "Handler";
        }

        const std::string call_args =
            "\"" + path + "\", Args" +
            (mode == script_call::paginated ? std::string(", InitialNumItems") : std::string()) +
            ", " + handler + ")";
        switch (mode) {
            case script_call::one_shot:
                s += "\t\tClient." + dyn_call + "(" + call_args + ";\n";
                break;
            case script_call::watch:
            case script_call::paginated: {
                const char* method = mode == script_call::watch ? "Subscribe" : "SubscribePaginated";
                if (adapter.empty()) {
                    s += "\t\treturn Client." + std::string(method) + "(" + call_args + ";\n";
                } else {
                    s += "\t\t" + ret + " Subscription = Client." + method + "(" + call_args + ";\n";
                    s += "\t\tSubscription.AttachListener(Adapter);\n";
                    s += "\t\treturn Subscription;\n";
                }
                break;
            }
        }
        s += "\t}\n";
        if (overload && !include_optional) s += "\n";
    }
}

void emit_script(const emit_options& opt, const std::vector<function_spec>& specs,
                 std::map<std::string, std::string>& files) {
    if (!opt.emit_script) return;
    const std::string& prefix = opt.prefix;
    const std::vector<group> groups = group_by_namespace(specs);

    // Pass one: settle every struct, delegate, and adapter name.
    script_type_registry reg(prefix);
    std::vector<std::vector<resolved_fn>> resolved;
    std::vector<std::vector<script_fn>> typed;
    for (const group& g : groups) {
        resolved.push_back(resolve_names(g));
        std::vector<script_fn> fns;
        for (const resolved_fn& r : resolved.back()) fns.push_back(plan_script_fn(reg, prefix, g, r));
        typed.push_back(std::move(fns));
    }

    std::string s = file_header(opt);
    s += "\n// Typed AngelScript wrappers for the deployed Convex functions, for the\n";
    s += "// Hazelight UnrealEngine-Angelscript fork. Put this file under the project's\n";
    s += "// Script/ folder; it needs no build step and hot-reloads. Requires the Convex\n";
    s += "// plugin. Object shapes the functions declare are script structs, decoded and\n";
    s += "// encoded by the " + prefix + "::Types namespace; a function with a declared\n";
    s += "// return takes a typed delegate. Functions with optional arguments have two\n";
    s += "// overloads: required arguments only, and every argument. Named arguments\n";
    s += "// work: Fn(Client, Name = \"hits\", By = 2.0, OnResult = Handler).\n\n";

    emit_script_structs(s, reg);
    emit_script_types_namespace(s, prefix, reg);

    for (std::size_t gi = 0; gi < groups.size(); ++gi) {
        for (std::size_t i = 0; i < resolved[gi].size(); ++i) {
            emit_script_adapters(s, prefix, *resolved[gi][i].spec, typed[gi][i]);
        }
    }

    for (std::size_t gi = 0; gi < groups.size(); ++gi) {
        const group& g = groups[gi];
        const std::vector<resolved_fn>& fns = resolved[gi];
        std::string ns = prefix;
        for (const std::string& seg : g.segments) ns += "::" + seg;

        s += "namespace " + ns + "\n{\n";
        for (std::size_t i = 0; i < fns.size(); ++i) {
            const resolved_fn& r = fns[i];
            const script_fn& sf = typed[gi][i];
            const function_spec& spec = *r.spec;
            const char* dyn_call = spec.type == function_type::query      ? "Query"
                                   : spec.type == function_type::mutation ? "Mutation"
                                                                          : "Action";
            if (i != 0) s += "\n";

            // --- One-shot ---
            s += doc_comment(spec);
            script_function(s, prefix, script_call::one_shot, r.call_name, r.plan, sf.arg_types,
                            spec.canonical_identifier, dyn_call, sf);

            if (spec.type != function_type::query) continue;

            // --- Subscription ---
            s += "\n";
            script_function(s, prefix, script_call::watch, r.watch_name, r.plan, sf.arg_types,
                            spec.canonical_identifier, dyn_call, sf);

            // --- Paginated live list ---
            if (r.paginated) {
                s += "\n\t/// Subscribes " + spec.canonical_identifier +
                     " as a growing live list (the usePaginatedQuery\n";
                s += "\t/// pattern); paginationOpts is injected by the client.\n";
                script_function(s, prefix, script_call::paginated, r.watch_paginated_name,
                                r.paginated_plan, sf.pag_arg_types, spec.canonical_identifier,
                                dyn_call, sf);
            }
        }
        s += "}\n\n";
    }

    files[prefix + ".as"] = s;
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
    emit_script(options, specs, files);
    emit_module_files(options, files);
    return files;
}

std::size_t count_functions(std::string_view api_spec_json, const emit_options& options) {
    return parse_api_spec(api_spec_json, options.include_internal).size();
}

}  // namespace convex_codegen
