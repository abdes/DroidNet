//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma unmanaged

#include "pch.h"

#include <array>
#include <cstdint>
#include <mutex>
#include <string_view>
#include <unordered_map>

#include <Commands/SetGeometryCommand.h>
#include <EditorModule/EditorCommand.h>

#include <Oxygen/Base/Hash.h>
#include <Oxygen/Content/AssetLoader.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Data/ProceduralMeshes.h>

namespace {

auto MakeDeterministicAssetKey(std::string_view seed) -> oxygen::data::AssetKey
{
  oxygen::data::AssetKey key { };

  const std::uint64_t h1 = oxygen::ComputeFNV1a64(seed.data(), seed.size());
  const auto salted = std::string(seed) + "#generated_v1";
  const std::uint64_t h2
    = oxygen::ComputeFNV1a64(salted.data(), salted.size());

  for (int i = 0; i < 8; ++i) {
    key.guid[static_cast<std::size_t>(i)]
      = static_cast<std::uint8_t>((h1 >> (i * 8)) & 0xFF);
    key.guid[static_cast<std::size_t>(8 + i)]
      = static_cast<std::uint8_t>((h2 >> (i * 8)) & 0xFF);
  }

  return key;
}

auto TryGetCachedGeneratedGeometry(const oxygen::data::AssetKey& key)
  -> std::shared_ptr<const oxygen::data::GeometryAsset>
{
  static std::mutex cache_mutex;
  static std::unordered_map<oxygen::data::AssetKey,
    std::weak_ptr<const oxygen::data::GeometryAsset>>
    cache;

  std::scoped_lock lock(cache_mutex);
  if (const auto it = cache.find(key); it != cache.end()) {
    return it->second.lock();
  }
  return nullptr;
}

auto CacheGeneratedGeometry(const oxygen::data::AssetKey& key,
  const std::shared_ptr<const oxygen::data::GeometryAsset>& geometry) -> void
{
  static std::mutex cache_mutex;
  static std::unordered_map<oxygen::data::AssetKey,
    std::weak_ptr<const oxygen::data::GeometryAsset>>
    cache;

  std::scoped_lock lock(cache_mutex);
  cache[key] = geometry;
}

} // namespace

namespace oxygen::interop::module {

