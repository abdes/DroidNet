//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Internal/SceneSource.cpp

#include <algorithm>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/Internal/SceneSource.h>
#include <Oxygen/Cooker/test/Support/Diagnostics.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/PakFormat_geometry.h>
#include <Oxygen/Data/PakFormat_world.h>
#include <Oxygen/Data/Schema/Generated.SceneComponents.h>
#include <Oxygen/Data/SurfaceId.h>
#include <Oxygen/Testing/GTest.h>

namespace oxygen::content::import::test {
namespace {
  using internal::SceneSource;
  using nlohmann::json;

  auto MakeScene() -> json
  {
    return {
      { "version", internal::kSceneDescriptorVersion },
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
    ASSERT_HAS_VALUE(source) << "Expected source to contain a value";
    EXPECT_TRUE(diagnostics.empty());
    EXPECT_EQ(source->name, "Scene.v2");
    EXPECT_TRUE(source->build.nodes.empty());
    ASSERT_HAS_VALUE(source->background)
      << "Expected source->background to contain a value";
    EXPECT_EQ(source->background->enabled, 0U);
    ASSERT_HAS_VALUE(source->post_process)
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
      "overrides":{"parts":[{
        "kind":"surface",
        "id":"018f8f8f-1111-7111-8111-111111111111",
        "layout_revision":"0000000000000000000000000000000000000000000000000000000000000000",
        "values":{"/material":"/Content/Materials/paint.omat"}
      }]}
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
    ASSERT_HAS_VALUE(source) << "Expected source to contain a value";
    EXPECT_TRUE(diagnostics.empty());
    ASSERT_EQ(source->renderables.size(), 1U);
    const auto& renderable = source->renderables.front();
    EXPECT_EQ(renderable.geometry, "/Content/Geometry/body.ogeo");
    ASSERT_EQ(renderable.overrides.size(), 1U);
    const auto& material = renderable.overrides.front();
    EXPECT_EQ(material.property->id,
      data::schema::OverrideId::kGeometrySurfaceMaterial);
    EXPECT_EQ(data::to_string(material.part_id),
      "018f8f8f-1111-7111-8111-111111111111");
    const auto* path
      = std::get_if<SceneSource::AssetReference>(&material.value);
    ASSERT_NE(path, nullptr);
    EXPECT_EQ(path->virtual_path, "/Content/Materials/paint.omat");
    ASSERT_EQ(source->references.size(), 6U);
    EXPECT_EQ(source->references.front().type, data::AssetType::kMaterial);
    EXPECT_EQ(
      source->references.back().virtual_path, "/Content/Scenes/other.oscene");
    EXPECT_FALSE(source->references.back().type.has_value());
    EXPECT_EQ(
      source->references.back().object_path, "references.extra_assets[0]");
  }

  //! Every override value is read as its property's value type, and its
  //! surface by id; the component's own properties name no part.
  NOLINT_TEST(SceneSourceTest, ReadsEachOverrideAsItsValueType)
  {
    auto document = MakeScene();
    document.at("nodes") = json::array({ json::object() });
    document.emplace("renderables", json::parse(R"([{
      "node":0,"geometry_ref":"/Content/Geometry/body.ogeo",
      "overrides":{
        "values":{"/lod_policy":{"type":"fixed","lod_index":2}},
        "parts":[{
          "kind":"surface",
          "id":"018f8f8f-1111-7111-8111-111111111111",
          "layout_revision":"0000000000000000000000000000000000000000000000000000000000000000",
          "values":{"/visible":false,"/casts_shadows":true}
        }]
      }
    }])"));
    auto diagnostics = std::vector<ImportDiagnostic> {};
    const auto source
      = SceneSource::FromDescriptor(document.dump(), "scene.json", diagnostics);
    ASSERT_HAS_VALUE(source) << "Expected source to contain a value";
    EXPECT_TRUE(diagnostics.empty());
    const auto& overrides = source->renderables.front().overrides;
    ASSERT_EQ(overrides.size(), 3U);
    using data::schema::OverrideId;
    EXPECT_EQ(overrides.at(0).property->id, OverrideId::kGeometryLodPolicy);
    EXPECT_TRUE(overrides.at(0).part_id.IsNil());
    const auto* policy = std::get_if<data::pak::geometry::LodPolicyRecord>(
      &overrides.at(0).value);
    ASSERT_NE(policy, nullptr);
    EXPECT_EQ(policy->type, data::pak::geometry::LodPolicyType::kFixed);
    EXPECT_EQ(policy->lod_index, 2U);
    const auto find = [&overrides](const OverrideId id) -> const auto& {
      return *std::ranges::find_if(
        overrides, [id](const SceneSource::Override& entry) -> bool {
          return entry.property->id == id;
        });
    };
    const auto& visible = find(OverrideId::kGeometrySurfaceVisibility);
    EXPECT_FALSE(visible.part_id.IsNil());
    EXPECT_EQ(std::get<bool>(visible.value), false);
    EXPECT_EQ(
      std::get<bool>(find(OverrideId::kGeometrySurfaceCastsShadows).value),
      true);
  }

