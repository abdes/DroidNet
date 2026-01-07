//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Content/Import/emit/GeometryEmitter.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <numbers>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Content/Import/ImportDiagnostics.h>
#include <Oxygen/Content/Import/LooseCookedLayout.h>
#include <Oxygen/Content/Import/emit/BufferEmitter.h>
#include <Oxygen/Content/Import/emit/ResourceAppender.h>
#include <Oxygen/Content/Import/emit/TextureEmitter.h>
#include <Oxygen/Content/Import/fbx/UfbxUtils.h>
#include <Oxygen/Content/Import/util/CoordTransform.h>
#include <Oxygen/Content/Import/util/ImportNaming.h>
#include <Oxygen/Content/Import/util/StringUtils.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/BufferResource.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/Vertex.h>
#include <Oxygen/Serio/MemoryStream.h>
#include <Oxygen/Serio/Writer.h>

#include <glm/glm.hpp>

namespace oxygen::content::import::emit {

namespace {

  using std::string_view_literals::operator""sv;

  using oxygen::data::AssetKey;
  using oxygen::data::AssetType;
  using oxygen::data::BufferResource;
  using oxygen::data::MeshType;
  using oxygen::data::Vertex;
  using oxygen::data::pak::GeometryAssetDesc;
  using oxygen::data::pak::MeshDesc;
  using oxygen::data::pak::MeshViewDesc;
  using oxygen::data::pak::SubMeshDesc;

  struct Bounds3 final {
    std::array<float, 3> min = {
      (std::numeric_limits<float>::max)(),
      (std::numeric_limits<float>::max)(),
      (std::numeric_limits<float>::max)(),
    };
    std::array<float, 3> max = {
      (std::numeric_limits<float>::lowest)(),
      (std::numeric_limits<float>::lowest)(),
      (std::numeric_limits<float>::lowest)(),
    };
  };

  auto ExpandBounds(Bounds3& b, const glm::vec3& p) -> void
  {
    b.min[0] = (std::min)(b.min[0], p.x);
    b.min[1] = (std::min)(b.min[1], p.y);
    b.min[2] = (std::min)(b.min[2], p.z);
    b.max[0] = (std::max)(b.max[0], p.x);
    b.max[1] = (std::max)(b.max[1], p.y);
    b.max[2] = (std::max)(b.max[2], p.z);
  }

  [[nodiscard]] auto HasUvs(const ufbx_mesh& mesh) -> bool
  {
    return mesh.vertex_uv.exists && mesh.vertex_uv.values.data != nullptr
      && mesh.vertex_uv.indices.data != nullptr;
  }

  [[nodiscard]] auto BuildEffectiveMaterialKeys(const ufbx_scene& scene,
    const ImportRequest& request, const std::vector<AssetKey>& material_keys)
    -> std::vector<AssetKey>
  {
    auto effective_material_keys = material_keys;
    if (!effective_material_keys.empty()) {
      return effective_material_keys;
    }

    const auto count = static_cast<uint32_t>(scene.materials.count);
    if (count == 0) {
      const auto name = util::BuildMaterialName("M_Default", request, 0);
      const auto storage_name = util::NamespaceImportedAssetName(request, name);
      const auto virtual_path
        = request.loose_cooked_layout.MaterialVirtualPath(storage_name);

      AssetKey key {};
      switch (request.options.asset_key_policy) {
      case AssetKeyPolicy::kDeterministicFromVirtualPath:
        key = util::MakeDeterministicAssetKey(virtual_path);
        break;
      case AssetKeyPolicy::kRandom:
        key = util::MakeRandomAssetKey();
        break;
      }

      effective_material_keys.push_back(key);
      return effective_material_keys;
    }

    effective_material_keys.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
      const auto* mat = scene.materials.data[i];
      const auto authored_name
        = (mat != nullptr) ? fbx::ToStringView(mat->name) : std::string_view {};
      const auto name = util::BuildMaterialName(authored_name, request, i);
      const auto storage_name = util::NamespaceImportedAssetName(request, name);
      const auto virtual_path
        = request.loose_cooked_layout.MaterialVirtualPath(storage_name);

      AssetKey key {};
      switch (request.options.asset_key_policy) {
      case AssetKeyPolicy::kDeterministicFromVirtualPath:
        key = util::MakeDeterministicAssetKey(virtual_path);
        break;
      case AssetKeyPolicy::kRandom:
        key = util::MakeRandomAssetKey();
        break;
      }

      effective_material_keys.push_back(key);
    }

