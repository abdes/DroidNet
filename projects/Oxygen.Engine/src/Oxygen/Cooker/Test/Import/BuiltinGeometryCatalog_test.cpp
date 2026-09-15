//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

#include <nlohmann/json-schema.hpp>
#include <nlohmann/json.hpp>

#include <Oxygen/Cooker/Import/BuiltinGeometryCatalog.h>
#include <Oxygen/Data/BuiltinGeometry.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using nlohmann::json;
using oxygen::content::import::ExportBuiltinGeometryCatalog;

auto LoadSchema(const std::string& name) -> json
{
  const auto path = std::filesystem::path(__FILE__).parent_path()
    / "../../Import/Schemas" / name;
  auto stream = std::ifstream(path);
  return json::parse(stream);
}

//! Exported contributions validate against the native schemas and use actual
//! live bounds/materials.
NOLINT_TEST(BuiltinGeometryCatalogTest, ContributionsMatchSchemasAndLiveAssets)
{
  auto geometry_validator = nlohmann::json_schema::json_validator {};
  geometry_validator.set_root_schema(
    LoadSchema("oxygen.geometry-descriptor.schema.json"));
  auto material_validator = nlohmann::json_schema::json_validator {};
  material_validator.set_root_schema(
    LoadSchema("oxygen.material-descriptor.schema.json"));
  const auto catalog = json::parse(ExportBuiltinGeometryCatalog("Content"));
  EXPECT_EQ(catalog.at("schema"), "oxygen.builtin-geometry-catalog.v2");
  ASSERT_EQ(catalog.at("geometries").size(),
    oxygen::data::GetBuiltinGeometryNames().size());
  const auto& material = catalog.at("default_material").at("descriptor");
  EXPECT_NO_THROW(static_cast<void>(material_validator.validate(material)));
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
    EXPECT_NO_THROW(static_cast<void>(geometry_validator.validate(descriptor)));
    const auto geometry = oxygen::data::ResolveBuiltinGeometry(
      entry.at("asset_uri").get<std::string>());
    ASSERT_NE(geometry, nullptr);
    for (auto axis = 0; axis < 3; ++axis) {
      EXPECT_FLOAT_EQ(descriptor.at("bounds").at("min").at(axis).get<float>(),
        geometry->BoundingBoxMin()[axis]);
      EXPECT_FLOAT_EQ(descriptor.at("bounds").at("max").at(axis).get<float>(),
        geometry->BoundingBoxMax()[axis]);
    }
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
    ASSERT_TRUE(identity.has_value());
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
  EXPECT_THROW(
    ExportBuiltinGeometryCatalog("../Content"), std::invalid_argument);
  EXPECT_THROW(ExportBuiltinGeometryCatalog(""), std::invalid_argument);
}

} // namespace
