//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <memory>
#include <numbers>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "AsyncImporterFullTestBase.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <Oxygen/Content/Loaders/SceneLoader.h>
#include <Oxygen/Cooker/Import/ImportOptions.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/Import/Naming.h>
#include <Oxygen/Cooker/Loose/LooseCookedLayout.h>
#include <Oxygen/Data/PakFormat_geometry.h>
#include <Oxygen/Data/PakFormat_render.h>
#include <Oxygen/Data/PakFormat_world.h>
#include <Oxygen/Data/SceneAsset.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using oxygen::content::import::ImportContentFlags;
using oxygen::content::import::ImportRequest;
using oxygen::content::import::LooseCookedLayout;
using oxygen::content::import::NormalizeNamingStrategy;
using oxygen::content::import::test::AsyncImporterFullTestBase;
namespace world = oxygen::data::pak::world;

class AsyncGltfImporterFullTest : public AsyncImporterFullTestBase {
protected:
  static auto LoadCameraScene(
    const oxygen::content::import::ImportReport& report)
    -> std::unique_ptr<oxygen::data::SceneAsset>
  {
    const auto inspection = LoadInspection(report.cooked_root);
    const auto entry
      = FindAssetOfType(inspection, oxygen::data::AssetType::kScene);
    EXPECT_TRUE(entry.has_value());
    if (!entry) {
      return {};
    }
    oxygen::serio::FileStream<> stream(
      report.cooked_root / entry->descriptor_relpath, std::ios::in);
    oxygen::serio::Reader reader(stream);
    const oxygen::content::LoaderContext context {
      .current_asset_key = entry->key,
      .desc_reader = &reader,
      .work_offline = true,
      .parse_only = true,
    };
    return oxygen::content::loaders::LoadSceneAsset(context);
  }
};

auto CameraNodeTransform(const world::NodeRecord& node) -> glm::mat4
{
  return glm::translate(glm::mat4(1.0F),
           glm::vec3(
             node.translation[0], node.translation[1], node.translation[2]))
    * glm::mat4_cast(glm::quat(
      node.rotation[3], node.rotation[0], node.rotation[1], node.rotation[2]))
    * glm::scale(
      glm::mat4(1.0F), glm::vec3(node.scale[0], node.scale[1], node.scale[2]));
}

auto CameraWorldTransform(const oxygen::data::SceneAsset& scene, uint32_t index)
  -> glm::mat4
{
  auto result = glm::mat4(1.0F);
  for (size_t depth = 0; depth < scene.GetNodes().size(); ++depth) {
    const auto& node = scene.GetNode(index);
    result = CameraNodeTransform(node) * result;
    if (node.parent_index == index) {
      return result;
    }
    index = node.parent_index;
  }
  ADD_FAILURE() << "Camera attachment hierarchy contains a cycle";
  return result;
}

auto ExpectCameraVector(const glm::vec3 actual, const glm::vec3 expected)
  -> void
{
  EXPECT_NEAR(actual.x, expected.x, 0.0001F);
  EXPECT_NEAR(actual.y, expected.y, 0.0001F);
  EXPECT_NEAR(actual.z, expected.z, 0.0001F);
}

