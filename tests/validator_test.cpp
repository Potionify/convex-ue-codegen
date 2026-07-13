#include <convex_codegen/type_map.h>
#include <convex_codegen/validator.h>

#include <gtest/gtest.h>

using namespace convex_codegen;

namespace {

validator_ptr parse(const char* json) { return parse_validator_json(json); }

mapped_type map(const char* json) { return map_type(parse(json)); }

}  // namespace

TEST(Validator, ScalarKinds) {
    EXPECT_EQ(parse(R"({"type":"string"})")->kind, validator_kind::string);
    EXPECT_EQ(parse(R"({"type":"number"})")->kind, validator_kind::number);
    EXPECT_EQ(parse(R"({"type":"float64"})")->kind, validator_kind::number);
    EXPECT_EQ(parse(R"({"type":"boolean"})")->kind, validator_kind::boolean);
    EXPECT_EQ(parse(R"({"type":"int64"})")->kind, validator_kind::int64);
    EXPECT_EQ(parse(R"({"type":"bigint"})")->kind, validator_kind::int64);
    EXPECT_EQ(parse(R"({"type":"null"})")->kind, validator_kind::null);
    EXPECT_EQ(parse(R"({"type":"bytes"})")->kind, validator_kind::bytes);
    EXPECT_EQ(parse(R"({"type":"any"})")->kind, validator_kind::any);
}

TEST(Validator, NullOrMissingBecomesAny) {
    EXPECT_EQ(parse_validator_json("")->kind, validator_kind::any);
    EXPECT_EQ(parse("null")->kind, validator_kind::any);
    EXPECT_EQ(parse(R"({"noType":true})")->kind, validator_kind::any);
}

TEST(Validator, UnknownTypeDegradesToAny) {
    EXPECT_EQ(parse(R"({"type":"someFutureType"})")->kind, validator_kind::any);
}

TEST(Validator, IdCarriesTableName) {
    validator_ptr v = parse(R"({"type":"id","tableName":"documents"})");
    EXPECT_EQ(v->kind, validator_kind::id);
    EXPECT_EQ(v->id_table, "documents");
}

TEST(Validator, LiteralBase) {
    EXPECT_EQ(parse(R"({"type":"literal","value":"hi"})")->literal_kind,
              validator::literal_base::string);
    EXPECT_EQ(parse(R"({"type":"literal","value":42})")->literal_kind,
              validator::literal_base::number);
    EXPECT_EQ(parse(R"({"type":"literal","value":true})")->literal_kind,
              validator::literal_base::boolean);
}

TEST(Validator, ObjectFieldsSortedWithOptionalFlag) {
    validator_ptr v = parse(
        R"({"type":"object","value":{
            "b":{"fieldType":{"type":"string"},"optional":true},
            "a":{"fieldType":{"type":"number"},"optional":false}}})");
    ASSERT_EQ(v->fields.size(), 2u);
    EXPECT_EQ(v->fields[0].name, "a");
    EXPECT_FALSE(v->fields[0].optional);
    EXPECT_EQ(v->fields[1].name, "b");
    EXPECT_TRUE(v->fields[1].optional);
}

// ---- type mapping --------------------------------------------------------

TEST(TypeMap, Scalars) {
    EXPECT_EQ(map(R"({"type":"string"})").param, "const FString&");
    EXPECT_EQ(map(R"({"type":"number"})").param, "double");
    EXPECT_EQ(map(R"({"type":"float64"})").param, "double");
    EXPECT_EQ(map(R"({"type":"int64"})").param, "int64");
    EXPECT_EQ(map(R"({"type":"bigint"})").param, "int64");
    EXPECT_EQ(map(R"({"type":"boolean"})").param, "bool");
    EXPECT_EQ(map(R"({"type":"bytes"})").param, "const TArray<uint8>&");
}

TEST(TypeMap, IdIsStringWithTable) {
    mapped_type m = map(R"({"type":"id","tableName":"users"})");
    EXPECT_EQ(m.param, "const FString&");
    EXPECT_EQ(m.id_table, "users");
}

TEST(TypeMap, LiteralUsesBaseType) {
    EXPECT_EQ(map(R"({"type":"literal","value":"x"})").param, "const FString&");
    EXPECT_EQ(map(R"({"type":"literal","value":7})").param, "double");
    EXPECT_EQ(map(R"({"type":"literal","value":false})").param, "bool");
}

TEST(TypeMap, ScalarArrays) {
    EXPECT_EQ(map(R"({"type":"array","value":{"type":"string"}})").param, "const TArray<FString>&");
    EXPECT_EQ(map(R"({"type":"array","value":{"type":"number"}})").param, "const TArray<double>&");
    EXPECT_EQ(map(R"({"type":"array","value":{"type":"int64"}})").param, "const TArray<int64>&");
    EXPECT_EQ(map(R"({"type":"array","value":{"type":"boolean"}})").param, "const TArray<bool>&");
}

TEST(TypeMap, NonScalarArraysAreConvexArrays) {
    EXPECT_EQ(map(R"({"type":"array","value":{"type":"object","value":{}}})").param,
              "const TArray<FConvexValue>&");
    // array<bytes> is not one of the four scalar element types.
    EXPECT_EQ(map(R"({"type":"array","value":{"type":"bytes"}})").param,
              "const TArray<FConvexValue>&");
}

TEST(TypeMap, CompoundsAreConvexValue) {
    EXPECT_EQ(map(R"({"type":"object","value":{}})").param, "const FConvexValue&");
    EXPECT_EQ(map(R"({"type":"record","keys":{"type":"string"},"values":{"type":"number"}})").param,
              "const FConvexValue&");
    EXPECT_EQ(map(R"({"type":"union","value":[{"type":"string"}]})").param, "const FConvexValue&");
    EXPECT_EQ(map(R"({"type":"any"})").param, "const FConvexValue&");
    EXPECT_EQ(map(R"({"type":"null"})").param, "const FConvexValue&");
}

TEST(Validator, Summaries) {
    EXPECT_EQ(summarize(parse(R"({"type":"array","value":{"type":"string"}})")), "array<string>");
    EXPECT_EQ(summarize(parse(R"({"type":"id","tableName":"docs"})")), "id<docs>");
    EXPECT_EQ(summarize(parse(R"({"type":"literal","value":"hi"})")), "literal(\"hi\")");
    EXPECT_EQ(summarize(parse(R"({"type":"union","value":[{"type":"string"},{"type":"number"}]})")),
              "union<string | number>");
}
