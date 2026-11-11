//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <ostream>
#include <ranges>
#include <span>
#include <string>
#include <string_view>

#include "AssetDumpHelpers.h"
#include "AssetDumper.h"
#include "DecodedAsset.h"
#include <fmt/format.h>
#include <glm/glm.hpp>

#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Content/Loaders/GeometryLoader.h>
#include <Oxygen/Cooker/Tools/Common/EnumString.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/MeshType.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Serio/ByteReader.h>

namespace oxygen::content::pakdump {

//! Dumps geometry asset descriptors.
class GeometryAssetDumper final : public AssetDumper {
public:
  auto DumpAsync(const oxygen::content::PakFile& pak,
    const oxygen::data::pak::core::AssetDirectoryEntry& entry, DumpContext& ctx,
    const size_t idx, oxygen::content::AssetLoader& asset_loader) const
    -> oxygen::co::Co<> override
  {
    (void)asset_loader;

    using oxygen::data::pak::geometry::GeometryAssetDesc;

    ctx.Out() << "Asset #" << idx << ":\n";
    asset_dump_helpers::PrintAssetKey(entry.asset_key, ctx);
    asset_dump_helpers::PrintAssetMetadata(ctx.Out(), entry);

    const auto data = asset_dump_helpers::TryDescriptorBytes(pak, entry);
    if (!data) {
      ctx.Out() << "    Failed to read asset descriptor data\n\n";
      co_return;
    }

    asset_dump_helpers::PrintAssetDescriptorHexPreview(*data, ctx);
    // The runtime reads it first; a descriptor it refuses is not printed.
    const auto geometry = DecodeAsset(pak, entry, &loaders::LoadGeometryAsset);
    const auto geo_read = serio::CopyAt<GeometryAssetDesc>(*data);
    if (!geo_read) {
      ctx.Out() << "    GeometryAssetDesc: (insufficient data)\n\n";
      co_return;
    }
    const auto& geo = *geo_read;

    asset_dump_helpers::PrintAssetHeaderFields(ctx.Out(), geo.header,
      asset_dump_helpers::DescriptorString(
        *data, geo.header, geo.header.name_offset),
      4);

    ctx.Out() << "    --- Geometry Descriptor Fields ---\n";
    PrintUtils::Field(ctx.Out(), "LOD Count", geo.lod_count, 8);
    PrintUtils::Field(ctx.Out(), "AABB Min",
      asset_dump_helpers::FormatVec3(geo.bounding_box_min), 8);
    PrintUtils::Field(ctx.Out(), "AABB Max",
      asset_dump_helpers::FormatVec3(geo.bounding_box_max), 8);
    PrintUtils::Field(ctx.Out(), "Surface Count", geo.surface_count, 8);
    PrintLodPolicy(ctx.Out(), geo.default_lod_policy);
    ctx.Out() << "\n";

    // The meshes, submeshes and views as the runtime decoded them.
    for (const auto& [lod, mesh] : std::views::enumerate(geometry->Meshes())) {
      if (!mesh) {
        continue;
      }
      PrintMesh(ctx, lod, *mesh);
    }

    ctx.Out() << "    Surfaces (" << geo.surface_count << "):\n";
    for (const auto& surface : geometry->Surfaces().surfaces) {
      if (!surface.defaults.has_value()) {
        continue;
      }
      ctx.Out() << "        " << data::to_string(surface.surface_id)
                << std::boolalpha << ": visible=" << surface.defaults->visible
                << ", casts_shadows=" << surface.defaults->casts_shadows
                << std::noboolalpha << "\n";
    }

    ctx.Out() << "\n";
    co_return;
  }

private:
  static auto PrintMesh(
    DumpContext& ctx, const std::ptrdiff_t lod, const data::Mesh& mesh) -> void
  {
    ctx.Out() << "    LOD[" << lod << "] Mesh: " << mesh.GetName() << "\n";
    const auto& descriptor = mesh.Descriptor();
    if (descriptor.has_value()) {
      PrintMeshInfo(ctx, *descriptor);
    }

    const auto submeshes = mesh.SubMeshes();
    if (!submeshes.empty()) {
      ctx.Out() << "      SubMeshes (" << submeshes.size() << "):\n";
    }
    const auto shown = Shown(ctx, submeshes.size());
    for (const auto& [index, submesh] :
      std::views::enumerate(submeshes.first(shown))) {
      PrintSubMesh(ctx, index, submesh);
    }
    if (submeshes.size() > shown) {
      ctx.Out() << "        ... (" << (submeshes.size() - shown)
                << " more submeshes)\n";
    }
    ctx.Out() << "\n";
  }

