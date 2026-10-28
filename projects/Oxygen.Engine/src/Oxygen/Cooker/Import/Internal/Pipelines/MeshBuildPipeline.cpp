//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <map>
#include <numeric>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <fmt/format.h>
#include <glm/ext/matrix_float3x3.hpp>
#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/ext/vector_float4.hpp>
#include <glm/ext/vector_uint4.hpp>
#include <glm/geometric.hpp>
#include <glm/matrix.hpp>
#include <glm/trigonometric.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/Span.h>
#include <Oxygen/Cooker/Import/BufferImportTypes.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportOptions.h>
#include <Oxygen/Cooker/Import/ImportReport.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/Import/Internal/GeometryDefaults.h>
#include <Oxygen/Cooker/Import/Internal/ImportPipeline.h>
#include <Oxygen/Cooker/Import/Internal/LodGeneration.h>
#include <Oxygen/Cooker/Import/Internal/MeshOptimization.h>
#include <Oxygen/Cooker/Import/Internal/MeshTransformBake.h>
#include <Oxygen/Cooker/Import/Internal/Pipelines/GeometryPipeline_tangents.h>
#include <Oxygen/Cooker/Import/Internal/Pipelines/MeshBuildPipeline.h>
#include <Oxygen/Cooker/Import/Internal/SurfaceAllocation.h>
#include <Oxygen/Cooker/Import/Internal/Utils/ContentHashUtils.h>
#include <Oxygen/Cooker/Import/Internal/Utils/VirtualPathResolution.h>
#include <Oxygen/Cooker/Import/SurfaceProvenance.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Core/Types/DescriptorExtensions.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/BufferResource.h>
#include <Oxygen/Data/DescriptorStrings.h>
#include <Oxygen/Data/MeshType.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/StableId.h>
#include <Oxygen/Data/SurfaceId.h>
#include <Oxygen/Data/Vertex.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Nursery.h>
#include <Oxygen/OxCo/ThreadPool.h>
#include <Oxygen/Serio/MemoryStream.h>
#include <Oxygen/Serio/Writer.h>

namespace oxygen::content::import {

namespace {

  using WorkItem = MeshBuildPipeline::WorkItem;

  constexpr uint32_t kGeomAttr_Normal = 1U << 0U;
  constexpr uint32_t kGeomAttr_Tangent = 1U << 1U;
  constexpr uint32_t kGeomAttr_Bitangent = 1U << 2U;
  constexpr uint32_t kGeomAttr_Texcoord0 = 1U << 3U;
  constexpr uint32_t kGeomAttr_Color0 = 1U << 4U;
  constexpr uint32_t kGeomAttr_JointWeights = 1U << 5U;
  constexpr uint32_t kGeomAttr_JointIndices = 1U << 6U;

  constexpr uint32_t kDefaultStaticUsageFlags
    = static_cast<uint32_t>(data::BufferResource::UsageFlags::kStatic);

  struct SubmeshBucket {
    uint32_t scene_material_index = 0;
    uint32_t source_slot = 0;
    data::AssetKey material_key;
    std::vector<uint32_t> indices;
    //! Triangles per mesh view, in index order; empty means one view.
    std::vector<uint32_t> view_triangle_counts;
  };

  struct LodBuildData {
    std::string lod_name;
    data::MeshType mesh_type = data::MeshType::kStandard;
    std::vector<data::Vertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<data::pak::geometry::SubMeshDesc> submeshes;
    std::vector<std::string> submesh_names;
    std::vector<uint32_t> submesh_surfaces;
    std::vector<data::pak::geometry::MeshViewDesc> views;
    std::vector<glm::uvec4> joint_indices;
    std::vector<glm::vec4> joint_weights;
    std::vector<glm::mat4> inverse_bind_matrices;
    std::vector<uint32_t> joint_remap;
    uint16_t joint_count = 0;
    uint16_t influences_per_vertex = 0;
    Bounds3 bounds {};
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
    bounds.min.at(0) = (std::min)(bounds.min.at(0), p.x);
    bounds.min.at(1) = (std::min)(bounds.min.at(1), p.y);
    bounds.min.at(2) = (std::min)(bounds.min.at(2), p.z);
    bounds.max.at(0) = (std::max)(bounds.max.at(0), p.x);
    bounds.max.at(1) = (std::max)(bounds.max.at(1), p.y);
    bounds.max.at(2) = (std::max)(bounds.max.at(2), p.z);
  }

  //! Shortest accumulated normal that still normalizes reliably.
  constexpr auto kMinNormalLength = 1e-8F;
  //! Shortest tangent-frame vector treated as valid.
  constexpr auto kMinTangentLength = 1e-6F;

  //! Spreads the low 10 bits of `value` to every third bit.
  [[nodiscard]] constexpr auto SpreadMortonBits(uint32_t value) -> uint32_t
  {
    // Standard 3D Morton bit-interleave masks, widest shift first.
    constexpr auto kLow10Bits = 0x3FFU;
    constexpr auto kShift16Mask = 0x030000FFU;
    constexpr auto kShift8Mask = 0x0300F00FU;
    constexpr auto kShift4Mask = 0x030C30C3U;
    constexpr auto kShift2Mask = 0x09249249U;
    value &= kLow10Bits;
    value = (value | (value << 16U)) & kShift16Mask;
    value = (value | (value << 8U)) & kShift8Mask;
    value = (value | (value << 4U)) & kShift4Mask;
    value = (value | (value << 2U)) & kShift2Mask;
    return value;
  }

  //! 30-bit Morton code of `point`, quantized to 10 bits per axis in `bounds`.
  [[nodiscard]] auto MortonCode(const glm::vec3& point, const Bounds3& bounds)
    -> uint32_t
  {
    constexpr auto kAxisCells = 1024.0F;
    constexpr auto kLastCell = 1023U;
    const auto coordinates = std::array { point.x, point.y, point.z };
    auto code = 0U;
    for (uint32_t axis = 0U; axis < 3U; ++axis) {
      const auto min = bounds.min.at(axis);
      const auto extent = bounds.max.at(axis) - min;
      const auto t
        = extent > 0.0F ? (coordinates.at(axis) - min) / extent : 0.0F;
      const auto cell = static_cast<uint32_t>(
        std::clamp(t * kAxisCells, 0.0F, static_cast<float>(kLastCell)));
      code |= SpreadMortonBits(cell) << axis;
    }
    return code;
  }

  [[nodiscard]] auto MakeEmptyBounds() -> Bounds3
  {
    return Bounds3 {
      .min = { std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max(), },
      .max = { std::numeric_limits<float>::lowest(),
        std::numeric_limits<float>::lowest(),
        std::numeric_limits<float>::lowest(), },
    };
  }

