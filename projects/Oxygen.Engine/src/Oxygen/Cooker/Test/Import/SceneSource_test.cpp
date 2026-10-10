//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Internal/SceneSource.cpp

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/Internal/SceneSource.h>
#include <Oxygen/Cooker/Test/Support/Diagnostics.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/PakFormat_world.h>
#include <Oxygen/Testing/GTest.h>

namespace oxygen::content::import::test {
namespace {
  using internal::SceneSource;
  using nlohmann::json;

  auto MakeScene() -> json
  {
    return {
      { "version", data::pak::world::kSceneAssetVersion },
      { "name", "Scene.v2" },
      { "nodes", json::array() },
    };
  }

  using oxygen::cooker::test::HasDiagnosticCode;

  NOLINT_TEST(SceneSourceTest, EmptyScenePreservesEnvironmentAndCurve)
  {
    auto document = MakeScene();
    document.emplace("environment", json::parse(R"({
      "background":{"enabled":false,"color_rgb":[0.1,0.2,0.3]},
      "post_process_volume":{
        "auto_exposure_metering_mask":"/Content/Textures/mask.otex",
        "auto_exposure_compensation_curve":[
          {"metered_ev":0,"compensation_ev":1},
          {"metered_ev":4,"compensation_ev":2}]
      }
    })"));
    auto diagnostics = std::vector<ImportDiagnostic> {};
    const auto source
      = SceneSource::FromDescriptor(document.dump(), "scene.json", diagnostics);
    ASSERT_TRUE(source.has_value()) << "Expected source to contain a value";
    EXPECT_TRUE(diagnostics.empty());
    EXPECT_EQ(source->name, "Scene.v2");
    EXPECT_TRUE(source->build.nodes.empty());
    ASSERT_TRUE(source->background.has_value())
      << "Expected source->background to contain a value";
    EXPECT_EQ(source->background->enabled, 0U);
    ASSERT_TRUE(source->post_process.has_value())
      << "Expected source->post_process to contain a value";
    EXPECT_EQ(
      source->post_process->metering_mask, "/Content/Textures/mask.otex");
    ASSERT_EQ(source->post_process->curve.size(), 2U);
    EXPECT_FLOAT_EQ(source->post_process->curve.back().metered_ev, 4.0F);
    EXPECT_FLOAT_EQ(source->post_process->curve.back().compensation_ev, 2.0F);
  }

  NOLINT_TEST(SceneSourceTest, RetainsGeometryMaterialsAndEveryReferenceGroup)
  {
    auto document = MakeScene();
    document.at("nodes") = json::array({ json::object() });
    document.emplace("renderables", json::parse(R"([{
      "node":0,"geometry_ref":"/Content/Geometry/body.ogeo",
      "material_overrides":[{
        "slot_id":"018f8f8f-1111-7111-8111-111111111111",
        "material_ref":"/Content/Materials/paint.omat",
        "layout_revision":"0000000000000000000000000000000000000000000000000000000000000000"
      }]
    }])"));
    document.emplace("references", json::parse(R"({
      "materials":["/Content/Materials/extra.omat"],
      "scripts":["/Content/Scripts/move.oscript"],
      "input_actions":["/Content/Input/jump.oiact"],
      "input_mapping_contexts":["/Content/Input/controls.oimap"],
      "physics_sidecars":["/Content/Physics/body.opscene"],
      "extra_assets":["/Content/Scenes/other.oscene"]
    })"));
    auto diagnostics = std::vector<ImportDiagnostic> {};
    const auto source
      = SceneSource::FromDescriptor(document.dump(), "scene.json", diagnostics);
    ASSERT_TRUE(source.has_value()) << "Expected source to contain a value";
    EXPECT_TRUE(diagnostics.empty());
    ASSERT_EQ(source->renderables.size(), 1U);
    const auto& renderable = source->renderables.front();
    EXPECT_EQ(renderable.geometry, "/Content/Geometry/body.ogeo");
    ASSERT_EQ(renderable.materials.size(), 1U);
    EXPECT_EQ(
      renderable.materials.front().material, "/Content/Materials/paint.omat");
    EXPECT_EQ(
      renderable.materials.front().layout_revision, std::string(64, '0'));
    ASSERT_EQ(source->references.size(), 6U);
    EXPECT_EQ(source->references.front().type, data::AssetType::kMaterial);
    EXPECT_EQ(
      source->references.back().virtual_path, "/Content/Scenes/other.oscene");
    EXPECT_FALSE(source->references.back().type.has_value());
    EXPECT_EQ(
      source->references.back().object_path, "references.extra_assets[0]");
  }

  NOLINT_TEST(SceneSourceTest, DisabledSkyRetainsItsCubemapDependency)
  {
    auto document = MakeScene();
    document.emplace("environment", json::parse(R"({"sky_light":{
      "enabled":false,"source":1,"intensity":1,"tint_rgb":[1,1,1],
      "diffuse_intensity":1,"specular_intensity":1,
      "lower_hemisphere_color":[0,0,0],"volumetric_scattering_intensity":1,
      "affect_reflections":true,"cubemap_ref":"/Content/Textures/sky.otex"
    }})"));
    auto diagnostics = std::vector<ImportDiagnostic> {};
    const auto source
      = SceneSource::FromDescriptor(document.dump(), "scene.json", diagnostics);
    ASSERT_TRUE(source.has_value()) << "Expected source to contain a value";
    ASSERT_TRUE(source->sky_light.has_value())
      << "Expected source->sky_light to contain a value";
    EXPECT_EQ(source->sky_light->record.enabled, 0U);
    EXPECT_EQ(source->sky_light->cubemap, "/Content/Textures/sky.otex");
  }

  NOLINT_TEST(SceneSourceTest, RejectsComponentNodeOutsideTheScene)
  {
    auto document = MakeScene();
    document.emplace("cameras", json::parse(R"({"perspective":[{
      "node":0,"aspect_mode":"auto"
    }]})"));
    auto diagnostics = std::vector<ImportDiagnostic> {};
    EXPECT_FALSE(
      SceneSource::FromDescriptor(document.dump(), "scene.json", diagnostics));
    EXPECT_TRUE(HasDiagnosticCode(
      diagnostics, "scene.descriptor.camera_node_index_out_of_range"));
  }

  NOLINT_TEST(SceneSourceTest, RejectsInvalidExposureBeforeCookedLinking)
  {
    auto document = MakeScene();
    document.emplace("environment", json::parse(R"({"post_process_volume":{
      "auto_exposure_min_ev":10,"auto_exposure_max_ev":0
    }})"));
    auto diagnostics = std::vector<ImportDiagnostic> {};
    EXPECT_FALSE(
      SceneSource::FromDescriptor(document.dump(), "scene.json", diagnostics));
    EXPECT_TRUE(
      HasDiagnosticCode(diagnostics, "scene.descriptor.exposure_invalid"));
  }

  NOLINT_TEST(SceneSourceTest, NonObjectInputIsNotAFormatUpgradeError)
  {
    for (const auto text : { "[]", "null", "1", "\"scene\"" }) {
      auto diagnostics = std::vector<ImportDiagnostic> {};
      EXPECT_FALSE(
        SceneSource::FromDescriptor(text, "scene.json", diagnostics));
      EXPECT_TRUE(HasDiagnosticCode(
        diagnostics, "scene.descriptor.request_invalid_json"));
      EXPECT_FALSE(
        HasDiagnosticCode(diagnostics, "scene.descriptor.recook_required"));
    }
  }
} // namespace
} // namespace oxygen::content::import::test
