#include <convex_codegen/emit.h>

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>

namespace fs = std::filesystem;

namespace {

// Fixed provenance line so output is byte-stable. Keep in sync with the value
// documented in README ("Regenerating golden files").
constexpr const char* kStamp = "// Source: apispec_full.json (golden fixture)";

std::string read_text(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream buf;
    buf << in.rdbuf();
    return buf.str();
}

void write_text(const fs::path& path, const std::string& contents) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(contents.data(), static_cast<std::streamsize>(contents.size()));
}

std::map<std::string, std::string> emit_fixture() {
    const std::string spec = read_text(FIXTURE_PATH);
    convex_codegen::emit_options opts;
    opts.prefix = "ConvexApi";
    opts.stamp = kStamp;
    opts.emit_script = true;
    return convex_codegen::emit_all(spec, opts);
}

}  // namespace

TEST(Golden, ByteExactAgainstExpected) {
    const std::map<std::string, std::string> files = emit_fixture();
    const fs::path expected_dir(EXPECTED_DIR);

    const bool regenerate = std::getenv("CODEGEN_REGENERATE") != nullptr;
    if (regenerate) {
        fs::create_directories(expected_dir);
        for (const auto& [name, contents] : files) {
            write_text(expected_dir / name, contents);
        }
        GTEST_SKIP() << "Regenerated " << files.size() << " expected files in " << EXPECTED_DIR;
    }

    ASSERT_FALSE(files.empty());
    for (const auto& [name, contents] : files) {
        const fs::path expected_path = expected_dir / name;
        ASSERT_TRUE(fs::exists(expected_path))
            << "missing expected file " << expected_path.string()
            << " (run with CODEGEN_REGENERATE=1 to create)";
        EXPECT_EQ(contents, read_text(expected_path)) << "content mismatch for " << name;
    }
}

TEST(Golden, DeterministicAcrossRuns) {
    EXPECT_EQ(emit_fixture(), emit_fixture());
}

TEST(Golden, EmitsExpectedFileSet) {
    const std::map<std::string, std::string> files = emit_fixture();
    EXPECT_TRUE(files.count("ConvexApi.h"));
    EXPECT_TRUE(files.count("ConvexApi.cpp"));
    EXPECT_TRUE(files.count("ConvexApiBP.h"));
    EXPECT_TRUE(files.count("ConvexApiBP.cpp"));
    EXPECT_TRUE(files.count("ConvexApi.as"));
    EXPECT_EQ(files.size(), 5u);
}
