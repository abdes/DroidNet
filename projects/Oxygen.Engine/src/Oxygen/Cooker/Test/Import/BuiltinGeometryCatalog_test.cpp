//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/BuiltinGeometryCatalog.cpp

#include <stdexcept>
#include <string>
#include <vector>

#include <fmt/format.h>
#include <fmt/ranges.h>
#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <Oxygen/Cooker/Import/BuiltinGeometryCatalog.h>
#include <Oxygen/Cooker/Test/Support/JsonSchema.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/BuiltinGeometry.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Data/MaterialSlotId.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using nlohmann::json;
using oxygen::content::import::ExportBuiltinGeometryCatalog;

auto GeometrySchema() -> const json&
{
  static const auto schema = oxygen::cooker::test::LoadSchema(
    "Import/Schemas/oxygen.geometry-descriptor.schema.json");
  return schema;
}

auto MaterialSchema() -> const json&
{
  static const auto schema = oxygen::cooker::test::LoadSchema(
    "Import/Schemas/oxygen.material-descriptor.schema.json");
  return schema;
}

//! Exported contributions validate against the native schemas and use actual
//! live bounds/materials.
NOLINT_TEST(BuiltinGeometryCatalogTest, ContributionsMatchSchemasAndLiveAssets)
{
  const auto catalog = json::parse(ExportBuiltinGeometryCatalog("Content"));
  EXPECT_EQ(catalog.at("schema"), "oxygen.builtin-geometry-catalog.v3");
  ASSERT_EQ(catalog.at("geometries").size(),
    oxygen::data::GetBuiltinGeometryNames().size());
  const auto& material = catalog.at("default_material").at("descriptor");
  EXPECT_THAT(oxygen::cooker::test::ValidateJson(MaterialSchema(), material),
    testing::IsEmpty());
  const auto live_material = oxygen::data::MaterialAsset::CreateDefault();
  const auto& parameters = material.at("parameters");
  EXPECT_FLOAT_EQ(
    parameters.at("roughness").get<float>(), live_material->GetRoughness());
  EXPECT_FLOAT_EQ(
    parameters.at("metalness").get<float>(), live_material->GetMetalness());
  EXPECT_EQ(
    parameters.at("double_sided").get<bool>(), live_material->IsDoubleSided());

  auto saw_capsule = false;
  for (const auto& entry : catalog.at("geometries")) {
    SCOPED_TRACE(entry.at("name").get<std::string>());
    const auto& descriptor = entry.at("descriptor");
    EXPECT_THAT(
      oxygen::cooker::test::ValidateJson(GeometrySchema(), descriptor),
      testing::IsEmpty());
    const auto geometry = oxygen::data::ResolveBuiltinGeometry(
      entry.at("asset_uri").get<std::string>());
    ASSERT_NE(geometry, nullptr);
    const auto& inventory = geometry->MaterialSlots();
    const auto& exported = entry.at("material_slot_inventory");
    EXPECT_EQ(exported.at("geometry_asset_key"),
      oxygen::data::to_string(inventory.geometry_asset_key));
    EXPECT_EQ(exported.at("layout_revision"),
      fmt::format("{:02x}", fmt::join(inventory.layout_revision, "")));
    ASSERT_EQ(exported.at("slots").size(), inventory.slots.size());
    ASSERT_FALSE(inventory.slots.empty());
    EXPECT_EQ(exported.at("slots").front().at("slot_id"),
      oxygen::data::to_string(inventory.slots.front().slot_id));
    const auto minimum = geometry->BoundingBoxMin();
    const auto maximum = geometry->BoundingBoxMax();
    EXPECT_THAT(descriptor.at("bounds").at("min").get<std::vector<float>>(),
      testing::ElementsAre(testing::FloatEq(minimum.x),
        testing::FloatEq(minimum.y), testing::FloatEq(minimum.z)));
    EXPECT_THAT(descriptor.at("bounds").at("max").get<std::vector<float>>(),
      testing::ElementsAre(testing::FloatEq(maximum.x),
        testing::FloatEq(maximum.y), testing::FloatEq(maximum.z)));
    const auto& submesh = descriptor.at("lods").at(0).at("submeshes").at(0);
    ASSERT_EQ(submesh.at("views").size(), 1U);
    EXPECT_EQ(submesh.at("views").at(0).at("view_ref"), "__all__");
    EXPECT_EQ(submesh.at("material_ref"),
      catalog.at("default_material").at("virtual_path"));
    EXPECT_TRUE(
      descriptor.at("lods").at(0).at("procedural").at("params").empty());
    if (entry.at("name") == "Capsule") {
      saw_capsule = true;
      EXPECT_EQ(entry.at("canonical_name"), "Capsule");
      EXPECT_EQ(
        entry.at("asset_uri"), "asset:///Engine/Generated/BasicShapes/Capsule");
      EXPECT_EQ(descriptor.at("bounds").at("min"),
        json::array({ -0.5F, -0.5F, -1.0F }));
      EXPECT_EQ(
        descriptor.at("bounds").at("max"), json::array({ 0.5F, 0.5F, 1.0F }));
    }
  }
  EXPECT_TRUE(saw_capsule);
}

//! Project mount changes alter only output addressing; every entry retains its
//! canonical identity and native authoring classification.
NOLINT_TEST(BuiltinGeometryCatalogTest, MountAndAuthoringCategoriesArePreserved)
{
  const auto catalog = json::parse(ExportBuiltinGeometryCatalog("World"));
  EXPECT_EQ(catalog.at("default_material").at("virtual_path"),
    "/World/Materials/OxygenEditor_Default.omat");
  auto standard_count = 0U;
  auto advanced_count = 0U;
  auto internal_count = 0U;
  auto saw_icosphere = false;
  for (const auto& entry : catalog.at("geometries")) {
    EXPECT_TRUE(entry.at("virtual_path")
        .get<std::string>()
        .starts_with("/World/Geometry/"));
    EXPECT_EQ(entry.at("canonical_name"), entry.at("name"));
    const auto identity = oxygen::data::ResolveBuiltinGeometryIdentity(
      entry.at("asset_uri").get<std::string>());
    ASSERT_HAS_VALUE(identity) << "Expected identity to contain a value";
    using Category = oxygen::data::BuiltinGeometryAuthoringCategory;
    switch (identity->authoring_category) {
    case Category::kStandard:
      ++standard_count;
      EXPECT_EQ(entry.at("authoring_category"), "standard");
      break;
    case Category::kAdvanced:
      ++advanced_count;
      EXPECT_EQ(entry.at("authoring_category"), "advanced");
      EXPECT_EQ(entry.at("name"), "SubdividedCube");
      break;
    case Category::kInternal:
      ++internal_count;
      EXPECT_EQ(entry.at("authoring_category"), "internal");
      EXPECT_EQ(entry.at("name"), "ArrowGizmo");
      break;
    }
    if (entry.at("name") == "IcoSphere") {
      saw_icosphere = true;
      EXPECT_EQ(entry.at("canonical_name"), "IcoSphere");
      EXPECT_EQ(entry.at("asset_uri"),
        "asset:///Engine/Generated/BasicShapes/IcoSphere");
    }
  }
  EXPECT_TRUE(saw_icosphere);
  EXPECT_EQ(standard_count, 9U);
  EXPECT_EQ(advanced_count, 1U);
  EXPECT_EQ(internal_count, 1U);
  EXPECT_THROW(static_cast<void>(ExportBuiltinGeometryCatalog("../Content")),
    std::invalid_argument);
  EXPECT_THROW(
    static_cast<void>(ExportBuiltinGeometryCatalog("")), std::invalid_argument);
}

} // namespace
