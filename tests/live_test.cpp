// Live end-to-end test against the local self-hosted Convex backend used by
// convex-cpp's integration suite. Auto-skips when the backend is unreachable
// or the credentials file is absent; when the backend IS up, it must pass.

#include "cli_support.h"

#include <convex_codegen/emit.h>

#include <convex/convex.h>
#include "ixwebsocket/ixwebsocket_transport.h"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <sstream>
#include <string>

namespace fs = std::filesystem;

namespace {

std::string read_text(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream buf;
    buf << in.rdbuf();
    return buf.str();
}

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

/// Probe GET <url>/version; returns true when the backend answers.
bool backend_reachable(const std::string& url) {
    auto transport = convex::transports::make_ixwebsocket_http_transport();
    convex::http_request req;
    req.method = "GET";
    req.url = url + "/version";

    std::promise<convex::http_response> promise;
    std::future<convex::http_response> future = promise.get_future();
    transport->send(std::move(req), [&promise](convex::http_response resp) {
        promise.set_value(std::move(resp));
    });
    if (future.wait_for(std::chrono::seconds(5)) != std::future_status::ready) return false;
    const convex::http_response resp = future.get();
    return resp.transport_ok() && resp.status == 200;
}

}  // namespace

TEST(Live, FetchAndEmit) {
    const fs::path env_path(LOCAL_ENV_PATH);
    if (!fs::exists(env_path)) {
        GTEST_SKIP() << "local.env not found at " << LOCAL_ENV_PATH;
    }

    // Parse line-by-line (never shell-source: the admin key contains '|').
    const convex_codegen::cli::env_vars vars =
        convex_codegen::cli::parse_env_file(read_text(env_path));
    auto url_it = vars.find("CONVEX_LOCAL_URL");
    auto key_it = vars.find("CONVEX_LOCAL_ADMIN_KEY");
    if (url_it == vars.end() || key_it == vars.end()) {
        GTEST_SKIP() << "local.env missing CONVEX_LOCAL_URL / CONVEX_LOCAL_ADMIN_KEY";
    }
    const std::string url = url_it->second;
    const std::string key = key_it->second;

    if (!backend_reachable(url)) {
        GTEST_SKIP() << "local backend not reachable at " << url;
    }

    // Real fetch of the apiSpec over plain HTTP.
    convex::http_client client(url, convex::transports::make_ixwebsocket_http_transport());
    client.set_auth(convex::auth_token::admin(key));
    convex::function_result result = client.query("_system/cli/modules:apiSpec", {}).get();
    ASSERT_TRUE(result.ok()) << "apiSpec query failed: " << result.error_message();

    const std::string spec_json = convex::to_wire_json(result.get_value());

    convex_codegen::emit_options opts;
    opts.stamp = "// Source: live-local-backend";
    const std::map<std::string, std::string> files = convex_codegen::emit_all(spec_json, opts);

    ASSERT_TRUE(files.count("ConvexApi.h"));
    const std::string& h = files.at("ConvexApi.h");
    const std::string& c = files.at("ConvexApi.cpp");

    // The integration project deploys counters / messages / values modules.
    EXPECT_TRUE(contains(h, "namespace Counters"));
    EXPECT_TRUE(contains(h, "namespace Messages"));
    EXPECT_TRUE(contains(h, "namespace Values"));
    // counters:increment is a Mutation taking name + optional by.
    EXPECT_TRUE(contains(c, "Client.MutationNative(TEXT(\"counters:increment\")"));
    EXPECT_TRUE(contains(c, "Client.QueryNative(TEXT(\"counters:get\")"));

    // Also write into a temp dir to exercise the whole path.
    const fs::path tmp = fs::temp_directory_path() / "convex_ue_codegen_live_test";
    fs::create_directories(tmp);
    for (const auto& [name, contents] : files) {
        std::ofstream out(tmp / name, std::ios::binary | std::ios::trunc);
        out.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    }
    EXPECT_TRUE(fs::exists(tmp / "ConvexApi.h"));
}
