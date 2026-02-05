//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstring>
#include <iterator>
#include <limits>
#include <utility>

#include <glm/gtc/quaternion.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/Constants.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/ProceduralMeshes.h>
#include <Oxygen/Renderer/Renderer.h>
#include <Oxygen/Scene/Environment/SceneEnvironment.h>
#include <Oxygen/Scene/Light/PointLight.h>

#include "TexturedCube/SceneSetup.h"

namespace {

using oxygen::Quat;
using oxygen::Vec3;

auto ResolveBaseColorTextureResourceIndex(
  oxygen::examples::textured_cube::TextureIndexMode mode,
  std::uint32_t custom_resource_index) -> oxygen::data::pak::v2::ResourceIndexT
{
  using oxygen::data::pak::v2::ResourceIndexT;
  using oxygen::examples::textured_cube::TextureIndexMode;

  switch (mode) {
  case TextureIndexMode::kFallback:
    return oxygen::data::pak::v2::kFallbackResourceIndex;
  case TextureIndexMode::kCustom:
    return static_cast<ResourceIndexT>(custom_resource_index);
  case TextureIndexMode::kForcedError:
  default:
    return (std::numeric_limits<ResourceIndexT>::max)();
  }
}

auto MakeMaterial(const char* name, const glm::vec4& rgba,
  oxygen::data::pak::v2::ResourceIndexT base_color_texture_resource_index,
  oxygen::content::ResourceKey base_color_texture_key, float metalness,
  float roughness, bool disable_texture_sampling,
  oxygen::data::MaterialDomain domain = oxygen::data::MaterialDomain::kOpaque)
  -> std::shared_ptr<const oxygen::data::MaterialAsset>
{
  namespace d = oxygen::data;
  namespace c = oxygen::content;
  namespace pak = d::pak;

  pak::MaterialAssetDesc desc {};
  desc.header.asset_type
    = static_cast<uint8_t>(oxygen::data::AssetType::kMaterial);

  constexpr std::size_t maxn = std::size(desc.header.name) - 1;
  const std::size_t n = std::min(maxn, std::strlen(name));
  std::copy_n(name, n, std::begin(desc.header.name));
  desc.header.name[n] = '\0';

  desc.header.version = 1;
  desc.header.streaming_priority = 255;
  desc.material_domain = static_cast<uint8_t>(domain);
  desc.flags = disable_texture_sampling
    ? oxygen::data::pak::kMaterialFlag_NoTextureSampling
    : 0U;
  desc.shader_stages = 0;

  desc.base_color[0] = rgba.r;
  desc.base_color[1] = rgba.g;
  desc.base_color[2] = rgba.b;
  desc.base_color[3] = rgba.a;

  desc.normal_scale = 1.0F;

  desc.metalness = d::Unorm16 { std::clamp(metalness, 0.0F, 1.0F) };
  desc.roughness = d::Unorm16 { std::clamp(roughness, 0.0F, 1.0F) };
  desc.ambient_occlusion = d::Unorm16 { 1.0F };

  desc.base_color_texture = base_color_texture_resource_index;

  const d::AssetKey asset_key { .guid = d::GenerateAssetGuid() };

  if (base_color_texture_key != static_cast<c::ResourceKey>(0)) {
    std::vector<c::ResourceKey> texture_keys;
    texture_keys.push_back(base_color_texture_key);
    return std::make_shared<const d::MaterialAsset>(asset_key, desc,
      std::vector<d::ShaderReference> {}, std::move(texture_keys));
  }

  return std::make_shared<const d::MaterialAsset>(
    asset_key, desc, std::vector<d::ShaderReference> {});
}

auto BuildSphereGeometry(
  const std::shared_ptr<const oxygen::data::MaterialAsset>& material)
  -> std::shared_ptr<oxygen::data::GeometryAsset>
{
  namespace d = oxygen::data;
  namespace pak = d::pak;

  // Use a procedural UV sphere so IBL reflections are easier to validate
  // (smooth normal variation across the surface).
  auto sphere_data = d::MakeSphereMeshAsset(32, 64);
  if (!sphere_data) {
    return nullptr;
  }

  std::vector<d::Vertex> vertices = sphere_data->first;

  auto mesh
    = d::MeshBuilder(0, "SphereLOD0")
        .WithVertices(vertices)
        .WithIndices(sphere_data->second)
        .BeginSubMesh("full", material)
        .WithMeshView(pak::MeshViewDesc {
          .first_index = 0,
          .index_count = static_cast<uint32_t>(sphere_data->second.size()),
          .first_vertex = 0,
          .vertex_count = static_cast<uint32_t>(vertices.size()),
        })
        .EndSubMesh()
        .Build();

  pak::GeometryAssetDesc geo_desc {};
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

auto BuildCubeGeometry(
  const std::shared_ptr<const oxygen::data::MaterialAsset>& material)
  -> std::shared_ptr<oxygen::data::GeometryAsset>
{
  namespace d = oxygen::data;
  namespace pak = d::pak;

  auto cube_data = d::MakeCubeMeshAsset();
  if (!cube_data) {
    return nullptr;
  }

  std::vector<d::Vertex> vertices = cube_data->first;

  auto mesh
    = d::MeshBuilder(0, "CubeLOD0")
        .WithVertices(vertices)
        .WithIndices(cube_data->second)
        .BeginSubMesh("full", material)
        .WithMeshView(pak::MeshViewDesc {
          .first_index = 0,
          .index_count = static_cast<uint32_t>(cube_data->second.size()),
          .first_vertex = 0,
          .vertex_count = static_cast<uint32_t>(vertices.size()),
        })
        .EndSubMesh()
        .Build();

  pak::GeometryAssetDesc geo_desc {};
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

} // namespace

namespace oxygen::examples::textured_cube {

SceneSetup::SceneSetup(observer_ptr<scene::Scene> scene,
  TextureLoadingService& texture_service, SkyboxService& skybox_service,
  std::filesystem::path cooked_root)
  : texture_service_(&texture_service)
  , skybox_service_(&skybox_service)
  , cooked_root_(std::move(cooked_root))
  , scene_(scene)
{
}

auto SceneSetup::Initialize() -> void
{
  EnsureNodes();
  EnsureFillLight();
}

auto SceneSetup::EnsureNodes() -> void
{
  if (!scene_) {
    return;
  }

  if (!sphere_node_.IsAlive()) {
    sphere_node_ = scene_->CreateNode("Sphere");
  }

  // Place the sphere above the cube (Z-up world).
  sphere_node_.GetTransform().SetLocalPosition({ 0.0F, 0.0F, 3.0F });

  if (!cube_node_.IsAlive()) {
    cube_node_ = scene_->CreateNode("Cube");
  }

  // Make the cube the scene center and scale it up for easier inspection.
  cube_node_.GetTransform().SetLocalPosition({ 0.0F, 0.0F, 0.0F });
  cube_node_.GetTransform().SetLocalScale({ 4.0F, 4.0F, 4.0F });
}

auto SceneSetup::UpdateSphere(const ObjectTextureState& sphere_texture,
  const SurfaceParams& surface, oxygen::content::ResourceKey forced_error_key)
  -> void
{
  EnsureNodes();

  const auto sphere_res_index = ResolveBaseColorTextureResourceIndex(
    sphere_texture.mode, sphere_texture.resource_index);

  const auto ResolveKey =
    [&](TextureIndexMode mode,
      const oxygen::content::ResourceKey key) -> oxygen::content::ResourceKey {
    switch (mode) {
    case TextureIndexMode::kCustom:
      return key;
    case TextureIndexMode::kForcedError:
      return forced_error_key;
    case TextureIndexMode::kFallback:
    default:
      return static_cast<oxygen::content::ResourceKey>(0);
    }
  };

  auto new_sphere_material
    = MakeMaterial("SphereMat", surface.base_color, sphere_res_index,
      ResolveKey(sphere_texture.mode, sphere_texture.resource_key),
      surface.metalness, surface.roughness, surface.disable_texture_sampling);

  // Ensure geometry exists (create once). Geometry holds a default material
  // but we prefer using Renderable::SetMaterialOverride for runtime swaps.
  if (!sphere_geometry_) {
    sphere_geometry_ = BuildSphereGeometry(new_sphere_material);
    if (sphere_geometry_) {
      sphere_node_.GetRenderable().SetGeometry(sphere_geometry_);
    }
  }

  // Update material using per-submesh override (LOD 0, submesh 0)
  if (!sphere_material_
    || sphere_material_.get() != new_sphere_material.get()) {
    sphere_node_.GetRenderable().SetMaterialOverride(0, 0, new_sphere_material);
  }

  sphere_material_ = std::move(new_sphere_material);
}

auto SceneSetup::UpdateCube(const ObjectTextureState& cube_texture,
  const SurfaceParams& surface, oxygen::content::ResourceKey forced_error_key)
  -> void
{
  EnsureNodes();

  const auto cube_res_index = ResolveBaseColorTextureResourceIndex(
    cube_texture.mode, cube_texture.resource_index);

  const auto ResolveKey =
    [&](TextureIndexMode mode,
      const oxygen::content::ResourceKey key) -> oxygen::content::ResourceKey {
    switch (mode) {
    case TextureIndexMode::kCustom:
      return key;
    case TextureIndexMode::kForcedError:
      return forced_error_key;
    case TextureIndexMode::kFallback:
    default:
      return static_cast<oxygen::content::ResourceKey>(0);
    }
  };

  auto new_cube_material = MakeMaterial("CubeMat", surface.base_color,
    cube_res_index, ResolveKey(cube_texture.mode, cube_texture.resource_key),
    surface.metalness, surface.roughness, surface.disable_texture_sampling);

  if (!cube_geometry_) {
    cube_geometry_ = BuildCubeGeometry(new_cube_material);
    if (cube_geometry_) {
      cube_node_.GetRenderable().SetGeometry(cube_geometry_);
    }
  }

  if (!cube_material_ || cube_material_.get() != new_cube_material.get()) {
    cube_node_.GetRenderable().SetMaterialOverride(0, 0, new_cube_material);
  }

  cube_material_ = std::move(new_cube_material);
}

auto SceneSetup::UpdateUvTransform(engine::Renderer& renderer,
  const glm::vec2& scale, const glm::vec2& offset) -> void
{
  if (sphere_material_) {
    (void)renderer.OverrideMaterialUvTransform(
      *sphere_material_, scale, offset);
  }
  if (cube_material_) {
    (void)renderer.OverrideMaterialUvTransform(*cube_material_, scale, offset);
  }
}

auto SceneSetup::EnsureFillLight() -> void
{
  if (!scene_) {
    return;
  }

  if (!fill_light_node_.IsAlive()) {
    // Keep light configuration local to the implementation.
    const glm::vec3 position { -6.0F, 5.0F, 3.0F };
    const float intensity = 800.0F;
    const glm::vec3 color_rgb { 0.85F, 0.90F, 1.0F };
    const float range = 45.0F;

    fill_light_node_ = scene_->CreateNode("Fill");
    fill_light_node_.GetTransform().SetLocalPosition(position);

    auto fill_light = std::make_unique<scene::PointLight>();
    fill_light->SetLuminousFluxLm(intensity);
    fill_light->Common().color_rgb = color_rgb;
    fill_light->SetRange(range);

    const bool attached = fill_light_node_.AttachLight(std::move(fill_light));
    CHECK_F(attached, "Failed to attach PointLight to Fill");
  }
}

} // namespace oxygen::examples::textured_cube
