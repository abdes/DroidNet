//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Schemas/oxygen.surface-provenance.schema.json

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
    "Import/Schemas/oxygen.surface-provenance.schema.json");
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

constexpr auto kBinding = "/properties/geometries/items/properties/surfaces/"
                          "items/properties/bindings/items/properties/";
constexpr auto kAllocation = "/properties/geometries/items/properties/"
                             "allocations/items/properties/";

auto Maximum(const std::string& path) -> std::int64_t
{
  return Owned(path + "/maximum").get<std::int64_t>();
}

constexpr auto kKey = "0190a1b2-c3d4-7e5f-8a6b-7c8d9e0f1a2b";
constexpr auto kOtherKey = "0190a1b2-c3d4-7e5f-8a6b-7c8d9e0f1a2c";
constexpr auto kNilKey = "00000000-0000-0000-0000-000000000000";

auto Binding() -> json
{
  return json {
    { "lod_index", 0 },
    { "submesh_index", 1 },
    { "default_material_key", kOtherKey },
  };
}

auto Surface() -> json
{
  return json {
    { "surface_id", kKey },
    { "display_name", "Paint" },
    { "bindings", json::array({ Binding() }) },
  };
}

auto Allocation() -> json
{
  return json {
    { "declaration_key", 7 },
    { "surface_id", kKey },
  };
}

auto Geometry() -> json
{
  return json {
    { "geometry_asset_key", kOtherKey },
    { "source_geometry_anchor", "Root/Body" },
    { "source_layout_witness", std::string(64, 'a') },
    { "layout_revision", std::string(64, 'b') },
    { "surfaces", json::array({ Surface() }) },
    { "allocations", json::array({ Allocation() }) },
  };
}

auto Document(json geometries) -> json
{
  return json {
    { "schema_version", Owned("/properties/schema_version/const") },
    { "geometries", std::move(geometries) },
  };
}

auto Valid() -> json { return Document(json::array({ Geometry() })); }

class SurfaceProvenanceSchemaTest
  : public ::testing::TestWithParam<SchemaCase> { };

NOLINT_TEST_P(SurfaceProvenanceSchemaTest, ValidatesDocument)
{
  oxygen::cooker::test::ExpectSchemaCase(Schema(), GetParam());
}

