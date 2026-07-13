// convex-ue-codegen: fetch a Convex deployment's function spec and emit typed
// Unreal Engine wrappers. This file owns all the I/O (argv, env, filesystem,
// HTTP); the byte-for-byte emission lives in the pure core library.

#include "cli_support.h"

#include <convex_codegen/emit.h>

#include <convex/convex.h>
#include "ixwebsocket/ixwebsocket_transport.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace cli = convex_codegen::cli;

namespace {

std::optional<std::string> read_file(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::nullopt;
    std::ostringstream buf;
    buf << in.rdbuf();
    return buf.str();
}

/// Write with '\n' preserved (binary mode) so output is byte-identical across
/// platforms and matches the golden fixtures.
void write_file(const fs::path& path, const std::string& contents) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error("cannot write " + path.string());
    out.write(contents.data(), static_cast<std::streamsize>(contents.size()));
}

/// Read the requested process environment variables into an env map.
cli::env_vars process_env() {
    cli::env_vars vars;
    for (const char* name : {"CONVEX_DEPLOY_KEY", "CONVEX_DEPLOYMENT_TOKEN",
                             "CONVEX_SELF_HOSTED_ADMIN_KEY", "CONVEX_SELF_HOSTED_URL",
                             "CONVEX_URL"}) {
        if (const char* value = std::getenv(name)) {
            if (value[0] != '\0') vars.emplace(name, value);
        }
    }
    return vars;
}

/// Build the ordered env sources: process env, --env-file, then the standard
/// dotenv files in the current directory.
std::vector<cli::env_vars> gather_env_sources(const cli::cli_args& args) {
    std::vector<cli::env_vars> sources;
    sources.push_back(process_env());
    if (args.env_file) {
        if (auto contents = read_file(*args.env_file)) {
            sources.push_back(cli::parse_env_file(*contents));
        } else {
            throw cli::cli_error("cannot read --env-file " + *args.env_file);
        }
    }
    for (const char* name : {".env.local", "convex.env.local", ".env"}) {
        if (auto contents = read_file(name)) {
            sources.push_back(cli::parse_env_file(*contents));
        }
    }
    return sources;
}

/// Fetch the apiSpec JSON text from a live deployment via plain HTTP.
std::string fetch_api_spec(const std::string& url, const std::string& admin_key) {
    convex::http_client client(url, convex::transports::make_ixwebsocket_http_transport());
    client.set_auth(convex::auth_token::admin(admin_key));

    std::future<convex::function_result> future =
        client.query("_system/cli/modules:apiSpec", {});
    const convex::function_result result = future.get();
    if (!result.ok()) {
        throw cli::cli_error("apiSpec query failed: " + result.error_message());
    }
    // The apiSpec value contains only strings/bools/objects/arrays/null, so wire
    // JSON is plain JSON here; feed it straight to the core (bare-array form).
    return convex::to_wire_json(result.get_value());
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const cli::cli_args args = cli::parse_args(argc, argv);
        if (args.help) {
            std::cout << cli::usage_text();
            return 0;
        }

        convex_codegen::emit_options options;
        options.prefix = args.prefix;
        options.include_internal = args.include_internal;
        options.stamp = args.stamp;
        options.emit_module = args.emit_module;

        std::string spec_json;
        if (args.spec_json) {
            auto contents = read_file(*args.spec_json);
            if (!contents) throw cli::cli_error("cannot read --spec-json " + *args.spec_json);
            spec_json = std::move(*contents);
            options.source_label = *args.spec_json;
        } else {
            const cli::resolved_deployment dep =
                cli::resolve_deployment(args.url, args.deploy_key, gather_env_sources(args));
            options.source_label = dep.url;
            spec_json = fetch_api_spec(dep.url, dep.admin_key);
        }

        const std::map<std::string, std::string> files =
            convex_codegen::emit_all(spec_json, options);
        const std::size_t function_count =
            convex_codegen::count_functions(spec_json, options);

        const fs::path out_dir(args.out_dir);
        std::error_code ec;
        fs::create_directories(out_dir, ec);
        if (ec) throw cli::cli_error("cannot create --out directory: " + ec.message());

        for (const auto& [name, contents] : files) {
            write_file(out_dir / name, contents);
        }

        std::cout << "convex-ue-codegen: " << function_count << " function(s), " << files.size()
                  << " file(s) -> " << out_dir.string() << "\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "error: " << ex.what() << "\n";
        return 1;
    }
}