    return effective_material_keys;
  }

  auto BuildSceneMaterialMaps(const ufbx_scene& scene,
    const std::vector<AssetKey>& effective_material_keys,
    std::unordered_map<const ufbx_material*, uint32_t>& out_scene_index_by_ptr,
    std::unordered_map<const ufbx_material*, AssetKey>& out_key_by_ptr) -> void
  {
    out_scene_index_by_ptr.reserve(static_cast<size_t>(scene.materials.count));
    out_key_by_ptr.reserve(static_cast<size_t>(scene.materials.count));

    for (uint32_t mat_i = 0; mat_i < scene.materials.count; ++mat_i) {
      const auto* mat = scene.materials.data[mat_i];
      if (mat == nullptr) {
        continue;
      }

      out_scene_index_by_ptr.emplace(mat, mat_i);
      if (mat_i < effective_material_keys.size()) {
        out_key_by_ptr.emplace(mat, effective_material_keys[mat_i]);
      }
    }
  }

  [[nodiscard]] auto FindNodesForMesh(const ufbx_scene& scene,
    const ufbx_mesh* target_mesh) -> std::vector<const ufbx_node*>
  {
    std::vector<const ufbx_node*> nodes;
    for (size_t ni = 0; ni < scene.nodes.count; ++ni) {
      const auto* node = scene.nodes.data[ni];
      if (node != nullptr && node->mesh == target_mesh) {
        nodes.push_back(node);
      }
    }
    return nodes;
  }

  [[nodiscard]] auto DisambiguateMeshName(const ufbx_scene& scene,
    const ImportRequest& request, const ufbx_mesh& mesh, uint32_t ordinal,
    std::unordered_map<std::string, uint32_t>& geometry_name_usage_count)
    -> std::string
  {
    const auto authored_name = fbx::ToStringView(mesh.name);
    auto mesh_name = util::BuildMeshName(authored_name, request, ordinal);
    const auto original_mesh_name = mesh_name;

    if (const auto it = geometry_name_usage_count.find(mesh_name);
      it != geometry_name_usage_count.end()) {
      const auto collision_ordinal = it->second;
      std::string new_name;

      const auto nodes = FindNodesForMesh(scene, &mesh);
      if (!nodes.empty()) {
        const auto* node = nodes.front();
        const auto node_name = fbx::ToStringView(node->name);
        if (!node_name.empty()) {
          const auto prefix = mesh_name.starts_with("G_") ? ""sv : "G_"sv;
          new_name = std::string(prefix) + std::string(node_name) + "_"
            + std::string(authored_name.empty()
                ? ("Mesh_" + std::to_string(ordinal))
                : authored_name);
        }
      }

      if (new_name.empty()) {
        new_name = mesh_name + "_" + std::to_string(collision_ordinal);
      }

      LOG_F(INFO, "Geometry name collision detected for '{}', renamed to '{}'",
        original_mesh_name.c_str(), new_name.c_str());
      mesh_name = std::move(new_name);
    }

    geometry_name_usage_count[original_mesh_name]++;
    return mesh_name;
  }

  auto WarnMissingUvsIfNeeded(const ufbx_mesh& mesh,
    const ImportRequest& request, CookedContentWriter& out,
    const std::string_view mesh_name, const bool want_textures) -> void
  {
    if (HasUvs(mesh)) {
      return;
    }
    if (!want_textures) {
      return;
    }
    if (mesh.materials.data == nullptr || mesh.materials.count == 0) {
      return;
    }

    bool has_any_material_texture = false;
    for (uint32_t mi = 0; mi < mesh.materials.count; ++mi) {
      const auto* mat = mesh.materials.data[mi];
      if (mat == nullptr) {
        continue;
      }
      if (SelectBaseColorTexture(*mat) != nullptr || SelectNormalTexture(*mat)
        || SelectMetallicTexture(*mat) != nullptr
        || SelectRoughnessTexture(*mat) != nullptr
        || SelectAmbientOcclusionTexture(*mat) != nullptr
        || SelectEmissiveTexture(*mat) != nullptr) {
        has_any_material_texture = true;
        break;
      }
    }

    if (!has_any_material_texture) {
      return;
    }

    ImportDiagnostic diag {
      .severity = ImportSeverity::kWarning,
      .code = "fbx.mesh.missing_uvs",
      .message = "mesh has materials with textures but no UVs; "
                 "texture sampling and normal mapping may be incorrect",
      .source_path = request.source_path.string(),
      .object_path = std::string(mesh_name),
    };
    out.AddDiagnostic(std::move(diag));
  }

