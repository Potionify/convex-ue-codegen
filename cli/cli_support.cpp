#include "cli_support.h"

#include <cctype>
#include <sstream>

namespace convex_codegen::cli {

namespace {

std::string trim(const std::string& s) {
    std::size_t begin = 0;
    std::size_t end = s.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(s[begin]))) ++begin;
    while (end > begin && std::isspace(static_cast<unsigned char>(s[end - 1]))) --end;
    return s.substr(begin, end - begin);
}

/// The value of the first source that defines `name` with a non-empty value.
std::optional<std::string> lookup(const std::vector<env_vars>& sources, const std::string& name) {
    for (const env_vars& src : sources) {
        auto it = src.find(name);
        if (it != src.end() && !it->second.empty()) return it->second;
    }
    return std::nullopt;
}

/// First non-empty of several candidate variable names, in priority order.
std::optional<std::string> lookup_first(const std::vector<env_vars>& sources,
                                        std::initializer_list<const char*> names) {
    for (const char* name : names) {
        if (auto v = lookup(sources, name)) return v;
    }
    return std::nullopt;
}

std::string require_value(int argc, const char* const* argv, int& i, const std::string& flag) {
    if (i + 1 >= argc) throw cli_error("missing value for " + flag);
    return argv[++i];
}

}  // namespace

cli_args parse_args(int argc, const char* const* argv) {
    cli_args out;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            out.help = true;
        } else if (arg == "--out") {
            out.out_dir = require_value(argc, argv, i, arg);
        } else if (arg == "--url") {
            out.url = require_value(argc, argv, i, arg);
        } else if (arg == "--deploy-key") {
            out.deploy_key = require_value(argc, argv, i, arg);
        } else if (arg == "--env-file") {
            out.env_file = require_value(argc, argv, i, arg);
        } else if (arg == "--spec-json") {
            out.spec_json = require_value(argc, argv, i, arg);
        } else if (arg == "--prefix") {
            out.prefix = require_value(argc, argv, i, arg);
        } else if (arg == "--include-internal") {
            out.include_internal = true;
        } else if (arg == "--stamp") {
            out.stamp = require_value(argc, argv, i, arg);
        } else if (arg == "--emit-module") {
            out.emit_module = require_value(argc, argv, i, arg);
        } else {
            throw cli_error("unknown argument: " + arg);
        }
    }
    if (!out.help) {
        if (out.out_dir.empty()) throw cli_error("--out <dir> is required");
        if (out.prefix.empty()) throw cli_error("--prefix must not be empty");
    }
    return out;
}

std::string usage_text() {
    return
        "convex-ue-codegen - generate typed Unreal Engine wrappers for a Convex deployment\n"
        "\n"
        "Usage:\n"
        "  convex-ue-codegen --out <dir> [options]\n"
        "\n"
        "Data source (pick one):\n"
        "  --spec-json <file>     Offline: read the apiSpec from a file (value array or\n"
        "                         {\"status\",\"value\"} envelope). No network.\n"
        "  --url <u>              Deployment URL (e.g. https://x.convex.cloud).\n"
        "  --deploy-key <k>       Admin/deploy key (Authorization: Convex <k>).\n"
        "\n"
        "Options:\n"
        "  --out <dir>            Output directory (required). Created if missing.\n"
        "  --env-file <path>      Extra .env file to consult during resolution.\n"
        "  --prefix <name>        Generated API prefix (default: ConvexApi).\n"
        "  --include-internal     Also emit internal-visibility functions.\n"
        "  --stamp <text>         Verbatim provenance line for every file (determinism).\n"
        "  --emit-module <Name>   Also emit <Name>.Build.cs and <Name>Module.cpp.\n"
        "  -h, --help             Show this help.\n"
        "\n"
        "Deployment resolution (when --spec-json is not given):\n"
        "  Explicit --url/--deploy-key win. Otherwise, per key, the first of these that\n"
        "  defines it wins: process env, --env-file, ./.env.local, ./convex.env.local,\n"
        "  ./.env. Keys: CONVEX_DEPLOY_KEY (alias CONVEX_DEPLOYMENT_TOKEN),\n"
        "  CONVEX_SELF_HOSTED_URL + CONVEX_SELF_HOSTED_ADMIN_KEY, CONVEX_URL. When no URL\n"
        "  is set, https://<name>.convex.cloud is synthesized from the deploy-key name.\n";
}