NOLINT_TEST_F(AsyncGltfImporterFullTest,
  CameraAndLightAttachmentsPreserveSourceHierarchyAndBasis)
{
  using oxygen::content::import::NodePruningPolicy;
  using oxygen::data::AssetKey;
  for (const auto pruning :
    { NodePruningPolicy::kKeepAll, NodePruningPolicy::kDropEmptyNodes }) {
    const bool keep_empty = pruning == NodePruningPolicy::kKeepAll;
    SCOPED_TRACE(keep_empty);
    const auto root = MakeTempDir(
      keep_empty ? "gltf_camera_keep_all" : "gltf_camera_drop_empty");
    const auto source_path = root / "camera_attachments.gltf";
    {
      std::ofstream source(source_path);
      ASSERT_TRUE(source.is_open());
      source << R"({
        "asset": {"version": "2.0"},
        "extensionsUsed": ["KHR_lights_punctual"],
        "extensions": {"KHR_lights_punctual": {"lights": [
          {"type": "directional", "intensity": 5000,
           "color": [0.8, 0.6, 0.4]},
          {"type": "point", "intensity": 20},
          {"type": "spot", "intensity": 100, "color": [0.2, 0.4, 0.6],
           "spot": {"innerConeAngle": 0.2, "outerConeAngle": 0.5}}
        ]}},
        "buffers": [{
          "uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAA",
          "byteLength": 36
        }],
        "bufferViews": [{"buffer": 0, "byteOffset": 0, "byteLength": 36}],
        "accessors": [{
          "bufferView": 0, "componentType": 5126, "count": 3,
          "type": "VEC3", "min": [0, 0, 0], "max": [1, 1, 0]
        }],
        "materials": [{"name": "Surface"}],
        "meshes": [{"name": "Triangle", "primitives": [
          {"attributes": {"POSITION": 0}, "material": 0}
        ]}],
        "cameras": [
          {"type": "perspective", "perspective": {
            "yfov": 1.0, "aspectRatio": 1.5, "znear": 0.2, "zfar": 250}},
          {"type": "orthographic", "orthographic": {
            "xmag": 2, "ymag": 1, "znear": 0.3, "zfar": 40}},
          {"type": "perspective", "perspective": {
            "yfov": 0.7, "znear": 0.1, "zfar": 100}}
        ],
        "nodes": [
          {"name": "Parent", "translation": [4, 5, 6],
           "rotation": [0, 0, 0.7071067811865476, 0.7071067811865476],
           "extensions": {"KHR_lights_punctual": {"light": 1}},
           "children": [1, 4, 5, 7, 6]},
          {"name": "Mixed", "mesh": 0, "camera": 0,
           "translation": [1, 2, 3],
           "rotation": [0, 0.7071067811865476, 0, 0.7071067811865476],
           "extensions": {"KHR_lights_punctual": {"light": 0}},
           "children": [2]},
          {"name": "Child", "mesh": 0, "translation": [2, 0, 0]},
          {"name": "Identity", "camera": 2,
           "extensions": {"KHR_lights_punctual": {"light": 0}}},
          {"name": "Ortho", "camera": 1, "translation": [0, 4, 0],
           "rotation": [0.7071067811865476, 0, 0, 0.7071067811865476],
           "extensions": {"KHR_lights_punctual": {"light": 2}}},
          {"name": "Mixed_Camera", "mesh": 0},
          {"name": "Empty"},
          {"name": "Mixed_Light", "mesh": 0},
          {"name": "IdentitySpot",
           "extensions": {"KHR_lights_punctual": {"light": 2}}}
        ],
        "scenes": [{"nodes": [0, 8, 3]}],
        "scene": 0
      })";
      source.close();
      ASSERT_TRUE(source.good());
    }

    // Independent imports must retain the same source and attachment
    // identities.
    std::vector<AssetKey> first_ids;
    for (const auto pass : { 0, 1 }) {
      SCOPED_TRACE(pass);
      ImportRequest request {};
      request.source_path = source_path;
      request.cooked_root = root / ("cooked_" + std::to_string(pass));
      request.options.naming_strategy
        = std::make_shared<NormalizeNamingStrategy>();
      request.options.coordinate.bake_transforms_into_meshes = false;
      request.options.node_pruning = pruning;
      const auto scene_path
        = request.loose_cooked_layout.SceneVirtualPath(request.GetSceneName());
      const auto imported = RunImport(std::move(request));
      ASSERT_TRUE(imported.report.success);
      const auto scene = LoadCameraScene(imported.report);
      ASSERT_TRUE(scene);

      std::vector<std::string_view> names { "Parent", "Mixed", "Child", "Ortho",
        "Mixed_Camera", "Mixed_Light" };
      if (keep_empty) {
        names.push_back("Empty");
      }
      names.push_back("IdentitySpot");
      names.push_back("Identity");
      const auto source_count = static_cast<uint32_t>(names.size());
      names.insert(names.end(),
        { "Mixed_Camera_1", "Mixed_Light_1", "Ortho_Camera", "Ortho_Light",
          "IdentitySpot_Light", "Identity_Camera", "Identity_Light" });
      ASSERT_EQ(scene->GetNodes().size(), names.size());
      std::vector<AssetKey> ids;
      for (uint32_t index = 0; index < names.size(); ++index) {
        const auto& node = scene->GetNode(index);
        EXPECT_EQ(scene->GetNodeName(node), names[index]);
        EXPECT_EQ(node.node_id,
          AssetKey::FromVirtualPath(
            std::string(scene_path) + "/" + std::string(names[index])));
        EXPECT_TRUE(world::HasCanonicalNodeFlags(node));
        if (index >= source_count) {
          EXPECT_EQ(node.node_flags, 0U);
          EXPECT_EQ(node.inherited_flags, world::kSceneNodeFlags_Inheritable);
        } else {
          EXPECT_EQ(node.inherited_flags, 0U);
          const bool is_mesh
            = index == 1 || index == 2 || index == 4 || index == 5;
          const auto expected_flags = world::kSceneNodeFlag_Visible
            | (names[index] == "Empty" ? 0U
                                       : world::kSceneNodeFlag_CastsShadows)
            | (is_mesh ? world::kSceneNodeFlag_ReceivesShadows : 0U);
          EXPECT_EQ(node.node_flags, expected_flags);
        }
        ids.push_back(node.node_id);
      }
      if (pass == 0) {
        first_ids = ids;
      } else {
        EXPECT_EQ(ids, first_ids);
      }

      // Original local transforms and hierarchy are independent of attachments.
      const auto half_sqrt_two = std::sqrt(0.5F);
      const std::array positions { glm::vec3(4, -6, 5), glm::vec3(1, -3, 2),
        glm::vec3(2, 0, 0), glm::vec3(0, 0, 4) };
      const std::array rotations { glm::quat(
                                     half_sqrt_two, 0, -half_sqrt_two, 0),
        glm::quat(half_sqrt_two, 0, 0, half_sqrt_two), glm::quat(1, 0, 0, 0),
        glm::quat(half_sqrt_two, half_sqrt_two, 0, 0) };
      for (uint32_t index = 0; index < source_count; ++index) {
        const auto& node = scene->GetNode(index);
        const auto expected_parent = index >= source_count - 2 ? index
          : index == 2                                         ? 1U
                                                               : 0U;
        EXPECT_EQ(node.parent_index, expected_parent);
        const auto position
          = index < positions.size() ? positions.at(index) : glm::vec3(0);
        const auto rotation = index < rotations.size() ? rotations.at(index)
                                                       : glm::quat(1, 0, 0, 0);
        const auto expected
          = glm::translate(glm::mat4(1), position) * glm::mat4_cast(rotation);
        const auto actual = CameraNodeTransform(node);
        for (glm::length_t column = 0; column < 4; ++column) {
          ExpectCameraVector(
            glm::vec3(actual[column]), glm::vec3(expected[column]));
        }
      }

      const auto perspective
        = scene->GetComponents<world::PerspectiveCameraRecord>();
      const auto ortho
        = scene->GetComponents<world::OrthographicCameraRecord>();
      ASSERT_EQ(perspective.size(), 2U);
      ASSERT_EQ(ortho.size(), 1U);
      EXPECT_EQ(perspective[0].node_index, source_count);
      EXPECT_EQ(ortho[0].node_index, source_count + 2);
      EXPECT_EQ(perspective[1].node_index, source_count + 5);
      EXPECT_FLOAT_EQ(perspective[0].fov_y, 1.0F);
      EXPECT_FLOAT_EQ(perspective[0].aspect_ratio, 1.5F);
      EXPECT_FLOAT_EQ(perspective[0].near_plane, 0.2F);
      EXPECT_FLOAT_EQ(perspective[0].far_plane, 250.0F);
      EXPECT_FLOAT_EQ(ortho[0].near_plane, 0.3F);
      EXPECT_FLOAT_EQ(ortho[0].far_plane, 40.0F);

      const std::array camera_parents { 1U, 3U, source_count - 1 };
      const std::array camera_positions { glm::vec3(2, -9, 6),
        glm::vec3(0, -6, 5), glm::vec3(0) };
      const std::array camera_forwards { glm::vec3(0, 0, -1),
        glm::vec3(-1, 0, 0), glm::vec3(0, 1, 0) };
      const std::array camera_ups { glm::vec3(-1, 0, 0), glm::vec3(0, -1, 0),
        glm::vec3(0, 0, 1) };
      const std::array camera_offsets { 0U, 2U, 5U };
      for (uint32_t ordinal = 0; ordinal < camera_parents.size(); ++ordinal) {
        const auto index = source_count + camera_offsets[ordinal];
        SCOPED_TRACE(names[index]);
        const auto& node = scene->GetNode(index);
        EXPECT_EQ(node.parent_index, camera_parents[ordinal]);
        const auto transform = CameraWorldTransform(*scene, index);
        ExpectCameraVector(glm::vec3(transform[3]), camera_positions[ordinal]);
        ExpectCameraVector(
          glm::normalize(-glm::vec3(transform[2])), camera_forwards[ordinal]);
        ExpectCameraVector(
          glm::normalize(glm::vec3(transform[1])), camera_ups[ordinal]);
        EXPECT_NEAR(glm::determinant(glm::mat3(transform)), 1.0F, 0.0001F);
      }

      const auto renderables = scene->GetComponents<world::RenderableRecord>();
      ASSERT_EQ(renderables.size(), 4U);
      EXPECT_EQ(renderables[0].node_index, 1U);
      EXPECT_EQ(renderables[1].node_index, 2U);
      EXPECT_EQ(renderables[2].node_index, 4U);
      EXPECT_EQ(renderables[3].node_index, 5U);
      EXPECT_EQ(renderables[0].geometry_key, renderables[1].geometry_key);
      EXPECT_EQ(renderables[0].geometry_key, renderables[2].geometry_key);
      EXPECT_EQ(renderables[0].geometry_key, renderables[3].geometry_key);
      const auto lights = scene->GetComponents<world::DirectionalLightRecord>();
      ASSERT_EQ(lights.size(), 2U);
      EXPECT_EQ(lights[0].node_index, source_count + 1);
      EXPECT_EQ(lights[1].node_index, source_count + 6);
      for (const auto& light : lights) {
        EXPECT_FLOAT_EQ(light.intensity_lux, 5000.0F);
        ExpectCameraVector(
          { light.common.color_rgb[0], light.common.color_rgb[1],
            light.common.color_rgb[2] },
          { 0.8F, 0.6F, 0.4F });
      }
      const auto spots = scene->GetComponents<world::SpotLightRecord>();
      ASSERT_EQ(spots.size(), 2U);
      EXPECT_EQ(spots[0].node_index, source_count + 3);
      EXPECT_EQ(spots[1].node_index, source_count + 4);
      EXPECT_EQ(
        scene->GetNode(spots[1].node_index).parent_index, source_count - 2);
      const auto identity_spot
        = CameraWorldTransform(*scene, spots[1].node_index);
      ExpectCameraVector(glm::vec3(identity_spot[3]), { 0, 0, 0 });
      ExpectCameraVector(
        glm::normalize(glm::vec3(identity_spot * glm::vec4(0, -1, 0, 0))),
        { 0, 1, 0 });
      ExpectCameraVector(
        glm::normalize(glm::vec3(identity_spot[2])), { 0, 0, 1 });
      EXPECT_NEAR(glm::determinant(glm::mat3(identity_spot)), 1.0F, 0.0001F);
      EXPECT_FLOAT_EQ(spots[0].inner_cone_angle_radians, 0.2F);
      EXPECT_FLOAT_EQ(spots[0].outer_cone_angle_radians, 0.5F);
      EXPECT_NEAR(spots[0].luminous_flux_lm,
        200.0 * std::numbers::pi
          * ((1.0 - std::cos(0.2F))
            + ((std::cos(0.2F) - std::cos(0.5F)) / 3.0)),
        0.0001);
      ExpectCameraVector(
        { spots[0].common.color_rgb[0], spots[0].common.color_rgb[1],
          spots[0].common.color_rgb[2] },
        { 0.2F, 0.4F, 0.6F });
      for (uint32_t ordinal = 0; ordinal < camera_parents.size(); ++ordinal) {
        const auto index = source_count + camera_offsets[ordinal] + 1;
        SCOPED_TRACE(names[index]);
        const auto& node = scene->GetNode(index);
        EXPECT_EQ(node.parent_index, camera_parents[ordinal]);
        EXPECT_NE(
          node.inherited_flags & world::kSceneNodeFlag_CastsShadows, 0U);
        const auto transform = CameraWorldTransform(*scene, index);
        ExpectCameraVector(glm::vec3(transform[3]), camera_positions[ordinal]);
        // glTF cameras and oriented lights share local -Z. Oxygen uses -Z
        // for cameras and -Y for lights; their world rays must agree.
        ExpectCameraVector(
          glm::normalize(glm::vec3(transform * glm::vec4(0, -1, 0, 0))),
          camera_forwards[ordinal]);
        ExpectCameraVector(
          glm::normalize(glm::vec3(transform[2])), camera_ups[ordinal]);
        EXPECT_NEAR(glm::determinant(glm::mat3(transform)), 1.0F, 0.0001F);
      }
      const auto points = scene->GetComponents<world::PointLightRecord>();
      ASSERT_EQ(points.size(), 1U);
      EXPECT_EQ(points[0].node_index, 0U);
      EXPECT_NEAR(
        points[0].luminous_flux_lm, 80.0F * std::numbers::pi_v<float>, 0.0001F);
      // Check the original owner's unchanged basis. Under C * Rz * Ry * C^-1,
      // its local -Y maps to +Z; the camera child has a different local basis.
      ExpectCameraVector(
        glm::normalize(
          glm::vec3(CameraWorldTransform(*scene, 1) * glm::vec4(0, -1, 0, 0))),
        { 0, 0, 1 });
      ExpectCameraVector(
        glm::vec3(CameraWorldTransform(*scene, 2)[3]), { 2, -7, 6 });
    }
  }
}

