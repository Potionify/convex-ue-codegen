#include <convex_codegen/api_spec.h>

#include <gtest/gtest.h>

using namespace convex_codegen;

namespace {

const char* kBareArray = R"([
  {"identifier":"counters.js:get","functionType":"Query","visibility":{"kind":"public"},"args":null,"returns":null}
])";

const char* kEnvelope = R"({"status":"success","value":[
  {"identifier":"counters.js:get","functionType":"Query","visibility":{"kind":"public"},"args":null,"returns":null}
]})";

}  // namespace

TEST(ApiSpec, AcceptsBareArray) {
    auto specs = parse_api_spec(kBareArray, false);
    ASSERT_EQ(specs.size(), 1u);
    EXPECT_EQ(specs[0].canonical_identifier, "counters:get");
    EXPECT_EQ(specs[0].module_path, "counters");
    EXPECT_EQ(specs[0].function_name, "get");
}

TEST(ApiSpec, AcceptsEnvelope) {
    auto specs = parse_api_spec(kEnvelope, false);
    ASSERT_EQ(specs.size(), 1u);
    EXPECT_EQ(specs[0].canonical_identifier, "counters:get");
}

TEST(ApiSpec, EnvelopeAndArrayAgree) {
    EXPECT_EQ(parse_api_spec(kBareArray, false).size(), parse_api_spec(kEnvelope, false).size());
}

TEST(ApiSpec, RejectsErrorEnvelope) {
    EXPECT_THROW(parse_api_spec(R"({"status":"error","errorMessage":"bad key"})", false),
                 codegen_error);
}

TEST(ApiSpec, RejectsMalformedJson) {
    EXPECT_THROW(parse_api_spec("{not json", false), codegen_error);
}

TEST(ApiSpec, SkipsHttpActionsSystemAndInternal) {
    const char* spec = R"([
      {"identifier":"a.js:pub","functionType":"Query","visibility":{"kind":"public"},"args":null,"returns":null},
      {"identifier":"a.js:priv","functionType":"Query","visibility":{"kind":"internal"},"args":null,"returns":null},
      {"identifier":"_system/cli/modules.js:apiSpec","functionType":"Query","visibility":{"kind":"public"},"args":null,"returns":null},
      {"functionType":"HttpAction","method":"GET","path":"/x"}
    ])";
    auto specs = parse_api_spec(spec, false);
    ASSERT_EQ(specs.size(), 1u);
    EXPECT_EQ(specs[0].function_name, "pub");
}

TEST(ApiSpec, IncludeInternalKeepsInternal) {
    const char* spec = R"([
      {"identifier":"a.js:pub","functionType":"Query","visibility":{"kind":"public"},"args":null,"returns":null},
      {"identifier":"a.js:priv","functionType":"Query","visibility":{"kind":"internal"},"args":null,"returns":null}
    ])";
    EXPECT_EQ(parse_api_spec(spec, true).size(), 2u);
}

TEST(ApiSpec, SortedByCanonicalIdentifier) {
    const char* spec = R"([
      {"identifier":"z.js:a","functionType":"Query","visibility":{"kind":"public"},"args":null,"returns":null},
      {"identifier":"a.js:z","functionType":"Query","visibility":{"kind":"public"},"args":null,"returns":null}
    ])";
    auto specs = parse_api_spec(spec, false);
    ASSERT_EQ(specs.size(), 2u);
    EXPECT_EQ(specs[0].canonical_identifier, "a:z");
    EXPECT_EQ(specs[1].canonical_identifier, "z:a");
}