env_vars parse_env_file(const std::string& contents) {
    env_vars vars;
    std::istringstream stream(contents);
    std::string line;
    while (std::getline(stream, line)) {
        // Strip a trailing CR so CRLF files parse cleanly.
        if (!line.empty() && line.back() == '\r') line.pop_back();

        std::string trimmed = trim(line);
        if (trimmed.empty() || trimmed[0] == '#') continue;
        if (trimmed.rfind("export ", 0) == 0) trimmed = trim(trimmed.substr(7));

        const std::size_t eq = trimmed.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = trim(trimmed.substr(0, eq));
        if (key.empty()) continue;
        std::string value = trim(trimmed.substr(eq + 1));

        if (!value.empty() && (value.front() == '"' || value.front() == '\'')) {
            const char quote = value.front();
            const std::size_t close = value.find(quote, 1);
            value = (close == std::string::npos) ? value.substr(1)
                                                 : value.substr(1, close - 1);
        } else {
            // Strip an inline comment introduced by whitespace + '#'. This never
            // affects a '|'-bearing admin key (no spaces before its content).
            const std::size_t hash = value.find(" #");
            if (hash != std::string::npos) value = trim(value.substr(0, hash));
        }
        vars.emplace(key, value);
    }
    return vars;
}

std::optional<std::string> deployment_name_from_key(const std::string& key) {
    const std::size_t colon = key.find(':');
    const std::size_t bar = key.find('|');
    if (colon == std::string::npos || bar == std::string::npos || colon >= bar) {
        return std::nullopt;
    }
    std::string name = key.substr(colon + 1, bar - colon - 1);
    if (name.empty()) return std::nullopt;
    return name;
}

resolved_deployment resolve_deployment(const std::optional<std::string>& explicit_url,
                                       const std::optional<std::string>& explicit_key,
                                       const std::vector<env_vars>& env_sources) {
    resolved_deployment out;

    // Admin/deploy key: explicit wins, then CONVEX_DEPLOY_KEY, its alias, then
    // the self-hosted admin key.
    if (explicit_key) {
        out.admin_key = *explicit_key;
    } else if (auto key = lookup_first(env_sources, {"CONVEX_DEPLOY_KEY", "CONVEX_DEPLOYMENT_TOKEN",
                                                     "CONVEX_SELF_HOSTED_ADMIN_KEY"})) {
        out.admin_key = *key;
    } else {
        throw cli_error(
            "no deploy key: pass --deploy-key, or set CONVEX_DEPLOY_KEY "
            "(alias CONVEX_DEPLOYMENT_TOKEN) or CONVEX_SELF_HOSTED_ADMIN_KEY");
    }

    // URL: explicit wins, then self-hosted URL, then CONVEX_URL, then synthesize
    // from the deploy-key name.
    if (explicit_url) {
        out.url = *explicit_url;
    } else if (auto url = lookup_first(env_sources, {"CONVEX_SELF_HOSTED_URL", "CONVEX_URL"})) {
        out.url = *url;
    } else if (auto name = deployment_name_from_key(out.admin_key)) {
        out.url = "https://" + *name + ".convex.cloud";
    } else {
        throw cli_error(
            "no deployment URL: pass --url, or set CONVEX_URL / CONVEX_SELF_HOSTED_URL "
            "(the deploy key has no embedded deployment name to synthesize one)");
    }

    return out;
}

}  // namespace convex_codegen::cli