  //! A surface named by two parts, and a LOD policy whose distances
  //! decrease, are both refused before cooked linking.
  NOLINT_TEST(SceneSourceTest, RejectsRepeatedSurfacesAndInvalidLodPolicies)
  {
    const auto read = [](const std::string_view overrides) -> auto {
      auto document = MakeScene();
      document.at("nodes") = json::array({ json::object() });
      auto renderable = json::object(
        { { "node", 0 }, { "geometry_ref", "/Content/Geometry/body.ogeo" },
          { "overrides", json::parse(overrides) } });
      document.emplace("renderables", json::array({ renderable }));
      auto diagnostics = std::vector<ImportDiagnostic> {};
      const auto source = SceneSource::FromDescriptor(
        document.dump(), "scene.json", diagnostics);
      EXPECT_FALSE(source.has_value());
      return diagnostics;
    };
    constexpr auto kRevision
      = "0000000000000000000000000000000000000000000000000000000000000000";
    const auto part = std::string(R"({"kind":"surface",
      "id":"018f8f8f-1111-7111-8111-111111111111","layout_revision":")")
      + kRevision + R"(","values":{"/visible":false}})";

    EXPECT_TRUE(
      HasDiagnosticCode(read(R"({"parts":[)" + part + "," + part + "]}"),
        "scene.descriptor.surface_invalid"));
    EXPECT_TRUE(HasDiagnosticCode(
      read(
        R"({"values":{"/lod_policy":{"type":"distance","thresholds":[9,4],"hysteresis_ratio":0.1}}})"),
      "scene.descriptor.lod_policy_invalid"));
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
    ASSERT_HAS_VALUE(source) << "Expected source to contain a value";
    ASSERT_HAS_VALUE(source->sky_light)
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

  //! Instances, the starting camera and starting contexts stay symbolic until
  //! linking; contexts keep their authored order and priorities.
  NOLINT_TEST(SceneSourceTest, RetainsInstancesAndStartState)
  {
    auto document = MakeScene();
    document.at("nodes") = json::parse(R"([
      {"name":"root"},
      {"name":"model","parent":0,
       "instance":{"scene_ref":"/Content/Scenes/model.oscene"}},
      {"name":"camera","parent":0}])");
    document.emplace("starting_camera", 2);
    document.emplace("starting_input_contexts", json::parse(R"([
      {"context_ref":"/Content/Input/gameplay.oimap","priority":5},
      {"context_ref":"/Content/Input/menu.oimap"}])"));
    auto diagnostics = std::vector<ImportDiagnostic> {};
    const auto source
      = SceneSource::FromDescriptor(document.dump(), "scene.json", diagnostics);
    ASSERT_HAS_VALUE(source) << "Expected source to contain a value";
    EXPECT_TRUE(diagnostics.empty());
    ASSERT_EQ(source->instances.size(), 1U);
    EXPECT_EQ(source->instances.front().node_index, 1U);
    EXPECT_EQ(source->instances.front().scene, "/Content/Scenes/model.oscene");
    EXPECT_EQ(source->build.starting_camera, 2U);
    ASSERT_EQ(source->starting_input_contexts.size(), 2U);
    EXPECT_EQ(source->starting_input_contexts.front().context,
      "/Content/Input/gameplay.oimap");
    EXPECT_EQ(source->starting_input_contexts.front().priority, 5);
    EXPECT_EQ(source->starting_input_contexts.back().priority, 0);
  }

  //! A scene without start state has no starting camera.
  NOLINT_TEST(SceneSourceTest, DefaultsToNoStartingCamera)
  {
    auto diagnostics = std::vector<ImportDiagnostic> {};
    const auto source = SceneSource::FromDescriptor(
      MakeScene().dump(), "scene.json", diagnostics);
    ASSERT_HAS_VALUE(source) << "Expected source to contain a value";
    EXPECT_EQ(
      source->build.starting_camera, data::pak::world::kNoStartingCamera);
    EXPECT_TRUE(source->instances.empty());
    EXPECT_TRUE(source->starting_input_contexts.empty());
  }

  NOLINT_TEST(SceneSourceTest, RejectsStartingCameraOutsideTheScene)
  {
    auto document = MakeScene();
    document.emplace("starting_camera", 0);
    auto diagnostics = std::vector<ImportDiagnostic> {};
    EXPECT_FALSE(
      SceneSource::FromDescriptor(document.dump(), "scene.json", diagnostics));
    EXPECT_TRUE(HasDiagnosticCode(
      diagnostics, "scene.descriptor.starting_camera_out_of_range"));
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
    for (const auto* const text : { "[]", "null", "1", "\"scene\"" }) {
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