NOLINT_TEST_F(AsyncGltfImporterFullTest, LocalRangesPreserveExplicitAndResolveOmittedPolicy)
{
  for (const float fallback : { 4096.0F, 123.0F }) {
    const auto root = MakeTempDir("gltf_ranges_" + std::to_string(fallback));
    const auto source_path = root / "lights.gltf";
    std::ofstream source(source_path);
    source << R"({"asset":{"version":"2.0"},"extensionsUsed":["KHR_lights_punctual"],
      "extensions":{"KHR_lights_punctual":{"lights":[
        {"type":"point","range":17.5}, {"type":"point"},
        {"type":"spot","range":23.75,"spot":{}}, {"type":"spot","spot":{}}]}},
      "nodes":[{"extensions":{"KHR_lights_punctual":{"light":0}}},
        {"extensions":{"KHR_lights_punctual":{"light":1}}},
        {"extensions":{"KHR_lights_punctual":{"light":2}}},
        {"extensions":{"KHR_lights_punctual":{"light":3}}}],
      "scenes":[{"nodes":[0,1,2,3]}],"scene":0})";
    source.close();
    ImportRequest request {};
    request.source_path = source_path;
    request.cooked_root = root / "cooked";
    request.options.gltf_omitted_light_range_m = fallback;
    const auto imported = RunImport(std::move(request));
    ASSERT_TRUE(imported.report.success);
    const auto scene = LoadCameraScene(imported.report);
    ASSERT_TRUE(scene);
    const auto points = scene->GetComponents<world::PointLightRecord>();
    const auto spots = scene->GetComponents<world::SpotLightRecord>();
    ASSERT_EQ(points.size(), 2U);
    ASSERT_EQ(spots.size(), 2U);
    auto point = points.begin();
    EXPECT_FLOAT_EQ(point->range, 17.5F);
    EXPECT_FLOAT_EQ((++point)->range, fallback);
    auto spot = spots.begin();
    EXPECT_FLOAT_EQ(spot->range, 23.75F);
    EXPECT_FLOAT_EQ((++spot)->range, fallback);
  }
}

