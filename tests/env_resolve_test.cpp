#include "cli_support.h"

#include <gtest/gtest.h>

using namespace convex_codegen::cli;

TEST(EnvResolve, ExplicitArgsWin) {
    env_vars env{{"CONVEX_URL", "https://from-env.convex.cloud"},
                 {"CONVEX_DEPLOY_KEY", "dev:from-env|zzz"}};
    resolved_deployment d =
        resolve_deployment("https://explicit.convex.cloud", "explicit-key", {env});
    EXPECT_EQ(d.url, "https://explicit.convex.cloud");
    EXPECT_EQ(d.admin_key, "explicit-key");
}

TEST(EnvResolve, DeployKeyAliasAndPrecedence) {
    // CONVEX_DEPLOY_KEY wins over CONVEX_DEPLOYMENT_TOKEN.
    env_vars env{{"CONVEX_DEPLOY_KEY", "dev:winner|aaa"},
                 {"CONVEX_DEPLOYMENT_TOKEN", "dev:loser|bbb"}};
    resolved_deployment d = resolve_deployment(std::nullopt, std::nullopt, {env});
    EXPECT_EQ(d.admin_key, "dev:winner|aaa");
    EXPECT_EQ(d.url, "https://winner.convex.cloud");
}

TEST(EnvResolve, TokenAliasUsedWhenNoDeployKey) {
    env_vars env{{"CONVEX_DEPLOYMENT_TOKEN", "dev:tok|bbb"}};
    resolved_deployment d = resolve_deployment(std::nullopt, std::nullopt, {env});
    EXPECT_EQ(d.admin_key, "dev:tok|bbb");
}

TEST(EnvResolve, FirstSourceWinsPerKey) {
    env_vars process{{"CONVEX_DEPLOY_KEY", "dev:proc|aaa"}};
    env_vars file{{"CONVEX_DEPLOY_KEY", "dev:file|bbb"}, {"CONVEX_URL", "https://file.convex.cloud"}};
    // process env is first in the list; it wins for the shared key, but the
    // URL only defined in the file source is still picked up.
    resolved_deployment d = resolve_deployment(std::nullopt, std::nullopt, {process, file});
    EXPECT_EQ(d.admin_key, "dev:proc|aaa");
    EXPECT_EQ(d.url, "https://file.convex.cloud");
}

TEST(EnvResolve, SelfHostedPair) {
    env_vars env{{"CONVEX_SELF_HOSTED_URL", "http://127.0.0.1:3210"},
                 {"CONVEX_SELF_HOSTED_ADMIN_KEY", "convex-integration|abc"}};
    resolved_deployment d = resolve_deployment(std::nullopt, std::nullopt, {env});
    EXPECT_EQ(d.url, "http://127.0.0.1:3210");
    EXPECT_EQ(d.admin_key, "convex-integration|abc");
}

TEST(EnvResolve, SelfHostedUrlPreferredOverConvexUrl) {
    env_vars env{{"CONVEX_SELF_HOSTED_URL", "http://self"},
                 {"CONVEX_URL", "https://cloud.convex.cloud"},
                 {"CONVEX_DEPLOY_KEY", "dev:x|k"}};
    resolved_deployment d = resolve_deployment(std::nullopt, std::nullopt, {env});
    EXPECT_EQ(d.url, "http://self");
}

TEST(EnvResolve, SynthesizesUrlFromKeyName) {
    env_vars env{{"CONVEX_DEPLOY_KEY", "prod:happy-animal-123|secret"}};
    resolved_deployment d = resolve_deployment(std::nullopt, std::nullopt, {env});
    EXPECT_EQ(d.url, "https://happy-animal-123.convex.cloud");
}

TEST(EnvResolve, ThrowsWhenNoKey) {
    EXPECT_THROW(resolve_deployment(std::nullopt, std::nullopt, {env_vars{}}), cli_error);
}

TEST(EnvResolve, ThrowsWhenNoUrlAndUnprefixedKey) {
    // Self-hosted-style key has no embedded name; without a URL we cannot
    // synthesize one.
    env_vars env{{"CONVEX_DEPLOY_KEY", "convex-integration|abc"}};
    EXPECT_THROW(resolve_deployment(std::nullopt, std::nullopt, {env}), cli_error);
}

TEST(EnvResolve, DeploymentNameParsing) {
    EXPECT_EQ(deployment_name_from_key("dev:tall-forest-1234|xyz"), "tall-forest-1234");
    EXPECT_EQ(deployment_name_from_key("prod:name|xyz"), "name");
    EXPECT_FALSE(deployment_name_from_key("convex-integration|xyz").has_value());
    EXPECT_FALSE(deployment_name_from_key("noseparators").has_value());
}

// ---- .env file parsing ---------------------------------------------------

TEST(EnvFile, ParsesKeyValueCommentsAndExport) {
    const std::string contents =
        "# a comment\n"
        "\n"
        "export CONVEX_URL=https://x.convex.cloud\n"
        "CONVEX_DEPLOYMENT=dev:disciplined-cow-12 # team: potionify\n"
        "QUOTED=\"quoted value\"\n";
    env_vars v = parse_env_file(contents);
    EXPECT_EQ(v["CONVEX_URL"], "https://x.convex.cloud");
    EXPECT_EQ(v["CONVEX_DEPLOYMENT"], "dev:disciplined-cow-12");
    EXPECT_EQ(v["QUOTED"], "quoted value");
}

TEST(EnvFile, PreservesPipeInAdminKey) {
    // The local self-hosted admin key contains '|'; it must survive verbatim
    // (never shell-sourced).
    const std::string contents =
        "CONVEX_LOCAL_ADMIN_KEY=convex-integration|019b0c6d58bb8dfce754f95d728178922f05\n";
    env_vars v = parse_env_file(contents);
    EXPECT_EQ(v["CONVEX_LOCAL_ADMIN_KEY"],
              "convex-integration|019b0c6d58bb8dfce754f95d728178922f05");
}