  [[nodiscard]] auto BuildVerticesAndBounds(const ufbx_mesh& mesh,
    const ImportRequest& request) -> std::pair<std::vector<Vertex>, Bounds3>
  {
    std::vector<Vertex> vertices;
    vertices.reserve(mesh.num_indices);

    Bounds3 bounds {};

    const bool has_uv = HasUvs(mesh);

    for (size_t idx = 0; idx < mesh.num_indices; ++idx) {
      auto p = mesh.vertex_position[idx];
      p = coord::ApplySwapYZIfEnabled(request.options.coordinate, p);

      Vertex v {
        .position = { static_cast<float>(p.x), static_cast<float>(p.y),
          static_cast<float>(p.z) },
        .normal = { 0.0f, 1.0f, 0.0f },
        .texcoord = { 0.0f, 0.0f },
        .tangent = { 1.0f, 0.0f, 0.0f },
        .bitangent = { 0.0f, 0.0f, 1.0f },
        .color = { 1.0f, 1.0f, 1.0f, 1.0f },
      };

      if (mesh.vertex_normal.exists && mesh.vertex_normal.values.data != nullptr
        && mesh.vertex_normal.indices.data != nullptr) {
        auto n = mesh.vertex_normal[idx];
        n = coord::ApplySwapYZDirIfEnabled(request.options.coordinate, n);
        v.normal = { static_cast<float>(n.x), static_cast<float>(n.y),
          static_cast<float>(n.z) };
      }

      if (has_uv) {
        const auto uv = mesh.vertex_uv[idx];
        v.texcoord = { static_cast<float>(uv.x), static_cast<float>(uv.y) };
      }

      const auto tangent_policy = request.options.tangent_policy;
      const bool preserve_authored_tangents
        = tangent_policy == GeometryAttributePolicy::kPreserveIfPresent
        || tangent_policy == GeometryAttributePolicy::kGenerateMissing;

      if (preserve_authored_tangents && mesh.vertex_tangent.exists
        && mesh.vertex_tangent.values.data != nullptr
        && mesh.vertex_tangent.indices.data != nullptr) {
        auto t = mesh.vertex_tangent[idx];
        t = coord::ApplySwapYZDirIfEnabled(request.options.coordinate, t);
        const auto tx = static_cast<float>(t.x);
        const auto ty = static_cast<float>(t.y);
        const auto tz = static_cast<float>(t.z);
        if (std::isfinite(tx) && std::isfinite(ty) && std::isfinite(tz)) {
          v.tangent = { tx, ty, tz };
        }
      }

      if (preserve_authored_tangents && mesh.vertex_bitangent.exists
        && mesh.vertex_bitangent.values.data != nullptr
        && mesh.vertex_bitangent.indices.data != nullptr) {
        auto b = mesh.vertex_bitangent[idx];
        b = coord::ApplySwapYZDirIfEnabled(request.options.coordinate, b);
        const auto bx = static_cast<float>(b.x);
        const auto by = static_cast<float>(b.y);
        const auto bz = static_cast<float>(b.z);
        if (std::isfinite(bx) && std::isfinite(by) && std::isfinite(bz)) {
          v.bitangent = { bx, by, bz };
        }
      }

      if (mesh.vertex_color.exists && mesh.vertex_color.values.data != nullptr
        && mesh.vertex_color.indices.data != nullptr) {
        const auto c = mesh.vertex_color[idx];
        v.color = { static_cast<float>(c.x), static_cast<float>(c.y),
          static_cast<float>(c.z), static_cast<float>(c.w) };
      }

      ExpandBounds(bounds, v.position);
      vertices.push_back(v);
    }

    return { std::move(vertices), bounds };
  }

