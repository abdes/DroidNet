//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Content/Import/Internal/Pipelines/GeometryPipeline.h>
#include <Oxygen/Content/Import/Internal/Pipelines/GeometryPipeline_tangents.h>
#include <Oxygen/Content/Import/Internal/Utils/AssetKeyUtils.h>
#include <Oxygen/Content/Import/Internal/Utils/ContentHashUtils.h>
#include <Oxygen/Content/Import/Internal/Utils/StringUtils.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/BufferResource.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/Vertex.h>
#include <Oxygen/Serio/MemoryStream.h>
#include <Oxygen/Serio/Reader.h>
#include <Oxygen/Serio/Writer.h>

namespace oxygen::content::import {

namespace {

  using WorkItem = GeometryPipeline::WorkItem;

  constexpr uint32_t kGeomAttr_Normal = 1u << 0u;
  constexpr uint32_t kGeomAttr_Tangent = 1u << 1u;
  constexpr uint32_t kGeomAttr_Bitangent = 1u << 2u;
  constexpr uint32_t kGeomAttr_Texcoord0 = 1u << 3u;
  constexpr uint32_t kGeomAttr_Color0 = 1u << 4u;
  constexpr uint32_t kGeomAttr_JointWeights = 1u << 5u;
  constexpr uint32_t kGeomAttr_JointIndices = 1u << 6u;

  constexpr uint32_t kDefaultStaticUsageFlags
    = static_cast<uint32_t>(data::BufferResource::UsageFlags::kStatic);

  struct SubmeshBucket {
    uint32_t scene_material_index = 0;
    data::AssetKey material_key {};
    std::vector<uint32_t> indices;
  };

  struct LodBuildData {
    std::string lod_name;
    data::MeshType mesh_type = data::MeshType::kStandard;
    std::vector<data::Vertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<data::pak::SubMeshDesc> submeshes;
    std::vector<data::pak::MeshViewDesc> views;
    std::vector<glm::uvec4> joint_indices;
    std::vector<glm::vec4> joint_weights;
    std::vector<glm::mat4> inverse_bind_matrices;
    std::vector<uint32_t> joint_remap;
    uint16_t joint_count = 0;
    uint16_t influences_per_vertex = 0;
    Bounds3 bounds {};
    uint32_t attr_mask = 0;
  };

  [[nodiscard]] auto MakeErrorDiagnostic(std::string code, std::string message,
    std::string_view source_id, std::string_view object_path)
    -> ImportDiagnostic
  {
    return ImportDiagnostic {
      .severity = ImportSeverity::kError,
      .code = std::move(code),
      .message = std::move(message),
      .source_path = std::string(source_id),
      .object_path = std::string(object_path),
    };
  }

  [[nodiscard]] auto MakeWarningDiagnostic(std::string code,
    std::string message, std::string_view source_id,
    std::string_view object_path) -> ImportDiagnostic
  {
    return ImportDiagnostic {
      .severity = ImportSeverity::kWarning,
      .code = std::move(code),
      .message = std::move(message),
      .source_path = std::string(source_id),
      .object_path = std::string(object_path),
    };
  }

  auto ExpandBounds(Bounds3& bounds, const glm::vec3& p) -> void
  {
    bounds.min[0] = (std::min)(bounds.min[0], p.x);
    bounds.min[1] = (std::min)(bounds.min[1], p.y);
    bounds.min[2] = (std::min)(bounds.min[2], p.z);
    bounds.max[0] = (std::max)(bounds.max[0], p.x);
    bounds.max[1] = (std::max)(bounds.max[1], p.y);
    bounds.max[2] = (std::max)(bounds.max[2], p.z);
  }

  [[nodiscard]] auto MakeEmptyBounds() -> Bounds3
  {
    return Bounds3 {
      .min = { (std::numeric_limits<float>::max)(),
        (std::numeric_limits<float>::max)(),
        (std::numeric_limits<float>::max)(), },
      .max = { (std::numeric_limits<float>::lowest)(),
        (std::numeric_limits<float>::lowest)(),
        (std::numeric_limits<float>::lowest)(), },
    };
  }

  [[nodiscard]] auto HasAnyError(
    const std::vector<ImportDiagnostic>& diagnostics) -> bool
  {
    return std::any_of(
      diagnostics.begin(), diagnostics.end(), [](const ImportDiagnostic& diag) {
        return diag.severity == ImportSeverity::kError;
      });
  }

  auto BuildBucketsForRanges(const std::span<const TriangleRange> ranges,
    const std::vector<data::AssetKey>& material_keys,
    const data::AssetKey default_material_key) -> std::vector<SubmeshBucket>
  {
    std::vector<SubmeshBucket> buckets;
    buckets.reserve(ranges.size());

    for (const auto& range : ranges) {
      const auto existing = std::find_if(
        buckets.begin(), buckets.end(), [&](const SubmeshBucket& bucket) {
          return bucket.scene_material_index == range.material_slot;
        });
      if (existing != buckets.end()) {
        continue;
      }

      const auto material_key = (range.material_slot < material_keys.size())
        ? material_keys[range.material_slot]
        : default_material_key;

      buckets.push_back(SubmeshBucket {
        .scene_material_index = range.material_slot,
        .material_key = material_key,
        .indices = {},
      });
    }

    std::sort(buckets.begin(), buckets.end(),
      [](const SubmeshBucket& a, const SubmeshBucket& b) {
        return a.scene_material_index < b.scene_material_index;
      });

    return buckets;
  }

  auto ComputeNormalsFromTriangles(std::vector<data::Vertex>& vertices,
    const std::span<const SubmeshBucket> buckets) -> void
  {
    if (vertices.empty()) {
      return;
    }

    std::vector normals(vertices.size(), glm::vec3(0.0F));

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

        const auto& v0 = vertices[i0].position;
        const auto& v1 = vertices[i1].position;
        const auto& v2 = vertices[i2].position;

        const auto e1 = v1 - v0;
        const auto e2 = v2 - v0;
        const auto n = glm::cross(e1, e2);

        normals[i0] += n;
        normals[i1] += n;
        normals[i2] += n;
      }
    }