NOLINT_TEST_F(AsyncGltfImporterFullTest,
  SpotPhotometryPreservesPeakCandelaThroughCookAndLoad)
{
  struct Cone {
    float inner;
    float outer;
  };
  const auto cones = std::array {
    Cone { .inner = 0.0F, .outer = 0.5F },
    Cone { .inner = 0.2F, .outer = 0.5F },
    Cone { .inner = 0.5F, .outer = 0.5F },
    Cone { .inner = 0.0F, .outer = std::numbers::pi_v<float> / 2.0F },
    Cone { .inner = 1.2F, .outer = std::numbers::pi_v<float> / 2.0F },
  };
  auto lights = nlohmann::json::array();
  auto nodes = nlohmann::json::array();
  auto roots = nlohmann::json::array();
  for (std::size_t index = 0; index < cones.size(); ++index) {
    const auto cone = cones.at(index);
    lights.push_back({
      { "type", "spot" },
      { "intensity", 100.0F },
      {
        "spot",
        {
          { "innerConeAngle", cone.inner },
          { "outerConeAngle", cone.outer },
        },
      },
    });
    nodes.push_back({
      { "name", "Spot" + std::to_string(index) },
      { "extensions", { { "KHR_lights_punctual", { { "light", index } } } } },
    });
    roots.push_back(index);
  }
  const auto document = nlohmann::json {
    { "asset", { { "version", "2.0" } } },
    { "extensionsUsed", { "KHR_lights_punctual" } },
    { "extensions", { { "KHR_lights_punctual", { { "lights", lights } } } } },
    { "nodes", nodes },
    { "scenes", { { { "nodes", roots } } } },
    { "scene", 0 },
  };
  const auto root = MakeTempDir("gltf_spot_photometry");
  const auto source_path = root / "lights.gltf";
  {
    std::ofstream source(source_path);
    source << document.dump();
    source.close();
    ASSERT_TRUE(source.good());
  }
  ImportRequest request {};
  request.source_path = source_path;
  request.cooked_root = root / "cooked";
  const auto imported = RunImport(std::move(request));
  ASSERT_TRUE(imported.report.success);
  const auto scene = LoadCameraScene(imported.report);
  ASSERT_TRUE(scene);
  const auto spots = scene->GetComponents<world::SpotLightRecord>();
  ASSERT_EQ(spots.size(), cones.size());
  std::size_t cone_index = 0;
  for (const auto& spot : spots) {
    SCOPED_TRACE(cone_index);
    const auto cone = cones.at(cone_index++);
    EXPECT_EQ(spot.inner_cone_angle_radians, cone.inner);
    EXPECT_EQ(spot.outer_cone_angle_radians, cone.outer);
    const auto inner = static_cast<double>(cone.inner);
    const auto outer = cone.outer == std::numbers::pi_v<float> / 2.0F
      ? std::numbers::pi / 2.0
      : static_cast<double>(cone.outer);
    // Independently integrate intensity over polar solid angle, using the
    // cosine-difference identity to retain the narrow cone's angular ramp.
    constexpr auto kSamples = 32768;
    const auto step = outer / kSamples;
    auto integral = 0.0;
    for (auto sample = 0; sample < kSamples; ++sample) {
      const auto theta = (sample + 0.5) * step;
      const auto ramp = theta <= inner
        ? 1.0
        : std::sin((outer + theta) / 2.0) * std::sin((outer - theta) / 2.0)
          / (std::sin((outer + inner) / 2.0) * std::sin((outer - inner) / 2.0));
      integral += ramp * ramp * std::sin(theta) * step;
    }
    const auto solid_angle = 2.0 * std::numbers::pi * integral;
    EXPECT_NEAR(spot.luminous_flux_lm / solid_angle, 100.0, 2.0e-5);
  }
}