  struct SubmeshBucket final {
    uint32_t scene_material_index = 0;
    AssetKey material_key {};
    std::vector<uint32_t> indices;
  };

  [[nodiscard]] auto BuildSubmeshBuckets(const ufbx_mesh& mesh,
    const std::unordered_map<const ufbx_material*, uint32_t>&
      scene_material_index_by_ptr,
    const std::unordered_map<const ufbx_material*, AssetKey>&
      material_key_by_ptr,
    const AssetKey default_material_key) -> std::vector<SubmeshBucket>
  {
    std::unordered_map<uint32_t, size_t> bucket_index_by_material;
    std::vector<SubmeshBucket> buckets;

    std::vector<uint32_t> tri_indices;
    tri_indices.resize(static_cast<size_t>(mesh.max_face_triangles) * 3);

    auto resolve_bucket = [&](const size_t face_i) -> SubmeshBucket& {
      uint32_t scene_material_index = 0;
      AssetKey material_key = default_material_key;

      if (mesh.face_material.data != nullptr
        && face_i < mesh.face_material.count && mesh.materials.data != nullptr
        && mesh.materials.count > 0) {
        const uint32_t slot = mesh.face_material.data[face_i];
        if (slot != UFBX_NO_INDEX && slot < mesh.materials.count) {
          const auto* mat = mesh.materials.data[slot];
          if (mat != nullptr) {
            if (const auto it = scene_material_index_by_ptr.find(mat);
              it != scene_material_index_by_ptr.end()) {
              scene_material_index = it->second;
            }

            if (const auto it = material_key_by_ptr.find(mat);
              it != material_key_by_ptr.end()) {
              material_key = it->second;
            }
          }
        }
      }

      const auto found = bucket_index_by_material.find(scene_material_index);
      if (found != bucket_index_by_material.end()) {
        return buckets[found->second];
      }

      const auto bucket_i = buckets.size();
      bucket_index_by_material.emplace(scene_material_index, bucket_i);
      buckets.push_back(SubmeshBucket {
        .scene_material_index = scene_material_index,
        .material_key = material_key,
        .indices = {},
      });
      return buckets.back();
    };

    for (size_t face_i = 0; face_i < mesh.faces.count; ++face_i) {
      const auto face = mesh.faces.data[face_i];
      if (face.num_indices < 3) {
        continue;
      }

      auto& bucket = resolve_bucket(face_i);
      const auto tri_count = ufbx_triangulate_face(
        tri_indices.data(), tri_indices.size(), &mesh, face);

      bucket.indices.insert(bucket.indices.end(), tri_indices.begin(),
        tri_indices.begin() + static_cast<ptrdiff_t>(tri_count) * 3);
    }

    buckets.erase(std::remove_if(buckets.begin(), buckets.end(),
                    [](const SubmeshBucket& b) { return b.indices.empty(); }),
      buckets.end());

    std::sort(buckets.begin(), buckets.end(),
      [](const SubmeshBucket& a, const SubmeshBucket& b) {
        return a.scene_material_index < b.scene_material_index;
      });

    return buckets;
  }

