//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Schemas/oxygen.asset-key-map.schema.json

#include <cstdint>
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
    "Import/Schemas/oxygen.asset-key-map.schema.json");
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
auto Version() -> std::uint32_t
{
  return Schema()
    .at(json::json_pointer { "/properties/schema_version/const" })
    .get<std::uint32_t>();
}

auto Entry() -> json
{
  return json {
    { "virtual_path", "/Content/Materials/Brick.omat" },
    { "asset_key", "0190a1b2-c3d4-7e5f-8a6b-7c8d9e0f1a2b" },
  };
}

auto Valid() -> json
{
  return json {
    { "schema_version", Version() },
    { "assets", json::array({ Entry() }) },
  };
}

auto WithPath(const std::string& path) -> json
{
  return With(Valid(), "/assets/0/virtual_path", path);
}

class AssetKeyMapSchemaTest : public ::testing::TestWithParam<SchemaCase> { };

NOLINT_TEST_P(AssetKeyMapSchemaTest, ValidatesDocument)
{
  oxygen::cooker::test::ExpectSchemaCase(Schema(), GetParam());
}

constexpr auto kPath = "/assets/0/virtual_path: ";
constexpr auto kKey = "/assets/0/asset_key: ";

INSTANTIATE_TEST_SUITE_P(Cases, AssetKeyMapSchemaTest,
  ::testing::Values(
    Accepts("AcceptsEmptyMap",
      json { { "schema_version", Version() }, { "assets", json::array() } }),
    Accepts("AcceptsOneEntry", Valid()),
    Accepts("AcceptsTwoSegmentPath", WithPath("/Content/Brick")),
    Accepts("AcceptsPathWithSpaces", WithPath("/Content/Sniper Rifle/Paint")),
    Accepts("AcceptsMultipleEntries",
      With(Valid(), "/assets/1",
        json {
          { "virtual_path", "/Content/Textures/Brick.otex" },
          { "asset_key", "00000000-0000-0000-0000-000000000000" },
        })),

    Rejects("RejectsMissingSchemaVersion", Without(Valid(), "/schema_version"),
      "'schema_version'"),
    Rejects("RejectsMissingAssets", Without(Valid(), "/assets"), "'assets'"),
    Rejects("RejectsMissingVirtualPath",
      Without(Valid(), "/assets/0/virtual_path"), "'virtual_path'"),
    Rejects("RejectsMissingAssetKey", Without(Valid(), "/assets/0/asset_key"),
      "'asset_key'"),
    Rejects("RejectsUnknownTopLevelField", With(Valid(), "/unknown", true),
      "'unknown'"),
    Rejects("RejectsUnknownEntryField",
      With(Valid(), "/assets/0/unknown", true), "'unknown'"),

    Rejects("RejectsOtherSchemaVersion",
      With(Valid(), "/schema_version", Version() + 1), "/schema_version: "),
    Rejects("RejectsNonIntegerSchemaVersion",
      With(Valid(), "/schema_version", "2"), "/schema_version: "),
    Rejects(
      "RejectsNonArrayAssets", With(Valid(), "/assets", "x"), "/assets: "),
    Rejects(
      "RejectsNonObjectEntry", With(Valid(), "/assets/0", "x"), "/assets/0: "),
    Rejects("RejectsNonStringVirtualPath",
      With(Valid(), "/assets/0/virtual_path", 7), kPath),
    Rejects("RejectsNonStringAssetKey", With(Valid(), "/assets/0/asset_key", 7),
      kKey),

    Rejects("RejectsSingleSegmentPath", WithPath("/Content"), kPath),
    Rejects("RejectsRelativePath", WithPath("Content/Brick"), kPath),
    Rejects("RejectsEmptyPath", WithPath(""), kPath),
    Rejects("RejectsEmptySegment", WithPath("/Content//Brick"), kPath),
    Rejects("RejectsTrailingSlash", WithPath("/Content/Brick/"), kPath),
    Rejects("RejectsBackslash", WithPath("/Content\\Brick"), kPath),
    Rejects("RejectsDotSegment", WithPath("/Content/./Brick"), kPath),
    Rejects("RejectsClimbSegment", WithPath("/Content/../Brick"), kPath),
    Rejects("RejectsControlCharacter", WithPath("/Content/Br\x01ick"), kPath),

    Rejects("RejectsEmptyKey", With(Valid(), "/assets/0/asset_key", ""), kKey),
    Rejects("RejectsUppercaseKey",
      With(
        Valid(), "/assets/0/asset_key", "0190A1B2-C3D4-7E5F-8A6B-7C8D9E0F1A2B"),
      kKey),
    Rejects("RejectsKeyWithoutDashes",
      With(Valid(), "/assets/0/asset_key", "0190a1b2c3d47e5f8a6b7c8d9e0f1a2b"),
      kKey),
    Rejects("RejectsShortKey",
      With(
        Valid(), "/assets/0/asset_key", "0190a1b2-c3d4-7e5f-8a6b-7c8d9e0f1a2"),
      kKey)),
  oxygen::cooker::test::SchemaCaseName);

} // namespace
