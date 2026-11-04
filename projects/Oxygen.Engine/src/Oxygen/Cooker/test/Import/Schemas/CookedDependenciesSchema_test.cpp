//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Schemas/oxygen.cooked-dependencies.schema.json

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include <Oxygen/Cooker/test/Support/JsonSchema.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using nlohmann::json;
using oxygen::cooker::test::SchemaCase;

auto Schema() -> const json&
{
  static const auto schema = oxygen::cooker::test::LoadSchema(
    "Import/Schemas/oxygen.cooked-dependencies.schema.json");
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

//! A schema constant or limit, read from the schema.
auto Owned(const std::string_view pointer) -> const json&
{
  return Schema().at(json::json_pointer { std::string { pointer } });
}

constexpr auto kAsset = "/properties/assets/items/properties/";

//! The largest value of an asset property's `maximum`, or of its `enum`.
auto Largest(const std::string& path) -> std::int64_t
{
  const auto& node = Owned(path);
  if (node.is_array()) {
    const auto values = node.get<std::vector<std::int64_t>>();
    return *std::ranges::max_element(values);
  }
  return node.get<std::int64_t>();
}

constexpr auto kKey = "0190a1b2-c3d4-7e5f-8a6b-7c8d9e0f1a2b";
constexpr auto kOtherKey = "0190a1b2-c3d4-7e5f-8a6b-7c8d9e0f1a2c";

auto KeyReference() -> json
{
  return json {
    { "asset_key", kOtherKey },
    { "target_kind", 1 },
    { "expected_asset_type", 3 },
  };
}

auto Binding() -> json
{
  return json {
    { "kind", 1 },
    { "index", 0 },
    { "state", "resource" },
  };
}

auto Asset() -> json
{
  return json {
    { "asset_key", kKey },
    { "asset_type", 3 },
    { "virtual_path", "/Content/Materials/Brick.omat" },
    { "dependencies", json::array() },
    { "key_references", json::array() },
    { "resource_bindings", json::array() },
    { "complete", true },
  };
}

auto Document(json asset) -> json
{
  return json {
    { "schema", Owned("/properties/schema/const") },
    { "source_key", kKey },
    { "assets", json::array({ std::move(asset) }) },
  };
}

auto Valid() -> json { return Document(Asset()); }

auto Maximal() -> json
{
  auto asset = Asset();
  asset["dependencies"] = json::array({ kOtherKey });
  asset["key_references"] = json::array({ KeyReference() });
  asset["resource_bindings"] = json::array({ Binding() });
  asset["complete"] = false;
  asset["diagnostic"] = "A texture is missing.";
  return Document(std::move(asset));
}

class CookedDependenciesSchemaTest
  : public ::testing::TestWithParam<SchemaCase> { };

NOLINT_TEST_P(CookedDependenciesSchemaTest, ValidatesDocument)
{
  oxygen::cooker::test::ExpectSchemaCase(Schema(), GetParam());
}

constexpr auto kRefs = "/assets/0/key_references";
constexpr auto kBindings = "/assets/0/resource_bindings";

INSTANTIATE_TEST_SUITE_P(Cases, CookedDependenciesSchemaTest,
  ::testing::Values(
    Accepts("AcceptsNoAssets", With(Valid(), "/assets", json::array())),
    Accepts("AcceptsMinimalAsset", Valid()),
    Accepts("AcceptsMaximalAsset", Maximal()),
    Accepts("AcceptsAssetTypeAtMaximum",
      With(Valid(), "/assets/0/asset_type",
        Largest(std::string { kAsset } + "asset_type/maximum"))),
    Accepts("AcceptsLargestTargetKind",
      With(Maximal(), "/assets/0/key_references/0/target_kind",
        Largest(std::string { kAsset }
          + "key_references/items/properties/target_kind/enum"))),
    Accepts("AcceptsLargestResourceKind",
      With(Maximal(), "/assets/0/resource_bindings/0/kind",
        Largest(std::string { kAsset }
          + "resource_bindings/items/properties/kind/enum"))),
    Accepts("AcceptsFallbackState",
      With(Maximal(), "/assets/0/resource_bindings/0/state", "fallback")),
    Accepts("AcceptsErrorState",
      With(Maximal(), "/assets/0/resource_bindings/0/state", "error")),
    Accepts("AcceptsExternalState",
      With(Maximal(), "/assets/0/resource_bindings/0/state", "external")),
    Accepts("AcceptsBindingIndexAtMaximum",
      With(Maximal(), "/assets/0/resource_bindings/0/index",
        Largest(std::string { kAsset }
          + "resource_bindings/items/properties/index/maximum"))),

    Rejects("RejectsMissingSchema", Without(Valid(), "/schema"), "'schema'"),
    Rejects("RejectsMissingSourceKey", Without(Valid(), "/source_key"),
      "'source_key'"),
    Rejects("RejectsMissingAssets", Without(Valid(), "/assets"), "'assets'"),
    Rejects("RejectsMissingAssetKey", Without(Valid(), "/assets/0/asset_key"),
      "'asset_key'"),
    Rejects("RejectsMissingAssetType", Without(Valid(), "/assets/0/asset_type"),
      "'asset_type'"),
    Rejects("RejectsMissingVirtualPath",
      Without(Valid(), "/assets/0/virtual_path"), "'virtual_path'"),
    Rejects("RejectsMissingDependencies",
      Without(Valid(), "/assets/0/dependencies"), "'dependencies'"),
    Rejects("RejectsMissingKeyReferences",
      Without(Valid(), "/assets/0/key_references"), "'key_references'"),
    Rejects("RejectsMissingResourceBindings",
      Without(Valid(), "/assets/0/resource_bindings"), "'resource_bindings'"),
    Rejects("RejectsMissingComplete", Without(Valid(), "/assets/0/complete"),
      "'complete'"),
    Rejects("RejectsMissingReferenceKey",
      Without(Maximal(), "/assets/0/key_references/0/asset_key"),
      "'asset_key'"),
    Rejects("RejectsMissingReferenceTargetKind",
      Without(Maximal(), "/assets/0/key_references/0/target_kind"),
      "'target_kind'"),
    Rejects("RejectsMissingReferenceExpectedType",
      Without(Maximal(), "/assets/0/key_references/0/expected_asset_type"),
      "'expected_asset_type'"),
    Rejects("RejectsMissingBindingKind",
      Without(Maximal(), "/assets/0/resource_bindings/0/kind"), "'kind'"),
    Rejects("RejectsMissingBindingIndex",
      Without(Maximal(), "/assets/0/resource_bindings/0/index"), "'index'"),
    Rejects("RejectsMissingBindingState",
      Without(Maximal(), "/assets/0/resource_bindings/0/state"), "'state'"),

    Rejects("RejectsUnknownTopLevelField", With(Valid(), "/unknown", true),
      "'unknown'"),
    Rejects("RejectsUnknownAssetField",
      With(Valid(), "/assets/0/unknown", true), "'unknown'"),
    Rejects("RejectsUnknownReferenceField",
      With(Maximal(), "/assets/0/key_references/0/unknown", true), "'unknown'"),
    Rejects("RejectsUnknownBindingField",
      With(Maximal(), "/assets/0/resource_bindings/0/unknown", true),
      "'unknown'"),

    Rejects("RejectsOtherSchemaIdentifier",
      With(Valid(), "/schema",
        Owned("/properties/schema/const").get<std::string>() + "x"),
      "/schema: "),
    Rejects("RejectsMalformedSourceKey",
      With(Valid(), "/source_key", "not-a-key"), "/source_key: "),
    Rejects("RejectsUppercaseSourceKey",
      With(Valid(), "/source_key", "0190A1B2-C3D4-7E5F-8A6B-7C8D9E0F1A2B"),
      "/source_key: "),
    Rejects("RejectsNonStringSourceKey", With(Valid(), "/source_key", 7),
      "/source_key: "),
    Rejects(
      "RejectsNonArrayAssets", With(Valid(), "/assets", "x"), "/assets: "),
    Rejects("RejectsMalformedAssetKey",
      With(Valid(), "/assets/0/asset_key", "x"), "/assets/0/asset_key: "),
    Rejects("RejectsNonIntegerAssetType",
      With(Valid(), "/assets/0/asset_type", "3"), "/assets/0/asset_type: "),
    Rejects("RejectsNegativeAssetType",
      With(Valid(), "/assets/0/asset_type", -1), "/assets/0/asset_type: "),
    Rejects("RejectsAssetTypeAboveMaximum",
      With(Valid(), "/assets/0/asset_type",
        Largest(std::string { kAsset } + "asset_type/maximum") + 1),
      "/assets/0/asset_type: "),
    Rejects("RejectsRelativeVirtualPath",
      With(Valid(), "/assets/0/virtual_path", "Content/Brick.omat"),
      "/assets/0/virtual_path: "),
    Rejects("RejectsNonStringVirtualPath",
      With(Valid(), "/assets/0/virtual_path", 7), "/assets/0/virtual_path: "),
    Rejects("RejectsNonArrayDependencies",
      With(Valid(), "/assets/0/dependencies", "x"), "/assets/0/dependencies: "),
    Rejects("RejectsMalformedDependency",
      With(Valid(), "/assets/0/dependencies", json::array({ "x" })),
      "/assets/0/dependencies/0: "),
    Rejects("RejectsDuplicateDependencies",
      With(Valid(), "/assets/0/dependencies",
        json::array({ kOtherKey, kOtherKey })),
      "/assets/0/dependencies: "),
    Rejects("RejectsNonBooleanComplete",
      With(Valid(), "/assets/0/complete", "true"), "/assets/0/complete: "),
    Rejects("RejectsNonStringDiagnostic",
      With(Valid(), "/assets/0/diagnostic", 7), "/assets/0/diagnostic: "),

    Rejects("RejectsDuplicateKeyReferences",
      With(Valid(), kRefs, json::array({ KeyReference(), KeyReference() })),
      kRefs),
    Rejects("RejectsMalformedReferenceKey",
      With(Maximal(), "/assets/0/key_references/0/asset_key", "x"),
      "/assets/0/key_references/0/asset_key: "),
    Rejects("RejectsTargetKindBelowEnum",
      With(Maximal(), "/assets/0/key_references/0/target_kind", 0),
      "/assets/0/key_references/0/target_kind: "),
    Rejects("RejectsTargetKindAboveEnum",
      With(Maximal(), "/assets/0/key_references/0/target_kind",
        Largest(std::string { kAsset }
          + "key_references/items/properties/target_kind/enum")
          + 1),
      "/assets/0/key_references/0/target_kind: "),
    Rejects("RejectsNonIntegerTargetKind",
      With(Maximal(), "/assets/0/key_references/0/target_kind", "1"),
      "/assets/0/key_references/0/target_kind: "),
    Rejects("RejectsExpectedAssetTypeAboveMaximum",
      With(Maximal(), "/assets/0/key_references/0/expected_asset_type",
        Largest(std::string { kAsset }
          + "key_references/items/properties/expected_asset_type/maximum")
          + 1),
      "/assets/0/key_references/0/expected_asset_type: "),
    Rejects("RejectsNegativeExpectedAssetType",
      With(Maximal(), "/assets/0/key_references/0/expected_asset_type", -1),
      "/assets/0/key_references/0/expected_asset_type: "),

    Rejects("RejectsNonArrayResourceBindings", With(Valid(), kBindings, "x"),
      "/assets/0/resource_bindings: "),
    Rejects("RejectsResourceKindBelowEnum",
      With(Maximal(), "/assets/0/resource_bindings/0/kind", 0),
      "/assets/0/resource_bindings/0/kind: "),
    Rejects("RejectsResourceKindAboveEnum",
      With(Maximal(), "/assets/0/resource_bindings/0/kind",
        Largest(std::string { kAsset }
          + "resource_bindings/items/properties/kind/enum")
          + 1),
      "/assets/0/resource_bindings/0/kind: "),
    Rejects("RejectsNegativeBindingIndex",
      With(Maximal(), "/assets/0/resource_bindings/0/index", -1),
      "/assets/0/resource_bindings/0/index: "),
    Rejects("RejectsBindingIndexAboveMaximum",
      With(Maximal(), "/assets/0/resource_bindings/0/index",
        Largest(std::string { kAsset }
          + "resource_bindings/items/properties/index/maximum")
          + 1),
      "/assets/0/resource_bindings/0/index: "),
    Rejects("RejectsUnknownBindingState",
      With(Maximal(), "/assets/0/resource_bindings/0/state", "missing"),
      "/assets/0/resource_bindings/0/state: "),
    Rejects("RejectsNonStringBindingState",
      With(Maximal(), "/assets/0/resource_bindings/0/state", 1),
      "/assets/0/resource_bindings/0/state: ")),
  oxygen::cooker::test::SchemaCaseName);

} // namespace