  auto GenerateTangentsIfRequested(const ufbx_mesh& mesh,
    const ImportRequest& request, std::vector<Vertex>& vertices,
    const std::vector<SubmeshBucket>& buckets) -> void
  {
    const auto tangent_policy = request.options.tangent_policy;
    const bool has_authored_tangents
      = mesh.vertex_tangent.exists && mesh.vertex_bitangent.exists;

    const bool should_generate_tangents
      = (tangent_policy == GeometryAttributePolicy::kGenerateMissing
          && !has_authored_tangents)
      || (tangent_policy == GeometryAttributePolicy::kAlwaysRecalculate);

    bool has_any_indices = false;
    for (const auto& b : buckets) {
      if (b.indices.size() >= 3) {
        has_any_indices = true;
        break;
      }
    }

    if (tangent_policy == GeometryAttributePolicy::kNone
      || !should_generate_tangents || !HasUvs(mesh) || !has_any_indices) {
      return;
    }

    std::vector<glm::vec3> tan1(vertices.size(), glm::vec3(0.0F));
    std::vector<glm::vec3> tan2(vertices.size(), glm::vec3(0.0F));

    for (const auto& bucket : buckets) {
      const auto tri_count = bucket.indices.size() / 3;
      for (size_t tri = 0; tri < tri_count; ++tri) {
        const auto i0 = bucket.indices[tri * 3 + 0];
        const auto i1 = bucket.indices[tri * 3 + 1];
        const auto i2 = bucket.indices[tri * 3 + 2];
        if (i0 >= vertices.size() || i1 >= vertices.size()
          || i2 >= vertices.size()) {
          continue;
        }

        const auto& v0 = vertices[i0];
        const auto& v1 = vertices[i1];
        const auto& v2 = vertices[i2];

        const glm::vec3 p0 = v0.position;
        const glm::vec3 p1 = v1.position;
        const glm::vec3 p2 = v2.position;

        const glm::vec2 w0 = v0.texcoord;
        const glm::vec2 w1 = v1.texcoord;
        const glm::vec2 w2 = v2.texcoord;

        const glm::vec3 e1 = p1 - p0;
        const glm::vec3 e2 = p2 - p0;
        const glm::vec2 d1 = w1 - w0;
        const glm::vec2 d2 = w2 - w0;

        const float denom = d1.x * d2.y - d1.y * d2.x;
        if (std::abs(denom) < 1e-8F) {
          continue;
        }
        const float r = 1.0F / denom;

        const glm::vec3 t = (e1 * d2.y - e2 * d1.y) * r;
        const glm::vec3 b = (e2 * d1.x - e1 * d2.x) * r;

        tan1[i0] += t;
        tan1[i1] += t;
        tan1[i2] += t;

        tan2[i0] += b;
        tan2[i1] += b;
        tan2[i2] += b;
      }
    }

    for (size_t vi = 0; vi < vertices.size(); ++vi) {
      auto n = vertices[vi].normal;
      const auto n_len = glm::length(n);
      if (n_len > 1e-8F) {
        n /= n_len;
      } else {
        n = glm::vec3(0.0F, 0.0F, 1.0F);
      }

      glm::vec3 t = tan1[vi];
      if (glm::length(t) < 1e-8F) {
        const glm::vec3 axis = (std::abs(n.z) < 0.9F)
          ? glm::vec3(0.0F, 0.0F, 1.0F)
          : glm::vec3(1.0F, 0.0F, 0.0F);
        t = glm::normalize(glm::cross(n, axis));
      } else {
        t = glm::normalize(t - n * glm::dot(n, t));
      }

      glm::vec3 b = glm::cross(n, t);
      if (glm::dot(b, tan2[vi]) < 0.0F) {
        b = -b;
      }

      const auto b_len = glm::length(b);
      if (b_len > 1e-8F) {
        b = b / b_len;
      } else {
        b = glm::normalize(glm::cross(n, t));
      }

      vertices[vi].normal = n;
      vertices[vi].tangent = t;
      vertices[vi].bitangent = b;
    }
  }

  auto FixInvalidTangents(std::vector<Vertex>& vertices) -> void
  {
    for (auto& v : vertices) {
      const bool t_has_nan = !std::isfinite(v.tangent.x)
        || !std::isfinite(v.tangent.y) || !std::isfinite(v.tangent.z);
      const bool b_has_nan = !std::isfinite(v.bitangent.x)
        || !std::isfinite(v.bitangent.y) || !std::isfinite(v.bitangent.z);

      const auto t_len = t_has_nan ? 0.0F : glm::length(v.tangent);
      const auto b_len = b_has_nan ? 0.0F : glm::length(v.bitangent);

      constexpr float kMinValidLen = 0.5F;
      constexpr float kMaxValidLen = 2.0F;

      const bool t_invalid
        = t_has_nan || t_len < kMinValidLen || t_len > kMaxValidLen;
      const bool b_invalid
        = b_has_nan || b_len < kMinValidLen || b_len > kMaxValidLen;

      if (t_invalid || b_invalid) {
        glm::vec3 n = v.normal;
        if (!std::isfinite(n.x) || !std::isfinite(n.y) || !std::isfinite(n.z)
          || glm::length(n) < 1e-6F) {
          n = glm::vec3(0.0F, 0.0F, 1.0F);
        } else {
          n = glm::normalize(n);
        }

        const glm::vec3 axis = (std::abs(n.z) < 0.9F)
          ? glm::vec3(0.0F, 0.0F, 1.0F)
          : glm::vec3(1.0F, 0.0F, 0.0F);

        const glm::vec3 t = glm::normalize(glm::cross(n, axis));
        const glm::vec3 b = glm::normalize(glm::cross(n, t));

        v.tangent = t;
        v.bitangent = b;
        continue;
      }

      v.tangent = glm::normalize(v.tangent);
      v.bitangent = glm::normalize(v.bitangent);
    }
  }

