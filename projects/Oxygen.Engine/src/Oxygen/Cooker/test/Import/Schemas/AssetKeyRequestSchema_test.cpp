//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Schemas/oxygen.asset-key-request.schema.json

#include <string>
#include <string_view>
#include <utility>

#include <nlohmann/json.hpp>

#include <Oxygen/Cooker/test/Support/JsonSchema.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using nlohmann::json;
using oxygen::cooker::test::SchemaCase;

auto Schema() -> const json&
{
  static const auto schema = oxygen::cooker::test::LoadSchema(
    "Import/Schemas/oxygen.asset-key-request.schema.json");
  return schema;
}

auto Accepts(const std::string_view name, const json& document) -> SchemaCase
{
  return SchemaCase {
    .name = name,
    .json = document.dump(),
    .valid = true,
    .error_substr = "",
  };
}

auto Rejects(const std::string_view name, const json& document,
  const std::string_view error) -> SchemaCase
{
  return SchemaCase {
    .name = name,
    .json = document.dump(),
    .valid = false,
    .error_substr = error,
  };
}

auto With(json document, const std::string_view pointer, json value) -> json
{
  document[json::json_pointer { std::string { pointer } }] = std::move(value);
  return document;
}

auto Without(json document, const std::string_view pointer) -> json
{
  const auto at = json::json_pointer { std::string { pointer } };
  document[at.parent_pointer()].erase(at.back());
  return document;
}

//! The schema identifier the schema owns.
auto Identifier() -> std::string
{
  return Schema()
    .at(json::json_pointer { "/properties/schema/const" })
    .get<std::string>();
}

auto Valid() -> json
{
  return json {
    { "schema", Identifier() },
    {
      "virtual_paths",
      json::array(
        { "/Content/Materials/Brick.omat", "/Content/Textures/Brick.otex" }),
    },
  };
}

auto WithPath(const std::string& path) -> json
{
  return With(Valid(), "/virtual_paths", json::array({ path }));
}

class AssetKeyRequestSchemaTest : public ::testing::TestWithParam<SchemaCase> {
};

NOLINT_TEST_P(AssetKeyRequestSchemaTest, ValidatesDocument)
{
  oxygen::cooker::test::ExpectSchemaCase(Schema(), GetParam());
}

constexpr auto kPath = "/virtual_paths/0: ";

INSTANTIATE_TEST_SUITE_P(Cases, AssetKeyRequestSchemaTest,
  ::testing::Values(
    Accepts("AcceptsEmptyRequest",
      json { { "schema", Identifier() }, { "virtual_paths", json::array() } }),
    Accepts("AcceptsTwoPaths", Valid()),
    Accepts("AcceptsTwoSegmentPath", WithPath("/Content/Brick")),
    Accepts("AcceptsPathWithSpaces", WithPath("/Content/Sniper Rifle/Paint")),

    Rejects("RejectsMissingSchema", Without(Valid(), "/schema"), "'schema'"),
    Rejects("RejectsMissingVirtualPaths", Without(Valid(), "/virtual_paths"),
      "'virtual_paths'"),
    Rejects(
      "RejectsUnknownField", With(Valid(), "/unknown", true), "'unknown'"),

    Rejects("RejectsOtherSchemaIdentifier",
      With(Valid(), "/schema", Identifier() + "x"), "/schema: "),
    Rejects("RejectsNonStringSchema", With(Valid(), "/schema", 1), "/schema: "),
    Rejects("RejectsNonArrayVirtualPaths", With(Valid(), "/virtual_paths", "x"),
      "/virtual_paths: "),
    Rejects("RejectsNonStringPath",
      With(Valid(), "/virtual_paths", json::array({ 7 })), kPath),
    Rejects("RejectsDuplicatePaths",
      With(Valid(), "/virtual_paths",
        json::array({ "/Content/Brick", "/Content/Brick" })),
      "/virtual_paths: "),

    Rejects("RejectsSingleSegmentPath", WithPath("/Content"), kPath),
    Rejects("RejectsRelativePath", WithPath("Content/Brick"), kPath),
    Rejects("RejectsEmptyPath", WithPath(""), kPath),
    Rejects("RejectsEmptySegment", WithPath("/Content//Brick"), kPath),
    Rejects("RejectsTrailingSlash", WithPath("/Content/Brick/"), kPath),
    Rejects("RejectsBackslash", WithPath("/Content\\Brick"), kPath),
    Rejects("RejectsDotSegment", WithPath("/Content/./Brick"), kPath),
    Rejects("RejectsClimbSegment", WithPath("/Content/../Brick"), kPath),
    Rejects("RejectsControlCharacter", WithPath("/Content/Br\x01ick"), kPath)),
  oxygen::cooker::test::SchemaCaseName);

} // namespace