NOLINT_TEST_F(
  AsyncGltfImporterFullTest, InvalidLocalLightValuesFailImportWithoutClamping)
{
  const auto invalid_lights = std::array {
    R"({"type":"point","intensity":-1})",
    R"({"type":"point","intensity":3e38})",
    R"({"type":"point","range":0})",
    R"({"type":"point","range":-1})",
    R"({"type":"spot","range":0,"spot":{}})",
    R"({"type":"spot","intensity":-1,"spot":{"innerConeAngle":0.2,"outerConeAngle":0.5}})",
    R"({"type":"spot","intensity":100,"spot":{"innerConeAngle":-0.1,"outerConeAngle":0.5}})",
    R"({"type":"spot","intensity":100,"spot":{"innerConeAngle":0.6,"outerConeAngle":0.5}})",
    R"({"type":"spot","intensity":100,"spot":{"innerConeAngle":0,"outerConeAngle":0}})",
    R"({"type":"spot","intensity":100,"spot":{"innerConeAngle":0,"outerConeAngle":1.6}})",
    R"({"type":"spot","intensity":100,"spot":{"innerConeAngle":1.5707963267948966,"outerConeAngle":1.5707963267948966}})",
    R"({"type":"spot","intensity":1e-30,"spot":{"innerConeAngle":0,"outerConeAngle":1e-5}})",
  };
  for (std::size_t index = 0; index < invalid_lights.size(); ++index) {
    SCOPED_TRACE(invalid_lights.at(index));
    const auto document = nlohmann::json {
      { "asset", { { "version", "2.0" } } },
      { "extensionsUsed", { "KHR_lights_punctual" } },
      {
        "extensions",
        {
          {
            "KHR_lights_punctual",
            {
              {
                "lights",
                {
                  nlohmann::json { { "type", "point" }, { "intensity", 20 } },
                  nlohmann::json::parse(invalid_lights.at(index)),
                },
              },
            },
          },
        },
      },
      {
        "nodes",
        {
          {
            {
              "extensions",
              { { "KHR_lights_punctual", { { "light", 0 } } } },
            },
          },
          {
            {
              "extensions",
              { { "KHR_lights_punctual", { { "light", 1 } } } },
            },
          },
        },
      },
      { "scenes", { { { "nodes", { 0, 1 } } } } },
      { "scene", 0 },
    };
    const auto root
      = MakeTempDir("gltf_invalid_photometry_" + std::to_string(index));
    const auto source_path = root / "invalid.gltf";
    {
      std::ofstream source(source_path);
      source << document.dump();
      source.close();
      ASSERT_TRUE(source.good());
    }
    ImportRequest request {};
    request.source_path = source_path;
    request.cooked_root = root / "cooked";
    const auto imported = RunImport(std::move(request));
    EXPECT_FALSE(imported.report.success);
    const auto expected_code = nlohmann::json::parse(invalid_lights.at(index)).contains("range")
      ? "scene.light.range_invalid" : "scene.light.photometry_invalid";
    EXPECT_TRUE(std::ranges::any_of(
      imported.report.diagnostics, [expected_code](const auto& diagnostic) {
        return diagnostic.code == expected_code;
      }));
  }
}

