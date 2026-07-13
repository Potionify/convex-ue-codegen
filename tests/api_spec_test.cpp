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

// ---- paginated-query detection -------------------------------------------

namespace {

// A Query whose paginationOpts holds `fields` (a JSON object body). Wrapped in
// a channel arg so the args object has a non-pagination field too.
function_spec paginated_spec(const char* pagination_fields) {
    std::string spec = std::string(R"([{
      "identifier":"m.js:f","functionType":"Query","visibility":{"kind":"public"},
      "args":{"type":"object","value":{
        "channel":{"fieldType":{"type":"string"},"optional":false},
        "paginationOpts":{"fieldType":{"type":"object","value":)") +
                       pagination_fields + R"(},"optional":false}}},"returns":{"type":"any"}}])";
    auto specs = parse_api_spec(spec, false);
    return specs.at(0);
}

}  // namespace

TEST(Paginated, DetectsFullPaginationOptsShape) {
    // The convex-js paginationOptsValidator shape: numItems + cursor plus the
    // optional endCursor/id/maximumRowsRead/maximumBytesRead.
    EXPECT_TRUE(is_paginated_query(paginated_spec(R"({
      "numItems":{"fieldType":{"type":"number"},"optional":false},
      "cursor":{"fieldType":{"type":"union","value":[{"type":"string"},{"type":"null"}]},"optional":false},
      "endCursor":{"fieldType":{"type":"union","value":[{"type":"string"},{"type":"null"}]},"optional":true},
      "id":{"fieldType":{"type":"number"},"optional":true},
      "maximumRowsRead":{"fieldType":{"type":"number"},"optional":true},
      "maximumBytesRead":{"fieldType":{"type":"number"},"optional":true}})")));
}

TEST(Paginated, DetectsMinimalNumItemsAndCursorOnly) {
    // The optional fields are not required for detection.
    EXPECT_TRUE(is_paginated_query(paginated_spec(R"({
      "numItems":{"fieldType":{"type":"number"},"optional":false},
      "cursor":{"fieldType":{"type":"string"},"optional":false}})")));
}

TEST(Paginated, RejectsMissingCursor) {
    EXPECT_FALSE(is_paginated_query(paginated_spec(R"({
      "numItems":{"fieldType":{"type":"number"},"optional":false}})")));
}

TEST(Paginated, RejectsMissingNumItems) {
    EXPECT_FALSE(is_paginated_query(paginated_spec(R"({
      "cursor":{"fieldType":{"type":"string"},"optional":false}})")));
}

TEST(Paginated, RejectsPaginationOptsThatIsNotAnObject) {
    // paginationOpts present but not an object validator -> not paginated.
    const char* spec = R"([{
      "identifier":"m.js:f","functionType":"Query","visibility":{"kind":"public"},
      "args":{"type":"object","value":{
        "paginationOpts":{"fieldType":{"type":"string"},"optional":false}}},"returns":{"type":"any"}}])";
    EXPECT_FALSE(is_paginated_query(parse_api_spec(spec, false).at(0)));
}

TEST(Paginated, RejectsNonQueryEvenWithMatchingShape) {
    const char* spec = R"([{
      "identifier":"m.js:f","functionType":"Mutation","visibility":{"kind":"public"},
      "args":{"type":"object","value":{
        "paginationOpts":{"fieldType":{"type":"object","value":{
          "numItems":{"fieldType":{"type":"number"},"optional":false},
          "cursor":{"fieldType":{"type":"string"},"optional":false}}},"optional":false}}},
      "returns":{"type":"any"}}])";
    EXPECT_FALSE(is_paginated_query(parse_api_spec(spec, false).at(0)));
}

TEST(Paginated, RejectsQueryWithoutPaginationOpts) {
    const char* spec = R"([{
      "identifier":"m.js:f","functionType":"Query","visibility":{"kind":"public"},
      "args":{"type":"object","value":{
        "numItems":{"fieldType":{"type":"number"},"optional":false},
        "cursor":{"fieldType":{"type":"string"},"optional":false}}},"returns":{"type":"any"}}])";
    EXPECT_FALSE(is_paginated_query(parse_api_spec(spec, false).at(0)));
}