    for (size_t i = 0; i < vertices.size(); ++i) {
      auto n = normals[i];
      const auto len = glm::length(n);
      if (len > 1e-8F) {
        n /= len;
      } else {
        n = glm::vec3(0.0F, 1.0F, 0.0F);
      }
      vertices[i].normal = n;
    }
  }

  auto FixInvalidTangents(std::vector<data::Vertex>& vertices) -> void
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

  [[nodiscard]] auto BuildDescriptorBytes(const std::string_view mesh_name,
    const std::vector<LodBuildData>& lods, const Bounds3& bounds,
    const uint32_t attr_mask, std::vector<ImportDiagnostic>& diagnostics,
    std::string_view source_id) -> std::vector<std::byte>
  {
    using data::pak::GeometryAssetDesc;
    using data::pak::MeshDesc;
    using data::pak::MeshViewDesc;
    using data::pak::SkinnedMeshInfo;
    using data::pak::SubMeshDesc;

    GeometryAssetDesc asset_desc {};
    asset_desc.header.asset_type
      = static_cast<uint8_t>(data::AssetType::kGeometry);
    asset_desc.header.version = data::pak::kGeometryAssetVersion;
    asset_desc.header.variant_flags = attr_mask;
    if (mesh_name.size() >= std::size(asset_desc.header.name)) {
      diagnostics.push_back(MakeWarningDiagnostic("mesh.name_truncated",
        "Mesh name truncated to fit descriptor limit", source_id, mesh_name));
    }
    util::TruncateAndNullTerminate(
      asset_desc.header.name, std::size(asset_desc.header.name), mesh_name);
    asset_desc.lod_count = static_cast<uint32_t>(lods.size());
    std::copy_n(bounds.min.data(), 3, asset_desc.bounding_box_min);
    std::copy_n(bounds.max.data(), 3, asset_desc.bounding_box_max);

    serio::MemoryStream stream;
    serio::Writer writer(stream);
    const auto pack = writer.ScopedAlignment(1);

    const auto asset_result = writer.WriteBlob(
      std::as_bytes(std::span<const GeometryAssetDesc, 1>(&asset_desc, 1)));
    if (!asset_result.has_value()) {
      diagnostics.push_back(MakeErrorDiagnostic("mesh.serialize_failed",
        "Failed to serialize geometry asset descriptor", source_id, mesh_name));
      return {};
    }

    for (const auto& lod : lods) {
      MeshDesc mesh_desc {};
      const std::string_view name_view = [&]() -> std::string_view {
        if (lods.size() <= 1U) {
          return mesh_name;
        }
        return lod.lod_name;
      }();
      if (name_view.size() >= std::size(mesh_desc.name)) {
        const std::string path
          = std::string(mesh_name) + "/" + std::string(name_view);
        diagnostics.push_back(MakeWarningDiagnostic("mesh.lod_name_truncated",
          "LOD name truncated to fit descriptor limit", source_id, path));
      }
      util::TruncateAndNullTerminate(
        mesh_desc.name, std::size(mesh_desc.name), name_view);
      mesh_desc.mesh_type = static_cast<uint8_t>(lod.mesh_type);
      mesh_desc.submesh_count = static_cast<uint32_t>(lod.submeshes.size());
      mesh_desc.mesh_view_count = static_cast<uint32_t>(lod.views.size());

      if (lod.mesh_type == data::MeshType::kSkinned) {
        mesh_desc.info.skinned.vertex_buffer = 0;
        mesh_desc.info.skinned.index_buffer = 0;
        mesh_desc.info.skinned.joint_index_buffer = 0;
        mesh_desc.info.skinned.joint_weight_buffer = 0;
        mesh_desc.info.skinned.inverse_bind_buffer = 0;
        mesh_desc.info.skinned.joint_remap_buffer = 0;
        mesh_desc.info.skinned.joint_count = lod.joint_count;
        mesh_desc.info.skinned.influences_per_vertex
          = lod.influences_per_vertex;
        mesh_desc.info.skinned.flags = 0;
        std::copy_n(
          lod.bounds.min.data(), 3, mesh_desc.info.skinned.bounding_box_min);
        std::copy_n(
          lod.bounds.max.data(), 3, mesh_desc.info.skinned.bounding_box_max);
      } else {
        mesh_desc.info.standard.vertex_buffer = 0;
        mesh_desc.info.standard.index_buffer = 0;
        std::copy_n(
          lod.bounds.min.data(), 3, mesh_desc.info.standard.bounding_box_min);
        std::copy_n(
          lod.bounds.max.data(), 3, mesh_desc.info.standard.bounding_box_max);
      }

      const auto mesh_result = writer.WriteBlob(
        std::as_bytes(std::span<const MeshDesc, 1>(&mesh_desc, 1)));
      if (!mesh_result.has_value()) {
        diagnostics.push_back(MakeErrorDiagnostic("mesh.serialize_failed",
          "Failed to serialize mesh descriptor", source_id, mesh_name));
        return {};
      }

      if (lod.mesh_type == data::MeshType::kSkinned) {
        const auto& skinned_blob = mesh_desc.info.skinned;
        const auto blob_result = writer.WriteBlob(
          std::as_bytes(std::span<const SkinnedMeshInfo, 1>(&skinned_blob, 1)));
        if (!blob_result.has_value()) {
          diagnostics.push_back(MakeErrorDiagnostic("mesh.serialize_failed",
            "Failed to serialize skinned mesh blob", source_id, mesh_name));
          return {};
        }
      }

      for (size_t i = 0; i < lod.submeshes.size(); ++i) {
        const auto& sm = lod.submeshes[i];
        const auto& view = lod.views[i];

        const auto sm_result = writer.WriteBlob(
          std::as_bytes(std::span<const SubMeshDesc, 1>(&sm, 1)));
        if (!sm_result.has_value()) {
          diagnostics.push_back(MakeErrorDiagnostic("mesh.serialize_failed",
            "Failed to serialize submesh descriptor", source_id, mesh_name));
          return {};
        }

        const auto view_result = writer.WriteBlob(
          std::as_bytes(std::span<const MeshViewDesc, 1>(&view, 1)));
        if (!view_result.has_value()) {
          diagnostics.push_back(MakeErrorDiagnostic("mesh.serialize_failed",
            "Failed to serialize mesh view descriptor", source_id, mesh_name));
          return {};
        }
      }
    }

    const auto data = stream.Data();
    return std::vector(data.begin(), data.end());
  }

  [[nodiscard]] auto ResolveGeometryKey(const ImportRequest& request,
    std::string_view virtual_path) -> data::AssetKey
  {
    switch (request.options.asset_key_policy) {
    case AssetKeyPolicy::kRandom:
      return util::MakeRandomAssetKey();
    case AssetKeyPolicy::kDeterministicFromVirtualPath:
      return util::MakeDeterministicAssetKey(virtual_path);
    }

    return util::MakeDeterministicAssetKey(virtual_path);
  }

  auto PopulateVertexDefaults(data::Vertex& vertex) -> void
  {
    vertex.normal = glm::vec3(0.0F, 1.0F, 0.0F);
    vertex.texcoord = glm::vec2(0.0F, 0.0F);
    vertex.tangent = glm::vec3(1.0F, 0.0F, 0.0F);
    vertex.bitangent = glm::vec3(0.0F, 0.0F, 1.0F);
    vertex.color = glm::vec4(1.0F, 1.0F, 1.0F, 1.0F);
  }

  auto BuildVerticesFromRanges(const TriangleMesh& mesh,
    const ImportRequest& request, const GeometryAttributePolicy normal_policy,
    const std::span<const TriangleRange> ranges,
    std::vector<SubmeshBucket>& buckets, std::vector<data::Vertex>& vertices,
    std::vector<uint32_t>& indices, std::vector<glm::uvec4>& joint_indices,
    std::vector<glm::vec4>& joint_weights,
    std::vector<ImportDiagnostic>& diagnostics,
    const std::string_view source_id, const std::string_view object_path)
    -> void
  {
    const auto positions = mesh.streams.positions;
    const auto normals = mesh.streams.normals;
    const auto texcoords = mesh.streams.texcoords;
    const auto tangents = mesh.streams.tangents;
    const auto bitangents = mesh.streams.bitangents;
    const auto colors = mesh.streams.colors;
    const auto joint_ids = mesh.streams.joint_indices;
    const auto joint_wts = mesh.streams.joint_weights;

    const bool has_positions = !positions.empty();
    if (!has_positions) {
      diagnostics.push_back(MakeErrorDiagnostic("mesh.missing_positions",
        "Mesh has no vertex positions", source_id, object_path));
      return;
    }

    const bool has_normals = normals.size() == positions.size();
    const bool has_uvs = texcoords.size() == positions.size();
    const bool has_tangents = tangents.size() == positions.size();
    const bool has_bitangents = bitangents.size() == positions.size();
    const bool has_colors = colors.size() == positions.size();
    const bool has_joints = joint_ids.size() == positions.size();
    const bool has_weights = joint_wts.size() == positions.size();

    auto find_bucket = [&](const uint32_t material_slot) -> SubmeshBucket* {
      const auto it = std::find_if(
        buckets.begin(), buckets.end(), [&](const SubmeshBucket& bucket) {
          return bucket.scene_material_index == material_slot;
        });
      if (it == buckets.end()) {
        return nullptr;
      }
      return &(*it);
    };

    uint32_t next_index = 0;
    for (const auto& range : ranges) {
      uint32_t range_count = range.index_count;
      if (range_count == 0) {
        diagnostics.push_back(MakeWarningDiagnostic("mesh.invalid_range",
          "Triangle range index_count is zero; skipping range", source_id,
          object_path));
        continue;
      }

      const auto remainder = range_count % 3;
      if (remainder != 0) {
        diagnostics.push_back(MakeErrorDiagnostic("mesh.invalid_range",
          "Triangle range index_count must be a multiple of 3", source_id,
          object_path));
        return;
      }

      auto* bucket = find_bucket(range.material_slot);
      if (bucket == nullptr) {
        diagnostics.push_back(MakeWarningDiagnostic("mesh.invalid_range",
          "Triangle range references unknown material slot; skipping range",
          source_id, object_path));
        continue;
      }

      const auto range_end = range.first_index + range_count;
      if (range_end > mesh.indices.size()) {
        diagnostics.push_back(MakeWarningDiagnostic("mesh.invalid_range",
          "Triangle range exceeds index buffer bounds; truncating", source_id,
          object_path));
        if (range.first_index >= mesh.indices.size()) {
          continue;
        }
        range_count
          = static_cast<uint32_t>(mesh.indices.size() - range.first_index);
        const auto rem = range_count % 3U;
        if (rem != 0U) {
          range_count -= rem;
        }
        if (range_count == 0) {
          continue;
        }
      }

      const bool preserve_authored_normals
        = normal_policy == GeometryAttributePolicy::kPreserveIfPresent
        || normal_policy == GeometryAttributePolicy::kGenerateMissing;
      const auto tangent_policy = request.options.tangent_policy;
      const bool preserve_authored_tangents
        = tangent_policy == GeometryAttributePolicy::kPreserveIfPresent
        || tangent_policy == GeometryAttributePolicy::kGenerateMissing;

      auto emit_vertex = [&](const uint32_t source_index) {
        data::Vertex vertex {};
        PopulateVertexDefaults(vertex);

        vertex.position = positions[source_index];

        if (preserve_authored_normals && has_normals) {
          vertex.normal = glm::normalize(normals[source_index]);
        }

        if (has_uvs) {
          vertex.texcoord = texcoords[source_index];
        }

        if (preserve_authored_tangents && has_tangents) {
          vertex.tangent = tangents[source_index];
        }

        if (preserve_authored_tangents && has_bitangents) {
          vertex.bitangent = bitangents[source_index];
        }

        if (has_colors) {
          vertex.color = colors[source_index];
        }

        vertices.push_back(vertex);
        indices.push_back(next_index);
        bucket->indices.push_back(next_index);
        ++next_index;

        if (mesh.mesh_type == data::MeshType::kSkinned) {
          if (has_joints && has_weights) {
            joint_indices.push_back(joint_ids[source_index]);
            joint_weights.push_back(joint_wts[source_index]);
          }
        }
      };

      size_t skipped_triangles = 0;
      for (uint32_t i = 0; i < range_count; i += 3) {
        const auto idx0 = mesh.indices[range.first_index + i + 0];
        const auto idx1 = mesh.indices[range.first_index + i + 1];
        const auto idx2 = mesh.indices[range.first_index + i + 2];
        if (idx0 >= positions.size() || idx1 >= positions.size()
          || idx2 >= positions.size()) {
          ++skipped_triangles;
          continue;
        }

        emit_vertex(idx0);
        emit_vertex(idx1);
        emit_vertex(idx2);
      }

      if (skipped_triangles > 0) {
        diagnostics.push_back(MakeWarningDiagnostic("mesh.invalid_index",
          "Skipped triangles with out-of-range indices", source_id,
          object_path));
      }
    }

    if (mesh.mesh_type == data::MeshType::kSkinned) {
      if (!has_joints || !has_weights) {
        diagnostics.push_back(MakeErrorDiagnostic("mesh.missing_skinning",
          "Skinned mesh requires joint indices and weights", source_id,
          object_path));
        return;
      }
    }
  }

  auto BuildSubmeshDescriptors(const std::vector<data::Vertex>& vertices,
    const std::vector<SubmeshBucket>& buckets,
    std::vector<data::pak::SubMeshDesc>& submeshes,
    std::vector<data::pak::MeshViewDesc>& views,
    std::vector<uint32_t>& merged_indices) -> Bounds3
  {
    using data::pak::MeshViewDesc;
    using data::pak::SubMeshDesc;

    Bounds3 mesh_bounds = MakeEmptyBounds();

    size_t total_indices = 0;
    for (const auto& bucket : buckets) {
      total_indices += bucket.indices.size();
    }

    merged_indices.clear();
    merged_indices.reserve(total_indices);

    submeshes.clear();
    views.clear();
    submeshes.reserve(buckets.size());
    views.reserve(buckets.size());

    MeshViewDesc::BufferIndexT index_cursor = 0;
    for (const auto& bucket : buckets) {
      Bounds3 submesh_bounds = MakeEmptyBounds();
      uint32_t min_vertex = std::numeric_limits<uint32_t>::max();
      uint32_t max_vertex = 0;
      for (const auto vi : bucket.indices) {
        if (vi >= vertices.size()) {
          continue;
        }
        ExpandBounds(submesh_bounds, vertices[vi].position);
        ExpandBounds(mesh_bounds, vertices[vi].position);
        min_vertex = (std::min)(min_vertex, vi);
        max_vertex = (std::max)(max_vertex, vi);
      }

      const auto name = "mat_" + std::to_string(bucket.scene_material_index);

      SubMeshDesc submesh {};
      util::TruncateAndNullTerminate(
        submesh.name, std::size(submesh.name), name);
      submesh.material_asset_key = bucket.material_key;
      submesh.mesh_view_count = 1;
      std::copy_n(submesh_bounds.min.data(), 3, submesh.bounding_box_min);
      std::copy_n(submesh_bounds.max.data(), 3, submesh.bounding_box_max);
      submeshes.push_back(submesh);

      const auto first_index = index_cursor;
      const auto index_count
        = static_cast<MeshViewDesc::BufferIndexT>(bucket.indices.size());
      index_cursor += index_count;

      const auto vertex_count
        = (min_vertex <= max_vertex) ? (max_vertex - min_vertex + 1) : 0;
      if (vertex_count == 0) {
        min_vertex = 0;
      }

      views.push_back(MeshViewDesc {
        .first_index = first_index,
        .index_count = index_count,
        .first_vertex = static_cast<MeshViewDesc::BufferIndexT>(min_vertex),
        .vertex_count = static_cast<MeshViewDesc::BufferIndexT>(vertex_count),
      });

      for (const auto vi : bucket.indices) {
        const auto adjusted = (min_vertex > 0) ? (vi - min_vertex) : vi;
        merged_indices.push_back(adjusted);
      }
    }

    return mesh_bounds;
  }

  [[nodiscard]] auto BuildLodData(const TriangleMesh& mesh,
    const MeshLod& lod_source, const WorkItem& item,
    const uint64_t max_data_blob_bytes,
    std::vector<ImportDiagnostic>& diagnostics, uint32_t& attr_mask)
    -> std::optional<LodBuildData>
  {
    DLOG_F(1, "GeometryPipeline: Build LOD data");
    LodBuildData lod;
    lod.lod_name = lod_source.lod_name;
    lod.mesh_type = mesh.mesh_type;

    if (lod.mesh_type == data::MeshType::kProcedural) {
      diagnostics.push_back(MakeErrorDiagnostic("mesh.procedural_unsupported",
        "Procedural meshes are not supported by GeometryPipeline",
        item.source_id, item.mesh_name));
      return std::nullopt;
    }

    if (lod.mesh_type != data::MeshType::kStandard
      && lod.mesh_type != data::MeshType::kSkinned) {
      diagnostics.push_back(MakeErrorDiagnostic("mesh.unsupported_type",
        "Mesh type is unsupported in GeometryPipeline", item.source_id,
        item.mesh_name));
      return std::nullopt;
    }

    if (mesh.indices.empty() || mesh.ranges.empty()) {
      diagnostics.push_back(MakeErrorDiagnostic("mesh.missing_buffers",
        "Mesh is missing triangle indices or ranges", item.source_id,
        item.mesh_name));
      return std::nullopt;
    }

    lod.vertices.reserve(mesh.indices.size());
    lod.indices.reserve(mesh.indices.size());

    auto buckets = BuildBucketsForRanges(
      mesh.ranges, item.material_keys, item.default_material_key);

    BuildVerticesFromRanges(mesh, item.request,
      item.request.options.normal_policy, mesh.ranges, buckets, lod.vertices,
      lod.indices, lod.joint_indices, lod.joint_weights, diagnostics,
      item.source_id, item.mesh_name);

    if (HasAnyError(diagnostics)) {
      return std::nullopt;
    }

    if (lod.mesh_type == data::MeshType::kSkinned) {
      if (mesh.inverse_bind_matrices.empty()) {
        diagnostics.push_back(MakeErrorDiagnostic("mesh.missing_inverse_bind",
          "Skinned mesh missing inverse bind matrices", item.source_id,
          item.mesh_name));
        return std::nullopt;
      }
      if (mesh.joint_remap.empty()) {
        diagnostics.push_back(MakeErrorDiagnostic("mesh.missing_joint_remap",
          "Skinned mesh missing joint remap data", item.source_id,
          item.mesh_name));
        return std::nullopt;
      }
      lod.inverse_bind_matrices.assign(
        mesh.inverse_bind_matrices.begin(), mesh.inverse_bind_matrices.end());
      lod.joint_remap.assign(mesh.joint_remap.begin(), mesh.joint_remap.end());
    }

    const bool has_uvs
      = mesh.streams.texcoords.size() == mesh.streams.positions.size();

    if (item.want_textures && item.has_material_textures && !has_uvs) {
      diagnostics.push_back(MakeWarningDiagnostic("mesh.missing_uvs",
        "Mesh has textured materials but no UVs", item.source_id,
        item.mesh_name));
    }

    const auto normal_policy = item.request.options.normal_policy;
    const auto tangent_policy = item.request.options.tangent_policy;

    const bool has_normals
      = mesh.streams.normals.size() == mesh.streams.positions.size();

    const bool should_generate_normals
      = normal_policy == GeometryAttributePolicy::kGenerateMissing
      || normal_policy == GeometryAttributePolicy::kAlwaysRecalculate;

    if (normal_policy == GeometryAttributePolicy::kNone) {
      // Keep defaults; mask will not include normals.
    } else if (normal_policy == GeometryAttributePolicy::kAlwaysRecalculate) {
      ComputeNormalsFromTriangles(lod.vertices, buckets);
    } else if (!has_normals && should_generate_normals) {
      ComputeNormalsFromTriangles(lod.vertices, buckets);
    }

    const bool final_has_normals
      = normal_policy != GeometryAttributePolicy::kNone
      && (has_normals || should_generate_normals);

    const bool needs_tangents
      = tangent_policy != GeometryAttributePolicy::kNone;
    const bool has_tangent_prereq = final_has_normals && has_uvs;

    bool tangents_emitted = false;

    if (needs_tangents && !has_tangent_prereq) {
      diagnostics.push_back(MakeWarningDiagnostic("mesh.missing_tangent_prereq",
        "Tangents require positions, normals, and UVs", item.source_id,
        item.mesh_name));
    } else if (needs_tangents) {
      const bool has_tangents
        = mesh.streams.tangents.size() == mesh.streams.positions.size()
        && mesh.streams.bitangents.size() == mesh.streams.positions.size();

      if (tangent_policy == GeometryAttributePolicy::kAlwaysRecalculate
        || (tangent_policy == GeometryAttributePolicy::kGenerateMissing
          && !has_tangents)) {
        if (item.stop_token.stop_requested()) {
          return std::nullopt;
        }
        util::GenerateTangents(lod.vertices,
          std::span<const SubmeshBucket>(buckets.data(), buckets.size()));
        tangents_emitted = true;
      } else if (has_tangents) {
        tangents_emitted = true;
      }

      if (tangents_emitted) {
        FixInvalidTangents(lod.vertices);
      }
    }

    const auto computed_bounds = BuildSubmeshDescriptors(
      lod.vertices, buckets, lod.submeshes, lod.views, lod.indices);
    LOG_F(INFO, "Mesh '{}' LOD '{}' submesh_count={} view_count={}",
      item.mesh_name, lod.lod_name, lod.submeshes.size(), lod.views.size());
    std::size_t views_with_base_vertex = 0;
    for (const auto& view : lod.views) {
      if (view.first_vertex != 0) {
        ++views_with_base_vertex;
      }
    }
    if (views_with_base_vertex > 0) {
      LOG_F(INFO, "Mesh '{}' LOD '{}' views_with_base_vertex={}",
        item.mesh_name, lod.lod_name, views_with_base_vertex);
    }
    if (mesh.bounds.has_value()) {
      lod.bounds = *mesh.bounds;
    } else {
      lod.bounds = computed_bounds;
    }

    if (lod.vertices.empty() || lod.indices.empty() || lod.submeshes.empty()) {
      diagnostics.push_back(MakeErrorDiagnostic("mesh.missing_buffers",
        "Mesh does not produce valid vertex/index buffers", item.source_id,
        item.mesh_name));
      return std::nullopt;
    }

    if (lod.views.empty()) {
      diagnostics.push_back(MakeErrorDiagnostic("mesh.missing_buffers",
        "Mesh does not produce valid mesh views", item.source_id,
        item.mesh_name));
      return std::nullopt;
    }

    const auto max_u32 = (std::numeric_limits<uint32_t>::max)();
    if (lod.vertices.size() > max_u32 || lod.indices.size() > max_u32
      || lod.submeshes.size() > max_u32 || lod.views.size() > max_u32) {
      diagnostics.push_back(MakeErrorDiagnostic("mesh.count_overflow",
        "Mesh vertex/index/submesh counts exceed uint32 limits", item.source_id,
        item.mesh_name));
      return std::nullopt;
    }

    const auto vb_bytes
      = static_cast<uint64_t>(lod.vertices.size()) * sizeof(data::Vertex);
    const auto ib_bytes
      = static_cast<uint64_t>(lod.indices.size()) * sizeof(uint32_t);
    if (vb_bytes > max_data_blob_bytes || ib_bytes > max_data_blob_bytes) {
      diagnostics.push_back(MakeErrorDiagnostic("mesh.buffer_too_large",
        "Mesh buffer exceeds maximum data blob size", item.source_id,
        item.mesh_name));
      return std::nullopt;
    }

    if (final_has_normals) {
      attr_mask |= kGeomAttr_Normal;
    }
    if (tangents_emitted) {
      attr_mask |= kGeomAttr_Tangent;
      attr_mask |= kGeomAttr_Bitangent;
    }
    if (has_uvs) {
      attr_mask |= kGeomAttr_Texcoord0;
    }
    if (mesh.streams.colors.size() == mesh.streams.positions.size()) {
      attr_mask |= kGeomAttr_Color0;
    }

    if (mesh.mesh_type == data::MeshType::kSkinned) {
      const auto joint_bytes
        = static_cast<uint64_t>(lod.joint_indices.size()) * sizeof(glm::uvec4);
      const auto weight_bytes
        = static_cast<uint64_t>(lod.joint_weights.size()) * sizeof(glm::vec4);
      const auto inverse_bind_bytes
        = static_cast<uint64_t>(lod.inverse_bind_matrices.size())
        * sizeof(glm::mat4);
      const auto remap_bytes
        = static_cast<uint64_t>(lod.joint_remap.size()) * sizeof(uint32_t);
      if (joint_bytes > max_data_blob_bytes
        || weight_bytes > max_data_blob_bytes
        || inverse_bind_bytes > max_data_blob_bytes
        || remap_bytes > max_data_blob_bytes) {
        diagnostics.push_back(MakeErrorDiagnostic("mesh.buffer_too_large",
          "Skinned mesh buffer exceeds maximum data blob size", item.source_id,
          item.mesh_name));
        return std::nullopt;
      }

      uint32_t max_joint = 0;
      for (const auto& joints : lod.joint_indices) {
        max_joint = (std::max)(max_joint, joints.x);
        max_joint = (std::max)(max_joint, joints.y);
        max_joint = (std::max)(max_joint, joints.z);
        max_joint = (std::max)(max_joint, joints.w);
      }

      if (!lod.joint_indices.empty()) {
        const uint32_t required_joint_count = max_joint + 1;
        if (required_joint_count
            > static_cast<uint32_t>(lod.inverse_bind_matrices.size())
          || required_joint_count
            > static_cast<uint32_t>(lod.joint_remap.size())) {
          diagnostics.push_back(
            MakeErrorDiagnostic("mesh.skinning_buffers_mismatch",
              "Skinned mesh joint data exceeds inverse bind/remap counts",
              item.source_id, item.mesh_name));
          return std::nullopt;
        }

        const auto max_u16
          = static_cast<uint32_t>((std::numeric_limits<uint16_t>::max)());
        lod.joint_count
          = static_cast<uint16_t>((std::min)(required_joint_count, max_u16));
        lod.influences_per_vertex = 4;
      }

      attr_mask |= kGeomAttr_JointIndices;
      attr_mask |= kGeomAttr_JointWeights;
    }

    return lod;
  }

  struct GeometryBuildOutcome {
    std::string source_id;
    const void* source_key = nullptr;
    std::optional<GeometryPipeline::CookedGeometryPayload> cooked;
    std::vector<ImportDiagnostic> diagnostics;
    bool canceled = false;
    bool success = false;
  };

  template <typename T>
  [[nodiscard]] auto ToByteVector(const std::span<const T> data)
    -> std::vector<std::byte>
  {
    const auto bytes = std::as_bytes(data);
    return std::vector<std::byte>(bytes.begin(), bytes.end());
  }

} // namespace