NOLINT_TEST_F(AsyncGltfImporterFullTest,
  CaseOnlyMaterialNamesKeepDistinctDescriptorsAndMeshBindings)
{
  using oxygen::content::lc::Inspection;
  using oxygen::data::AssetType;
  using oxygen::data::pak::geometry::GeometryAssetDesc;
  using oxygen::data::pak::geometry::MeshDesc;
  using oxygen::data::pak::geometry::MeshViewDesc;
  using oxygen::data::pak::geometry::SubMeshDesc;
  using oxygen::data::pak::render::MaterialAssetDesc;
  using oxygen::serio::FileStream;
  using oxygen::serio::Reader;

  const auto temp_dir = MakeTempDir("async_gltf_case_only_materials");
  const auto source_path = temp_dir / "case_only_materials.gltf";
  {
    std::ofstream source(source_path);
    ASSERT_TRUE(source.is_open());
    source << R"({
      "asset": {"version": "2.0"},
      "buffers": [{
        "uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAA",
        "byteLength": 36
      }],
      "bufferViews": [{"buffer": 0, "byteOffset": 0, "byteLength": 36}],
      "accessors": [{
        "bufferView": 0, "componentType": 5126, "count": 3,
        "type": "VEC3", "min": [0, 0, 0], "max": [1, 1, 0]
      }],
      "materials": [
        {"name": "Paint", "pbrMetallicRoughness": {
          "baseColorFactor": [0.9, 0.1, 0.2, 1], "roughnessFactor": 0.2
        }},
        {"name": "paint", "pbrMetallicRoughness": {
          "baseColorFactor": [0.1, 0.8, 0.3, 1], "roughnessFactor": 0.8
        }}
      ],
      "meshes": [{"name": "TwoMaterials", "primitives": [
        {"attributes": {"POSITION": 0}, "material": 0},
        {"attributes": {"POSITION": 0}, "material": 1}
      ]}],
      "nodes": [{"name": "TwoMaterials", "mesh": 0}],
      "scenes": [{"nodes": [0]}],
      "scene": 0
    })";
    source.close();
    ASSERT_TRUE(source.good());
  }

  ImportRequest request {
    .source_path = source_path,
    .additional_sources = {},
    .cooked_root = temp_dir / "Cooked",
    .loose_cooked_layout = LooseCookedLayout {},
    .source_key = std::nullopt,
    .job_name = std::nullopt,
    .orchestration = std::nullopt,
    .options = {},
  };
  request.options.naming_strategy = std::make_shared<NormalizeNamingStrategy>();
  request.options.import_content = ImportContentFlags::kAll;

  const auto run_result = RunImport(std::move(request));
  EXPECT_EQ(run_result.finished_id, run_result.job_id);
  ASSERT_TRUE(run_result.report.success);

  const auto inspection = LoadInspection(run_result.report.cooked_root);
  std::vector<Inspection::AssetEntry> materials;
  for (const auto& entry : inspection.Assets()) {
    if (entry.asset_type == static_cast<uint8_t>(AssetType::kMaterial)) {
      materials.push_back(entry);
    }
  }
  ASSERT_EQ(materials.size(), 2U);
  EXPECT_NE(materials[0].key, materials[1].key);

  // Check the storage contract even on a case-sensitive test filesystem.
  const auto fold_ascii_case = [](std::string path) {
    std::ranges::transform(path, path.begin(), [](const char value) {
      return value >= 'A' && value <= 'Z'
        ? static_cast<char>(value + ('a' - 'A'))
        : value;
    });
    return path;
  };
  EXPECT_NE(fold_ascii_case(materials[0].descriptor_relpath),
    fold_ascii_case(materials[1].descriptor_relpath));
  EXPECT_NE(fold_ascii_case(materials[0].virtual_path),
    fold_ascii_case(materials[1].virtual_path));

  ASSERT_EQ(CountAssetsOfType(inspection, AssetType::kGeometry), 1U);
  const auto geometry_entry = FindAssetOfType(inspection, AssetType::kGeometry);
  ASSERT_TRUE(geometry_entry.has_value());
  FileStream<> geometry_stream(run_result.report.cooked_root
      / std::filesystem::path(geometry_entry->descriptor_relpath),
    std::ios::in);
  Reader<FileStream<>> geometry_reader(geometry_stream);
  auto packed = geometry_reader.ScopedAlignment(1);
  GeometryAssetDesc geometry {};
  ASSERT_TRUE(geometry_reader.ReadBlobInto(
    std::as_writable_bytes(std::span<GeometryAssetDesc, 1>(&geometry, 1))));
  ASSERT_EQ(geometry.lod_count, 1U);
  MeshDesc mesh {};
  ASSERT_TRUE(geometry_reader.ReadBlobInto(
    std::as_writable_bytes(std::span<MeshDesc, 1>(&mesh, 1))));
  ASSERT_TRUE(mesh.IsStandard());
  ASSERT_EQ(mesh.submesh_count, 2U);
  ASSERT_EQ(mesh.mesh_view_count, 2U);

  constexpr std::array expected_colors {
    std::array { 0.9F, 0.1F, 0.2F, 1.0F },
    std::array { 0.1F, 0.8F, 0.3F, 1.0F },
  };
  constexpr std::array expected_roughness { 0.2F, 0.8F };
  std::array<oxygen::data::AssetKey, 2> bound_keys {};
  for (size_t slot = 0; slot < bound_keys.size(); ++slot) {
    SCOPED_TRACE(slot);
    SubMeshDesc submesh {};
    ASSERT_TRUE(geometry_reader.ReadBlobInto(
      std::as_writable_bytes(std::span<SubMeshDesc, 1>(&submesh, 1))));
    ASSERT_EQ(submesh.mesh_view_count, 1U);
    EXPECT_EQ(std::string(submesh.name), "mat_" + std::to_string(slot));
    MeshViewDesc view {};
    ASSERT_TRUE(geometry_reader.ReadBlobInto(
      std::as_writable_bytes(std::span<MeshViewDesc, 1>(&view, 1))));
    EXPECT_EQ(view.index_count, 3U);
    bound_keys[slot] = submesh.material_asset_key;

    const auto material_entry = std::ranges::find(
      materials, submesh.material_asset_key, &Inspection::AssetEntry::key);
    ASSERT_NE(material_entry, materials.end());
    FileStream<> material_stream(run_result.report.cooked_root
        / std::filesystem::path(material_entry->descriptor_relpath),
      std::ios::in);
    Reader<FileStream<>> material_reader(material_stream);
    auto material_packed = material_reader.ScopedAlignment(1);
    MaterialAssetDesc material {};
    ASSERT_TRUE(material_reader.ReadBlobInto(
      std::as_writable_bytes(std::span<MaterialAssetDesc, 1>(&material, 1))));
    for (size_t channel = 0; channel < expected_colors[slot].size();
      ++channel) {
      EXPECT_FLOAT_EQ(
        material.base_color[channel], expected_colors[slot][channel]);
    }
    EXPECT_EQ(
      material.roughness, oxygen::data::Unorm16(expected_roughness[slot]));
  }
  EXPECT_NE(bound_keys[0], bound_keys[1]);
}

