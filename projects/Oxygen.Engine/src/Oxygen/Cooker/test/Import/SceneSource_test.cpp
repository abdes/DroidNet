//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Internal/SceneSource.cpp

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/Internal/SceneSource.h>
#include <Oxygen/Cooker/test/Support/Diagnostics.h>
#include <Oxygen/Core/Types/CameraAspectMode.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/PakFormat_geometry.h>
#include <Oxygen/Data/PakFormat_world.h>
#include <Oxygen/Data/SceneRecordDefaults.h>
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
      auto renderable = json::object({
        { "node", 0 },
        { "geometry_ref", "/Content/Geometry/body.ogeo" },
        { "overrides", json::parse(overrides) },
      });
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

  //! The first diagnostic with `code`, or null.
  auto FindDiagnostic(const std::vector<ImportDiagnostic>& diagnostics,
    const std::string_view code) -> const ImportDiagnostic*
  {
    const auto found = std::ranges::find(diagnostics, code,
      [](const ImportDiagnostic& diagnostic) -> std::string_view {
        return diagnostic.code;
      });
    return found == diagnostics.end() ? nullptr : &*found;
  }

  //! A property a descriptor omits takes the scene schema's default, whatever
  //! the record's own initializer holds.
  NOLINT_TEST(SceneSourceTest, OmittedComponentValuesTakeTheSchemaDefaults)
  {
    auto document = MakeScene();
    document.at("nodes") = json::parse(R"([{}, {"parent":0}])");
    document.emplace("cameras", json::parse(R"({
      "perspective":[{"node":0}],
      "orthographic":[{"node":1,"aspect_mode":"fixed"}]})"));
    document.emplace("lights", json::parse(R"({
      "directional":[{"node":0}],
      "point":[{"node":0}],
      "spot":[{"node":1}]})"));
    document.emplace("local_fog_volumes", json::parse(R"([{"node":0}])"));
    auto diagnostics = std::vector<ImportDiagnostic> {};
    const auto source
      = SceneSource::FromDescriptor(document.dump(), "scene.json", diagnostics);
    ASSERT_HAS_VALUE(source) << "Expected source to contain a value";
    EXPECT_TRUE(diagnostics.empty());

    namespace camera = data::schema::perspective_camera;
    const auto& perspective = source->build.perspective_cameras.front();
    EXPECT_FLOAT_EQ(perspective.fov_y, camera::kFieldOfViewYRadiansDefault);
    EXPECT_FLOAT_EQ(perspective.aspect_ratio, camera::kAspectRatioDefault);
    EXPECT_FLOAT_EQ(perspective.near_plane, camera::kNearPlaneDefault);
    EXPECT_FLOAT_EQ(perspective.far_plane, camera::kFarPlaneDefault);
    EXPECT_FLOAT_EQ(perspective.iso, camera::kIsoDefault);
    EXPECT_EQ(perspective.aspect_mode,
      data::pak::world::ToCameraAspectMode(camera::kAspectModeDefault));

    namespace ortho = data::schema::orthographic_camera;
    const auto& orthographic = source->build.orthographic_cameras.front();
    EXPECT_FLOAT_EQ(orthographic.top, ortho::kOrthographicSizeDefault);
    EXPECT_FLOAT_EQ(orthographic.bottom, -ortho::kOrthographicSizeDefault);
    EXPECT_FLOAT_EQ(orthographic.right,
      ortho::kOrthographicSizeDefault * ortho::kAspectRatioDefault);
    EXPECT_FLOAT_EQ(orthographic.near_plane, ortho::kNearPlaneDefault);
    EXPECT_FLOAT_EQ(orthographic.far_plane, ortho::kFarPlaneDefault);
    EXPECT_EQ(orthographic.aspect_mode, CameraAspectMode::kFixed);

    namespace sun = data::schema::directional_light;
    const auto& directional = source->build.directional_lights.front();
    EXPECT_FLOAT_EQ(directional.intensity_lux, sun::kIntensityLuxDefault);
    EXPECT_FLOAT_EQ(
      directional.angular_size_radians, sun::kAngularSizeRadiansDefault);
    EXPECT_EQ(directional.cascade_count,
      static_cast<uint32_t>(sun::kCascadeCountDefault));
    EXPECT_FLOAT_EQ(
      directional.cascade_distances[3], sun::kCascadeDistance3Default);
    EXPECT_EQ(directional.common.casts_shadows, 1U);
    EXPECT_FLOAT_EQ(
      directional.common.shadow.normal_bias, sun::kShadowNormalBiasDefault);
    EXPECT_EQ(directional.common.shadow.resolution_hint,
      std::to_underlying(sun::kShadowResolutionHintDefault));

    namespace point = data::schema::point_light;
    const auto& bulb = source->build.point_lights.front();
    EXPECT_FLOAT_EQ(bulb.range, point::kRangeDefault);
    EXPECT_FLOAT_EQ(bulb.luminous_flux_lm, point::kLuminousFluxLmDefault);

    namespace spot = data::schema::spot_light;
    const auto& lamp = source->build.spot_lights.front();
    EXPECT_FLOAT_EQ(
      lamp.inner_cone_angle_radians, spot::kInnerConeAngleRadiansDefault);
    EXPECT_FLOAT_EQ(
      lamp.outer_cone_angle_radians, spot::kOuterConeAngleRadiansDefault);
    EXPECT_FLOAT_EQ(lamp.range, spot::kRangeDefault);

    namespace fog = data::schema::local_fog_volume;
    ASSERT_EQ(source->build.local_fog_volumes.size(), 1U);
    const auto& volume = source->build.local_fog_volumes.front();
    EXPECT_EQ(volume.enabled, fog::kEnabledDefault ? 1U : 0U);
    EXPECT_FLOAT_EQ(
      volume.radial_fog_extinction, fog::kRadialFogExtinctionDefault);
    EXPECT_FLOAT_EQ(
      volume.height_fog_extinction, fog::kHeightFogExtinctionDefault);
    EXPECT_FLOAT_EQ(volume.height_fog_falloff, fog::kHeightFogFalloffDefault);
    EXPECT_FLOAT_EQ(volume.height_fog_offset, fog::kHeightFogOffsetDefault);
    EXPECT_FLOAT_EQ(volume.fog_phase_g, fog::kFogPhaseGDefault);
    EXPECT_FLOAT_EQ(volume.fog_albedo[0], fog::kFogAlbedoRDefault);
    EXPECT_FLOAT_EQ(volume.fog_emissive[2], fog::kFogEmissiveBDefault);
    EXPECT_EQ(volume.sort_priority, fog::kSortPriorityDefault);
  }

  //! An authored value replaces only its own property's default.
  NOLINT_TEST(SceneSourceTest, AuthoredValuesReplaceOnlyTheirOwnDefaults)
  {
    namespace world = data::pak::world;
    auto document = MakeScene();
    document.at("nodes") = json::parse(R"([{}])");
    document.emplace("lights", json::parse(R"({
      "point":[{"node":0,"range":5,"common":{"shadow":{"bias":0.5}}}]})"));
    auto diagnostics = std::vector<ImportDiagnostic> {};
    const auto source
      = SceneSource::FromDescriptor(document.dump(), "scene.json", diagnostics);
    ASSERT_HAS_VALUE(source) << "Expected source to contain a value";

    const auto defaults = world::MakeDefaultRecord<world::PointLightRecord>();
    const auto& light = source->build.point_lights.front();
    EXPECT_FLOAT_EQ(light.range, 5.0F);
    EXPECT_FLOAT_EQ(light.common.shadow.bias, 0.5F);
    EXPECT_FLOAT_EQ(
      light.common.shadow.normal_bias, defaults.common.shadow.normal_bias);
    EXPECT_FLOAT_EQ(light.luminous_flux_lm, defaults.luminous_flux_lm);
    EXPECT_EQ(light.common.casts_shadows, defaults.common.casts_shadows);
  }

  //! A node's omitted flags are the schema's root or child defaults, and its
  //! omitted transform the identity.
  NOLINT_TEST(SceneSourceTest, OmittedNodeValuesTakeTheSchemaDefaults)
  {
    namespace world = data::pak::world;
    auto document = MakeScene();
    document.at("nodes") = json::parse(R"([{}, {"parent":0}])");
    auto diagnostics = std::vector<ImportDiagnostic> {};
    const auto source
      = SceneSource::FromDescriptor(document.dump(), "scene.json", diagnostics);
    ASSERT_HAS_VALUE(source) << "Expected source to contain a value";
    ASSERT_EQ(source->build.nodes.size(), 2U);

    const auto& root = source->build.nodes.at(0);
    EXPECT_EQ(root.node_flags,
      world::kSceneNodeFlags_Inheritable
        | world::kSceneNodeFlag_RayCastingSelectable);
    EXPECT_EQ(root.inherited_flags, 0U);
    const auto& child = source->build.nodes.at(1);
    EXPECT_EQ(child.node_flags, world::kSceneNodeFlag_RayCastingSelectable);
    EXPECT_EQ(child.inherited_flags, world::kSceneNodeFlags_Inheritable);
    EXPECT_FLOAT_EQ(root.scale[0], data::schema::transform::kScaleXDefault);
    EXPECT_FLOAT_EQ(
      root.translation[2], data::schema::transform::kPositionZDefault);
  }

  //! A value outside the generated range, a value no float holds, and an enum
  //! value the schema does not declare each fail naming the property.
  NOLINT_TEST(SceneSourceTest, RejectsValuesOutsideTheGeneratedRanges)
  {
    constexpr auto kFog = std::string_view { R"([{"node":0,"enabled":true,
      "radial_fog_extinction":1,"height_fog_extinction":1,
      "height_fog_falloff":1,"height_fog_offset":0,"fog_phase_g":)" };
    constexpr auto kFogTail = std::string_view { R"(,"fog_albedo":[1,1,1],
      "fog_emissive":[0,0,0],"sort_priority":)" };
    struct Case {
      std::string_view group;
      std::string body;
      std::string_view object_path;
    };
    const auto cases = std::vector<Case> {
      {
        .group = "cameras",
        .body
        = R"({"perspective":[{"node":0,"aspect_mode":"auto","fov_y":0}]})",
        .object_path = "cameras.perspective[0].fov_y",
      },
      {
        .group = "cameras",
        .body
        = R"({"perspective":[{"node":0,"aspect_mode":"auto","fov_y":3.2}]})",
        .object_path = "cameras.perspective[0].fov_y",
      },
      {
        .group = "cameras",
        .body
        = R"({"perspective":[{"node":0,"aspect_mode":"auto","near_plane":0}]})",
        .object_path = "cameras.perspective[0].near_plane",
      },
      {
        .group = "cameras",
        .body
        = R"({"perspective":[{"node":0,"aspect_mode":"auto","iso":-100}]})",
        .object_path = "cameras.perspective[0].iso",
      },
      {
        .group = "cameras",
        .body
        = R"({"orthographic":[{"node":0,"aspect_mode":"auto","orthographic_size":-1}]})",
        .object_path = "cameras.orthographic[0].orthographic_size",
      },
      {
        .group = "cameras",
        .body
        = R"({"orthographic":[{"node":0,"aspect_mode":"auto","far_plane":-5}]})",
        .object_path = "cameras.orthographic[0].far_plane",
      },
      {
        .group = "lights",
        .body = R"({"directional":[{"node":0,"intensity_lux":-1}]})",
        .object_path = "lights.directional[0].intensity_lux",
      },
      {
        .group = "lights",
        .body = R"({"directional":[{"node":0,"intensity_lux":1e300}]})",
        .object_path = "lights.directional[0].intensity_lux",
      },
      {
        .group = "lights",
        .body = R"({"directional":[{"node":0,"cascade_count":5}]})",
        .object_path = "lights.directional[0].cascade_count",
      },
      {
        .group = "lights",
        .body = R"({"directional":[{"node":0,"split_mode":2}]})",
        .object_path = "lights.directional[0].split_mode",
      },
      {
        .group = "lights",
        .body = R"({"directional":[{"node":0,"cascade_distances":[1,2,3,0]}]})",
        .object_path = "lights.directional[0].cascade_distances[3]",
      },
      {
        .group = "lights",
        .body = R"({"point":[{"node":0,"range":0}]})",
        .object_path = "lights.point[0].range",
      },
      {
        .group = "lights",
        .body = R"({"point":[{"node":0,"luminous_flux_lm":0}]})",
        .object_path = "lights.point[0].luminous_flux_lm",
      },
      {
        .group = "lights",
        .body = R"({"point":[{"node":0,"common":{"color_rgb":[1,2,0]}}]})",
        .object_path = "lights.point[0].common.color_rgb[1]",
      },
      {
        .group = "lights",
        .body
        = R"({"point":[{"node":0,"common":{"exposure_compensation_ev":11}}]})",
        .object_path = "lights.point[0].common.exposure_compensation_ev",
      },
      {
        .group = "lights",
        .body
        = R"({"spot":[{"node":0,"common":{"shadow":{"resolution_hint":4}}}]})",
        .object_path = "lights.spot[0].common.shadow.resolution_hint",
      },
      {
        .group = "lights",
        .body = R"({"spot":[{"node":0,"outer_cone_angle_radians":2}]})",
        .object_path = "lights.spot[0].outer_cone_angle_radians",
      },
      {
        .group = "local_fog_volumes",
        .body = std::string(kFog) + "1" + std::string(kFogTail) + "0}]",
        .object_path = "local_fog_volumes[0].fog_phase_g",
      },
      {
        .group = "local_fog_volumes",
        .body = std::string(kFog) + "0" + std::string(kFogTail) + "200}]",
        .object_path = "local_fog_volumes[0].sort_priority",
      },
      {
        .group = "nodes",
        .body = R"([{"transform":{"scale":[1,0,1]}}])",
        .object_path = "nodes[0].transform.scale[1]",
      },
    };
    for (const auto& [group, body, object_path] : cases) {
      SCOPED_TRACE(std::string(object_path));
      auto document = MakeScene();
      document.at("nodes") = json::parse(R"([{}])");
      document[std::string(group)] = json::parse(body);
      auto diagnostics = std::vector<ImportDiagnostic> {};
      EXPECT_FALSE(SceneSource::FromDescriptor(
        document.dump(), "scene.json", diagnostics));
      const auto* const found
        = FindDiagnostic(diagnostics, internal::kScenePropertyOutOfRangeCode);
      ASSERT_NE(found, nullptr)
        << oxygen::cooker::test::DiagnosticSummary(diagnostics);
      EXPECT_EQ(found->object_path, object_path);
    }
  }

  //! Every violation of an object is reported, not only the first.
  NOLINT_TEST(SceneSourceTest, ReportsEveryPropertyOutsideItsRange)
  {
    auto document = MakeScene();
    document.at("nodes") = json::parse(R"([{}])");
    document.emplace("lights", json::parse(R"({"point":[{"node":0,"range":0,
      "source_radius":-1,"luminous_flux_lm":0}]})"));
    auto diagnostics = std::vector<ImportDiagnostic> {};
    EXPECT_FALSE(
      SceneSource::FromDescriptor(document.dump(), "scene.json", diagnostics));
    EXPECT_EQ(
      std::ranges::count(diagnostics, internal::kScenePropertyOutOfRangeCode,
        &ImportDiagnostic::code),
      3);
  }

  //! Values on the bounds the schema declares are valid.
  NOLINT_TEST(SceneSourceTest, AcceptsValuesOnTheGeneratedBounds)
  {
    auto document = MakeScene();
    document.at("nodes") = json::parse(R"([{}])");
    document.emplace("lights", json::parse(R"({
      "directional":[{"node":0,"intensity_lux":0,"cascade_count":1,
        "common":{"color_rgb":[0,1,0],"exposure_compensation_ev":-10}}]})"));
    auto diagnostics = std::vector<ImportDiagnostic> {};
    EXPECT_TRUE(
      SceneSource::FromDescriptor(document.dump(), "scene.json", diagnostics));
    EXPECT_TRUE(diagnostics.empty());
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