INSTANTIATE_TEST_SUITE_P(Cases, SurfaceProvenanceSchemaTest,
  ::testing::Values(Accepts("AcceptsNoGeometries", Document(json::array())),
    Accepts("AcceptsRetainedGeometry", Valid()),
    Accepts("AcceptsGeometryWithoutSurfaces",
      With(With(Valid(), "/geometries/0/surfaces", json::array()),
        "/geometries/0/allocations", json::array())),
    Accepts("AcceptsEmptyDisplayName",
      With(Valid(), "/geometries/0/surfaces/0/display_name", "")),
    Accepts("AcceptsMaximumIndices",
      With(With(With(Valid(), "/geometries/0/surfaces/0/bindings/0/lod_index",
                  Maximum(std::string { kBinding } + "lod_index")),
             "/geometries/0/surfaces/0/bindings/0/submesh_index",
             Maximum(std::string { kBinding } + "submesh_index")),
        "/geometries/0/allocations/0/declaration_key",
        Maximum(std::string { kAllocation } + "declaration_key"))),
    Accepts("AcceptsMaterialKeyOfZeros",
      With(Valid(), "/geometries/0/surfaces/0/bindings/0/default_material_key",
        kNilKey)),

    Rejects("RejectsMissingSchemaVersion", Without(Valid(), "/schema_version"),
      "'schema_version'"),
    Rejects("RejectsMissingGeometries", Without(Valid(), "/geometries"),
      "'geometries'"),
    Rejects("RejectsMissingGeometryKey",
      Without(Valid(), "/geometries/0/geometry_asset_key"),
      "'geometry_asset_key'"),
    Rejects("RejectsMissingAnchor",
      Without(Valid(), "/geometries/0/source_geometry_anchor"),
      "'source_geometry_anchor'"),
    Rejects("RejectsMissingLayoutWitness",
      Without(Valid(), "/geometries/0/source_layout_witness"),
      "'source_layout_witness'"),
    Rejects("RejectsMissingLayoutRevision",
      Without(Valid(), "/geometries/0/layout_revision"), "'layout_revision'"),
    Rejects("RejectsMissingSurfaces",
      Without(Valid(), "/geometries/0/surfaces"), "'surfaces'"),
    Rejects("RejectsMissingAllocations",
      Without(Valid(), "/geometries/0/allocations"), "'allocations'"),
    Rejects("RejectsMissingSurfaceId",
      Without(Valid(), "/geometries/0/surfaces/0/surface_id"), "'surface_id'"),
    Rejects("RejectsMissingDisplayName",
      Without(Valid(), "/geometries/0/surfaces/0/display_name"),
      "'display_name'"),
    Rejects("RejectsMissingBindings",
      Without(Valid(), "/geometries/0/surfaces/0/bindings"), "'bindings'"),
    Rejects("RejectsMissingLodIndex",
      Without(Valid(), "/geometries/0/surfaces/0/bindings/0/lod_index"),
      "'lod_index'"),
    Rejects("RejectsMissingSubmeshIndex",
      Without(Valid(), "/geometries/0/surfaces/0/bindings/0/submesh_index"),
      "'submesh_index'"),
    Rejects("RejectsMissingDefaultMaterialKey",
      Without(
        Valid(), "/geometries/0/surfaces/0/bindings/0/default_material_key"),
      "'default_material_key'"),
    Rejects("RejectsMissingDeclarationKey",
      Without(Valid(), "/geometries/0/allocations/0/declaration_key"),
      "'declaration_key'"),
    Rejects("RejectsMissingAllocationSurfaceId",
      Without(Valid(), "/geometries/0/allocations/0/surface_id"),
      "'surface_id'"),

    Rejects("RejectsUnknownTopLevelField", With(Valid(), "/unknown", true),
      "'unknown'"),
    Rejects("RejectsUnknownGeometryField",
      With(Valid(), "/geometries/0/unknown", true), "'unknown'"),
    Rejects("RejectsUnknownSurfaceField",
      With(Valid(), "/geometries/0/surfaces/0/unknown", true), "'unknown'"),
    Rejects("RejectsUnknownBindingField",
      With(Valid(), "/geometries/0/surfaces/0/bindings/0/unknown", true),
      "'unknown'"),
    Rejects("RejectsUnknownAllocationField",
      With(Valid(), "/geometries/0/allocations/0/unknown", true), "'unknown'"),

    Rejects("RejectsOtherSchemaVersion",
      With(Valid(), "/schema_version",
        Owned("/properties/schema_version/const").get<std::int64_t>() + 1),
      "/schema_version: "),
    Rejects("RejectsNonArrayGeometries", With(Valid(), "/geometries", "x"),
      "/geometries: "),
    Rejects("RejectsMalformedGeometryKey",
      With(Valid(), "/geometries/0/geometry_asset_key", "x"),
      "/geometries/0/geometry_asset_key: "),
    Rejects("RejectsNilGeometryKey",
      With(Valid(), "/geometries/0/geometry_asset_key", kNilKey),
      "/geometries/0/geometry_asset_key: "),
    Rejects("RejectsEmptyAnchor",
      With(Valid(), "/geometries/0/source_geometry_anchor", ""),
      "/geometries/0/source_geometry_anchor: "),
    Rejects("RejectsNonStringAnchor",
      With(Valid(), "/geometries/0/source_geometry_anchor", 7),
      "/geometries/0/source_geometry_anchor: "),
    Rejects("RejectsShortLayoutWitness",
      With(Valid(), "/geometries/0/source_layout_witness", "abc"),
      "/geometries/0/source_layout_witness: "),
    Rejects("RejectsUppercaseLayoutRevision",
      With(Valid(), "/geometries/0/layout_revision", std::string(64, 'B')),
      "/geometries/0/layout_revision: "),
    Rejects("RejectsNonArraySurfaces",
      With(Valid(), "/geometries/0/surfaces", "x"), "/geometries/0/surfaces: "),
    Rejects("RejectsMalformedSurfaceId",
      With(Valid(), "/geometries/0/surfaces/0/surface_id", "x"),
      "/geometries/0/surfaces/0/surface_id: "),
    Rejects("RejectsNilSurfaceId",
      With(Valid(), "/geometries/0/surfaces/0/surface_id", kNilKey),
      "/geometries/0/surfaces/0/surface_id: "),
    Rejects("RejectsNonStringDisplayName",
      With(Valid(), "/geometries/0/surfaces/0/display_name", 7),
      "/geometries/0/surfaces/0/display_name: "),
    Rejects("RejectsEmptyBindings",
      With(Valid(), "/geometries/0/surfaces/0/bindings", json::array()),
      "/geometries/0/surfaces/0/bindings: "),

    Rejects("RejectsNegativeLodIndex",
      With(Valid(), "/geometries/0/surfaces/0/bindings/0/lod_index", -1),
      "/geometries/0/surfaces/0/bindings/0/lod_index: "),
    Rejects("RejectsLodIndexAboveMaximum",
      With(Valid(), "/geometries/0/surfaces/0/bindings/0/lod_index",
        Maximum(std::string { kBinding } + "lod_index") + 1),
      "/geometries/0/surfaces/0/bindings/0/lod_index: "),
    Rejects("RejectsNonIntegerLodIndex",
      With(Valid(), "/geometries/0/surfaces/0/bindings/0/lod_index", "0"),
      "/geometries/0/surfaces/0/bindings/0/lod_index: "),
    Rejects("RejectsNegativeSubmeshIndex",
      With(Valid(), "/geometries/0/surfaces/0/bindings/0/submesh_index", -1),
      "/geometries/0/surfaces/0/bindings/0/submesh_index: "),
    Rejects("RejectsSubmeshIndexAboveMaximum",
      With(Valid(), "/geometries/0/surfaces/0/bindings/0/submesh_index",
        Maximum(std::string { kBinding } + "submesh_index") + 1),
      "/geometries/0/surfaces/0/bindings/0/submesh_index: "),
    Rejects("RejectsMalformedDefaultMaterialKey",
      With(Valid(), "/geometries/0/surfaces/0/bindings/0/default_material_key",
        "x"),
      "/geometries/0/surfaces/0/bindings/0/default_material_key: "),
    Rejects("RejectsNegativeDeclarationKey",
      With(Valid(), "/geometries/0/allocations/0/declaration_key", -1),
      "/geometries/0/allocations/0/declaration_key: "),
    Rejects("RejectsDeclarationKeyAboveMaximum",
      With(Valid(), "/geometries/0/allocations/0/declaration_key",
        Maximum(std::string { kAllocation } + "declaration_key") + 1),
      "/geometries/0/allocations/0/declaration_key: "),
    Rejects("RejectsMalformedAllocationSurfaceId",
      With(Valid(), "/geometries/0/allocations/0/surface_id", "x"),
      "/geometries/0/allocations/0/surface_id: "),
    Rejects("RejectsNilAllocationSurfaceId",
      With(Valid(), "/geometries/0/allocations/0/surface_id", kNilKey),
      "/geometries/0/allocations/0/surface_id: ")),
  oxygen::cooker::test::SchemaCaseName);

} // namespace