GeometryPipeline::GeometryPipeline(co::ThreadPool& thread_pool, Config config)
  : thread_pool_(thread_pool)
  , config_(config)
  , input_channel_(config.queue_capacity)
  , output_channel_(config.queue_capacity)
{
}

GeometryPipeline::~GeometryPipeline()
{
  if (started_) {
    DLOG_IF_F(WARNING, HasPending(),
      "GeometryPipeline destroyed with {} pending items", PendingCount());
  }

  input_channel_.Close();
  output_channel_.Close();
}

auto GeometryPipeline::Start(co::Nursery& nursery) -> void
{
  DCHECK_F(!started_, "GeometryPipeline::Start() called more than once");
  started_ = true;

  const auto worker_count = std::max(1U, config_.worker_count);
  for (uint32_t i = 0; i < worker_count; ++i) {
    nursery.Start([this]() -> co::Co<> { co_await Worker(); });
  }
}

auto GeometryPipeline::Submit(WorkItem item) -> co::Co<>
{
  ++pending_;
  submitted_.fetch_add(1, std::memory_order_acq_rel);
  co_await input_channel_.Send(std::move(item));
}

auto GeometryPipeline::TrySubmit(WorkItem item) -> bool
{
  if (input_channel_.Closed() || input_channel_.Full()) {
    return false;
  }

  const auto ok = input_channel_.TrySend(std::move(item));
  if (ok) {
    ++pending_;
    submitted_.fetch_add(1, std::memory_order_acq_rel);
  }
  return ok;
}

