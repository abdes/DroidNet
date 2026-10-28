//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <ostream>
#include <span>
#include <string_view>

#include "AssetDumpHelpers.h"
#include "AssetDumper.h"

#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Data/MeshType.h>
#include <Oxygen/Data/PakFormat.h>

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

    using oxygen::data::MeshType;
    using oxygen::data::pak::geometry::GeometryAssetDesc;
    using oxygen::data::pak::geometry::MeshDesc;
    using oxygen::data::pak::geometry::MeshViewDesc;
    using oxygen::data::pak::geometry::SubMeshDesc;

    ctx.Out() << "Asset #" << idx << ":\n";
    asset_dump_helpers::PrintAssetKey(entry.asset_key, ctx);
    asset_dump_helpers::PrintAssetMetadata(ctx.Out(), entry);

    const auto data = asset_dump_helpers::ReadDescriptorBytes(pak, entry);
    if (!data) {
      ctx.Out() << "    Failed to read asset descriptor data\n\n";
      co_return;
    }

    asset_dump_helpers::PrintAssetDescriptorHexPreview(*data, ctx);
    if (data->size() < sizeof(GeometryAssetDesc)) {
      ctx.Out() << "    GeometryAssetDesc: (insufficient data)\n\n";
      co_return;
    }

    GeometryAssetDesc geo {};
    std::memcpy(&geo, data->data(), sizeof(geo));

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

    if (geo.lod_count == 0) {
      ctx.Out() << "\n";
      co_return;
    }

    const size_t mesh_desc_bytes
      = static_cast<size_t>(geo.lod_count) * sizeof(MeshDesc);
    const size_t min_required = sizeof(GeometryAssetDesc) + mesh_desc_bytes;
    if (data->size() < min_required) {
      ctx.Out() << "    MeshDesc array (" << geo.lod_count
                << "): (not present in descriptor: need at least "
                << min_required << " bytes, have " << data->size() << ")\n\n";
      co_return;
    }

    size_t offset = sizeof(GeometryAssetDesc);
    for (uint32_t lod = 0; lod < geo.lod_count; ++lod) {
      MeshDesc mesh_desc {};
      if (!asset_dump_helpers::ReadStructAt(
            *data, offset, sizeof(mesh_desc), &mesh_desc)) {
        ctx.Out() << "    LOD[" << lod << "]: MeshDesc: (insufficient data)\n";
        break;
      }

      const auto mesh_name = asset_dump_helpers::DescriptorString(
        *data, geo.header, mesh_desc.name_offset);

      const auto mesh_type = static_cast<MeshType>(mesh_desc.mesh_type);

      ctx.Out() << "    LOD[" << lod << "] Mesh: " << mesh_name << "\n";
      PrintUtils::Field(ctx.Out(), "Mesh Type",
        std::string(nostd::to_string(mesh_type)) + " ("
          + std::to_string(mesh_desc.mesh_type) + ")",
        8);
      PrintUtils::Field(ctx.Out(), "SubMesh Count", mesh_desc.submesh_count, 8);
      PrintUtils::Field(
        ctx.Out(), "MeshView Count", mesh_desc.mesh_view_count, 8);

      if (mesh_type == MeshType::kStandard || mesh_desc.IsStandard()) {
        PrintUtils::Field(
          ctx.Out(), "Vertex Buffer", mesh_desc.info.standard.vertex_buffer, 8);
        PrintUtils::Field(
          ctx.Out(), "Index Buffer", mesh_desc.info.standard.index_buffer, 8);
        PrintUtils::Field(ctx.Out(), "Mesh AABB Min",
          asset_dump_helpers::FormatVec3(
            mesh_desc.info.standard.bounding_box_min),
          8);
        PrintUtils::Field(ctx.Out(), "Mesh AABB Max",
          asset_dump_helpers::FormatVec3(
            mesh_desc.info.standard.bounding_box_max),
          8);
      } else if (mesh_type == MeshType::kSkinned || mesh_desc.IsSkinned()) {
        PrintUtils::Field(
          ctx.Out(), "Vertex Buffer", mesh_desc.info.skinned.vertex_buffer, 8);
        PrintUtils::Field(
          ctx.Out(), "Index Buffer", mesh_desc.info.skinned.index_buffer, 8);
        PrintUtils::Field(ctx.Out(), "Joint Index Buffer",
          mesh_desc.info.skinned.joint_index_buffer, 8);
        PrintUtils::Field(ctx.Out(), "Joint Weight Buffer",
          mesh_desc.info.skinned.joint_weight_buffer, 8);
        PrintUtils::Field(ctx.Out(), "Inverse Bind Buffer",
          mesh_desc.info.skinned.inverse_bind_buffer, 8);
        PrintUtils::Field(ctx.Out(), "Joint Remap Buffer",
          mesh_desc.info.skinned.joint_remap_buffer, 8);
        PrintUtils::Field(ctx.Out(), "Skeleton Asset",
          oxygen::data::to_string(mesh_desc.info.skinned.skeleton_asset_key),
          8);
        PrintUtils::Field(
          ctx.Out(), "Joint Count", mesh_desc.info.skinned.joint_count, 8);
        PrintUtils::Field(ctx.Out(), "Influences Per Vertex",
          mesh_desc.info.skinned.influences_per_vertex, 8);
        PrintUtils::Field(
          ctx.Out(), "Skinned Flags", mesh_desc.info.skinned.flags, 8);
        PrintUtils::Field(ctx.Out(), "Mesh AABB Min",
          asset_dump_helpers::FormatVec3(
            mesh_desc.info.skinned.bounding_box_min),
          8);
        PrintUtils::Field(ctx.Out(), "Mesh AABB Max",
          asset_dump_helpers::FormatVec3(
            mesh_desc.info.skinned.bounding_box_max),
          8);
      } else if (mesh_type == MeshType::kProcedural
        || mesh_desc.IsProcedural()) {
        PrintUtils::Field(
          ctx.Out(), "Params Size", mesh_desc.info.procedural.params_size, 8);
      }

      offset += sizeof(MeshDesc);

      if (mesh_desc.IsProcedural()) {
        const size_t params_size = mesh_desc.info.procedural.params_size;
        if (offset > data->size() || params_size > (data->size() - offset)) {
          ctx.Out() << "      Procedural params: (insufficient data)\n\n";
          break;
        }

        if (ctx.verbose && ctx.show_asset_descriptors && params_size > 0) {
          ctx.Out() << "      Procedural Params Preview (" << params_size
                    << " bytes):\n";
          PrintUtils::HexDump(ctx.Out(),
            std::span(*data).subspan(offset, params_size), ctx.max_data_bytes);
        }
        offset += params_size;
      }

      const uint32_t submesh_count = mesh_desc.submesh_count;
      const uint32_t submesh_limit
        = ctx.verbose ? submesh_count : (std::min)(submesh_count, 8U);

      if (submesh_count > 0) {
        ctx.Out() << "      SubMeshes (" << submesh_count << "):\n";
      }

      for (uint32_t sm = 0; sm < submesh_limit; ++sm) {
        SubMeshDesc submesh_desc {};
        if (!asset_dump_helpers::ReadStructAt(
              *data, offset, sizeof(submesh_desc), &submesh_desc)) {
          ctx.Out() << "        [" << sm
                    << "] SubMeshDesc: (insufficient data)\n";
          break;
        }

        const auto submesh_name = asset_dump_helpers::DescriptorString(
          *data, geo.header, submesh_desc.name_offset);

        ctx.Out() << "        [" << sm << "] " << submesh_name << "\n";
        PrintUtils::Field(ctx.Out(), "Material Key",
          oxygen::data::to_string(submesh_desc.material_asset_key), 12);
        PrintUtils::Field(
          ctx.Out(), "MeshView Count", submesh_desc.mesh_view_count, 12);
        PrintUtils::Field(ctx.Out(), "AABB Min",
          asset_dump_helpers::FormatVec3(submesh_desc.bounding_box_min), 12);
        PrintUtils::Field(ctx.Out(), "AABB Max",
          asset_dump_helpers::FormatVec3(submesh_desc.bounding_box_max), 12);

        offset += sizeof(SubMeshDesc);

        const uint32_t view_count = submesh_desc.mesh_view_count;
        const uint32_t view_limit
          = ctx.verbose ? view_count : (std::min)(view_count, 8U);

        for (uint32_t v = 0; v < view_limit; ++v) {
          MeshViewDesc view_desc {};
          if (!asset_dump_helpers::ReadStructAt(
                *data, offset, sizeof(view_desc), &view_desc)) {
            ctx.Out() << "          [" << v
                      << "] MeshViewDesc: (insufficient data)\n";
            break;
          }

          ctx.Out() << "          [" << v << "]";
          ctx.Out() << " first_index=" << view_desc.first_index;
          ctx.Out() << ", index_count=" << view_desc.index_count;
          ctx.Out() << ", first_vertex=" << view_desc.first_vertex;
          ctx.Out() << ", vertex_count=" << view_desc.vertex_count;
          ctx.Out() << ", bounds=(" << view_desc.bounding_box_min[0] << ", "
                    << view_desc.bounding_box_min[1] << ", "
                    << view_desc.bounding_box_min[2] << ")-("
                    << view_desc.bounding_box_max[0] << ", "
                    << view_desc.bounding_box_max[1] << ", "
                    << view_desc.bounding_box_max[2] << ")\n";

          offset += sizeof(MeshViewDesc);
        }

        if (view_count > view_limit) {
          ctx.Out() << "          ... (" << (view_count - view_limit)
                    << " more views)\n";
          offset += static_cast<size_t>(view_count - view_limit)
            * sizeof(MeshViewDesc);
        }
      }

      if (submesh_count > submesh_limit) {
        ctx.Out() << "        ... (" << (submesh_count - submesh_limit)
                  << " more submeshes)\n";

        for (uint32_t sm = submesh_limit; sm < submesh_count; ++sm) {
          SubMeshDesc submesh_desc {};
          if (!asset_dump_helpers::ReadStructAt(
                *data, offset, sizeof(submesh_desc), &submesh_desc)) {
            break;
          }
          offset += sizeof(SubMeshDesc);
          offset += static_cast<size_t>(submesh_desc.mesh_view_count)
            * sizeof(MeshViewDesc);
        }
      }

      ctx.Out() << "\n";
    }

    ctx.Out() << "    Surfaces (" << geo.surface_count << "):\n";
    for (uint32_t surface = 0; surface < geo.surface_count; ++surface) {
      data::pak::geometry::SurfaceDesc surface_desc {};
      if (!asset_dump_helpers::ReadStructAt(
            *data, offset, sizeof(surface_desc), &surface_desc)) {
        ctx.Out() << "        (surface table not present in descriptor)\n";
        break;
      }
      offset += sizeof(surface_desc);
      ctx.Out() << "        " << data::to_string(surface_desc.surface_id)
                << std::boolalpha << ": visible="
                << ((surface_desc.flags
                      & data::pak::geometry::kSurfaceFlag_Visible)
                     != 0U)
                << ", casts_shadows="
                << ((surface_desc.flags
                      & data::pak::geometry::kSurfaceFlag_CastsShadows)
                     != 0U)
                << std::noboolalpha << "\n";
    }

    ctx.Out() << "\n";
    co_return;
  }

private:
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
        << static_cast<uint32_t>(policy.type) << "\n";
  }
};

} // namespace oxygen::content::pakdump
