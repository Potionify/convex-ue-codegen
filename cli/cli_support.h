#pragma once

// Argument parsing and Convex-CLI-style deployment resolution. Deliberately
// free of networking and filesystem side effects so it can be unit-tested:
// env sources are passed in as already-parsed maps, and the process env / .env
// files are gathered by the CLI main and handed here.

#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace convex_codegen::cli {

/// Thrown on invalid usage or unresolvable deployment. The message is intended
/// to be printed to stderr as-is.
class cli_error : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

/// Parsed command-line options.
struct cli_args {
    std::string out_dir;
    std::optional<std::string> url;
    std::optional<std::string> deploy_key;
    std::optional<std::string> env_file;
    std::optional<std::string> spec_json;  ///< offline mode: read spec from file
    std::string prefix = "ConvexApi";
    bool include_internal = false;
    std::optional<std::string> stamp;
    std::optional<std::string> emit_module;
    bool help = false;
};

/// Parse argv (argv[0] is the program name). Throws cli_error on bad usage.
cli_args parse_args(int argc, const char* const* argv);

/// The --help / usage text.
std::string usage_text();

/// A parsed set of environment variables (from the process env or a .env file).
using env_vars = std::map<std::string, std::string>;

/// Parse .env file contents (KEY=VALUE lines). Handles `export ` prefixes,
/// `#` comments (whole-line and inline after unquoted values), and single/
/// double quoted values. Never shell-expands: values containing '|' or '$'
/// are taken verbatim.
env_vars parse_env_file(const std::string& contents);

/// The resolved deployment the CLI will fetch from.
struct resolved_deployment {
    std::string url;
    std::string admin_key;
};

/// Resolve the deployment URL and admin key. Explicit values win; otherwise
/// `env_sources` are consulted in priority order (first source defining a key
/// wins). Throws cli_error with a clear message when nothing resolves.
resolved_deployment resolve_deployment(const std::optional<std::string>& explicit_url,
                                       const std::optional<std::string>& explicit_key,
                                       const std::vector<env_vars>& env_sources);

/// Extract the deployment name from a deploy key: the text between the first
/// ':' and the '|'. Returns nullopt for keys without that shape (e.g.
/// self-hosted keys like "convex-integration|...").
std::optional<std::string> deployment_name_from_key(const std::string& key);

}  // namespace convex_codegen::cli