//! Full async import validates supported glTF content is emitted.
/*!
 Uses the async glTF import job to process Tabuleiro.glb and validates the
 cooked outputs contain the expected content types.
*/
NOLINT_TEST_F(AsyncGltfImporterFullTest, AsyncBackendImportsFullTabuleiroScene)
{
  // Arrange
  const auto models_dir = TestModelsDirFromFile();
  const auto source_path = models_dir / "Tabuleiro.glb";
  if (!std::filesystem::exists(source_path)) {
    GTEST_SKIP() << "Missing test asset: " << source_path.string();
  }

  const auto temp_dir = MakeTempDir("async_gltf_tabuleiro");
  ImportRequest request {
    .source_path = source_path,
    .additional_sources = {},
    .cooked_root = temp_dir,
    .loose_cooked_layout = LooseCookedLayout {},
    .source_key = std::nullopt,
    .job_name = std::nullopt,
    .orchestration = std::nullopt,
    .options = {},
  };
  request.options.naming_strategy = std::make_shared<NormalizeNamingStrategy>();
  request.options.import_content = ImportContentFlags::kAll;

  // Act
  const auto run_result = RunImport(std::move(request));

  // Assert
  EXPECT_EQ(run_result.finished_id, run_result.job_id);
  EXPECT_TRUE(run_result.report.success);

  const ExpectedSceneOutputs expected {
    .materials = 3U,
    .geometry = 5U,
    .scenes = 1U,
    .nodes_min = std::nullopt,
    .texture_files = 0U,
  };
  ValidateSceneOutputs(run_result.report, expected);

  const auto scene = LoadSceneReadback(run_result.report);
  ASSERT_FALSE(scene.renderables.empty());
  for (const auto& renderable : scene.renderables) {
    ASSERT_LT(renderable.node_index, scene.nodes.size());
    const auto node_flags = scene.nodes[renderable.node_index].node_flags;
    EXPECT_NE(node_flags & world::kSceneNodeFlag_CastsShadows, 0U);
    EXPECT_NE(node_flags & world::kSceneNodeFlag_ReceivesShadows, 0U);
  }

  GTEST_LOG_(INFO) << "Cooked root: " << run_result.report.cooked_root.string();
}