  struct SubmeshBuildResult final {
    std::vector<uint32_t> indices;
    std::vector<SubMeshDesc> submeshes;
    std::vector<MeshViewDesc> views;
  };

  [[nodiscard]] auto BuildSubmeshesAndViews(const std::vector<Vertex>& vertices,
    const std::vector<SubmeshBucket>& buckets) -> SubmeshBuildResult
  {
    SubmeshBuildResult result;

    result.submeshes.reserve(buckets.size());
    result.views.reserve(buckets.size());

    size_t total_indices = 0;
    for (const auto& b : buckets) {
      total_indices += b.indices.size();
    }
    result.indices.reserve(total_indices);

    MeshViewDesc::BufferIndexT index_cursor = 0;
    for (const auto& bucket : buckets) {
      Bounds3 sm_bounds {};

      for (const auto vi : bucket.indices) {
        if (vi >= vertices.size()) {
          continue;
        }
        ExpandBounds(sm_bounds, vertices[vi].position);
      }

      const auto name = "mat_" + std::to_string(bucket.scene_material_index);

      SubMeshDesc sm {};
      util::TruncateAndNullTerminate(sm.name, std::size(sm.name), name);
      sm.material_asset_key = bucket.material_key;
      sm.mesh_view_count = 1;
      std::copy_n(sm_bounds.min.data(), 3, sm.bounding_box_min);
      std::copy_n(sm_bounds.max.data(), 3, sm.bounding_box_max);
      result.submeshes.push_back(sm);

      const auto first_index = index_cursor;
      const auto index_count
        = static_cast<MeshViewDesc::BufferIndexT>(bucket.indices.size());
      index_cursor += index_count;

      result.views.push_back(MeshViewDesc {
        .first_index = first_index,
        .index_count = index_count,
        .first_vertex = 0,
        .vertex_count
        = static_cast<MeshViewDesc::BufferIndexT>(vertices.size()),
      });

      result.indices.insert(
        result.indices.end(), bucket.indices.begin(), bucket.indices.end());
    }

    return result;
  }

  [[nodiscard]] auto ComputeGeometryKey(const ImportRequest& request,
    const std::string_view geo_virtual_path) -> AssetKey
  {
    switch (request.options.asset_key_policy) {
    case AssetKeyPolicy::kDeterministicFromVirtualPath:
      return util::MakeDeterministicAssetKey(geo_virtual_path);
    case AssetKeyPolicy::kRandom:
      return util::MakeRandomAssetKey();
    }
    return {};
  }

