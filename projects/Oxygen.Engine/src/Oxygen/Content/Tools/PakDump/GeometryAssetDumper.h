//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <iostream>

#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Data/MeshType.h>
#include <Oxygen/Data/PakFormat.h>

#include "AssetDumpHelpers.h"
#include "AssetDumper.h"

namespace oxygen::content::pakdump {

//! Dumps geometry asset descriptors.
class GeometryAssetDumper final : public AssetDumper {
public:
  auto DumpAsync(const oxygen::content::PakFile& pak,
    const oxygen::data::pak::v2::AssetDirectoryEntry& entry, DumpContext& ctx,
    const size_t idx, oxygen::content::AssetLoader& asset_loader) const
    -> oxygen::co::Co<> override
  {
    (void)asset_loader;

    using oxygen::data::MeshType;
    using oxygen::data::pak::GeometryAssetDesc;
    using oxygen::data::pak::MeshDesc;
    using oxygen::data::pak::MeshViewDesc;
    using oxygen::data::pak::SubMeshDesc;

    std::cout << "Asset #" << idx << ":\n";
    asset_dump_helpers::PrintAssetKey(entry.asset_key, ctx);
    asset_dump_helpers::PrintAssetMetadata(entry);

    const auto data = asset_dump_helpers::ReadDescriptorBytes(pak, entry);
    if (!data) {
      std::cout << "    Failed to read asset descriptor data\n\n";
      co_return;
    }

    asset_dump_helpers::PrintAssetDescriptorHexPreview(*data, ctx);
    if (data->size() < sizeof(GeometryAssetDesc)) {
      std::cout << "    GeometryAssetDesc: (insufficient data)\n\n";
      co_return;
    }

    GeometryAssetDesc geo {};
    std::memcpy(&geo, data->data(), sizeof(geo));

    asset_dump_helpers::PrintAssetHeaderFields(geo.header, 4);

    std::cout << "    --- Geometry Descriptor Fields ---\n";
    PrintUtils::Field("LOD Count", geo.lod_count, 8);
    PrintUtils::Field(
      "AABB Min", asset_dump_helpers::FormatVec3(geo.bounding_box_min), 8);
    PrintUtils::Field(
      "AABB Max", asset_dump_helpers::FormatVec3(geo.bounding_box_max), 8);
    std::cout << "\n";

    if (geo.lod_count == 0) {
      std::cout << "\n";
      co_return;
    }

    const size_t mesh_desc_bytes
      = static_cast<size_t>(geo.lod_count) * sizeof(MeshDesc);
    const size_t min_required = sizeof(GeometryAssetDesc) + mesh_desc_bytes;
    if (data->size() < min_required) {
      std::cout << "    MeshDesc array (" << geo.lod_count
                << "): (not present in descriptor: need at least "
                << min_required << " bytes, have " << data->size() << ")\n\n";
      co_return;
    }

    size_t offset = sizeof(GeometryAssetDesc);
    for (uint32_t lod = 0; lod < geo.lod_count; ++lod) {
      MeshDesc mesh_desc {};
      if (!asset_dump_helpers::ReadStructAt(
            *data, offset, sizeof(mesh_desc), &mesh_desc)) {
        std::cout << "    LOD[" << lod << "]: MeshDesc: (insufficient data)\n";
        break;
      }

      const auto mesh_name = std::string(
        mesh_desc.name, strnlen(mesh_desc.name, sizeof(mesh_desc.name)));

      const auto mesh_type
        = static_cast<MeshType>(static_cast<uint8_t>(mesh_desc.mesh_type));

      std::cout << "    LOD[" << lod << "] Mesh: " << mesh_name << "\n";
      PrintUtils::Field("Mesh Type",
        std::string(nostd::to_string(mesh_type)) + " ("
          + std::to_string(mesh_desc.mesh_type) + ")",
        8);
      PrintUtils::Field("SubMesh Count", mesh_desc.submesh_count, 8);
      PrintUtils::Field("MeshView Count", mesh_desc.mesh_view_count, 8);

      if (mesh_type == MeshType::kStandard || mesh_desc.IsStandard()) {
        PrintUtils::Field(
          "Vertex Buffer", mesh_desc.info.standard.vertex_buffer, 8);
        PrintUtils::Field(
          "Index Buffer", mesh_desc.info.standard.index_buffer, 8);
        PrintUtils::Field("Mesh AABB Min",
          asset_dump_helpers::FormatVec3(
            mesh_desc.info.standard.bounding_box_min),
          8);
        PrintUtils::Field("Mesh AABB Max",
          asset_dump_helpers::FormatVec3(
            mesh_desc.info.standard.bounding_box_max),
          8);
      } else if (mesh_type == MeshType::kSkinned || mesh_desc.IsSkinned()) {
        PrintUtils::Field(
          "Vertex Buffer", mesh_desc.info.skinned.vertex_buffer, 8);
        PrintUtils::Field(
          "Index Buffer", mesh_desc.info.skinned.index_buffer, 8);
        PrintUtils::Field(
          "Joint Index Buffer", mesh_desc.info.skinned.joint_index_buffer, 8);
        PrintUtils::Field(
          "Joint Weight Buffer", mesh_desc.info.skinned.joint_weight_buffer, 8);
        PrintUtils::Field(
          "Inverse Bind Buffer", mesh_desc.info.skinned.inverse_bind_buffer, 8);
        PrintUtils::Field(
          "Joint Remap Buffer", mesh_desc.info.skinned.joint_remap_buffer, 8);
        PrintUtils::Field("Skeleton Asset",
          oxygen::data::to_string(mesh_desc.info.skinned.skeleton_asset_key),
          8);
        PrintUtils::Field("Joint Count", mesh_desc.info.skinned.joint_count, 8);
        PrintUtils::Field("Influences Per Vertex",
          mesh_desc.info.skinned.influences_per_vertex, 8);
        PrintUtils::Field("Skinned Flags", mesh_desc.info.skinned.flags, 8);
        PrintUtils::Field("Mesh AABB Min",
          asset_dump_helpers::FormatVec3(
            mesh_desc.info.skinned.bounding_box_min),
          8);
        PrintUtils::Field("Mesh AABB Max",
          asset_dump_helpers::FormatVec3(
            mesh_desc.info.skinned.bounding_box_max),
          8);
      } else if (mesh_type == MeshType::kProcedural
        || mesh_desc.IsProcedural()) {
        PrintUtils::Field(
          "Params Size", mesh_desc.info.procedural.params_size, 8);
      }

      offset += sizeof(MeshDesc);

      if (mesh_desc.IsProcedural()) {
        const size_t params_size = mesh_desc.info.procedural.params_size;
        if (offset > data->size() || params_size > (data->size() - offset)) {
          std::cout << "      Procedural params: (insufficient data)\n\n";
          break;
        }

        if (ctx.verbose && ctx.show_asset_descriptors && params_size > 0) {
          std::cout << "      Procedural Params Preview (" << params_size
                    << " bytes):\n";
          PrintUtils::HexDump(
            reinterpret_cast<const uint8_t*>(data->data()) + offset,
            (std::min)(params_size, ctx.max_data_bytes), ctx.max_data_bytes);
        }
        offset += params_size;
      }

      const uint32_t submesh_count = mesh_desc.submesh_count;
      const uint32_t submesh_limit
        = ctx.verbose ? submesh_count : (std::min)(submesh_count, 8u);

      if (submesh_count > 0) {
        std::cout << "      SubMeshes (" << submesh_count << "):\n";
      }

      for (uint32_t sm = 0; sm < submesh_limit; ++sm) {
        SubMeshDesc submesh_desc {};
        if (!asset_dump_helpers::ReadStructAt(
              *data, offset, sizeof(submesh_desc), &submesh_desc)) {
          std::cout << "        [" << sm
                    << "] SubMeshDesc: (insufficient data)\n";
          break;
        }

        const auto submesh_name = std::string(submesh_desc.name,
          strnlen(submesh_desc.name, sizeof(submesh_desc.name)));

        std::cout << "        [" << sm << "] " << submesh_name << "\n";
        PrintUtils::Field("Material Key",
          oxygen::data::to_string(submesh_desc.material_asset_key), 12);
        PrintUtils::Field("MeshView Count", submesh_desc.mesh_view_count, 12);
        if (ctx.verbose) {
          PrintUtils::Field("AABB Min",
            asset_dump_helpers::FormatVec3(submesh_desc.bounding_box_min), 12);
          PrintUtils::Field("AABB Max",
            asset_dump_helpers::FormatVec3(submesh_desc.bounding_box_max), 12);
        }

        offset += sizeof(SubMeshDesc);

        const uint32_t view_count = submesh_desc.mesh_view_count;
        const uint32_t view_limit
          = ctx.verbose ? view_count : (std::min)(view_count, 8u);

        for (uint32_t v = 0; v < view_limit; ++v) {
          MeshViewDesc view_desc {};
          if (!asset_dump_helpers::ReadStructAt(
                *data, offset, sizeof(view_desc), &view_desc)) {
            std::cout << "          [" << v
                      << "] MeshViewDesc: (insufficient data)\n";
            break;
          }

          std::cout << "          [" << v << "]";
          std::cout << " first_index=" << view_desc.first_index;
          std::cout << ", index_count=" << view_desc.index_count;
          std::cout << ", first_vertex=" << view_desc.first_vertex;
          std::cout << ", vertex_count=" << view_desc.vertex_count << "\n";

          offset += sizeof(MeshViewDesc);
        }

        if (view_count > view_limit) {
          std::cout << "          ... (" << (view_count - view_limit)
                    << " more views)\n";
          offset += static_cast<size_t>(view_count - view_limit)
            * sizeof(MeshViewDesc);
        }
      }

      if (submesh_count > submesh_limit) {
        std::cout << "        ... (" << (submesh_count - submesh_limit)
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

      std::cout << "\n";
    }

    std::cout << "\n";
    co_return;
  }
};

} // namespace oxygen::content::pakdump
