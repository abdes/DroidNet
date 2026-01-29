//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/Constants.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/ProceduralMeshes.h>
#include <Oxygen/Data/ShaderReference.h>
#include <Oxygen/Scene/Light/PointLight.h>
#include <Oxygen/Scene/Light/SpotLight.h>

#include "LightBench/LightScene.h"

namespace oxygen::examples::light_bench {

namespace {
  auto RotationFromForwardToDir(const Vec3& to_dir) -> Quat
  {
    const Vec3 from_dir = oxygen::space::move::Forward;
    const Vec3 to = glm::normalize(to_dir);
    const float cos_theta = std::clamp(glm::dot(from_dir, to), -1.0F, 1.0F);

    if (cos_theta >= 0.9999F) {
      return Quat(1.0F, 0.0F, 0.0F, 0.0F);
    }

    if (cos_theta <= -0.9999F) {
      const Vec3 axis = oxygen::space::move::Up;
      return glm::angleAxis(oxygen::math::Pi, axis);
    }

    const Vec3 axis = glm::normalize(glm::cross(from_dir, to));
    const float angle = std::acos(cos_theta);
    return glm::angleAxis(angle, axis);
  }

  auto NormalizeOrFallback(const Vec3& direction) -> Vec3
  {
    const float len2 = glm::dot(direction, direction);
    if (len2 <= oxygen::math::Epsilon) {
      return oxygen::space::move::Forward;
    }
    return direction / std::sqrt(len2);
  }
} // namespace

LightScene::LightScene()
  : LightScene("LightBench")
{
}

LightScene::LightScene(std::string_view name)
  : name_(name)
{
  ResetSceneObject("18% Gray Card");
  ResetSceneObject("White Card");
  ResetSceneObject("Black Card");
  ResetSceneObject("Matte Sphere");
  ResetSceneObject("Glossy Sphere");
  ResetSceneObject("Ground Plane");
}

auto LightScene::CreateScene() -> std::unique_ptr<scene::Scene>
{
  return std::make_unique<scene::Scene>(name_);
}

void LightScene::SetScene(observer_ptr<scene::Scene> scene)
{
  if (scene_ == scene) {
    return;
  }

  scene_ = scene;
  point_light_node_ = {};
  spot_light_node_ = {};
  gray_card_node_ = {};
  white_card_node_ = {};
  black_card_node_ = {};
  matte_sphere_node_ = {};
  glossy_sphere_node_ = {};
  ground_plane_node_ = {};

  if (scene_) {
    EnsureSceneGeometry();
  }
}

void LightScene::ClearScene()
{
  SetScene(observer_ptr<scene::Scene> { nullptr });
}

auto LightScene::Update() -> void
{
  if (!scene_) {
    return;
  }

  EnsureSceneGeometry();
  ApplySceneTransforms();

  if (point_light_state_.enabled) {
    EnsurePointLightNode();
    ApplyPointLightState();
  } else if (point_light_node_.IsAlive()) {
    if (auto point = point_light_node_.GetLightAs<scene::PointLight>()) {
      point->get().Common().affects_world = false;
    }
  }

  if (spot_light_state_.enabled) {
    EnsureSpotLightNode();
    ApplySpotLightState();
  } else if (spot_light_node_.IsAlive()) {
    if (auto spot = spot_light_node_.GetLightAs<scene::SpotLight>()) {
      spot->get().Common().affects_world = false;
    }
  }
}

auto LightScene::ApplyScenePreset(const ScenePreset preset) -> void
{
  switch (preset) {
  case ScenePreset::kBaseline:
    gray_card_state_.enabled = true;
    white_card_state_.enabled = false;
    black_card_state_.enabled = false;
    matte_sphere_state_.enabled = false;
    glossy_sphere_state_.enabled = false;
    ground_plane_state_.enabled = false;
    break;
  case ScenePreset::kThreeCards:
    gray_card_state_.enabled = true;
    white_card_state_.enabled = true;
    black_card_state_.enabled = true;
    matte_sphere_state_.enabled = false;
    glossy_sphere_state_.enabled = false;
    ground_plane_state_.enabled = false;
    break;
  case ScenePreset::kSpecular:
    gray_card_state_.enabled = false;
    white_card_state_.enabled = false;
    black_card_state_.enabled = false;
    matte_sphere_state_.enabled = true;
    glossy_sphere_state_.enabled = true;
    ground_plane_state_.enabled = true;
    break;
  case ScenePreset::kFull:
    gray_card_state_.enabled = true;
    white_card_state_.enabled = true;
    black_card_state_.enabled = true;
    matte_sphere_state_.enabled = true;
    glossy_sphere_state_.enabled = true;
    ground_plane_state_.enabled = true;
    break;
  }
}

auto LightScene::ResetSceneObject(std::string_view label) -> void
{
  if (label == "18% Gray Card") {
    gray_card_state_ = SceneObjectState {
      .enabled = true,
      .position = Vec3 { -1.6F, 0.0F, 1.0F },
      .rotation_deg = Vec3 { -90.0F, 0.0F, 0.0F },
      .scale = Vec3 { 1.0F, 1.0F, 1.0F },
    };
    return;
  }
  if (label == "White Card") {
    white_card_state_ = SceneObjectState {
      .enabled = true,
      .position = Vec3 { 0.0F, 0.0F, 1.0F },
      .rotation_deg = Vec3 { -90.0F, 0.0F, 0.0F },
      .scale = Vec3 { 1.0F, 1.0F, 1.0F },
    };
    return;
  }
  if (label == "Black Card") {
    black_card_state_ = SceneObjectState {
      .enabled = true,
      .position = Vec3 { 1.6F, 0.0F, 1.0F },
      .rotation_deg = Vec3 { -90.0F, 0.0F, 0.0F },
      .scale = Vec3 { 1.0F, 1.0F, 1.0F },
    };
    return;
  }
  if (label == "Matte Sphere") {
    matte_sphere_state_ = SceneObjectState {
      .enabled = false,
      .position = Vec3 { -1.0F, 2.0F, 1.0F },
      .rotation_deg = Vec3 { 0.0F, 0.0F, 0.0F },
      .scale = Vec3 { 1.0F, 1.0F, 1.0F },
    };
    return;
  }
  if (label == "Glossy Sphere") {
    glossy_sphere_state_ = SceneObjectState {
      .enabled = false,
      .position = Vec3 { 1.0F, 2.0F, 1.0F },
      .rotation_deg = Vec3 { 0.0F, 0.0F, 0.0F },
      .scale = Vec3 { 1.0F, 1.0F, 1.0F },
    };
    return;
  }
  if (label == "Ground Plane") {
    ground_plane_state_ = SceneObjectState {
      .enabled = false,
      .position = Vec3 { 0.0F, 0.0F, 0.0F },
      .rotation_deg = Vec3 { 0.0F, 0.0F, 0.0F },
      .scale = Vec3 { 8.0F, 8.0F, 1.0F },
    };
  }
}

auto LightScene::Reset() -> void { ClearScene(); }

auto LightScene::EnsureSceneGeometry() -> void
{
  EnsureGeometryAssets();
  EnsureReferenceNodes();
}

auto LightScene::EnsureGeometryAssets() -> void
{
  if (gray_card_geo_) {
    return;
  }

  const auto gray_mat = MakeSolidColorMaterial(
    "GrayCard", Vec4 { 0.18F, 0.18F, 0.18F, 1.0F }, 0.9F, 0.0F, true);
  const auto white_mat = MakeSolidColorMaterial(
    "WhiteCard", Vec4 { 1.0F, 1.0F, 1.0F, 1.0F }, 0.9F, 0.0F, true);
  const auto black_mat = MakeSolidColorMaterial(
    "BlackCard", Vec4 { 0.02F, 0.02F, 0.02F, 1.0F }, 0.9F, 0.0F, true);
  const auto matte_mat = MakeSolidColorMaterial(
    "MatteSphere", Vec4 { 0.5F, 0.5F, 0.5F, 1.0F }, 0.95F, 0.0F, false);
  const auto glossy_mat = MakeSolidColorMaterial(
    "GlossySphere", Vec4 { 0.9F, 0.9F, 0.9F, 1.0F }, 0.08F, 0.0F, false);
  const auto ground_mat = MakeSolidColorMaterial(
    "Ground", Vec4 { 0.15F, 0.15F, 0.15F, 1.0F }, 0.9F, 0.0F, false);

  gray_card_geo_ = BuildQuadGeometry("GrayCard", gray_mat);
  white_card_geo_ = BuildQuadGeometry("WhiteCard", white_mat);
  black_card_geo_ = BuildQuadGeometry("BlackCard", black_mat);
  matte_sphere_geo_ = BuildSphereGeometry("MatteSphere", matte_mat);
  glossy_sphere_geo_ = BuildSphereGeometry("GlossySphere", glossy_mat);
  ground_plane_geo_ = BuildQuadGeometry("GroundPlane", ground_mat);
}

auto LightScene::EnsureReferenceNodes() -> void
{
  if (!scene_) {
    return;
  }

  if (!gray_card_node_.IsAlive()) {
    gray_card_node_ = scene_->CreateNode("GrayCard");
    gray_card_node_.GetRenderable().SetGeometry(gray_card_geo_);
  }
  if (!white_card_node_.IsAlive()) {
    white_card_node_ = scene_->CreateNode("WhiteCard");
    white_card_node_.GetRenderable().SetGeometry(white_card_geo_);
  }
  if (!black_card_node_.IsAlive()) {
    black_card_node_ = scene_->CreateNode("BlackCard");
    black_card_node_.GetRenderable().SetGeometry(black_card_geo_);
  }
  if (!matte_sphere_node_.IsAlive()) {
    matte_sphere_node_ = scene_->CreateNode("MatteSphere");
    matte_sphere_node_.GetRenderable().SetGeometry(matte_sphere_geo_);
  }
  if (!glossy_sphere_node_.IsAlive()) {
    glossy_sphere_node_ = scene_->CreateNode("GlossySphere");
    glossy_sphere_node_.GetRenderable().SetGeometry(glossy_sphere_geo_);
  }
  if (!ground_plane_node_.IsAlive()) {
    ground_plane_node_ = scene_->CreateNode("GroundPlane");
    ground_plane_node_.GetRenderable().SetGeometry(ground_plane_geo_);
  }
}

auto LightScene::ApplySceneObjectState(scene::SceneNode& node,
  const SceneObjectState& state, const bool allow_rotation) -> void
{
  if (!node.IsAlive()) {
    return;
  }

  auto tf = node.GetTransform();
  tf.SetLocalPosition(state.position);
  if (allow_rotation) {
    const Vec3 radians = glm::radians(state.rotation_deg);
    tf.SetLocalRotation(glm::quat(radians));
  }
  tf.SetLocalScale(state.scale);

  ApplySceneVisibility(node, state.enabled);
}

auto LightScene::ApplySceneVisibility(
  scene::SceneNode& node, const bool visible) -> void
{
  if (!node.IsAlive()) {
    return;
  }

  node.GetRenderable().SetAllSubmeshesVisible(visible);
}

auto LightScene::ApplySceneTransforms() -> void
{
  ApplySceneObjectState(gray_card_node_, gray_card_state_, true);
  ApplySceneObjectState(white_card_node_, white_card_state_, true);
  ApplySceneObjectState(black_card_node_, black_card_state_, true);
  ApplySceneObjectState(matte_sphere_node_, matte_sphere_state_, false);
  ApplySceneObjectState(glossy_sphere_node_, glossy_sphere_state_, false);
  ApplySceneObjectState(ground_plane_node_, ground_plane_state_, false);
}

auto LightScene::EnsurePointLightNode() -> void
{
  if (!scene_) {
    return;
  }

  if (!point_light_node_.IsAlive()) {
    point_light_node_ = scene_->CreateNode("PointLightA");
    auto point_light = std::make_unique<scene::PointLight>();
    point_light->Common().affects_world = true;
    point_light->Common().color_rgb = point_light_state_.color_rgb;
    point_light->Common().intensity = point_light_state_.intensity;
    point_light->SetRange(point_light_state_.range);
    point_light->SetSourceRadius(point_light_state_.source_radius);
    const bool attached = point_light_node_.AttachLight(std::move(point_light));
    CHECK_F(attached, "Failed to attach PointLight to PointLightA");
  }
}

auto LightScene::EnsureSpotLightNode() -> void
{
  if (!scene_) {
    return;
  }

  if (!spot_light_node_.IsAlive()) {
    spot_light_node_ = scene_->CreateNode("SpotLightA");
    auto spot_light = std::make_unique<scene::SpotLight>();
    spot_light->Common().affects_world = true;
    spot_light->Common().color_rgb = spot_light_state_.color_rgb;
    spot_light->Common().intensity = spot_light_state_.intensity;
    spot_light->SetRange(spot_light_state_.range);
    spot_light->SetSourceRadius(spot_light_state_.source_radius);
    const bool attached = spot_light_node_.AttachLight(std::move(spot_light));
    CHECK_F(attached, "Failed to attach SpotLight to SpotLightA");
  }
}

auto LightScene::ApplyPointLightState() -> void
{
  if (!point_light_node_.IsAlive()) {
    return;
  }

  point_light_node_.GetTransform().SetLocalPosition(
    point_light_state_.position);

  if (auto point = point_light_node_.GetLightAs<scene::PointLight>()) {
    auto& light = point->get();
    light.Common().affects_world = point_light_state_.enabled;
    light.Common().color_rgb = point_light_state_.color_rgb;
    light.Common().intensity = point_light_state_.intensity;
    light.SetRange(point_light_state_.range);
    light.SetSourceRadius(point_light_state_.source_radius);
  }
}

auto LightScene::ApplySpotLightState() -> void
{
  if (!spot_light_node_.IsAlive()) {
    return;
  }

  spot_light_node_.GetTransform().SetLocalPosition(spot_light_state_.position);
  const Vec3 direction = NormalizeOrFallback(spot_light_state_.direction_ws);
  const Quat rot = RotationFromForwardToDir(direction);
  spot_light_node_.GetTransform().SetLocalRotation(rot);

  if (auto spot = spot_light_node_.GetLightAs<scene::SpotLight>()) {
    auto& light = spot->get();
    light.Common().affects_world = spot_light_state_.enabled;
    light.Common().color_rgb = spot_light_state_.color_rgb;
    light.Common().intensity = spot_light_state_.intensity;
    light.SetRange(spot_light_state_.range);
    light.SetSourceRadius(spot_light_state_.source_radius);
    const float inner_angle_rad
      = glm::radians(spot_light_state_.inner_angle_deg);
    const float outer_angle_rad
      = glm::radians(spot_light_state_.outer_angle_deg);
    light.SetConeAnglesRadians(inner_angle_rad, outer_angle_rad);
  }
}

auto LightScene::BuildQuadGeometry(
  std::string_view name, std::shared_ptr<const data::MaterialAsset> material)
  -> std::shared_ptr<const data::GeometryAsset>
{
  using oxygen::data::MeshBuilder;
  using oxygen::data::pak::GeometryAssetDesc;
  using oxygen::data::pak::MeshViewDesc;

  auto quad_data = oxygen::data::MakeQuadMeshAsset(1.0F, 1.0F);
  CHECK_F(quad_data.has_value());

  auto mesh
    = MeshBuilder(0, std::string(name))
        .WithVertices(quad_data->first)
        .WithIndices(quad_data->second)
        .BeginSubMesh("full", material)
        .WithMeshView(MeshViewDesc {
          .first_index = 0,
          .index_count = static_cast<uint32_t>(quad_data->second.size()),
          .first_vertex = 0,
          .vertex_count = static_cast<uint32_t>(quad_data->first.size()),
        })
        .EndSubMesh()
        .Build();

  GeometryAssetDesc geo_desc {};
  geo_desc.lod_count = 1;
  const auto bb_min = mesh->BoundingBoxMin();
  const auto bb_max = mesh->BoundingBoxMax();
  geo_desc.bounding_box_min[0] = bb_min.x;
  geo_desc.bounding_box_min[1] = bb_min.y;
  geo_desc.bounding_box_min[2] = bb_min.z;
  geo_desc.bounding_box_max[0] = bb_max.x;
  geo_desc.bounding_box_max[1] = bb_max.y;
  geo_desc.bounding_box_max[2] = bb_max.z;

  std::shared_ptr<oxygen::data::Mesh> mesh_shared = std::move(mesh);
  return std::make_shared<oxygen::data::GeometryAsset>(
    oxygen::data::AssetKey { .guid = oxygen::data::GenerateAssetGuid() },
    geo_desc, std::vector<std::shared_ptr<oxygen::data::Mesh>> { mesh_shared });
}

auto LightScene::BuildSphereGeometry(
  std::string_view name, std::shared_ptr<const data::MaterialAsset> material)
  -> std::shared_ptr<const data::GeometryAsset>
{
  using oxygen::data::MeshBuilder;
  using oxygen::data::pak::GeometryAssetDesc;
  using oxygen::data::pak::MeshViewDesc;

  auto sphere_data = oxygen::data::MakeSphereMeshAsset(32, 64);
  CHECK_F(sphere_data.has_value());

  auto mesh
    = MeshBuilder(0, std::string(name))
        .WithVertices(sphere_data->first)
        .WithIndices(sphere_data->second)
        .BeginSubMesh("full", material)
        .WithMeshView(MeshViewDesc {
          .first_index = 0,
          .index_count = static_cast<uint32_t>(sphere_data->second.size()),
          .first_vertex = 0,
          .vertex_count = static_cast<uint32_t>(sphere_data->first.size()),
        })
        .EndSubMesh()
        .Build();

  GeometryAssetDesc geo_desc {};
  geo_desc.lod_count = 1;
  const auto bb_min = mesh->BoundingBoxMin();
  const auto bb_max = mesh->BoundingBoxMax();
  geo_desc.bounding_box_min[0] = bb_min.x;
  geo_desc.bounding_box_min[1] = bb_min.y;
  geo_desc.bounding_box_min[2] = bb_min.z;
  geo_desc.bounding_box_max[0] = bb_max.x;
  geo_desc.bounding_box_max[1] = bb_max.y;
  geo_desc.bounding_box_max[2] = bb_max.z;

  std::shared_ptr<oxygen::data::Mesh> mesh_shared = std::move(mesh);
  return std::make_shared<oxygen::data::GeometryAsset>(
    oxygen::data::AssetKey { .guid = oxygen::data::GenerateAssetGuid() },
    geo_desc, std::vector<std::shared_ptr<oxygen::data::Mesh>> { mesh_shared });
}

auto LightScene::MakeSolidColorMaterial(std::string_view name, const Vec4& rgba,
  const float roughness, const float metalness, const bool double_sided)
  -> std::shared_ptr<const data::MaterialAsset>
{
  using namespace oxygen::data;

  pak::MaterialAssetDesc desc {};
  desc.header.asset_type = 7;
  constexpr std::size_t maxn = sizeof(desc.header.name) - 1;
  const std::size_t n = (std::min)(maxn, name.size());
  std::memcpy(desc.header.name, name.data(), n);
  desc.header.name[n] = '\0';
  desc.header.version = 1;
  desc.header.streaming_priority = 255;
  desc.material_domain = static_cast<uint8_t>(MaterialDomain::kOpaque);
  desc.flags = double_sided ? pak::kMaterialFlag_DoubleSided : 0u;
  desc.shader_stages = 0;
  desc.base_color[0] = rgba.r;
  desc.base_color[1] = rgba.g;
  desc.base_color[2] = rgba.b;
  desc.base_color[3] = rgba.a;
  desc.normal_scale = 1.0F;
  desc.metalness = Unorm16 { metalness };
  desc.roughness = Unorm16 { roughness };
  desc.ambient_occlusion = Unorm16 { 1.0F };
  const AssetKey asset_key { .guid = GenerateAssetGuid() };
  return std::make_shared<const MaterialAsset>(
    asset_key, desc, std::vector<ShaderReference> {});
}

} // namespace oxygen::examples::light_bench