  auto EmitGeometryAsset(const ImportRequest& request, CookedContentWriter& out,
    const std::string_view mesh_name, const Bounds3& bounds, uint32_t vb_index,
    uint32_t ib_index, const SubmeshBuildResult& submeshes,
    uint32_t& written_geometry, std::vector<ImportedGeometry>& out_geometry,
    const ufbx_mesh* mesh, const std::string_view storage_mesh_name) -> void
  {
    const auto geo_virtual_path
      = request.loose_cooked_layout.GeometryVirtualPath(storage_mesh_name);

    const auto geo_relpath
      = request.loose_cooked_layout.DescriptorDirFor(AssetType::kGeometry) + "/"
      + LooseCookedLayout::GeometryDescriptorFileName(storage_mesh_name);

    const auto geo_key = ComputeGeometryKey(request, geo_virtual_path);

    GeometryAssetDesc geo_desc {};
    geo_desc.header.asset_type = static_cast<uint8_t>(AssetType::kGeometry);
    util::TruncateAndNullTerminate(
      geo_desc.header.name, std::size(geo_desc.header.name), mesh_name);
    geo_desc.lod_count = 1;
    std::copy_n(bounds.min.data(), 3, geo_desc.bounding_box_min);
    std::copy_n(bounds.max.data(), 3, geo_desc.bounding_box_max);

    MeshDesc lod0 {};
    util::TruncateAndNullTerminate(lod0.name, std::size(lod0.name), mesh_name);
    lod0.mesh_type = static_cast<uint8_t>(MeshType::kStandard);
    lod0.submesh_count = static_cast<uint32_t>(submeshes.submeshes.size());
    lod0.mesh_view_count = static_cast<uint32_t>(submeshes.views.size());
    lod0.info.standard.vertex_buffer = vb_index;
    lod0.info.standard.index_buffer = ib_index;
    std::copy_n(bounds.min.data(), 3, lod0.info.standard.bounding_box_min);
    std::copy_n(bounds.max.data(), 3, lod0.info.standard.bounding_box_max);

    oxygen::serio::MemoryStream desc_stream;
    oxygen::serio::Writer<oxygen::serio::MemoryStream> writer(desc_stream);
    const auto pack = writer.ScopedAlignment(1);

    (void)writer.WriteBlob(
      std::as_bytes(std::span<const GeometryAssetDesc, 1>(&geo_desc, 1)));
    (void)writer.WriteBlob(
      std::as_bytes(std::span<const MeshDesc, 1>(&lod0, 1)));

    for (size_t sm_i = 0; sm_i < submeshes.submeshes.size(); ++sm_i) {
      const auto& sm = submeshes.submeshes[sm_i];
      const auto& view = submeshes.views[sm_i];

      (void)writer.WriteBlob(
        std::as_bytes(std::span<const SubMeshDesc, 1>(&sm, 1)));
      (void)writer.WriteBlob(
        std::as_bytes(std::span<const MeshViewDesc, 1>(&view, 1)));
    }

    const auto geo_bytes = desc_stream.Data();

    LOG_F(INFO, "Emit geometry {} '{}' -> {} (vb={}, ib={}, vtx={}, idx={})",
      written_geometry, std::string(mesh_name).c_str(), geo_relpath.c_str(),
      vb_index, ib_index,
      submeshes.views.empty() ? 0U : submeshes.views[0].vertex_count,
      submeshes.indices.size());

    out.WriteAssetDescriptor(
      geo_key, AssetType::kGeometry, geo_virtual_path, geo_relpath, geo_bytes);

    out_geometry.push_back(ImportedGeometry {
      .mesh = mesh,
      .key = geo_key,
    });

    written_geometry += 1;
  }

} // namespace