  static auto PrintMeshInfo(
    DumpContext& ctx, const data::pak::geometry::MeshDesc& mesh_desc) -> void
  {
    PrintUtils::Field(ctx.Out(), "Mesh Type",
      toolkit::EnumString(static_cast<data::MeshType>(mesh_desc.mesh_type)), 8);
    PrintUtils::Field(ctx.Out(), "SubMesh Count", mesh_desc.submesh_count, 8);
    PrintUtils::Field(
      ctx.Out(), "MeshView Count", mesh_desc.mesh_view_count, 8);

    if (mesh_desc.IsStandard()) {
      const auto& standard = mesh_desc.info.standard;
      PrintUtils::Field(ctx.Out(), "Vertex Buffer", standard.vertex_buffer, 8);
      PrintUtils::Field(ctx.Out(), "Index Buffer", standard.index_buffer, 8);
      PrintUtils::Field(ctx.Out(), "Mesh AABB Min",
        asset_dump_helpers::FormatVec3(standard.bounding_box_min), 8);
      PrintUtils::Field(ctx.Out(), "Mesh AABB Max",
        asset_dump_helpers::FormatVec3(standard.bounding_box_max), 8);
    } else if (mesh_desc.IsSkinned()) {
      const auto& skinned = mesh_desc.info.skinned;
      PrintUtils::Field(ctx.Out(), "Vertex Buffer", skinned.vertex_buffer, 8);
      PrintUtils::Field(ctx.Out(), "Index Buffer", skinned.index_buffer, 8);
      PrintUtils::Field(
        ctx.Out(), "Joint Index Buffer", skinned.joint_index_buffer, 8);
      PrintUtils::Field(
        ctx.Out(), "Joint Weight Buffer", skinned.joint_weight_buffer, 8);
      PrintUtils::Field(
        ctx.Out(), "Inverse Bind Buffer", skinned.inverse_bind_buffer, 8);
      PrintUtils::Field(
        ctx.Out(), "Joint Remap Buffer", skinned.joint_remap_buffer, 8);
      PrintUtils::Field(ctx.Out(), "Skeleton Asset",
        data::to_string(skinned.skeleton_asset_key), 8);
      PrintUtils::Field(ctx.Out(), "Joint Count", skinned.joint_count, 8);
      PrintUtils::Field(
        ctx.Out(), "Influences Per Vertex", skinned.influences_per_vertex, 8);
      PrintUtils::Field(ctx.Out(), "Skinned Flags", skinned.flags, 8);
      PrintUtils::Field(ctx.Out(), "Mesh AABB Min",
        asset_dump_helpers::FormatVec3(skinned.bounding_box_min), 8);
      PrintUtils::Field(ctx.Out(), "Mesh AABB Max",
        asset_dump_helpers::FormatVec3(skinned.bounding_box_max), 8);
    } else if (mesh_desc.IsProcedural()) {
      PrintUtils::Field(
        ctx.Out(), "Params Size", mesh_desc.info.procedural.params_size, 8);
    }
  }

  static auto PrintSubMesh(DumpContext& ctx, const std::ptrdiff_t index,
    const data::SubMesh& submesh) -> void
  {
    const auto views = submesh.MeshViews();
    ctx.Out() << "        [" << index << "] " << submesh.GetName() << "\n";
    const auto& descriptor = submesh.Descriptor();
    if (descriptor.has_value()) {
      PrintUtils::Field(ctx.Out(), "Material Key",
        data::to_string(descriptor->material_asset_key), 12);
    }
    PrintUtils::Field(ctx.Out(), "MeshView Count", views.size(), 12);
    PrintUtils::Field(
      ctx.Out(), "AABB Min", FormatPoint(submesh.BoundingBoxMin()), 12);
    PrintUtils::Field(
      ctx.Out(), "AABB Max", FormatPoint(submesh.BoundingBoxMax()), 12);

    const auto shown = Shown(ctx, views.size());
    for (const auto& [view_index, view] :
      std::views::enumerate(views.first(shown))) {
      const auto min = view.BoundingBoxMin();
      const auto max = view.BoundingBoxMax();
      ctx.Out() << "          [" << view_index << "]"
                << " first_index=" << view.FirstIndex()
                << ", index_count=" << view.IndexCount()
                << ", first_vertex=" << view.FirstVertex()
                << ", vertex_count=" << view.VertexCount() << ", bounds=("
                << min.x << ", " << min.y << ", " << min.z << ")-(" << max.x
                << ", " << max.y << ", " << max.z << ")\n";
    }
    if (views.size() > shown) {
      ctx.Out() << "          ... (" << (views.size() - shown)
                << " more views)\n";
    }
  }

  [[nodiscard]] static auto FormatPoint(const glm::vec3& point) -> std::string
  {
    return fmt::format("[{:.3f}, {:.3f}, {:.3f}]", point.x, point.y, point.z);
  }

  //! How many of `count` entries to print: all when verbose, else a few.
  [[nodiscard]] static auto Shown(
    const DumpContext& ctx, const std::size_t count) -> std::size_t
  {
    return ctx.verbose ? count : (std::min)(count, std::size_t { 8 });
  }

  static auto PrintLodPolicy(std::ostream& out,
    const data::pak::geometry::LodPolicyRecord& policy) -> void
  {
    using data::pak::geometry::LodPolicyType;

    const auto print_list = [&out, &policy](const std::string_view name,
                              const std::span<const float> values) -> void {
      out << "        " << name << ": [";
      // A damaged record may claim more boundaries than the arrays hold.
      const auto shown = values.first(
        (std::min)(std::size_t { policy.boundary_count }, values.size()));
      auto separator = std::string_view {};
      for (const auto value : shown) {
        out << separator << value;
        separator = ", ";
      }
      out << "]\n";
    };
    switch (policy.type) {
    case LodPolicyType::kFixed:
      out << "        LOD Policy: fixed, lod_index="
          << static_cast<uint32_t>(policy.lod_index) << "\n";
      return;
    case LodPolicyType::kDistance:
      out << "        LOD Policy: distance, hysteresis_ratio="
          << policy.hysteresis_ratio << "\n";
      print_list("thresholds", policy.thresholds);
      return;
    case LodPolicyType::kScreenSpaceError:
      out << "        LOD Policy: screen_space_error\n";
      print_list("enter_finer_sse", policy.enter_finer_sse);
      print_list("exit_coarser_sse", policy.exit_coarser_sse);
      return;
    }
    out << "        LOD Policy: unknown type "
        << toolkit::EnumString(policy.type) << "\n";
  }
};

} // namespace oxygen::content::pakdump