auto GeometryPipeline::Collect() -> co::Co<WorkResult>
{
  auto maybe_result = co_await output_channel_.Receive();
  if (!maybe_result.has_value()) {
    co_return WorkResult {
      .source_id = {},
      .source_key = nullptr,
      .cooked = std::nullopt,
      .diagnostics = {},
      .success = false,
    };
  }

  pending_.fetch_sub(1, std::memory_order_acq_rel);
  if (maybe_result->success) {
    completed_.fetch_add(1, std::memory_order_acq_rel);
  } else {
    failed_.fetch_add(1, std::memory_order_acq_rel);
  }

  co_return std::move(*maybe_result);
}

auto GeometryPipeline::FinalizeDescriptorBytes(
  const std::span<const MeshBufferBindings> bindings,
  const std::span<const std::byte> descriptor_bytes,
  std::vector<ImportDiagnostic>& diagnostics)
  -> co::Co<std::optional<std::vector<std::byte>>>
{
  if (descriptor_bytes.empty()) {
    diagnostics.push_back(MakeErrorDiagnostic(
      "mesh.finalize_failed", "Descriptor bytes are empty", "", ""));
    co_return std::nullopt;
  }

  std::vector input_copy(descriptor_bytes.begin(), descriptor_bytes.end());
  serio::MemoryStream input_stream(
    std::span(input_copy.data(), input_copy.size()));
  serio::Reader reader(input_stream);
  const auto pack_reader = reader.ScopedAlignment(1);

  auto read_pod = [&reader](auto& value) -> bool {
    auto bytes = std::as_writable_bytes(std::span { &value, 1 });
    return static_cast<bool>(reader.ReadBlobInto(bytes));
  };

  data::pak::GeometryAssetDesc asset_desc {};
  if (!read_pod(asset_desc)) {
    diagnostics.push_back(MakeErrorDiagnostic("mesh.finalize_failed",
      "Failed to read geometry asset descriptor", "", ""));
    co_return std::nullopt;
  }

  if (bindings.size() != asset_desc.lod_count) {
    diagnostics.push_back(MakeErrorDiagnostic("mesh.finalize_failed",
      "Descriptor LOD count does not match bindings", "", ""));
    co_return std::nullopt;
  }

  serio::MemoryStream output_stream;
  serio::Writer writer(output_stream);
  const auto pack_writer = writer.ScopedAlignment(1);

  asset_desc.header.content_hash = 0;
  if (!writer.WriteBlob(std::as_bytes(
        std::span<const data::pak::GeometryAssetDesc, 1>(&asset_desc, 1)))) {
    diagnostics.push_back(MakeErrorDiagnostic("mesh.finalize_failed",
      "Failed to write geometry asset descriptor", "", ""));
    co_return std::nullopt;
  }

  for (uint32_t lod_i = 0; lod_i < asset_desc.lod_count; ++lod_i) {
    data::pak::MeshDesc mesh_desc {};
    if (!read_pod(mesh_desc)) {
      diagnostics.push_back(MakeErrorDiagnostic(
        "mesh.finalize_failed", "Failed to read mesh descriptor", "", ""));
      co_return std::nullopt;
    }

    const auto& binding = bindings[lod_i];

    if (static_cast<data::MeshType>(mesh_desc.mesh_type)
      == data::MeshType::kSkinned) {
      data::pak::SkinnedMeshInfo skinned_blob {};
      if (!read_pod(skinned_blob)) {
        diagnostics.push_back(MakeErrorDiagnostic(
          "mesh.finalize_failed", "Failed to read skinned mesh blob", "", ""));
        co_return std::nullopt;
      }

      mesh_desc.info.skinned.vertex_buffer = binding.vertex_buffer;
      mesh_desc.info.skinned.index_buffer = binding.index_buffer;
      mesh_desc.info.skinned.joint_index_buffer = binding.joint_index_buffer;
      mesh_desc.info.skinned.joint_weight_buffer = binding.joint_weight_buffer;
      mesh_desc.info.skinned.inverse_bind_buffer = binding.inverse_bind_buffer;
      mesh_desc.info.skinned.joint_remap_buffer = binding.joint_remap_buffer;

      skinned_blob.vertex_buffer = binding.vertex_buffer;
      skinned_blob.index_buffer = binding.index_buffer;
      skinned_blob.joint_index_buffer = binding.joint_index_buffer;
      skinned_blob.joint_weight_buffer = binding.joint_weight_buffer;
      skinned_blob.inverse_bind_buffer = binding.inverse_bind_buffer;
      skinned_blob.joint_remap_buffer = binding.joint_remap_buffer;

      if (!writer.WriteBlob(std::as_bytes(
            std::span<const data::pak::MeshDesc, 1>(&mesh_desc, 1)))) {
        diagnostics.push_back(MakeErrorDiagnostic(
          "mesh.finalize_failed", "Failed to write mesh descriptor", "", ""));
        co_return std::nullopt;
      }

      if (!writer.WriteBlob(
            std::as_bytes(std::span<const data::pak::SkinnedMeshInfo, 1>(
              &skinned_blob, 1)))) {
        diagnostics.push_back(MakeErrorDiagnostic(
          "mesh.finalize_failed", "Failed to write skinned mesh blob", "", ""));
        co_return std::nullopt;
      }
    } else if (static_cast<data::MeshType>(mesh_desc.mesh_type)
      == data::MeshType::kProcedural) {
      data::pak::ProceduralMeshInfo procedural_info {};
      if (!read_pod(procedural_info)) {
        diagnostics.push_back(MakeErrorDiagnostic("mesh.finalize_failed",
          "Failed to read procedural mesh blob", "", ""));
        co_return std::nullopt;
      }

      auto blob = reader.ReadBlob(procedural_info.params_size);
      if (!blob) {
        diagnostics.push_back(MakeErrorDiagnostic("mesh.finalize_failed",
          "Failed to read procedural mesh params", "", ""));
        co_return std::nullopt;
      }

      if (!writer.WriteBlob(std::as_bytes(
            std::span<const data::pak::MeshDesc, 1>(&mesh_desc, 1)))) {
        diagnostics.push_back(MakeErrorDiagnostic(
          "mesh.finalize_failed", "Failed to write mesh descriptor", "", ""));
        co_return std::nullopt;
      }

      if (!writer.WriteBlob(
            std::as_bytes(std::span<const data::pak::ProceduralMeshInfo, 1>(
              &procedural_info, 1)))) {
        diagnostics.push_back(MakeErrorDiagnostic("mesh.finalize_failed",
          "Failed to write procedural mesh blob", "", ""));
        co_return std::nullopt;
      }

      if (!writer.WriteBlob(std::as_bytes(
            std::span<const std::byte>(blob->data(), blob->size())))) {
        diagnostics.push_back(MakeErrorDiagnostic("mesh.finalize_failed",
          "Failed to write procedural mesh params", "", ""));
        co_return std::nullopt;
      }
    } else {
      mesh_desc.info.standard.vertex_buffer = binding.vertex_buffer;
      mesh_desc.info.standard.index_buffer = binding.index_buffer;

      if (!writer.WriteBlob(std::as_bytes(
            std::span<const data::pak::MeshDesc, 1>(&mesh_desc, 1)))) {
        diagnostics.push_back(MakeErrorDiagnostic(
          "mesh.finalize_failed", "Failed to write mesh descriptor", "", ""));
        co_return std::nullopt;
      }
    }

    for (uint32_t sub = 0; sub < mesh_desc.submesh_count; ++sub) {
      data::pak::SubMeshDesc submesh_desc {};
      if (!read_pod(submesh_desc)) {
        diagnostics.push_back(MakeErrorDiagnostic(
          "mesh.finalize_failed", "Failed to read submesh descriptor", "", ""));
        co_return std::nullopt;
      }

      if (!writer.WriteBlob(std::as_bytes(
            std::span<const data::pak::SubMeshDesc, 1>(&submesh_desc, 1)))) {
        diagnostics.push_back(MakeErrorDiagnostic("mesh.finalize_failed",
          "Failed to write submesh descriptor", "", ""));
        co_return std::nullopt;
      }
    }

    for (uint32_t view = 0; view < mesh_desc.mesh_view_count; ++view) {
      data::pak::MeshViewDesc view_desc {};
      if (!read_pod(view_desc)) {
        diagnostics.push_back(MakeErrorDiagnostic("mesh.finalize_failed",
          "Failed to read mesh view descriptor", "", ""));
        co_return std::nullopt;
      }

      if (!writer.WriteBlob(std::as_bytes(
            std::span<const data::pak::MeshViewDesc, 1>(&view_desc, 1)))) {
        diagnostics.push_back(MakeErrorDiagnostic("mesh.finalize_failed",
          "Failed to write mesh view descriptor", "", ""));
        co_return std::nullopt;
      }
    }
  }

  const auto output_span = output_stream.Data();
  std::vector output_bytes(output_span.begin(), output_span.end());

  if (config_.with_content_hashing) {
    const auto hash = co_await thread_pool_.Run(
      [bytes = std::span<const std::byte>(output_bytes.data(),
         output_bytes.size())](co::ThreadPool::CancelToken canceled) noexcept {
        DLOG_F(1, "GeometryPipeline: Compute content hash");
        if (canceled) {
          return uint64_t { 0 };
        }
        return util::ComputeContentHash(bytes);
      });

    if (hash != 0) {
      asset_desc.header.content_hash = hash;
      serio::MemoryStream patch_stream(
        std::span(output_bytes.data(), output_bytes.size()));
      serio::Writer patch_writer(patch_stream);
      const auto patch_pack = patch_writer.ScopedAlignment(1);
      if (!patch_writer.WriteBlob(
            std::as_bytes(std::span<const data::pak::GeometryAssetDesc, 1>(
              &asset_desc, 1)))) {
        diagnostics.push_back(MakeErrorDiagnostic("mesh.finalize_failed",
          "Failed to write geometry asset descriptor hash", "", ""));
        co_return std::nullopt;
      }
    }
  }

  co_return output_bytes;
}