auto WriteGeometryAssets(const ufbx_scene& scene, const ImportRequest& request,
  CookedContentWriter& out, const std::vector<AssetKey>& material_keys,
  std::vector<ImportedGeometry>& out_geometry, uint32_t& written_geometry,
  const bool want_textures) -> void
{
  using oxygen::data::loose_cooked::v1::FileKind;
  using oxygen::data::pak::BufferResourceDesc;

  const auto cooked_root = request.cooked_root.value_or(
    std::filesystem::absolute(request.source_path.parent_path()));

  const auto buffers_table_path = cooked_root
    / std::filesystem::path(request.loose_cooked_layout.BuffersTableRelPath());
  const auto buffers_data_path = cooked_root
    / std::filesystem::path(request.loose_cooked_layout.BuffersDataRelPath());

  auto buffers = InitBufferEmissionState(buffers_table_path, buffers_data_path);
  BuildBufferSignatureIndex(buffers, buffers_data_path);

  const auto effective_material_keys
    = BuildEffectiveMaterialKeys(scene, request, material_keys);

  std::unordered_map<std::string, uint32_t> geometry_name_usage_count;

  const auto mesh_count = static_cast<uint32_t>(scene.meshes.count);
  for (uint32_t i = 0; i < mesh_count; ++i) {
    const auto* mesh = scene.meshes.data[i];
    if (mesh == nullptr || mesh->num_indices == 0 || mesh->num_faces == 0) {
      continue;
    }

    std::unordered_map<const ufbx_material*, uint32_t> scene_index_by_ptr;
    std::unordered_map<const ufbx_material*, AssetKey> material_key_by_ptr;
    BuildSceneMaterialMaps(
      scene, effective_material_keys, scene_index_by_ptr, material_key_by_ptr);

    if (!mesh->vertex_position.exists
      || mesh->vertex_position.values.data == nullptr
      || mesh->vertex_position.indices.data == nullptr) {
      ImportDiagnostic diag {
        .severity = ImportSeverity::kError,
        .code = "fbx.mesh.missing_positions",
        .message = "FBX mesh is missing vertex positions",
        .source_path = request.source_path.string(),
        .object_path = std::string(fbx::ToStringView(mesh->name)),
      };
      out.AddDiagnostic(std::move(diag));
      throw std::runtime_error("FBX mesh missing positions");
    }

    const auto mesh_name = DisambiguateMeshName(
      scene, request, *mesh, i, geometry_name_usage_count);

    WarnMissingUvsIfNeeded(*mesh, request, out, mesh_name, want_textures);

    auto [vertices, bounds] = BuildVerticesAndBounds(*mesh, request);

    const auto default_material_key = (!effective_material_keys.empty())
      ? effective_material_keys.front()
      : AssetKey {};

    const auto buckets = BuildSubmeshBuckets(
      *mesh, scene_index_by_ptr, material_key_by_ptr, default_material_key);

    if (vertices.empty() || buckets.empty()) {
      ImportDiagnostic diag {
        .severity = ImportSeverity::kError,
        .code = "fbx.mesh.missing_buffers",
        .message = "FBX mesh does not produce valid vertex/index buffers",
        .source_path = request.source_path.string(),
        .object_path = std::string(mesh_name),
      };
      out.AddDiagnostic(std::move(diag));
      throw std::runtime_error("FBX mesh produced empty buffers");
    }

    GenerateTangentsIfRequested(*mesh, request, vertices, buckets);
    FixInvalidTangents(vertices);

    const auto vb_bytes = std::as_bytes(std::span(vertices));
    constexpr uint32_t vb_stride = sizeof(Vertex);

    const auto vb_usage_flags
      = static_cast<uint32_t>(BufferResource::UsageFlags::kVertexBuffer)
      | static_cast<uint32_t>(BufferResource::UsageFlags::kStatic);

    const auto vb_index = GetOrCreateBufferResourceIndex(buffers, vb_bytes,
      vb_stride, vb_usage_flags, vb_stride,
      static_cast<uint8_t>(oxygen::Format::kUnknown));

    const auto submesh_build = BuildSubmeshesAndViews(vertices, buckets);

    const auto ib_bytes = std::as_bytes(std::span(submesh_build.indices));
    const auto ib_usage_flags
      = static_cast<uint32_t>(BufferResource::UsageFlags::kIndexBuffer)
      | static_cast<uint32_t>(BufferResource::UsageFlags::kStatic);

    const auto ib_index
      = GetOrCreateBufferResourceIndex(buffers, ib_bytes, alignof(uint32_t),
        ib_usage_flags, 0, static_cast<uint8_t>(oxygen::Format::kR32UInt));

    const auto storage_mesh_name
      = util::NamespaceImportedAssetName(request, mesh_name);

    EmitGeometryAsset(request, out, mesh_name, bounds, vb_index, ib_index,
      submesh_build, written_geometry, out_geometry, mesh, storage_mesh_name);
  }

  CloseAppender(buffers.appender);

  if (buffers.table.empty()) {
    return;
  }

  LOG_F(INFO, "Emit buffers table: count={} data_file='{}' -> table='{}'",
    buffers.table.size(),
    request.loose_cooked_layout.BuffersDataRelPath().c_str(),
    request.loose_cooked_layout.BuffersTableRelPath().c_str());

  oxygen::serio::MemoryStream table_stream;
  oxygen::serio::Writer<oxygen::serio::MemoryStream> table_writer(table_stream);
  const auto pack = table_writer.ScopedAlignment(1);
  (void)table_writer.WriteBlob(
    std::as_bytes(std::span<const BufferResourceDesc>(buffers.table)));

  out.WriteFile(FileKind::kBuffersTable,
    request.loose_cooked_layout.BuffersTableRelPath(), table_stream.Data());

  out.RegisterExternalFile(
    FileKind::kBuffersData, request.loose_cooked_layout.BuffersDataRelPath());
}

} // namespace oxygen::content::import::emit