  [[nodiscard]] auto HasAnyError(
    const std::vector<ImportDiagnostic>& diagnostics) -> bool
  {
    return std::ranges::any_of(
      diagnostics, [](const ImportDiagnostic& diag) -> bool {
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
      const auto existing = std::ranges::find_if(
        buckets, [&](const SubmeshBucket& bucket) -> bool {
          return bucket.source_slot == range.source_slot;
        });
      if (existing != buckets.end()) {
        continue;
      }

      const auto material_key = (range.surface < material_keys.size())
        ? material_keys.at(range.surface)
        : default_material_key;

      buckets.push_back(SubmeshBucket {
        .scene_material_index = range.surface,
        .source_slot = range.source_slot,
        .material_key = material_key,
        .indices = {},
        .view_triangle_counts = {},
      });
    }

    std::ranges::sort(
      buckets, [](const SubmeshBucket& a, const SubmeshBucket& b) -> bool {
        return a.source_slot < b.source_slot;
      });

    return buckets;
  }

  //! A position's bits, with negative zero as zero, so equal positions sort
  //! together.
  [[nodiscard]] auto PositionBits(const glm::vec3& position)
    -> std::array<uint32_t, 3>
  {
    const auto bits = [](const float value) -> uint32_t {
      return std::bit_cast<uint32_t>(value == 0.0F ? 0.0F : value);
    };
    return { bits(position.x), bits(position.y), bits(position.z) };
  }

  //! Generates the normals of `vertices`, one vertex per triangle corner.
  /*!
   The triangles around a position fall into smoothing groups, as DCC tools
   auto-smooth: two of them are in one group when they share an edge there and
   their face normals are within `smoothing_angle_deg` of each other, and a
   group takes in every triangle it reaches that way. Every corner of a group
   gets the group's one normal, the area-weighted sum of its triangles' face
   normals, summed in one order, so the corners are equal and weld into one
   vertex; a sharper edge splits the groups and stays hard, and an angle of 0
   makes every triangle its own group, flat. A corner of a degenerate triangle
   takes the sum of every triangle at its position.
  */
  auto ComputeNormalsFromTriangles(std::vector<data::Vertex>& vertices,
    const std::span<const SubmeshBucket> buckets,
    const float smoothing_angle_deg) -> void
  {
    if (vertices.empty()) {
      return;
    }

    using PositionKey = std::array<uint32_t, 3>;

    // Each corner's face normal, weighted by twice the triangle's area, and
    // the positions of its triangle's two other corners.
    std::vector faces(vertices.size(), glm::vec3(0.0F));
    auto others = std::vector<std::array<PositionKey, 2>>(vertices.size());
    for (const auto& bucket : buckets) {
      const auto tri_count = bucket.indices.size() / 3;
      for (size_t tri = 0; tri < tri_count; ++tri) {
        const auto i0 = bucket.indices.at((tri * 3) + 0);
        const auto i1 = bucket.indices.at((tri * 3) + 1);
        const auto i2 = bucket.indices.at((tri * 3) + 2);
        if (i0 >= vertices.size() || i1 >= vertices.size()
          || i2 >= vertices.size()) {
          continue;
        }

        const auto& p0 = vertices.at(i0).position;
        const auto& p1 = vertices.at(i1).position;
        const auto& p2 = vertices.at(i2).position;
        const auto n = glm::cross(p1 - p0, p2 - p0);
        faces.at(i0) = n;
        faces.at(i1) = n;
        faces.at(i2) = n;
        const auto k0 = PositionBits(p0);
        const auto k1 = PositionBits(p1);
        const auto k2 = PositionBits(p2);
        others.at(i0) = { k1, k2 };
        others.at(i1) = { k2, k0 };
        others.at(i2) = { k0, k1 };
      }
    }

    auto units = std::vector<glm::vec3>(faces.size());
    std::ranges::transform(
      faces, units.begin(), [](const glm::vec3& face) -> glm::vec3 {
        const auto len = glm::length(face);
        return len > kMinNormalLength ? face / len : glm::vec3(0.0F);
      });
    const bool smooths = smoothing_angle_deg > 0.0F;
    const auto min_cos = std::cos(glm::radians(smoothing_angle_deg));
    const auto degenerate = [&units](const uint32_t corner) -> bool {
      return units.at(corner) == glm::vec3(0.0F);
    };
    const auto share_edge
      = [&others](const uint32_t a, const uint32_t b) -> bool {
      const auto& first = others.at(a);
      const auto& second = others.at(b);
      return std::ranges::any_of(
        first, [&second](const PositionKey& key) -> bool {
          return std::ranges::find(second, key) != second.end();
        });
    };
    const auto normal_of = [](const glm::vec3& sum) -> glm::vec3 {
      const auto len = glm::length(sum);
      return len > kMinNormalLength ? sum / len : glm::vec3(0.0F, 1.0F, 0.0F);
    };

    // Corners sorted by position, so the corners of one position are a run.
    using Keyed = std::pair<PositionKey, uint32_t>;
    auto corners = std::vector<Keyed>(vertices.size());
    for (uint32_t corner = 0; corner < corners.size(); ++corner) {
      corners.at(corner)
        = { PositionBits(vertices.at(corner).position), corner };
    }
    std::ranges::sort(corners);

    auto groups = std::vector<std::size_t> {};
    auto sums = std::vector<glm::vec3> {};
    for (size_t first = 0; first < corners.size();) {
      auto last = first + 1;
      while (last < corners.size()
        && corners.at(last).first == corners.at(first).first) {
        ++last;
      }
      const auto shared = std::span(corners).subspan(first, last - first);
      first = last;

      // Union-find over the run: each entry starts as its own group.
      groups.resize(shared.size());
      std::ranges::iota(groups, std::size_t { 0 });
      const auto root = [&groups](std::size_t entry) -> std::size_t {
        while (groups.at(entry) != entry) {
          groups.at(entry) = groups.at(groups.at(entry));
          entry = groups.at(entry);
        }
        return entry;
      };
      for (size_t a = 0; smooths && a < shared.size(); ++a) {
        const auto corner_a = shared[a].second;
        if (degenerate(corner_a)) {
          continue;
        }
        for (size_t b = a + 1; b < shared.size(); ++b) {
          const auto corner_b = shared[b].second;
          if (!degenerate(corner_b) && share_edge(corner_a, corner_b)
            && glm::dot(units.at(corner_a), units.at(corner_b)) >= min_cos) {
            groups.at(root(b)) = root(a);
          }
        }
      }

      // Each group's sum, and the whole position's, in run order.
      sums.assign(shared.size(), glm::vec3(0.0F));
      auto all = glm::vec3(0.0F);
      for (size_t entry = 0; entry < shared.size(); ++entry) {
        const auto& face = faces.at(shared[entry].second);
        sums.at(root(entry)) += face;
        all += face;
      }
      for (size_t entry = 0; entry < shared.size(); ++entry) {
        const auto corner = shared[entry].second;
        vertices.at(corner).normal
          = normal_of(degenerate(corner) ? all : sums.at(root(entry)));
      }
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
          || glm::length(n) < kMinTangentLength) {
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

  //! The surface defaults `defaults` sets on the surfaces `declared` lists;
  //! each other surface it names is reported as missing.
  [[nodiscard]] auto AuthoredSurfaces(const GeometryImportDefaults& defaults,
    std::span<const data::SurfaceId> declared,
    std::vector<ImportDiagnostic>& diagnostics, std::string_view source_id,
    std::string_view mesh_name)
    -> std::vector<internal::AuthoredSurfaceDefaults>
  {
    auto authored = std::vector<internal::AuthoredSurfaceDefaults> {};
    for (const auto& [surface_id, surface] : defaults.surfaces) {
      if (std::ranges::find(declared, surface_id) == declared.end()) {
        diagnostics.push_back(
          MakeWarningDiagnostic("import.setting_target_missing",
            "Geometry defaults name a surface the geometry no longer has; the "
            "setting is kept and not applied",
            source_id,
            std::string(mesh_name) + "/" + data::to_string(surface_id)));
        continue;
      }
      authored.push_back({
        .surface_id = surface_id,
        .visible = surface.visible,
        .casts_shadows = surface.casts_shadows,
      });
    }
    return authored;
  }

  [[nodiscard]] auto BuildDescriptorBytes(const std::string_view mesh_name,
    const std::vector<LodBuildData>& lods, const Bounds3& bounds,
    const GeometryImportDefaults* defaults,
    const data::pak::geometry::LodPolicyRecord& imported_policy,
    const uint32_t attr_mask,
    std::vector<MeshBuildPipeline::SurfacePatchOffset>& material_patch_offsets,
    std::vector<ImportDiagnostic>& diagnostics, std::string_view source_id)
    -> std::vector<std::byte>
  {
    using data::pak::geometry::GeometryAssetDesc;
    using data::pak::geometry::MeshDesc;
    using data::pak::geometry::MeshViewDesc;
    using data::pak::geometry::SubMeshDesc;

    GeometryAssetDesc asset_desc {};
    asset_desc.header.asset_type
      = static_cast<uint8_t>(data::AssetType::kGeometry);
    asset_desc.header.version = data::pak::geometry::kGeometryAssetVersion;
    asset_desc.header.variant_flags = attr_mask;
    auto strings = data::DescriptorStringsBuilder {};
    asset_desc.header.name_offset = strings.Add(mesh_name);
    asset_desc.lod_count = static_cast<uint32_t>(lods.size());
    std::ranges::copy(bounds.min, std::begin(asset_desc.bounding_box_min));
    std::ranges::copy(bounds.max, std::begin(asset_desc.bounding_box_max));

    // The import settings' geometry defaults, when they name this geometry;
    // otherwise the surfaces take the scene schema's, and several LODs the
    // import's policy that switches them.
    auto declared = std::vector<data::SurfaceId> {};
    for (const auto& lod : lods) {
      for (const auto& submesh : lod.submeshes) {
        if (std::ranges::find(declared, submesh.surface_id) == declared.end()) {
          declared.push_back(submesh.surface_id);
        }
      }
    }
    const auto authored = defaults != nullptr
      ? AuthoredSurfaces(*defaults, declared, diagnostics, source_id, mesh_name)
      : std::vector<internal::AuthoredSurfaceDefaults> {};
    const auto surface_table = internal::MakeSurfaceTable(declared, authored);
    asset_desc.surface_count = static_cast<uint32_t>(surface_table.size());
    asset_desc.default_lod_policy
      = defaults != nullptr && defaults->lod_policy.has_value()
      ? *defaults->lod_policy
      : imported_policy;

    serio::MemoryStream stream;
    serio::Writer writer(stream);
    const auto pack = writer.ScopedAlignment(1);

    material_patch_offsets.clear();
    size_t total_submeshes = 0;
    for (const auto& lod : lods) {
      total_submeshes += lod.submeshes.size();
    }
    material_patch_offsets.reserve(total_submeshes);

    const auto asset_result = writer.WriteBlob(
      std::as_bytes(std::span<const GeometryAssetDesc, 1>(&asset_desc, 1)));
    if (!asset_result.has_value()) {
      diagnostics.push_back(MakeErrorDiagnostic("mesh.serialize_failed",
        "Failed to serialize geometry asset descriptor", source_id, mesh_name));
      return {};
    }

    for (const auto& lod : lods) {
      MeshDesc mesh_desc {};
      const std::string_view name_view = [&] -> std::string_view {
        if (lods.size() <= 1U) {
          return mesh_name;
        }
        return lod.lod_name;
      }();
      mesh_desc.name_offset = strings.Add(name_view);
      mesh_desc.mesh_type = static_cast<uint8_t>(lod.mesh_type);
      mesh_desc.submesh_count = static_cast<uint32_t>(lod.submeshes.size());
      mesh_desc.mesh_view_count = static_cast<uint32_t>(lod.views.size());

      if (lod.mesh_type == data::MeshType::kSkinned) {
        mesh_desc.info.skinned.vertex_buffer = data::kNoResourceReference;
        mesh_desc.info.skinned.index_buffer = data::kNoResourceReference;
        mesh_desc.info.skinned.joint_index_buffer = data::kNoResourceReference;
        mesh_desc.info.skinned.joint_weight_buffer = data::kNoResourceReference;
        mesh_desc.info.skinned.inverse_bind_buffer = data::kNoResourceReference;
        mesh_desc.info.skinned.joint_remap_buffer = data::kNoResourceReference;
        mesh_desc.info.skinned.joint_count = lod.joint_count;
        mesh_desc.info.skinned.influences_per_vertex
          = lod.influences_per_vertex;
        mesh_desc.info.skinned.flags = 0;
        std::ranges::copy(
          lod.bounds.min, std::begin(mesh_desc.info.skinned.bounding_box_min));
        std::ranges::copy(
          lod.bounds.max, std::begin(mesh_desc.info.skinned.bounding_box_max));
      } else {
        mesh_desc.info.standard.vertex_buffer = data::kNoResourceReference;
        mesh_desc.info.standard.index_buffer = data::kNoResourceReference;
        std::ranges::copy(
          lod.bounds.min, std::begin(mesh_desc.info.standard.bounding_box_min));
        std::ranges::copy(
          lod.bounds.max, std::begin(mesh_desc.info.standard.bounding_box_max));
      }

      const auto mesh_result = writer.WriteBlob(
        std::as_bytes(std::span<const MeshDesc, 1>(&mesh_desc, 1)));
      if (!mesh_result.has_value()) {
        diagnostics.push_back(MakeErrorDiagnostic("mesh.serialize_failed",
          "Failed to serialize mesh descriptor", source_id, mesh_name));
        return {};
      }

      if (lod.submesh_surfaces.size() != lod.submeshes.size()
        || lod.submesh_names.size() != lod.submeshes.size()) {
        diagnostics.push_back(MakeErrorDiagnostic("mesh.finalize_failed",
          "Submesh surface or name list size does not match submesh count",
          source_id, mesh_name));
        return {};
      }

      size_t view_cursor = 0;
      for (size_t i = 0; i < lod.submeshes.size(); ++i) {
        auto sm = lod.submeshes.at(i);
        sm.name_offset = strings.Add(lod.submesh_names.at(i));
        const auto submesh_views
          = std::span(lod.views).subspan(view_cursor, sm.mesh_view_count);
        view_cursor += sm.mesh_view_count;

        const auto pos = writer.Position();
        if (!pos) {
          diagnostics.push_back(MakeErrorDiagnostic("mesh.serialize_failed",
            "Failed to query submesh descriptor offset", source_id, mesh_name));
          return {};
        }

        const auto offset
          = pos.value() + offsetof(SubMeshDesc, material_asset_key);
        if (offset
          > std::numeric_limits<data::pak::core::DataBlobSizeT>::max()) {
          diagnostics.push_back(MakeErrorDiagnostic("mesh.serialize_failed",
            "Submesh material patch offset exceeds supported range", source_id,
            mesh_name));
          return {};
        }
        material_patch_offsets.push_back(MeshBuildPipeline::SurfacePatchOffset {
          .surface = lod.submesh_surfaces.at(i),
          .material_key_offset
          = static_cast<data::pak::core::DataBlobSizeT>(offset),
        });

        const auto sm_result = writer.WriteBlob(
          std::as_bytes(std::span<const SubMeshDesc, 1>(&sm, 1)));
        if (!sm_result.has_value()) {
          diagnostics.push_back(MakeErrorDiagnostic("mesh.serialize_failed",
            "Failed to serialize submesh descriptor", source_id, mesh_name));
          return {};
        }

        const auto view_result = writer.WriteBlob(std::as_bytes(submesh_views));
        if (!view_result.has_value()) {
          diagnostics.push_back(MakeErrorDiagnostic("mesh.serialize_failed",
            "Failed to serialize mesh view descriptor", source_id, mesh_name));
          return {};
        }
      }
    }

    if (!writer.WriteBlob(std::as_bytes(std::span(surface_table)))) {
      diagnostics.push_back(MakeErrorDiagnostic("mesh.serialize_failed",
        "Failed to serialize the surface table", source_id, mesh_name));
      return {};
    }

    const auto data = stream.Data();
    auto bytes = std::vector<std::byte> { data.begin(), data.end() };
    data::AppendDescriptorStrings(bytes, strings);
    return bytes;
  }

  //! The import settings' defaults for `item`'s geometry, keyed by its anchor.
  [[nodiscard]] auto GeometryDefaultsOf(const MeshBuildPipeline::WorkItem& item)
    -> const GeometryImportDefaults*
  {
    const auto& all = item.request.options.geometry_defaults;
    const auto it = all.find(item.storage_mesh_name);
    return it != all.end() ? &it->second : nullptr;
  }

  [[nodiscard]] auto ResolveGeometryKey(const ImportRequest& request,
    std::string_view virtual_path) -> data::AssetKey
  {
    static_cast<void>(request);
    return oxygen::data::AssetKey::FromVirtualPath(virtual_path);
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
    std::vector<glm::uvec4>& joint_indices,
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

    auto find_bucket = [&](const uint32_t source_slot) -> SubmeshBucket* {
      const auto it = std::ranges::find_if(
        buckets, [&](const SubmeshBucket& bucket) -> bool {
          return bucket.source_slot == source_slot;
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

      auto* bucket = find_bucket(range.source_slot);
      if (bucket == nullptr) {
        diagnostics.push_back(MakeWarningDiagnostic("mesh.invalid_range",
          "Triangle range references unknown surface; skipping range",
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

      auto emit_vertex = [&](const uint32_t source_index) -> void {
        data::Vertex vertex {};
        PopulateVertexDefaults(vertex);

        vertex.position = oxygen::base::CheckedAt(positions, source_index);

        if (preserve_authored_normals && has_normals) {
          vertex.normal
            = glm::normalize(oxygen::base::CheckedAt(normals, source_index));
        }

        if (has_uvs) {
          vertex.texcoord = oxygen::base::CheckedAt(texcoords, source_index);
        }

        if (preserve_authored_tangents && has_tangents) {
          vertex.tangent = oxygen::base::CheckedAt(tangents, source_index);
        }

        if (preserve_authored_tangents && has_bitangents) {
          vertex.bitangent = oxygen::base::CheckedAt(bitangents, source_index);
        }

        if (has_colors) {
          vertex.color = oxygen::base::CheckedAt(colors, source_index);
        }

        vertices.push_back(vertex);
        bucket->indices.push_back(next_index);
        ++next_index;

        if ((mesh.mesh_type == data::MeshType::kSkinned)
          && (has_joints && has_weights)) {
          joint_indices.push_back(
            oxygen::base::CheckedAt(joint_ids, source_index));
          joint_weights.push_back(
            oxygen::base::CheckedAt(joint_wts, source_index));
        }
      };

      size_t skipped_triangles = 0;
      for (uint32_t i = 0; i < range_count; i += 3) {
        const auto idx0
          = oxygen::base::CheckedAt(mesh.indices, range.first_index + i + 0);
        const auto idx1
          = oxygen::base::CheckedAt(mesh.indices, range.first_index + i + 1);
        const auto idx2
          = oxygen::base::CheckedAt(mesh.indices, range.first_index + i + 2);
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

    if ((mesh.mesh_type == data::MeshType::kSkinned)
      && (!has_joints || !has_weights)) {
      diagnostics.push_back(MakeErrorDiagnostic("mesh.missing_skinning",
        "Skinned mesh requires joint indices and weights", source_id,
        object_path));
      return;
    }
  }

  //! Orders a static submesh's triangles into its mesh views.
  /*!
   A submesh above `kMaxMeshViewTriangles` has its triangles stably ordered by
   the Morton code of their centroids within its bounds, then cut into runs of
   at most `kMaxMeshViewTriangles`. Returns each view's triangle count, in
   index order; a smaller submesh is one view.
  */
  [[nodiscard]] auto OrderIntoViewRuns(
    const std::span<const data::Vertex> vertices,
    std::vector<uint32_t>& indices) -> std::vector<uint32_t>
  {
    constexpr auto kMaxTriangles = MeshBuildPipeline::kMaxMeshViewTriangles;
    const auto triangle_count = static_cast<uint32_t>(indices.size() / 3U);
    if (triangle_count <= kMaxTriangles) {
      return { triangle_count };
    }

    auto bounds = MakeEmptyBounds();
    for (const auto vi : indices) {
      ExpandBounds(bounds, base::CheckedAt(vertices, vi).position);
    }
    auto order = std::vector<std::pair<uint32_t, uint32_t>> {};
    order.reserve(triangle_count);
    for (uint32_t triangle = 0U; triangle < triangle_count; ++triangle) {
      const auto first = static_cast<size_t>(triangle) * 3U;
      const auto centroid
        = (base::CheckedAt(vertices, indices.at(first)).position
            + base::CheckedAt(vertices, indices.at(first + 1U)).position
            + base::CheckedAt(vertices, indices.at(first + 2U)).position)
        / 3.0F;
      order.emplace_back(MortonCode(centroid, bounds), triangle);
    }
    std::ranges::stable_sort(order, {}, &std::pair<uint32_t, uint32_t>::first);

    auto ordered = std::vector<uint32_t> {};
    ordered.reserve(indices.size());
    for (const auto& [code, triangle] : order) {
      static_cast<void>(code);
      const auto first = static_cast<size_t>(triangle) * 3U;
      ordered.push_back(indices.at(first));
      ordered.push_back(indices.at(first + 1U));
      ordered.push_back(indices.at(first + 2U));
    }
    indices = std::move(ordered);

    auto runs = std::vector<uint32_t> {};
    for (auto remaining = triangle_count; remaining > 0U;) {
      const auto run = (std::min)(remaining, kMaxTriangles);
      runs.push_back(run);
      remaining -= run;
    }
    return runs;
  }

  auto BuildSubmeshDescriptors(const std::vector<data::Vertex>& vertices,
    const std::vector<SubmeshBucket>& buckets,
    const SurfaceGeometryProvenance& allocation,
    const std::map<uint32_t, std::string>& surface_names,
    std::vector<data::pak::geometry::SubMeshDesc>& submeshes,
    std::vector<std::string>& submesh_names,
    std::vector<uint32_t>& submesh_surfaces,
    std::vector<data::pak::geometry::MeshViewDesc>& views,
    std::vector<uint32_t>& merged_indices) -> Bounds3
  {
    using data::pak::geometry::MeshViewDesc;
    using data::pak::geometry::SubMeshDesc;

    Bounds3 mesh_bounds = MakeEmptyBounds();

    size_t total_indices = 0;
    for (const auto& bucket : buckets) {
      total_indices += bucket.indices.size();
    }

    merged_indices.clear();
    merged_indices.reserve(total_indices);

    submeshes.clear();
    submesh_names.clear();
    views.clear();
    submesh_surfaces.clear();
    submeshes.reserve(buckets.size());
    submesh_names.reserve(buckets.size());
    submesh_surfaces.reserve(buckets.size());
    views.reserve(buckets.size());

    MeshViewDesc::BufferIndexT index_cursor = 0;
    for (const auto& bucket : buckets) {
      Bounds3 submesh_bounds = MakeEmptyBounds();
      for (const auto vi : bucket.indices) {
        if (vi >= vertices.size()) {
          continue;
        }
        ExpandBounds(submesh_bounds, vertices.at(vi).position);
        ExpandBounds(mesh_bounds, vertices.at(vi).position);
      }
      auto view_triangle_counts = bucket.view_triangle_counts;
      if (view_triangle_counts.empty()) {
        view_triangle_counts.push_back(
          static_cast<uint32_t>(bucket.indices.size() / 3U));
      }

      const auto named = surface_names.find(bucket.source_slot);
      const auto name = named != surface_names.end()
        ? named->second
        : "Surface " + std::to_string(bucket.source_slot);

      SubMeshDesc submesh {};
      submesh.material_asset_key = bucket.material_key;
      submesh.surface_id = allocation.allocations.at(bucket.source_slot);
      submesh.mesh_view_count
        = static_cast<uint32_t>(view_triangle_counts.size());
      std::ranges::copy(
        submesh_bounds.min, std::begin(submesh.bounding_box_min));
      std::ranges::copy(
        submesh_bounds.max, std::begin(submesh.bounding_box_max));
      submeshes.push_back(submesh);
      submesh_names.push_back(name);
      submesh_surfaces.push_back(bucket.scene_material_index);

      const auto bucket_indices = std::span(bucket.indices);
      size_t run_offset = 0;
      for (const auto triangle_count : view_triangle_counts) {
        const auto run = bucket_indices.subspan(run_offset,
          (std::min)(static_cast<size_t>(triangle_count) * 3U,
            bucket_indices.size() - run_offset));
        run_offset += run.size();

        Bounds3 view_bounds = MakeEmptyBounds();
        uint32_t min_vertex = std::numeric_limits<uint32_t>::max();
        uint32_t max_vertex = 0;
        for (const auto vi : run) {
          if (vi >= vertices.size()) {
            continue;
          }
          ExpandBounds(view_bounds, vertices.at(vi).position);
          min_vertex = (std::min)(min_vertex, vi);
          max_vertex = (std::max)(max_vertex, vi);
        }

        const auto first_index = index_cursor;
        const auto index_count
          = static_cast<MeshViewDesc::BufferIndexT>(run.size());
        index_cursor += index_count;

        const auto vertex_count
          = (min_vertex <= max_vertex) ? (max_vertex - min_vertex + 1) : 0;
        if (vertex_count == 0) {
          min_vertex = 0;
          view_bounds = submesh_bounds;
        }

        auto view = MeshViewDesc {
          .first_index = first_index,
          .index_count = index_count,
          .first_vertex = min_vertex,
          .vertex_count = static_cast<MeshViewDesc::BufferIndexT>(vertex_count),
        };
        std::ranges::copy(view_bounds.min, std::begin(view.bounding_box_min));
        std::ranges::copy(view_bounds.max, std::begin(view.bounding_box_max));
        views.push_back(view);

        for (const auto vi : run) {
          const auto adjusted = (min_vertex > 0) ? (vi - min_vertex) : vi;
          merged_indices.push_back(adjusted);
        }
      }
    }

    return mesh_bounds;
  }

  //! Why a skinned mesh's skin data cannot bind it, or null when it can.
  [[nodiscard]] auto SkinDefect(const TriangleMesh& mesh)
    -> std::optional<std::pair<const char*, const char*>>
  {
    const auto vertex_count = mesh.streams.positions.size();
    if (mesh.streams.joint_indices.size() != vertex_count
      || mesh.streams.joint_weights.size() != vertex_count) {
      return std::pair { "mesh.missing_skinning",
        "Skinned mesh lacks joint indices or weights for its vertices" };
    }
    if (mesh.inverse_bind_matrices.empty()) {
      return std::pair { "mesh.missing_inverse_bind",
        "Skinned mesh missing inverse bind matrices" };
    }
    if (mesh.joint_remap.empty()) {
      return std::pair { "mesh.missing_joint_remap",
        "Skinned mesh missing joint remap data" };
    }
    uint32_t max_joint = 0;
    for (const auto& joints : mesh.streams.joint_indices) {
      max_joint
        = (std::max)({ max_joint, joints.x, joints.y, joints.z, joints.w });
    }
    if (max_joint >= mesh.inverse_bind_matrices.size()
      || max_joint >= mesh.joint_remap.size()) {
      return std::pair { "mesh.skinning_buffers_mismatch",
        "Skinned mesh joint data exceeds inverse bind/remap counts" };
    }
    return std::nullopt;
  }

  //! The attributes the import generates for every LOD's triangles.
  struct AttributeGeneration {
    //! Whether normals are generated, with smoothing groups by this angle.
    bool normals = false;
    float smoothing_angle_deg = 0.0F;
    //! Whether MikkTSpace tangents are generated.
    bool tangents = false;
    //! Whether the LOD has tangents, authored or generated, to repair.
    bool repairs_tangents = false;
  };

  //! A LOD's submeshes over the source's own vertices, before the import
  //! generates their normals and tangents.
  struct PreparedLod {
    std::string lod_name;
    data::MeshType mesh_type = data::MeshType::kStandard;
    //! The submeshes' materials and slots; their indices are filled where a
    //! LOD's corners or layout are built.
    std::vector<SubmeshBucket> buckets;
    //! Each bucket's triangles over the source's own vertices, welded, in
    //! bucket order: what simplification reads.
    std::vector<internal::IndexedSurface> sources;
    AttributeGeneration generation;
    std::vector<glm::mat4> inverse_bind_matrices;
    std::vector<uint32_t> joint_remap;
    //! The source's bounds, when nothing moved its vertices.
    std::optional<Bounds3> authored_bounds;
    uint32_t attr_mask = 0;
  };

  //! Reads a LOD's triangles over the source's own vertices and welds them.
  /*!
   The vertices carry what the source authors (positions, UVs, colours,
   joints, and the normals and tangents the policies keep), already baked. The
   normals and tangents the import generates are not part of them: they are
   generated for each LOD's own triangles by `CompleteLod`, so simplification
   reads the source's topology and every LOD shades by the same rule.
  */
  [[nodiscard]] auto PrepareLod(const MeshLod& lod_source, const WorkItem& item,
    std::vector<ImportDiagnostic>& diagnostics) -> std::optional<PreparedLod>
  {
    DLOG_F(1, "Prepare LOD");
    // Skin data that cannot bind the mesh is dropped: the LOD builds as static
    // geometry in its bind pose.
    auto mesh = lod_source.source;
    if (mesh.mesh_type == data::MeshType::kSkinned) {
      if (const auto defect = SkinDefect(mesh); defect.has_value()) {
        diagnostics.push_back(MakeWarningDiagnostic(defect->first,
          std::string(defect->second)
            + "; the mesh imports as static geometry in its bind pose",
          item.source_id, item.mesh_name));
        mesh.mesh_type = data::MeshType::kStandard;
        mesh.streams.joint_indices = {};
        mesh.streams.joint_weights = {};
        mesh.inverse_bind_matrices = {};
        mesh.joint_remap = {};
      }
    }
    PreparedLod lod;
    lod.lod_name = lod_source.lod_name;
    lod.mesh_type = mesh.mesh_type;

    if (lod.mesh_type == data::MeshType::kProcedural) {
      diagnostics.push_back(MakeErrorDiagnostic("mesh.procedural_unsupported",
        "Procedural meshes are not supported by MeshBuildPipeline",
        item.source_id, item.mesh_name));
      return std::nullopt;
    }

    if (lod.mesh_type != data::MeshType::kStandard
      && lod.mesh_type != data::MeshType::kSkinned) {
      diagnostics.push_back(MakeErrorDiagnostic("mesh.unsupported_type",
        "Mesh type is unsupported in MeshBuildPipeline", item.source_id,
        item.mesh_name));
      return std::nullopt;
    }

    if (mesh.indices.empty() || mesh.ranges.empty()) {
      diagnostics.push_back(MakeErrorDiagnostic("mesh.missing_buffers",
        "Mesh is missing triangle indices or ranges", item.source_id,
        item.mesh_name));
      return std::nullopt;
    }

    auto corners = std::vector<data::Vertex> {};
    auto corner_joints = std::vector<glm::uvec4> {};
    auto corner_weights = std::vector<glm::vec4> {};
    corners.reserve(mesh.indices.size());

    lod.buckets = BuildBucketsForRanges(
      mesh.ranges, item.material_keys, item.default_material_key);

    BuildVerticesFromRanges(mesh, item.request,
      item.request.options.normal_policy, mesh.ranges, lod.buckets, corners,
      corner_joints, corner_weights, diagnostics, item.source_id,
      item.mesh_name);

    if (HasAnyError(diagnostics)) {
      return std::nullopt;
    }

    if (lod.mesh_type == data::MeshType::kSkinned) {
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

    // kNone keeps the defaults; the mask will not include normals.
    lod.generation.normals
      = normal_policy == GeometryAttributePolicy::kAlwaysRecalculate
      || (!has_normals && should_generate_normals);
    lod.generation.smoothing_angle_deg
      = item.request.options.normals_smoothing_angle_deg;

    const bool final_has_normals
      = normal_policy != GeometryAttributePolicy::kNone
      && (has_normals || should_generate_normals);

    const bool needs_tangents
      = tangent_policy != GeometryAttributePolicy::kNone;
    const bool has_tangent_prereq = final_has_normals && has_uvs;

    bool tangents_emitted = false;

    if (needs_tangents && !has_tangent_prereq) {
      // Tangents serve tangent-space textures; a mesh none of whose materials
      // samples one loses nothing without them.
      if (item.has_tangent_space_textures) {
        diagnostics.push_back(
          MakeWarningDiagnostic("mesh.missing_tangent_prereq",
            "Tangents require positions, normals, and UVs", item.source_id,
            item.mesh_name));
      }
    } else if (needs_tangents) {
      const bool has_tangents
        = mesh.streams.tangents.size() == mesh.streams.positions.size()
        && mesh.streams.bitangents.size() == mesh.streams.positions.size();

      if (tangent_policy == GeometryAttributePolicy::kAlwaysRecalculate
        || (tangent_policy == GeometryAttributePolicy::kGenerateMissing
          && !has_tangents)) {
        lod.generation.tangents = true;
        tangents_emitted = true;
      } else if (has_tangents) {
        tangents_emitted = true;
      }
    }
    lod.generation.repairs_tangents = tangents_emitted;

    // A LOD's own transform takes its source into the geometry's space; the
    // item's then applies to the geometry as a whole.
    auto bake_transform = lod_source.bake_transform;
    if (item.bake_transform.has_value()) {
      bake_transform = bake_transform.has_value()
        ? *item.bake_transform * *bake_transform
        : *item.bake_transform;
    }
    if (bake_transform.has_value()) {
      if (lod.mesh_type != data::MeshType::kStandard
        || !internal::IsBakeTransformInvertible(*bake_transform)) {
        diagnostics.push_back(MakeErrorDiagnostic("mesh.invalid_transform_bake",
          "Transform baking requires static geometry and an invertible "
          "transform",
          item.source_id, item.mesh_name));
        return std::nullopt;
      }
      const auto& transform = *bake_transform;
      const auto linear = glm::mat3(transform);
      const auto normal_matrix = glm::transpose(glm::inverse(linear));
      const auto finite = [](const glm::vec3& value) -> bool {
        return std::isfinite(value.x) && std::isfinite(value.y)
          && std::isfinite(value.z);
      };
      const auto normalize_direction
        = [&finite](const glm::vec3& value, glm::vec3& normalized) -> bool {
        const auto largest = (std::max)({
          std::abs(value.x),
          std::abs(value.y),
          std::abs(value.z),
        });
        if (!finite(value) || largest == 0.0F) {
          return false;
        }
        normalized = glm::normalize(value / largest);
        return finite(normalized);
      };
      for (auto& vertex : corners) {
        vertex.position
          = glm::vec3(transform * glm::vec4(vertex.position, 1.0F));
        if (!finite(vertex.position)
          || !normalize_direction(normal_matrix * vertex.normal, vertex.normal)
          || !normalize_direction(linear * vertex.tangent, vertex.tangent)
          || !normalize_direction(
            linear * vertex.bitangent, vertex.bitangent)) {
          diagnostics.push_back(
            MakeErrorDiagnostic("mesh.invalid_transform_bake",
              "Transform baking produced an invalid vertex attribute",
              item.source_id, item.mesh_name));
          return std::nullopt;
        }
      }
      if (glm::determinant(linear) < 0.0F) {
        for (auto& bucket : lod.buckets) {
          for (size_t index = 0; index < bucket.indices.size(); index += 3) {
            std::swap(
              bucket.indices.at(index + 1), bucket.indices.at(index + 2));
          }
        }
      }
    }

    if (mesh.bounds.has_value() && !bake_transform.has_value()) {
      lod.authored_bounds = *mesh.bounds;
    }

    if (final_has_normals) {
      lod.attr_mask |= kGeomAttr_Normal;
    }
    if (tangents_emitted) {
      lod.attr_mask |= kGeomAttr_Tangent;
      lod.attr_mask |= kGeomAttr_Bitangent;
    }
    if (has_uvs) {
      lod.attr_mask |= kGeomAttr_Texcoord0;
    }
    if (mesh.streams.colors.size() == mesh.streams.positions.size()) {
      lod.attr_mask |= kGeomAttr_Color0;
    }
    if (lod.mesh_type == data::MeshType::kSkinned) {
      lod.attr_mask |= kGeomAttr_JointIndices;
      lod.attr_mask |= kGeomAttr_JointWeights;
    }

    const auto streams = internal::CornerStreams {
      .vertices = corners,
      .joint_indices = corner_joints,
      .joint_weights = corner_weights,
    };
    lod.sources.reserve(lod.buckets.size());
    for (auto& bucket : lod.buckets) {
      if (item.stop_token.stop_requested()) {
        return std::nullopt;
      }
      lod.sources.push_back(internal::WeldCorners(streams, bucket.indices));
      bucket.indices.clear();
    }
    return lod;
  }

  //! A LOD's surfaces, its normals and tangents generated: the triangles
  //! `surface_indices` lists over each of `prepared`'s sources.
  /*!
   The triangles are expanded to corners, given the normals and tangents the
   import generates, by the same rule for LOD 0 and every generated LOD, then
   welded: corners left equal share a vertex, a hard edge or a faceted surface
   keeps its corners apart. Returns nullopt when the import is canceled.
  */
  [[nodiscard]] auto CompleteLod(const PreparedLod& prepared,
    const std::span<const std::vector<uint32_t>> surface_indices,
    const std::stop_token& stop_token)
    -> std::optional<std::vector<internal::IndexedSurface>>
  {
    auto corners = std::vector<data::Vertex> {};
    auto corner_joints = std::vector<glm::uvec4> {};
    auto corner_weights = std::vector<glm::vec4> {};
    auto buckets = prepared.buckets;
    for (auto&& [index, bucket] : std::views::enumerate(buckets)) {
      const auto at = static_cast<size_t>(index);
      const auto& source = prepared.sources.at(at);
      const auto skinned = !source.joint_indices.empty();
      bucket.indices.clear();
      for (const auto vertex : base::CheckedAt(surface_indices, at)) {
        bucket.indices.push_back(static_cast<uint32_t>(corners.size()));
        corners.push_back(source.vertices.at(vertex));
        if (skinned) {
          corner_joints.push_back(source.joint_indices.at(vertex));
          corner_weights.push_back(source.joint_weights.at(vertex));
        }
      }
    }

    const auto& generation = prepared.generation;
    if (generation.normals) {
      ComputeNormalsFromTriangles(
        corners, buckets, generation.smoothing_angle_deg);
    }
    if (generation.tangents) {
      if (stop_token.stop_requested()) {
        return std::nullopt;
      }
      util::GenerateTangents(corners, std::span<const SubmeshBucket>(buckets));
    }
    if (generation.repairs_tangents) {
      FixInvalidTangents(corners);
    }

    const auto streams = internal::CornerStreams {
      .vertices = corners,
      .joint_indices = corner_joints,
      .joint_weights = corner_weights,
    };
    auto surfaces = std::vector<internal::IndexedSurface> {};
    surfaces.reserve(buckets.size());
    for (const auto& bucket : buckets) {
      if (stop_token.stop_requested()) {
        return std::nullopt;
      }
      surfaces.push_back(internal::WeldCorners(streams, bucket.indices));
    }
    return surfaces;
  }

  //! The triangles of each of `surfaces`, as they are.
  [[nodiscard]] auto IndicesOf(
    const std::span<const internal::IndexedSurface> surfaces)
    -> std::vector<std::vector<uint32_t>>
  {
    auto indices = std::vector<std::vector<uint32_t>> {};
    indices.reserve(surfaces.size());
    for (const auto& surface : surfaces) {
      indices.push_back(surface.indices);
    }
    return indices;
  }

  //! Lays a LOD's completed surfaces out into its buffers, views and
  //! submeshes.
  /*!
   `surfaces` holds one surface per bucket of `prepared`, from `CompleteLod`.
   Each mesh view takes its own contiguous vertex range, ordered for the GPU,
   with only the vertices its triangles use.
  */
  [[nodiscard]] auto LayOutLod(const PreparedLod& prepared,
    std::string lod_name,
    const std::span<const internal::IndexedSurface> surfaces,
    const std::optional<Bounds3>& authored_bounds, const WorkItem& item,
    const SurfaceGeometryProvenance& allocation,
    const uint64_t max_data_blob_bytes,
    std::vector<ImportDiagnostic>& diagnostics) -> std::optional<LodBuildData>
  {
    LodBuildData lod;
    lod.lod_name = std::move(lod_name);
    lod.mesh_type = prepared.mesh_type;
    lod.inverse_bind_matrices = prepared.inverse_bind_matrices;
    lod.joint_remap = prepared.joint_remap;

    auto buckets = prepared.buckets;
    for (auto&& [index, bucket] : std::views::enumerate(buckets)) {
      const auto at = static_cast<size_t>(index);
      const auto& surface = base::CheckedAt(surfaces, at);
      auto triangles = surface.indices;
      const auto runs = lod.mesh_type == data::MeshType::kStandard
        ? OrderIntoViewRuns(surface.vertices, triangles)
        : std::vector<uint32_t> {
            static_cast<uint32_t>(triangles.size() / 3U),
          };
      // Each view takes its own vertices first, so ordering it costs the view,
      // not the whole surface.
      const auto parts = internal::SplitTriangles(surface,
        internal::TriangleRuns {
          .indices = triangles,
          .triangle_counts = runs,
        });
      for (const auto& [part, run] : std::views::zip(parts, runs)) {
        const auto view = internal::OrderForGpu(part, part.indices);
        const auto first_vertex = lod.vertices.size();
        lod.vertices.insert(
          lod.vertices.end(), view.vertices.begin(), view.vertices.end());
        lod.joint_indices.insert(lod.joint_indices.end(),
          view.joint_indices.begin(), view.joint_indices.end());
        lod.joint_weights.insert(lod.joint_weights.end(),
          view.joint_weights.begin(), view.joint_weights.end());
        for (const auto vi : view.indices) {
          bucket.indices.push_back(static_cast<uint32_t>(first_vertex + vi));
        }
        bucket.view_triangle_counts.push_back(run);
      }
    }

    const auto computed_bounds = BuildSubmeshDescriptors(lod.vertices, buckets,
      allocation, item.surface_names, lod.submeshes, lod.submesh_names,
      lod.submesh_surfaces, lod.views, lod.indices);
    DLOG_F(INFO, "Mesh '{}' LOD '{}' submesh_count={} view_count={}",
      item.mesh_name, lod.lod_name, lod.submeshes.size(), lod.views.size());
    lod.bounds = authored_bounds.value_or(computed_bounds);

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

    const auto max_u32 = std::numeric_limits<uint32_t>::max();
    if (lod.vertices.size() > max_u32 || lod.indices.size() > max_u32
      || lod.submeshes.size() > max_u32 || lod.views.size() > max_u32) {
      diagnostics.push_back(MakeErrorDiagnostic("mesh.count_overflow",
        "Mesh vertex/index/submesh counts exceed uint32 limits", item.source_id,
        item.mesh_name));
      return std::nullopt;
    }

    const auto vb_bytes = lod.vertices.size() * sizeof(data::Vertex);
    const auto ib_bytes = lod.indices.size() * sizeof(uint32_t);
    if (vb_bytes > max_data_blob_bytes || ib_bytes > max_data_blob_bytes) {
      diagnostics.push_back(MakeErrorDiagnostic("mesh.buffer_too_large",
        "Mesh buffer exceeds maximum data blob size", item.source_id,
        item.mesh_name));
      return std::nullopt;
    }

    if (lod.mesh_type == data::MeshType::kSkinned) {
      const auto joint_bytes = lod.joint_indices.size() * sizeof(glm::uvec4);
      const auto weight_bytes = lod.joint_weights.size() * sizeof(glm::vec4);
      const auto inverse_bind_bytes
        = lod.inverse_bind_matrices.size() * sizeof(glm::mat4);
      const auto remap_bytes = lod.joint_remap.size() * sizeof(uint32_t);
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
        // SkinDefect proved every joint has an inverse bind and a remap.
        const uint32_t required_joint_count = max_joint + 1;
        const auto max_u16
          = static_cast<uint32_t>(std::numeric_limits<uint16_t>::max());
        lod.joint_count
          = static_cast<uint16_t>((std::min)(required_joint_count, max_u16));
        lod.influences_per_vertex = 4;
      }
    }

    return lod;
  }

  // Moving the payload strings and vectors can only fail on allocation.
  // NOLINTNEXTLINE(bugprone-exception-escape)
  struct GeometryBuildOutcome {
    std::string source_id;
    const void* source_key = nullptr;
    std::optional<MeshBuildPipeline::CookedGeometryPayload> cooked;
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

  [[nodiscard]] auto IsCanceled(const std::stop_token& stop_token,
    const co::ThreadPool::CancelToken& canceled) -> bool
  {
    return stop_token.stop_requested() || canceled;
  }

  auto HashPayloadIfNeeded(CookedBufferPayload& payload,
    const bool with_content_hashing, const std::stop_token& stop_token,
    const co::ThreadPool::CancelToken& canceled) -> void
  {
    if (!with_content_hashing || payload.content_hash != 0
      || IsCanceled(stop_token, canceled)) {
      return;
    }
    const std::span<const std::byte> bytes(
      payload.data.data(), payload.data.size());
    const auto hash = util::ComputeContentHash(bytes);
    if (hash != 0) {
      payload.content_hash = hash;
    }
  }

  [[nodiscard]] auto BuildCookedMeshPayload(const LodBuildData& lod,
    const bool with_content_hashing, const std::stop_token& stop_token,
    const co::ThreadPool::CancelToken& canceled)
    -> MeshBuildPipeline::CookedMeshPayload
  {
    auto cooked_mesh = MeshBuildPipeline::CookedMeshPayload {};

    const auto vb_usage_flags
      = static_cast<uint32_t>(data::BufferResource::UsageFlags::kVertexBuffer)
      | kDefaultStaticUsageFlags;
    const auto ib_usage_flags
      = static_cast<uint32_t>(data::BufferResource::UsageFlags::kIndexBuffer)
      | kDefaultStaticUsageFlags;

    cooked_mesh.vertex_buffer.data
      = ToByteVector(std::span(lod.vertices.data(), lod.vertices.size()));
    cooked_mesh.vertex_buffer.alignment = sizeof(data::Vertex);
    cooked_mesh.vertex_buffer.usage_flags = vb_usage_flags;
    cooked_mesh.vertex_buffer.element_stride = sizeof(data::Vertex);
    cooked_mesh.vertex_buffer.element_format
      = static_cast<uint8_t>(Format::kUnknown);

    cooked_mesh.index_buffer.data
      = ToByteVector(std::span(lod.indices.data(), lod.indices.size()));
    cooked_mesh.index_buffer.alignment = alignof(uint32_t);
    cooked_mesh.index_buffer.usage_flags = ib_usage_flags;
    cooked_mesh.index_buffer.element_stride = 0;
    cooked_mesh.index_buffer.element_format
      = static_cast<uint8_t>(Format::kR32UInt);

    if (lod.mesh_type == data::MeshType::kSkinned) {
      const auto joint_usage_flags
        = static_cast<uint32_t>(
            data::BufferResource::UsageFlags::kStorageBuffer)
        | kDefaultStaticUsageFlags;

      auto joint_indices_payload = CookedBufferPayload {};
      joint_indices_payload.data = ToByteVector(
        std::span(lod.joint_indices.data(), lod.joint_indices.size()));
      joint_indices_payload.alignment = 16;
      joint_indices_payload.usage_flags = joint_usage_flags;
      joint_indices_payload.element_stride = 0;
      joint_indices_payload.element_format
        = static_cast<uint8_t>(Format::kRGBA32UInt);

      auto joint_weights_payload = CookedBufferPayload {};
      joint_weights_payload.data = ToByteVector(
        std::span(lod.joint_weights.data(), lod.joint_weights.size()));
      joint_weights_payload.alignment = 16;
      joint_weights_payload.usage_flags = joint_usage_flags;
      joint_weights_payload.element_stride = 0;
      joint_weights_payload.element_format
        = static_cast<uint8_t>(Format::kRGBA32Float);

      auto inverse_bind_payload = CookedBufferPayload {};
      inverse_bind_payload.data = ToByteVector(std::span(
        lod.inverse_bind_matrices.data(), lod.inverse_bind_matrices.size()));
      inverse_bind_payload.alignment = 16;
      inverse_bind_payload.usage_flags = joint_usage_flags;
      inverse_bind_payload.element_stride = sizeof(glm::mat4);
      inverse_bind_payload.element_format
        = static_cast<uint8_t>(Format::kUnknown);

      auto joint_remap_payload = CookedBufferPayload {};
      joint_remap_payload.data = ToByteVector(
        std::span(lod.joint_remap.data(), lod.joint_remap.size()));
      joint_remap_payload.alignment = alignof(uint32_t);
      joint_remap_payload.usage_flags = joint_usage_flags;
      joint_remap_payload.element_stride = 0;
      joint_remap_payload.element_format
        = static_cast<uint8_t>(Format::kR32UInt);

      HashPayloadIfNeeded(
        joint_indices_payload, with_content_hashing, stop_token, canceled);
      HashPayloadIfNeeded(
        joint_weights_payload, with_content_hashing, stop_token, canceled);
      HashPayloadIfNeeded(
        inverse_bind_payload, with_content_hashing, stop_token, canceled);
      HashPayloadIfNeeded(
        joint_remap_payload, with_content_hashing, stop_token, canceled);

      cooked_mesh.auxiliary_buffers.push_back(std::move(joint_indices_payload));
      cooked_mesh.auxiliary_buffers.push_back(std::move(joint_weights_payload));
      cooked_mesh.auxiliary_buffers.push_back(std::move(inverse_bind_payload));
      cooked_mesh.auxiliary_buffers.push_back(std::move(joint_remap_payload));
    }

    HashPayloadIfNeeded(
      cooked_mesh.vertex_buffer, with_content_hashing, stop_token, canceled);
    HashPayloadIfNeeded(
      cooked_mesh.index_buffer, with_content_hashing, stop_token, canceled);

    cooked_mesh.bounds = lod.bounds;
    return cooked_mesh;
  }

  [[nodiscard]] auto BuildCookedGeometryPayload(const WorkItem& item,
    const std::vector<LodBuildData>& lods,
    const std::span<const float> generated_errors, const uint32_t attr_mask,
    const bool with_content_hashing,
    const co::ThreadPool::CancelToken& canceled,
    std::vector<ImportDiagnostic>& diagnostics)
    -> std::optional<MeshBuildPipeline::CookedGeometryPayload>
  {
    auto geom_bounds = MakeEmptyBounds();
    for (const auto& lod : lods) {
      ExpandBounds(geom_bounds,
        glm::vec3(
          lod.bounds.min.at(0), lod.bounds.min.at(1), lod.bounds.min.at(2)));
      ExpandBounds(geom_bounds,
        glm::vec3(
          lod.bounds.max.at(0), lod.bounds.max.at(1), lod.bounds.max.at(2)));
    }

    const auto radius = 0.5F
      * glm::length(glm::vec3(geom_bounds.max.at(0), geom_bounds.max.at(1),
                      geom_bounds.max.at(2))
        - glm::vec3(
          geom_bounds.min.at(0), geom_bounds.min.at(1), geom_bounds.min.at(2)));
    const auto imported_policy = generated_errors.empty()
      ? internal::MakeImportedLodPolicy(lods.size())
      : internal::MakeGeneratedLodPolicy(generated_errors, radius);

    auto material_patch_offsets
      = std::vector<MeshBuildPipeline::SurfacePatchOffset> {};
    auto descriptor_bytes = BuildDescriptorBytes(item.mesh_name, lods,
      geom_bounds, GeometryDefaultsOf(item), imported_policy, attr_mask,
      material_patch_offsets, diagnostics, item.source_id);
    if (HasAnyError(diagnostics)) {
      return std::nullopt;
    }

    auto cooked_payload = MeshBuildPipeline::CookedGeometryPayload {};
    auto placement = internal::PlaceModelProduct(
      item.request, item.storage_mesh_name, data::kGeometryDescriptorExtension);
    cooked_payload.virtual_path = std::move(placement.virtual_path);
    cooked_payload.descriptor_relpath = std::move(placement.descriptor_relpath);
    cooked_payload.geometry_key
      = ResolveGeometryKey(item.request, cooked_payload.virtual_path);
    cooked_payload.descriptor_bytes = std::move(descriptor_bytes);
    cooked_payload.material_patch_offsets = std::move(material_patch_offsets);
    cooked_payload.lods.reserve(lods.size());
    for (const auto& lod : lods) {
      cooked_payload.lods.push_back(BuildCookedMeshPayload(
        lod, with_content_hashing, item.stop_token, canceled));
    }

    return cooked_payload;
  }

  [[nodiscard]] auto MakeInfoDiagnostic(std::string code, std::string message,
    std::string_view source_id, std::string_view object_path)
    -> ImportDiagnostic
  {
    return ImportDiagnostic {
      .severity = ImportSeverity::kInfo,
      .code = std::move(code),
      .message = std::move(message),
      .source_path = std::string(source_id),
      .object_path = std::string(object_path),
    };
  }

  //! The triangles and vertices of `surfaces`.
  struct SurfaceTotals {
    std::size_t triangles = 0;
    std::size_t vertices = 0;
  };

  [[nodiscard]] auto TotalsOf(
    const std::span<const internal::IndexedSurface> surfaces) -> SurfaceTotals
  {
    auto totals = SurfaceTotals {};
    for (const auto& surface : surfaces) {
      totals.triangles += surface.indices.size() / 3U;
      totals.vertices += surface.vertices.size();
    }
    return totals;
  }

  //! Generates the LODs after `lod0` and lays them out into `lods`.
  /*!
   Simplification reads `lod0`'s surfaces over the source's own vertices; each
   generated LOD is then completed like LOD 0, its normals and tangents
   generated for its own triangles. Returns each generated LOD's
   simplification error, or nullopt when one could not be laid out or the
   import was canceled.
  */
  [[nodiscard]] auto AppendGeneratedLods(const PreparedLod& lod0,
    const WorkItem& item, const SurfaceGeometryProvenance& allocation,
    const uint64_t max_data_blob_bytes, std::vector<LodBuildData>& lods,
    std::vector<ImportDiagnostic>& diagnostics)
    -> std::optional<std::vector<float>>
  {
    const auto& options = item.request.options.lod_generation;
    const auto source = TotalsOf(lod0.sources);
    LOG_F(INFO,
      "Mesh '{}': generating {} LODs, {}% fewer triangles each, from {} "
      "triangles over {} source vertices",
      item.mesh_name, options.levels, options.reduction_percent,
      source.triangles, source.vertices);

    // Normals the import generates are not the source's: simplification
    // weighs only the attributes the source authors.
    const auto generated = internal::GenerateLods(lod0.sources, options,
      internal::SimplifiedAttributes {
        .normals
        = (lod0.attr_mask & kGeomAttr_Normal) != 0U && !lod0.generation.normals,
        .texcoords = (lod0.attr_mask & kGeomAttr_Texcoord0) != 0U,
        .colors = (lod0.attr_mask & kGeomAttr_Color0) != 0U,
      },
      item.stop_token);
    if (item.stop_token.stop_requested()) {
      return std::nullopt;
    }

    auto errors = std::vector<float> {};
    errors.reserve(generated.lods.size());
    for (const auto& [index, generated_lod] :
      std::views::enumerate(generated.lods)) {
      const auto level = static_cast<std::size_t>(index) + 1U;
      const auto surfaces
        = CompleteLod(lod0, generated_lod.surface_indices, item.stop_token);
      if (!surfaces.has_value()) {
        return std::nullopt;
      }
      auto lod_data = LayOutLod(lod0, "LOD" + std::to_string(level), *surfaces,
        std::nullopt, item, allocation, max_data_blob_bytes, diagnostics);
      if (!lod_data.has_value()) {
        return std::nullopt;
      }
      LOG_F(INFO, "Mesh '{}': LOD {} has {} triangles, error {:.6g}",
        item.mesh_name, level, generated_lod.triangle_count,
        generated_lod.error);
      lods.push_back(std::move(*lod_data));
      errors.push_back(generated_lod.error);
    }
    if (generated.stopped.has_value()) {
      const auto& stopped = *generated.stopped;
      auto message = fmt::format(
        "Generated {} of {} LODs for '{}': LOD {} kept {} triangles, more than "
        "the {} that half its {}% reduction allows",
        generated.lods.size(), options.levels, item.mesh_name, stopped.level,
        stopped.triangle_count, stopped.allowed_triangle_count,
        options.reduction_percent);
      LOG_F(INFO, "{}", message);
      diagnostics.push_back(MakeInfoDiagnostic("import.lod_generation.stopped",
        std::move(message), item.source_id, item.mesh_name));
    }
    return errors;
  }

  [[nodiscard]] auto BuildGeometryOutcome(const WorkItem& item,
    const uint64_t max_data_blob_bytes, const bool with_content_hashing,
    const co::ThreadPool::CancelToken& canceled) -> GeometryBuildOutcome
  {
    DLOG_F(1, "Build geometry payload");
    auto out = GeometryBuildOutcome {
      .source_id = item.source_id,
      .source_key = item.source_key,
      .cooked = std::nullopt,
      .diagnostics = {},
    };

    if (IsCanceled(item.stop_token, canceled)) {
      out.canceled = true;
      return out;
    }

    if (item.lods.empty()) {
      out.diagnostics.push_back(MakeErrorDiagnostic("mesh.missing_lods",
        "Mesh LOD list is empty", item.source_id, item.mesh_name));
      return out;
    }

    if (item.lods.size() > data::pak::geometry::kMaxLodCount) {
      out.diagnostics.push_back(MakeErrorDiagnostic("mesh.invalid_lod_count",
        "Mesh LOD count exceeds maximum of "
          + std::to_string(data::pak::geometry::kMaxLodCount),
        item.source_id, item.mesh_name));
      return out;
    }

    auto lods = std::vector<LodBuildData> {};
    lods.reserve(item.lods.size());

    SurfaceGeometryProvenance allocation;
    try {
      allocation = AllocateSurfaces(item);
    } catch (const std::exception& error) {
      out.diagnostics.push_back(
        MakeErrorDiagnostic("mesh.surface_provenance_invalid", error.what(),
          item.source_id, item.mesh_name));
      return out;
    }

    uint32_t attr_mask = 0;
    auto only_lod = std::optional<PreparedLod> {};
    for (const auto& lod : item.lods) {
      if (item.stop_token.stop_requested()) {
        out.canceled = true;
        return out;
      }

      auto prepared = PrepareLod(lod, item, out.diagnostics);
      if (!prepared.has_value()) {
        out.canceled
          = item.stop_token.stop_requested() && !HasAnyError(out.diagnostics);
        return out;
      }
      attr_mask |= prepared->attr_mask;
      const auto surfaces
        = CompleteLod(*prepared, IndicesOf(prepared->sources), item.stop_token);
      if (!surfaces.has_value()) {
        out.canceled = true;
        return out;
      }
      auto lod_data = LayOutLod(*prepared, prepared->lod_name, *surfaces,
        prepared->authored_bounds, item, allocation, max_data_blob_bytes,
        out.diagnostics);
      if (!lod_data.has_value()) {
        return out;
      }
      lods.push_back(std::move(*lod_data));
      if (item.lods.size() == 1U) {
        only_lod = std::move(prepared);
      }
    }

    auto generated_errors = std::vector<float> {};
    if (item.request.options.lod_generation.enabled) {
      if (only_lod.has_value()) {
        auto errors = AppendGeneratedLods(*only_lod, item, allocation,
          max_data_blob_bytes, lods, out.diagnostics);
        if (!errors.has_value()) {
          out.canceled
            = item.stop_token.stop_requested() && !HasAnyError(out.diagnostics);
          return out;
        }
        generated_errors = std::move(*errors);
      } else {
        auto message = fmt::format(
          "Generate LODs is on, but '{}' has {} authored LODs, which are kept "
          "as they are",
          item.mesh_name, item.lods.size());
        LOG_F(INFO, "{}", message);
        out.diagnostics.push_back(
          MakeInfoDiagnostic("import.lod_generation.skipped",
            std::move(message), item.source_id, item.mesh_name));
      }
    }

    if (item.stop_token.stop_requested()) {
      out.canceled = true;
      return out;
    }
    if (HasAnyError(out.diagnostics)) {
      return out;
    }

    auto cooked_payload
      = BuildCookedGeometryPayload(item, lods, generated_errors, attr_mask,
        with_content_hashing, canceled, out.diagnostics);
    if (!cooked_payload.has_value()) {
      return out;
    }

    out.cooked = std::move(*cooked_payload);
    out.cooked->surface_provenance = std::move(allocation);
    out.success = true;
    return out;
  }

} // namespace

MeshBuildPipeline::MeshBuildPipeline(
  co::ThreadPool& thread_pool, std::optional<Config> config)
  : thread_pool_(thread_pool)
  , config_(config.has_value() ? config.value() : Config {})
  , input_channel_(config_.queue_capacity)
  , output_channel_(config_.queue_capacity)
{
}

// Only the debug log can throw (formatting), and it never does for counts.
// NOLINTNEXTLINE(bugprone-exception-escape)
MeshBuildPipeline::~MeshBuildPipeline()
{
  if (started_) {
    DLOG_IF_F(
      WARNING, HasPending(), "Destroyed with {} pending items", PendingCount());
  }

  input_channel_.Close();
  output_channel_.Close();
}

auto MeshBuildPipeline::Start(co::Nursery& nursery) -> void
{
  DCHECK_F(!started_, "MeshBuildPipeline::Start() called more than once");
  started_ = true;

  const auto worker_count = std::max(1U, config_.worker_count);
  for (uint32_t i = 0; i < worker_count; ++i) {
    // The pipeline outlives the nursery that runs its workers.
    nursery.Start([this] -> co::Co<> { co_await Worker(); });
  }
}

auto MeshBuildPipeline::Submit(WorkItem item) -> co::Co<>
{
  ++pending_;
  submitted_.fetch_add(1, std::memory_order_acq_rel);
  co_await input_channel_.Send(std::move(item));
}

auto MeshBuildPipeline::TrySubmit(WorkItem item) -> bool
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

auto MeshBuildPipeline::Collect() -> co::Co<WorkResult>
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

auto MeshBuildPipeline::Close() -> void { input_channel_.Close(); }

auto MeshBuildPipeline::HasPending() const noexcept -> bool
{
  return pending_.load(std::memory_order_acquire) > 0;
}

auto MeshBuildPipeline::PendingCount() const noexcept -> size_t
{
  return pending_.load(std::memory_order_acquire);
}

auto MeshBuildPipeline::GetProgress() const noexcept -> PipelineProgress
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

auto MeshBuildPipeline::Worker() -> co::Co<>
{
  const auto MakeDuration
    = [](const std::chrono::steady_clock::time_point start,
        const std::chrono::steady_clock::time_point end)
    -> std::chrono::microseconds {
    return std::chrono::duration_cast<std::chrono::microseconds>(end - start);
  };

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

    if (item.on_started) {
      item.on_started();
    }
    auto on_finished = std::move(item.on_finished);
    const auto cook_start = std::chrono::steady_clock::now();
    auto build_outcome = co_await thread_pool_.Run(
      // The build reports failures as diagnostics; only allocation can throw.
      // NOLINTNEXTLINE(bugprone-exception-escape)
      [item = std::move(item),
        max_data_blob_bytes = config_.max_data_blob_bytes,
        with_content_hashing = config_.with_content_hashing](
        co::ThreadPool::CancelToken canceled) mutable -> GeometryBuildOutcome {
        return BuildGeometryOutcome(
          item, max_data_blob_bytes, with_content_hashing, canceled);
      });

    if (build_outcome.canceled) {
      auto canceled = WorkResult {
        .source_id = std::move(build_outcome.source_id),
        .source_key = build_outcome.source_key,
        .cooked = std::nullopt,
        .diagnostics = {},
        .success = false,
      };
      if (on_finished) {
        on_finished();
      }
      co_await output_channel_.Send(std::move(canceled));
      continue;
    }

    WorkResult result {
      .source_id = std::move(build_outcome.source_id),
      .source_key = build_outcome.source_key,
      .cooked = std::move(build_outcome.cooked),
      .diagnostics = std::move(build_outcome.diagnostics),
      .telemetry = ImportWorkItemTelemetry {
        .cook_duration = MakeDuration(
          cook_start, std::chrono::steady_clock::now()),
      },
      .success = build_outcome.success,
    };
    if (on_finished) {
      on_finished();
    }
    co_await output_channel_.Send(std::move(result));
  }

  co_return;
}

auto MeshBuildPipeline::ReportCancelled(WorkItem item) -> co::Co<>
{
  WorkResult canceled {
    .source_id = std::move(item.source_id),
    .source_key = item.source_key,
    .cooked = std::nullopt,
    .diagnostics = {},
    .success = false,
  };
  if (item.on_finished) {
    item.on_finished();
  }
  co_await output_channel_.Send(std::move(canceled));
}

} // namespace oxygen::content::import
