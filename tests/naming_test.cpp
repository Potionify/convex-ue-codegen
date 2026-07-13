#include <convex_codegen/naming.h>

#include <gtest/gtest.h>

using namespace convex_codegen;

TEST(Naming, CanonicalizeStripsJsExtension) {
    EXPECT_EQ(canonicalize_identifier("counters.js:get"), "counters:get");
    EXPECT_EQ(canonicalize_identifier("dir/mod.js:fn"), "dir/mod:fn");
    EXPECT_EQ(canonicalize_identifier("admin/tools.js:reset"), "admin/tools:reset");
}

TEST(Naming, CanonicalizeHandlesMissingFunction) {
    EXPECT_EQ(canonicalize_identifier("module.js"), "module:default");
    EXPECT_EQ(canonicalize_identifier("module.js:"), "module:default");
}

TEST(Naming, SplitModuleFunction) {
    const split_identifier s = split_module_function("dir/mod.js:fn");
    EXPECT_EQ(s.module_path, "dir/mod");
    EXPECT_EQ(s.function_name, "fn");
}

TEST(Naming, PascalCaseBasics) {
    EXPECT_EQ(pascal_case("get"), "Get");
    EXPECT_EQ(pascal_case("echoQuery"), "EchoQuery");
    EXPECT_EQ(pascal_case("clearAll"), "ClearAll");
    EXPECT_EQ(pascal_case("foo_bar"), "FooBar");
    EXPECT_EQ(pascal_case("fooBar"), "FooBar");
}

TEST(Naming, PascalCaseSanitizes) {
    // Non-alnum removed, following char capitalized.
    EXPECT_EQ(pascal_case("a-b.c"), "ABC");
    EXPECT_EQ(pascal_case("hello world"), "HelloWorld");
    // Leading digit gets a '_' prefix.
    EXPECT_EQ(pascal_case("2fa"), "_2fa");
    // Empty / all-symbol input degrades to "_".
    EXPECT_EQ(pascal_case(""), "_");
    EXPECT_EQ(pascal_case("!!!"), "_");
}

TEST(Naming, NamespaceSegments) {
    const std::vector<std::string> segs = module_namespace_segments("admin/tools");
    ASSERT_EQ(segs.size(), 2u);
    EXPECT_EQ(segs[0], "Admin");
    EXPECT_EQ(segs[1], "Tools");
}

TEST(Naming, ModuleFlatConcatenates) {
    EXPECT_EQ(module_flat("admin/tools"), "AdminTools");
    EXPECT_EQ(module_flat("counters"), "Counters");
    // The collision pair flattens to the same class fragment.
    EXPECT_EQ(module_flat("foo_bar"), module_flat("fooBar"));
}