  void SetGeometryCommand::Execute(CommandContext& context) {
    if (!context.Scene) {
      return;
    }

    const auto scene_node_opt = context.Scene->GetNode(node_);
    if (!scene_node_opt || !scene_node_opt->IsAlive())
      return;

    auto scene_node = *scene_node_opt;

    std::shared_ptr<const oxygen::data::GeometryAsset> geometry;
    bool started_async_load = false;

    // 1. Check for procedural geometry
    const std::string generatedPrefix = "asset:///Engine/Generated/BasicShapes/";
    if (assetUri_.compare(0, generatedPrefix.length(), generatedPrefix) == 0) {
      const auto asset_key = MakeDeterministicAssetKey(assetUri_);
      geometry = TryGetCachedGeneratedGeometry(asset_key);
      if (geometry) {
        // Cached instance ensures shared mesh pointer for this identity,
        // avoiding per-frame hot-reload thrash in GeometryUploader.
        started_async_load = false;
      } else {
      std::string type = assetUri_.substr(generatedPrefix.length());
      std::transform(type.begin(), type.end(), type.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
      });

      std::optional<std::pair<std::vector<oxygen::data::Vertex>, std::vector<uint32_t>>> mesh_data;

      if (type == "cube") mesh_data = oxygen::data::MakeCubeMeshAsset();
      else if (type == "sphere") mesh_data = oxygen::data::MakeSphereMeshAsset();
      else if (type == "plane") mesh_data = oxygen::data::MakePlaneMeshAsset();
      else if (type == "cylinder") mesh_data = oxygen::data::MakeCylinderMeshAsset();
      else if (type == "cone") mesh_data = oxygen::data::MakeConeMeshAsset();
      else if (type == "torus") mesh_data = oxygen::data::MakeTorusMeshAsset();
      else if (type == "quad") mesh_data = oxygen::data::MakeQuadMeshAsset();
      else if (type == "arrowgizmo") mesh_data = oxygen::data::MakeArrowGizmoMeshAsset();

      if (mesh_data) {
        // Use the default material for procedural meshes
        auto material = oxygen::data::MaterialAsset::CreateDefault();

        auto& [vertices, indices] = mesh_data.value();
        oxygen::data::pak::MeshViewDesc view_desc{};
        view_desc.first_vertex = 0;
        view_desc.vertex_count = static_cast<uint32_t>(vertices.size());
        view_desc.first_index = 0;
        view_desc.index_count = static_cast<uint32_t>(indices.size());

        auto mesh = oxygen::data::MeshBuilder(0, type)
          .WithVertices(vertices)
          .WithIndices(indices)
          .BeginSubMesh("default", material)
          .WithMeshView(view_desc)
          .EndSubMesh()
          .Build();

        oxygen::data::pak::GeometryAssetDesc geo_desc{};
        geo_desc.header.asset_type = 6; // Geometry type
        geo_desc.header.version = 1;
        strncpy_s(geo_desc.header.name, sizeof(geo_desc.header.name),
          type.c_str(), _TRUNCATE);
        geo_desc.lod_count = 1;

        const auto bbox_min = mesh->BoundingBoxMin();
        const auto bbox_max = mesh->BoundingBoxMax();
        geo_desc.bounding_box_min[0] = bbox_min.x;
        geo_desc.bounding_box_min[1] = bbox_min.y;
        geo_desc.bounding_box_min[2] = bbox_min.z;
        geo_desc.bounding_box_max[0] = bbox_max.x;
        geo_desc.bounding_box_max[1] = bbox_max.y;
        geo_desc.bounding_box_max[2] = bbox_max.z;

        std::vector<std::shared_ptr<oxygen::data::Mesh>> lod_meshes;
        lod_meshes.push_back(std::move(mesh));

        geometry = std::make_shared<oxygen::data::GeometryAsset>(
          asset_key, geo_desc, std::move(lod_meshes));
        CacheGeneratedGeometry(asset_key, geometry);
      }
      }
    }
    // 2. Check for content assets
    else if (context.PathResolver && context.AssetLoader) {
      const std::string asset_uri = assetUri_;
      std::string_view uri = assetUri_;
      if (uri.starts_with("asset:")) {
        uri.remove_prefix(6);
      }

      // Strip leading slashes to normalize the path
      while (!uri.empty() && uri[0] == '/') {
        uri.remove_prefix(1);
      }

      const std::string virtualPath = "/" + std::string(uri);
      LOG_F(INFO, "SetGeometryCommand: resolving virtual path '{}'", virtualPath);

      auto key = context.PathResolver->ResolveAssetKey(virtualPath);
      if (key) {
        LOG_F(INFO, "SetGeometryCommand: resolved key, loading asset...");
        geometry = context.AssetLoader->GetAsset<oxygen::data::GeometryAsset>(*key);
        if (!geometry) {
          started_async_load = true;
          const auto asset_key = *key;
          const auto node = scene_node;
          context.AssetLoader->StartLoadAsset<oxygen::data::GeometryAsset>(
            asset_key,
            [node, asset_uri](
              std::shared_ptr<oxygen::data::GeometryAsset> loaded) {
              if (!loaded) {
                LOG_F(ERROR,
                  "SetGeometryCommand: async load failed for '{}'", asset_uri);
                return;
              }

              if (!node.IsAlive()) {
                LOG_F(WARNING,
                  "SetGeometryCommand: node no longer alive; skipping geometry "
                  "apply for '{}'", asset_uri);
                return;
              }

              LOG_F(INFO,
                "SetGeometryCommand: applying async geometry '{}'", asset_uri);
              try {
                node.GetRenderable().SetGeometry(std::move(loaded));
                LOG_F(INFO,
                  "SetGeometryCommand: async geometry applied successfully");
              } catch (const std::exception& e) {
                LOG_F(ERROR,
                  "SetGeometryCommand: exception in async SetGeometry: {}",
                  e.what());
                throw;
              } catch (...) {
                LOG_F(ERROR,
                  "SetGeometryCommand: unknown exception in async SetGeometry");
                throw;
              }
            });
        }
      } else {
        LOG_F(WARNING, "SetGeometryCommand: could not resolve asset key for virtual path '{}'", virtualPath);
      }
    }

    if (started_async_load) {
      return;
    }

    if (geometry) {
      LOG_F(INFO, "SetGeometryCommand: applying geometry to scene node");
      try {
        scene_node.GetRenderable().SetGeometry(geometry);
        LOG_F(INFO, "SetGeometryCommand: geometry applied successfully");
      } catch (const std::exception& e) {
        LOG_F(ERROR, "SetGeometryCommand: exception in SetGeometry: {}", e.what());
        throw;
      } catch (...) {
        LOG_F(ERROR, "SetGeometryCommand: unknown exception in SetGeometry");
        throw;
      }
    } else {
      LOG_F(WARNING, "SetGeometryCommand: no geometry to apply");
    }
  }

} // namespace oxygen::interop::module
