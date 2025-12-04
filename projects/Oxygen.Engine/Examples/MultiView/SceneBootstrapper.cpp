//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstring>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/ProceduralMeshes.h>

#include "SceneBootstrapper.h"

#include "Oxygen/Content/PakFile.h"

namespace oxygen::examples::multiview {
namespace {

  constexpr const char* kSceneName = "MultiViewScene";

  auto MakeSolidColorMaterial(const char* name, const glm::vec4& rgba)
    -> std::shared_ptr<const data::MaterialAsset>
  {
    using data::AssetType;
    using data::MaterialAsset;
    using data::MaterialDomain;
    using data::ShaderReference;
    namespace pak = data::pak;

    pak::MaterialAssetDesc desc {};
    desc.header.asset_type = static_cast<uint8_t>(AssetType::kMaterial);
    constexpr std::size_t maxn = sizeof(desc.header.name) - 1;
    const std::size_t n = (std::min)(maxn, std::strlen(name));
    std::memcpy(desc.header.name, name, n);
    desc.header.name[n] = '\0';
    desc.header.version = 1;
    desc.header.streaming_priority = 255;
    desc.material_domain = static_cast<uint8_t>(MaterialDomain::kOpaque);
    desc.flags = 0;
    desc.shader_stages = 0;
    desc.base_color[0] = rgba.r;
    desc.base_color[1] = rgba.g;
    desc.base_color[2] = rgba.b;
    desc.base_color[3] = rgba.a;
    desc.normal_scale = 1.0F;
    desc.metalness = 0.0F;
    desc.roughness = 0.5F;
    desc.ambient_occlusion = 1.0F;
    return std::make_shared<const MaterialAsset>(
      desc, std::vector<ShaderReference> {});
  }

} // namespace

auto SceneBootstrapper::EnsureScene() -> std::shared_ptr<scene::Scene>
{
  if (!scene_) {
    scene_ = std::make_shared<scene::Scene>(kSceneName);
    LOG_F(
      INFO, "[MultiView] SceneBootstrapper created scene '{}'.", kSceneName);
  }
  return scene_;
}

auto SceneBootstrapper::EnsureSceneWithContent()
  -> std::shared_ptr<scene::Scene>
{
  auto scene = EnsureScene();
  if (scene) {
    EnsureSphere(*scene);
  }
  return scene;
}

auto SceneBootstrapper::GetScene() const -> std::shared_ptr<scene::Scene>
{
  return scene_;
}

auto SceneBootstrapper::GetSphereNode() const -> scene::SceneNode
{
  return sphere_node_;
}

auto SceneBootstrapper::EnsureSphere(scene::Scene& scene) -> void
{
  if (sphere_node_.IsAlive()) {
    return;
  }

  auto sphere_geom_data = data::MakeSphereMeshAsset(32, 32);
  if (!sphere_geom_data.has_value()) {
    LOG_F(WARNING,
      "[MultiView] SceneBootstrapper failed to create sphere mesh data.");
    return;
  }

  auto material
    = MakeSolidColorMaterial("SphereMaterial", { 0.2F, 0.7F, 0.3F, 1.0F });

  using data::MeshBuilder;
  using data::pak::GeometryAssetDesc;
  using data::pak::MeshViewDesc;

  auto mesh
    = MeshBuilder(0, "Sphere")
        .WithVertices(sphere_geom_data->first)
        .WithIndices(sphere_geom_data->second)
        .BeginSubMesh("full", material)
        .WithMeshView(MeshViewDesc {
          .first_index = 0,
          .index_count = static_cast<uint32_t>(sphere_geom_data->second.size()),
          .first_vertex = 0,
          .vertex_count = static_cast<uint32_t>(sphere_geom_data->first.size()),
        })
        .EndSubMesh()
        .Build();

  GeometryAssetDesc geo_desc {};
  geo_desc.lod_count = 1;
  const glm::vec3 bb_min = mesh->BoundingBoxMin();
  const glm::vec3 bb_max = mesh->BoundingBoxMax();
  geo_desc.bounding_box_min[0] = bb_min.x;
  geo_desc.bounding_box_min[1] = bb_min.y;
  geo_desc.bounding_box_min[2] = bb_min.z;
  geo_desc.bounding_box_max[0] = bb_max.x;
  geo_desc.bounding_box_max[1] = bb_max.y;
  geo_desc.bounding_box_max[2] = bb_max.z;

  auto geom_asset = std::make_shared<data::GeometryAsset>(
    geo_desc, std::vector<std::shared_ptr<data::Mesh>> { std::move(mesh) });

  sphere_node_ = scene.CreateNode("Sphere");
  sphere_node_.GetRenderable().SetGeometry(std::move(geom_asset));
  sphere_node_.GetTransform().SetLocalPosition({ 0.0F, 0.0F, -2.0F });

  LOG_F(INFO,
    "[MultiView] SceneBootstrapper created sphere node (alive={}, "
    "geom_set={}).",
    sphere_node_.IsAlive(),
    sphere_node_.GetRenderable().GetGeometry() != nullptr);
}

} // namespace oxygen::examples::multiview
