//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "SceneSetup.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

#include <glm/gtc/quaternion.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/Constants.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/ProceduralMeshes.h>
#include <Oxygen/Scene/Environment/SceneEnvironment.h>
#include <Oxygen/Scene/Environment/SkyLight.h>
#include <Oxygen/Scene/Environment/SkySphere.h>
#include <Oxygen/Scene/Light/DirectionalLight.h>
#include <Oxygen/Scene/Light/PointLight.h>

namespace {

using oxygen::Quat;
using oxygen::Vec3;

auto MakeRotationFromForwardToDirWs(const Vec3& dir_ws) -> Quat
{
  const Vec3 from = glm::normalize(oxygen::space::move::Forward);
  const Vec3 to = glm::normalize(dir_ws);
  const float d = glm::dot(from, to);
  if (d > 0.9999f) {
    return Quat { 1.0f, 0.0f, 0.0f, 0.0f };
  }
  if (d < -0.9999f) {
    Vec3 axis = glm::cross(from, oxygen::space::move::Up);
    if (glm::dot(axis, axis) < 1e-6f) {
      axis = glm::cross(from, oxygen::space::move::Right);
    }
    axis = glm::normalize(axis);
    return glm::angleAxis(glm::pi<float>(), axis);
  }

  Vec3 axis = glm::cross(from, to);
  const float axis_len2 = glm::dot(axis, axis);
  if (axis_len2 < 1e-8f) {
    return Quat { 1.0f, 0.0f, 0.0f, 0.0f };
  }
  axis = glm::normalize(axis);
  const float angle = std::acos(std::clamp(d, -1.0f, 1.0f));
  return glm::angleAxis(angle, axis);
}

auto ResolveBaseColorTextureResourceIndex(
  oxygen::examples::textured_cube::SceneSetup::TextureIndexMode mode,
  std::uint32_t custom_resource_index) -> oxygen::data::pak::v2::ResourceIndexT
{
  using oxygen::data::pak::v2::ResourceIndexT;
  using enum oxygen::examples::textured_cube::SceneSetup::TextureIndexMode;

  switch (mode) {
  case kFallback:
    return oxygen::data::pak::v2::kFallbackResourceIndex;
  case kCustom:
    return static_cast<ResourceIndexT>(custom_resource_index);
  case kForcedError:
  default:
    return (std::numeric_limits<ResourceIndexT>::max)();
  }
}

auto MakeCubeMaterial(const char* name, const glm::vec4& rgba,
  oxygen::data::pak::v2::ResourceIndexT base_color_texture_resource_index,
  oxygen::content::ResourceKey base_color_texture_key,
  oxygen::data::MaterialDomain domain = oxygen::data::MaterialDomain::kOpaque)
  -> std::shared_ptr<const oxygen::data::MaterialAsset>
{
  using namespace oxygen::data;

  pak::MaterialAssetDesc desc {};
  desc.header.asset_type = 7;

  constexpr std::size_t maxn = sizeof(desc.header.name) - 1;
  const std::size_t n = (std::min)(maxn, std::strlen(name));
  std::memcpy(desc.header.name, name, n);
  desc.header.name[n] = '\0';

  desc.header.version = 1;
  desc.header.streaming_priority = 255;
  desc.material_domain = static_cast<uint8_t>(domain);
  desc.flags = 0;
  desc.shader_stages = 0;

  desc.base_color[0] = rgba.r;
  desc.base_color[1] = rgba.g;
  desc.base_color[2] = rgba.b;
  desc.base_color[3] = rgba.a;

  desc.normal_scale = 1.0f;
  desc.metalness = Unorm16 { 0.0f };
  desc.roughness = Unorm16 { 0.75f };
  desc.ambient_occlusion = Unorm16 { 1.0f };

  desc.base_color_texture = base_color_texture_resource_index;

  const AssetKey asset_key { .guid = GenerateAssetGuid() };

  if (base_color_texture_key != static_cast<oxygen::content::ResourceKey>(0)) {
    std::vector<oxygen::content::ResourceKey> texture_keys;
    texture_keys.push_back(base_color_texture_key);
    return std::make_shared<const MaterialAsset>(asset_key, desc,
      std::vector<ShaderReference> {}, std::move(texture_keys));
  }

  return std::make_shared<const MaterialAsset>(
    asset_key, desc, std::vector<ShaderReference> {});
}

auto BuildCubeGeometry(
  const std::shared_ptr<const oxygen::data::MaterialAsset>& material,
  const glm::vec2 /*uv_scale*/, const glm::vec2 /*uv_offset*/)
  -> std::shared_ptr<oxygen::data::GeometryAsset>
{
  using oxygen::data::MeshBuilder;
  using oxygen::data::Vertex;
  using oxygen::data::pak::GeometryAssetDesc;
  using oxygen::data::pak::MeshViewDesc;

  auto cube_data = oxygen::data::MakeCubeMeshAsset();
  if (!cube_data) {
    return nullptr;
  }

  std::vector<Vertex> vertices = cube_data->first;

  auto mesh
    = MeshBuilder(0, "CubeLOD0")
        .WithVertices(vertices)
        .WithIndices(cube_data->second)
        .BeginSubMesh("full", material)
        .WithMeshView(MeshViewDesc {
          .first_index = 0,
          .index_count = static_cast<uint32_t>(cube_data->second.size()),
          .first_vertex = 0,
          .vertex_count = static_cast<uint32_t>(vertices.size()),
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

  return std::make_shared<oxygen::data::GeometryAsset>(
    oxygen::data::AssetKey { .guid = oxygen::data::GenerateAssetGuid() },
    geo_desc,
    std::vector<std::shared_ptr<oxygen::data::Mesh>> { std::move(mesh) });
}

const Vec3 kDefaultSunRayDirWs = glm::normalize(Vec3 { 0.35F, -0.45F, -1.0F });

} // namespace

namespace oxygen::examples::textured_cube {

SceneSetup::SceneSetup(std::shared_ptr<scene::Scene> scene)
  : scene_(std::move(scene))
{
}

auto SceneSetup::EnsureCubeNode() -> scene::SceneNode
{
  if (!scene_) {
    return {};
  }

  if (!cube_node_.IsAlive()) {
    cube_node_ = scene_->CreateNode("Cube");
    cube_node_.GetTransform().SetLocalPosition({ 0.0f, 0.0f, 0.0f });
  }

  return cube_node_;
}

auto SceneSetup::RebuildCube(TextureIndexMode texture_mode,
  std::uint32_t custom_resource_index,
  oxygen::content::ResourceKey custom_texture_key,
  oxygen::content::ResourceKey forced_error_key, glm::vec2 uv_scale,
  glm::vec2 uv_offset) -> std::shared_ptr<const oxygen::data::MaterialAsset>
{
  if (!cube_node_.IsAlive()) {
    EnsureCubeNode();
  }

  const auto res_index
    = ResolveBaseColorTextureResourceIndex(texture_mode, custom_resource_index);

  const auto base_color_key = [&]() -> oxygen::content::ResourceKey {
    using enum TextureIndexMode;
    switch (texture_mode) {
    case kCustom:
      return custom_texture_key;
    case kForcedError:
      return forced_error_key;
    case kFallback:
    default:
      return static_cast<oxygen::content::ResourceKey>(0);
    }
  }();

  cube_material_ = MakeCubeMaterial(
    "CubeMat", { 1.0f, 1.0f, 1.0f, 1.0f }, res_index, base_color_key);

  auto cube_geo = BuildCubeGeometry(cube_material_, uv_scale, uv_offset);
  if (cube_geo) {
    RetireCurrentGeometry();
    cube_geometry_ = std::move(cube_geo);
    cube_node_.GetRenderable().SetGeometry(cube_geometry_);
    CleanupRetiredGeometries();
  }

  return cube_material_;
}

auto SceneSetup::EnsureLighting(
  const SunLightParams& sun, const FillLightParams& fill) -> void
{
  if (!scene_) {
    return;
  }

  // Sun (directional) light
  if (!sun_node_.IsAlive()) {
    sun_node_ = scene_->CreateNode("Sun");
    sun_node_.GetTransform().SetLocalPosition({ 0.0f, -20.0f, 20.0f });

    auto sun_light = std::make_unique<scene::DirectionalLight>();
    sun_light->Common().intensity = sun.intensity;
    sun_light->Common().color_rgb = sun.color_rgb;
    sun_light->SetIsSunLight(true);
    sun_light->SetEnvironmentContribution(true);

    const bool attached = sun_node_.AttachLight(std::move(sun_light));
    CHECK_F(attached, "Failed to attach DirectionalLight to Sun");
  }

  UpdateSunLight(sun);

  // Fill (point) light
  if (!fill_light_node_.IsAlive()) {
    fill_light_node_ = scene_->CreateNode("Fill");
    fill_light_node_.GetTransform().SetLocalPosition(fill.position);

    auto fill_light = std::make_unique<scene::PointLight>();
    fill_light->Common().intensity = fill.intensity;
    fill_light->Common().color_rgb = fill.color_rgb;
    fill_light->SetRange(fill.range);

    const bool attached = fill_light_node_.AttachLight(std::move(fill_light));
    CHECK_F(attached, "Failed to attach PointLight to Fill");
  }
}

auto SceneSetup::UpdateSunLight(const SunLightParams& params) -> void
{
  if (!sun_node_.IsAlive()) {
    return;
  }

  auto tf = sun_node_.GetTransform();
  const Vec3 dir = params.use_custom_direction
    ? glm::normalize(params.ray_direction)
    : kDefaultSunRayDirWs;
  const Quat rot = MakeRotationFromForwardToDirWs(dir);
  tf.SetLocalRotation(rot);

  // Position the node along the sun's apparent direction
  tf.SetLocalPosition(Vec3 { 0.0f, 0.0f, 0.0f } + dir * 50.0f);

  if (auto sun_light = sun_node_.GetLightAs<scene::DirectionalLight>()) {
    auto& light = sun_light->get();
    light.Common().intensity = params.intensity;
    light.Common().color_rgb = params.color_rgb;
    light.SetEnvironmentContribution(true);
    light.SetIsSunLight(true);
  }
}

auto SceneSetup::EnsureEnvironment(const EnvironmentParams& params) -> void
{
  if (!scene_) {
    return;
  }

  auto env = scene_->GetEnvironment();
  if (!env) {
    auto new_env = std::make_unique<scene::SceneEnvironment>();

    auto& sky = new_env->AddSystem<scene::environment::SkySphere>();
    sky.SetSource(scene::environment::SkySphereSource::kSolidColor);
    sky.SetSolidColorRgb(params.solid_sky_color);
    sky.SetIntensity(params.sky_intensity);

    auto& sky_light = new_env->AddSystem<scene::environment::SkyLight>();
    sky_light.SetIntensity(params.sky_light_intensity);
    sky_light.SetDiffuseIntensity(params.sky_light_diffuse);
    sky_light.SetSpecularIntensity(params.sky_light_specular);
    sky_light.SetTintRgb(Vec3 { 1.0f, 1.0f, 1.0f });
    sky_light.SetSource(scene::environment::SkyLightSource::kCapturedScene);

    scene_->SetEnvironment(std::move(new_env));
  } else {
    if (!env->TryGetSystem<scene::environment::SkySphere>()) {
      auto& sky = env->AddSystem<scene::environment::SkySphere>();
      sky.SetSource(scene::environment::SkySphereSource::kSolidColor);
      sky.SetSolidColorRgb(params.solid_sky_color);
      sky.SetIntensity(params.sky_intensity);
    }
    if (!env->TryGetSystem<scene::environment::SkyLight>()) {
      auto& sky_light = env->AddSystem<scene::environment::SkyLight>();
      sky_light.SetIntensity(params.sky_light_intensity);
      sky_light.SetDiffuseIntensity(params.sky_light_diffuse);
      sky_light.SetSpecularIntensity(params.sky_light_specular);
      sky_light.SetTintRgb(Vec3 { 1.0f, 1.0f, 1.0f });
      sky_light.SetSource(scene::environment::SkyLightSource::kCapturedScene);
    }
  }
}

auto SceneSetup::RetireCurrentGeometry() -> void
{
  if (cube_geometry_) {
    retired_cube_geometries_.push_back(cube_geometry_);
  }
}

auto SceneSetup::CleanupRetiredGeometries(std::size_t max_keep) -> void
{
  if (retired_cube_geometries_.size() > max_keep) {
    retired_cube_geometries_.erase(retired_cube_geometries_.begin(),
      retired_cube_geometries_.end() - max_keep);
  }
}

} // namespace oxygen::examples::textured_cube