NOLINT_TEST_F(
  AsyncGltfImporterFullTest, AsyncBackendImportsLightExtrasAsSceneSemantics)
{
  const auto models_dir = TestModelsDirFromFile();
  const auto source_path = models_dir / "light_overrides.gltf";
  if (!std::filesystem::exists(source_path)) {
    GTEST_SKIP() << "Missing test asset: " << source_path.string();
  }

  const auto temp_dir = MakeTempDir("async_gltf_light_overrides");
  ImportRequest request {
    .source_path = source_path,
    .additional_sources = {},
    .cooked_root = temp_dir,
    .loose_cooked_layout = LooseCookedLayout {},
    .source_key = std::nullopt,
    .job_name = std::nullopt,
    .orchestration = std::nullopt,
    .options = {},
  };
  request.options.naming_strategy = std::make_shared<NormalizeNamingStrategy>();
  request.options.import_content = ImportContentFlags::kAll;

  const auto run_result = RunImport(std::move(request));

  EXPECT_EQ(run_result.finished_id, run_result.job_id);
  EXPECT_TRUE(run_result.report.success);

  const auto scene = LoadSceneReadback(run_result.report);
  EXPECT_TRUE(scene.renderables.empty());
  EXPECT_TRUE(scene.directional_lights.empty());
  ASSERT_EQ(scene.point_lights.size(), 1U);
  EXPECT_TRUE(scene.spot_lights.empty());

  const auto& light = scene.point_lights.front();
  EXPECT_EQ(light.common.affects_world, 0U);
  EXPECT_EQ(light.common.casts_shadows, 0U);
}

//! Async import succeeds for glTF Sponza when asset is available.
/*!
 Validates the async glTF importer can handle the external-texture Sponza
 dataset when the source file is present on disk.
*/
NOLINT_TEST_F(AsyncGltfImporterFullTest, DISABLEDAsyncBackendImportsSponza)
{
  // Arrange
  const auto source_path = std::filesystem::path(
    "F:\\projects\\main_sponza\\NewSponza_Main_glTF_003.gltf");
  if (!std::filesystem::exists(source_path)) {
    GTEST_SKIP() << "Missing test asset: " << source_path.string();
  }

  const auto temp_dir = MakeTempDir("async_gltf_sponza");
  ImportRequest request {
    .source_path = source_path,
    .additional_sources = {},
    .cooked_root = temp_dir,
    .loose_cooked_layout = LooseCookedLayout {},
    .source_key = std::nullopt,
    .job_name = std::nullopt,
    .orchestration = std::nullopt,
    .options = {},
  };
  request.options.naming_strategy = std::make_shared<NormalizeNamingStrategy>();
  request.options.import_content = ImportContentFlags::kAll;

  const auto run_result = RunImport(std::move(request));

  // Assert
  EXPECT_EQ(run_result.finished_id, run_result.job_id);
  EXPECT_TRUE(run_result.report.success);
  GTEST_LOG_(INFO) << "Cooked root: " << run_result.report.cooked_root.string();
}

} // namespace