auto GeometryPipeline::Close() -> void { input_channel_.Close(); }

auto GeometryPipeline::HasPending() const noexcept -> bool
{
  return pending_.load(std::memory_order_acquire) > 0;
}

auto GeometryPipeline::PendingCount() const noexcept -> size_t
{
  return pending_.load(std::memory_order_acquire);
}

auto GeometryPipeline::GetProgress() const noexcept -> PipelineProgress
{
  const auto submitted = submitted_.load(std::memory_order_acquire);
  const auto completed = completed_.load(std::memory_order_acquire);
  const auto failed = failed_.load(std::memory_order_acquire);
  return PipelineProgress {
    .submitted = submitted,
    .completed = completed,
    .failed = failed,
    .in_flight = submitted - completed - failed,
    .throughput = 0.0F,
  };
}

auto GeometryPipeline::Worker() -> co::Co<>
{
  while (true) {
    auto maybe_item = co_await input_channel_.Receive();
    if (!maybe_item.has_value()) {
      break;
    }

    auto item = std::move(*maybe_item);
    if (item.stop_token.stop_requested()) {
      co_await ReportCancelled(std::move(item));
      continue;
    }
    auto build_outcome = co_await thread_pool_.Run(
      [item = std::move(item), max_bytes = config_.max_data_blob_bytes,
        with_content_hashing = config_.with_content_hashing](
        co::ThreadPool::CancelToken canceled) mutable -> GeometryBuildOutcome {
        DLOG_F(1, "GeometryPipeline: Build geometry payload");
        GeometryBuildOutcome out;
        out.source_id = item.source_id;
        out.source_key = item.source_key;

        if (canceled || item.stop_token.stop_requested()) {
          out.canceled = true;
          return out;
        }

        if (item.lods.empty()) {
          out.diagnostics.push_back(MakeErrorDiagnostic("mesh.missing_lods",
            "Mesh LOD list is empty", item.source_id, item.mesh_name));
          return out;
        }

        if (item.lods.size() > 8) {
          out.diagnostics.push_back(MakeErrorDiagnostic(
            "mesh.invalid_lod_count", "Mesh LOD count exceeds maximum of 8",
            item.source_id, item.mesh_name));
          return out;
        }

        std::vector<LodBuildData> lods;
        lods.reserve(item.lods.size());

        uint32_t attr_mask = 0;
        for (const auto& lod : item.lods) {
          if (item.stop_token.stop_requested()) {
            out.canceled = true;
            return out;
          }

          const auto& triangle_mesh = lod.source;
          auto lod_data = BuildLodData(
            triangle_mesh, lod, item, max_bytes, out.diagnostics, attr_mask);
          if (!lod_data.has_value()) {
            return out;
          }
          lods.push_back(std::move(*lod_data));
        }

        if (item.stop_token.stop_requested()) {
          out.canceled = true;
          return out;
        }

        if (HasAnyError(out.diagnostics)) {
          return out;
        }

        Bounds3 geom_bounds = MakeEmptyBounds();
        for (const auto& lod : lods) {
          ExpandBounds(geom_bounds,
            glm::vec3(lod.bounds.min[0], lod.bounds.min[1], lod.bounds.min[2]));
          ExpandBounds(geom_bounds,
            glm::vec3(lod.bounds.max[0], lod.bounds.max[1], lod.bounds.max[2]));
        }

        auto descriptor_bytes = BuildDescriptorBytes(item.mesh_name, lods,
          geom_bounds, attr_mask, out.diagnostics, item.source_id);

        if (HasAnyError(out.diagnostics)) {
          return out;
        }

        CookedGeometryPayload cooked_payload;
        cooked_payload.virtual_path
          = item.request.loose_cooked_layout.GeometryVirtualPath(
            item.storage_mesh_name);
        cooked_payload.descriptor_relpath
          = item.request.loose_cooked_layout.GeometryDescriptorRelPath(
            item.storage_mesh_name);
        cooked_payload.geometry_key
          = ResolveGeometryKey(item.request, cooked_payload.virtual_path);
        cooked_payload.descriptor_bytes = std::move(descriptor_bytes);

        for (const auto& lod : lods) {
          CookedMeshPayload cooked_mesh;

          const auto vb_usage_flags
            = static_cast<uint32_t>(
                data::BufferResource::UsageFlags::kVertexBuffer)
            | kDefaultStaticUsageFlags;
          const auto ib_usage_flags
            = static_cast<uint32_t>(
                data::BufferResource::UsageFlags::kIndexBuffer)
            | kDefaultStaticUsageFlags;

          cooked_mesh.vertex_buffer.data
            = ToByteVector(std::span(lod.vertices.data(), lod.vertices.size()));
          cooked_mesh.vertex_buffer.alignment = sizeof(data::Vertex);
          cooked_mesh.vertex_buffer.usage_flags = vb_usage_flags;
          cooked_mesh.vertex_buffer.element_stride = sizeof(data::Vertex);
          cooked_mesh.vertex_buffer.element_format
            = static_cast<uint8_t>(Format::kUnknown);
          if (with_content_hashing && !item.stop_token.stop_requested()) {
            cooked_mesh.vertex_buffer.content_hash = util::ComputeContentHash(
              std::span<const std::byte>(cooked_mesh.vertex_buffer.data.data(),
                cooked_mesh.vertex_buffer.data.size()));
          }

          cooked_mesh.index_buffer.data
            = ToByteVector(std::span(lod.indices.data(), lod.indices.size()));
          cooked_mesh.index_buffer.alignment = alignof(uint32_t);
          cooked_mesh.index_buffer.usage_flags = ib_usage_flags;
          cooked_mesh.index_buffer.element_stride = 0;
          cooked_mesh.index_buffer.element_format
            = static_cast<uint8_t>(Format::kR32UInt);
          if (with_content_hashing && !item.stop_token.stop_requested()) {
            cooked_mesh.index_buffer.content_hash = util::ComputeContentHash(
              std::span<const std::byte>(cooked_mesh.index_buffer.data.data(),
                cooked_mesh.index_buffer.data.size()));
          }

          if (lod.mesh_type == data::MeshType::kSkinned) {
            const auto joint_usage_flags
              = static_cast<uint32_t>(
                  data::BufferResource::UsageFlags::kStorageBuffer)
              | kDefaultStaticUsageFlags;

            CookedBufferPayload joint_indices_payload;
            joint_indices_payload.data = ToByteVector(
              std::span(lod.joint_indices.data(), lod.joint_indices.size()));
            joint_indices_payload.alignment = 16;
            joint_indices_payload.usage_flags = joint_usage_flags;
            joint_indices_payload.element_stride = 0;
            joint_indices_payload.element_format
              = static_cast<uint8_t>(Format::kRGBA32UInt);
            if (with_content_hashing && !item.stop_token.stop_requested()) {
              joint_indices_payload.content_hash = util::ComputeContentHash(
                std::span<const std::byte>(joint_indices_payload.data.data(),
                  joint_indices_payload.data.size()));
            }

            CookedBufferPayload joint_weights_payload;
            joint_weights_payload.data = ToByteVector(
              std::span(lod.joint_weights.data(), lod.joint_weights.size()));
            joint_weights_payload.alignment = 16;
            joint_weights_payload.usage_flags = joint_usage_flags;
            joint_weights_payload.element_stride = 0;
            joint_weights_payload.element_format
              = static_cast<uint8_t>(Format::kRGBA32Float);
            if (with_content_hashing && !item.stop_token.stop_requested()) {
              joint_weights_payload.content_hash = util::ComputeContentHash(
                std::span<const std::byte>(joint_weights_payload.data.data(),
                  joint_weights_payload.data.size()));
            }

            CookedBufferPayload inverse_bind_payload;
            inverse_bind_payload.data
              = ToByteVector(std::span(lod.inverse_bind_matrices.data(),
                lod.inverse_bind_matrices.size()));
            inverse_bind_payload.alignment = 16;
            inverse_bind_payload.usage_flags = joint_usage_flags;
            inverse_bind_payload.element_stride = sizeof(glm::mat4);
            inverse_bind_payload.element_format
              = static_cast<uint8_t>(Format::kUnknown);
            if (with_content_hashing && !item.stop_token.stop_requested()) {
              inverse_bind_payload.content_hash = util::ComputeContentHash(
                std::span<const std::byte>(inverse_bind_payload.data.data(),
                  inverse_bind_payload.data.size()));
            }

            CookedBufferPayload joint_remap_payload;
            joint_remap_payload.data = ToByteVector(
              std::span(lod.joint_remap.data(), lod.joint_remap.size()));
            joint_remap_payload.alignment = alignof(uint32_t);
            joint_remap_payload.usage_flags = joint_usage_flags;
            joint_remap_payload.element_stride = 0;
            joint_remap_payload.element_format
              = static_cast<uint8_t>(Format::kR32UInt);
            if (with_content_hashing && !item.stop_token.stop_requested()) {
              joint_remap_payload.content_hash = util::ComputeContentHash(
                std::span<const std::byte>(joint_remap_payload.data.data(),
                  joint_remap_payload.data.size()));
            }

            cooked_mesh.auxiliary_buffers.push_back(
              std::move(joint_indices_payload));
            cooked_mesh.auxiliary_buffers.push_back(
              std::move(joint_weights_payload));
            cooked_mesh.auxiliary_buffers.push_back(
              std::move(inverse_bind_payload));
            cooked_mesh.auxiliary_buffers.push_back(
              std::move(joint_remap_payload));
          }

          cooked_mesh.bounds = lod.bounds;
          cooked_payload.lods.push_back(std::move(cooked_mesh));
        }

        out.cooked = std::move(cooked_payload);
        out.success = true;
        return out;
      });

    if (build_outcome.canceled) {
      WorkResult canceled {
        .source_id = std::move(build_outcome.source_id),
        .source_key = build_outcome.source_key,
        .cooked = std::nullopt,
        .diagnostics = {},
        .success = false,
      };
      co_await output_channel_.Send(std::move(canceled));
      continue;
    }

    WorkResult result {
      .source_id = std::move(build_outcome.source_id),
      .source_key = build_outcome.source_key,
      .cooked = std::move(build_outcome.cooked),
      .diagnostics = std::move(build_outcome.diagnostics),
      .success = build_outcome.success,
    };
    co_await output_channel_.Send(std::move(result));
  }

  co_return;
}

auto GeometryPipeline::ReportCancelled(WorkItem item) -> co::Co<>
{
  WorkResult canceled {
    .source_id = std::move(item.source_id),
    .source_key = item.source_key,
    .cooked = std::nullopt,
    .diagnostics = {},
    .success = false,
  };
  co_await output_channel_.Send(std::move(canceled));
}

} // namespace oxygen::content::import
