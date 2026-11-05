//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <bit>
#include <cstddef>
#include <cstring>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "AssetDumpHelpers.h"
#include "AssetDumper.h"
#include <fmt/format.h>

#include <Oxygen/Cooker/Tools/Common/EnumString.h>
#include <Oxygen/Core/Types/ShaderType.h>
#include <Oxygen/Data/MaterialDomain.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Serio/ByteReader.h>

namespace oxygen::content::pakdump {

//! Dumps material asset descriptors.
class MaterialAssetDumper final : public AssetDumper {
public:
  auto DumpAsync(const oxygen::content::PakFile& pak,
    const oxygen::data::pak::core::AssetDirectoryEntry& entry, DumpContext& ctx,
    const size_t idx, oxygen::content::AssetLoader& asset_loader) const
    -> oxygen::co::Co<> override
  {
    (void)asset_loader;

    using oxygen::data::pak::render::MaterialAssetDesc;
    using oxygen::data::pak::render::ShaderReferenceDesc;

    ctx.Out() << "Asset #" << idx << ":\n";
    asset_dump_helpers::PrintAssetKey(entry.asset_key, ctx);
    asset_dump_helpers::PrintAssetMetadata(ctx.Out(), entry);

    const auto data = asset_dump_helpers::TryDescriptorBytes(pak, entry);
    if (!data) {
      ctx.Out() << "    Failed to read asset descriptor data\n\n";
      co_return;
    }

    asset_dump_helpers::PrintAssetDescriptorHexPreview(*data, ctx);
    const auto header
      = serio::CopyAt<oxygen::data::pak::core::AssetHeader>(*data);
    if (!header
      || header->version != oxygen::data::pak::render::kMaterialAssetVersion) {
      ctx.Out()
        << "    Unsupported material descriptor version; re-cook the asset\n\n";
      co_return;
    }
    const auto mat_read = serio::CopyAt<MaterialAssetDesc>(*data);
    if (!mat_read) {
      ctx.Out() << "    MaterialAssetDesc: (insufficient data)\n\n";
      co_return;
    }
    const auto& mat = *mat_read;

    asset_dump_helpers::PrintAssetHeaderFields(ctx.Out(), mat.header,
      asset_dump_helpers::DescriptorString(
        *data, mat.header, mat.header.name_offset),
      4);

    ctx.Out() << "    --- Material Descriptor Fields ---\n";

    PrintUtils::Field(ctx.Out(), "Material Domain",
      toolkit::EnumString(
        static_cast<data::MaterialDomain>(mat.material_domain)),
      8);
    PrintUtils::Field(
      ctx.Out(), "Flags", asset_dump_helpers::ToHexString(mat.flags), 8);
    PrintUtils::Field(ctx.Out(), "Shader Stages",
      asset_dump_helpers::ToHexString(mat.shader_stages), 8);
    PrintUtils::Field(ctx.Out(), "Base Color",
      fmt::format("[{:.3f}, {:.3f}, {:.3f}, {:.3f}]", mat.base_color[0],
        mat.base_color[1], mat.base_color[2], mat.base_color[3]),
      8);
    PrintUtils::Field(ctx.Out(), "Normal Scale", mat.normal_scale, 8);
    PrintUtils::Field(ctx.Out(), "Metalness", mat.metalness.ToFloat(), 8);
    PrintUtils::Field(ctx.Out(), "Roughness", mat.roughness.ToFloat(), 8);
    PrintUtils::Field(
      ctx.Out(), "Ambient Occlusion", mat.ambient_occlusion.ToFloat(), 8);
    PrintUtils::Field(
      ctx.Out(), "Base Color Texture", mat.base_color_texture, 8);
    PrintUtils::Field(ctx.Out(), "Normal Texture", mat.normal_texture, 8);
    PrintUtils::Field(ctx.Out(), "Metallic Texture", mat.metallic_texture, 8);
    PrintUtils::Field(ctx.Out(), "Roughness Texture", mat.roughness_texture, 8);
    PrintUtils::Field(
      ctx.Out(), "Ambient Occlusion Texture", mat.ambient_occlusion_texture, 8);

    PrintUtils::Field(ctx.Out(), "Emissive Texture", mat.emissive_texture, 8);
    PrintUtils::Field(ctx.Out(), "Specular Texture", mat.specular_texture, 8);
    PrintUtils::Field(
      ctx.Out(), "Sheen Color Texture", mat.sheen_color_texture, 8);
    PrintUtils::Field(ctx.Out(), "Clearcoat Texture", mat.clearcoat_texture, 8);
    PrintUtils::Field(
      ctx.Out(), "Clearcoat Normal Texture", mat.clearcoat_normal_texture, 8);
    PrintUtils::Field(
      ctx.Out(), "Transmission Texture", mat.transmission_texture, 8);
    PrintUtils::Field(ctx.Out(), "Thickness Texture", mat.thickness_texture, 8);

    PrintUtils::Field(ctx.Out(), "Emissive Factor",
      fmt::format("[{:.3f}, {:.3f}, {:.3f}]", mat.emissive_factor[0],
        mat.emissive_factor[1], mat.emissive_factor[2]),
      8);
    PrintUtils::Field(ctx.Out(), "Alpha Cutoff", mat.alpha_cutoff.ToFloat(), 8);
    PrintUtils::Field(ctx.Out(), "IOR", mat.ior, 8);
    PrintUtils::Field(
      ctx.Out(), "Specular Factor", mat.specular_factor.ToFloat(), 8);
    PrintUtils::Field(ctx.Out(), "Sheen Color Factor",
      fmt::format("[{:.3f}, {:.3f}, {:.3f}]",
        mat.sheen_color_factor[0].ToFloat(),
        mat.sheen_color_factor[1].ToFloat(),
        mat.sheen_color_factor[2].ToFloat()),
      8);
    PrintUtils::Field(
      ctx.Out(), "Clearcoat Factor", mat.clearcoat_factor.ToFloat(), 8);
    PrintUtils::Field(
      ctx.Out(), "Clearcoat Roughness", mat.clearcoat_roughness.ToFloat(), 8);
    PrintUtils::Field(
      ctx.Out(), "Transmission Factor", mat.transmission_factor.ToFloat(), 8);
    PrintUtils::Field(
      ctx.Out(), "Thickness Factor", mat.thickness_factor.ToFloat(), 8);
    PrintUtils::Field(ctx.Out(), "Attenuation Color",
      fmt::format("[{:.3f}, {:.3f}, {:.3f}]",
        mat.attenuation_color[0].ToFloat(), mat.attenuation_color[1].ToFloat(),
        mat.attenuation_color[2].ToFloat()),
      8);
    PrintUtils::Field(
      ctx.Out(), "Attenuation Distance", mat.attenuation_distance, 8);
    for (std::size_t index = 0; index < data::kMaterialTextureSlotCount;
      ++index) {
      const auto& sampling = mat.texture_sampling[index];
      PrintUtils::Field(ctx.Out(),
        fmt::format("{} Sampling",
          data::to_string(static_cast<data::MaterialTextureSlot>(index))),
        fmt::format("set {} scale [{:.3f}, {:.3f}] offset [{:.3f}, {:.3f}] "
                    "rotation {:.3f} address {}/{} filter {}/{}/{}",
          sampling.uv_set, float { sampling.uv_scale[0] },
          float { sampling.uv_scale[1] }, float { sampling.uv_offset[0] },
          float { sampling.uv_offset[1] },
          float { sampling.uv_rotation_radians }, sampling.address_u,
          sampling.address_v, sampling.mag_filter, sampling.min_filter,
          sampling.mip_filter),
        8);
    }
    PrintUtils::Field(ctx.Out(), "Grid Spacing",
      fmt::format("[{:.3f}, {:.3f}]", mat.grid_spacing[0], mat.grid_spacing[1]),
      8);
    PrintUtils::Field(
      ctx.Out(), "Grid Major Every", static_cast<int>(mat.grid_major_every), 8);
    PrintUtils::Field(
      ctx.Out(), "Grid Line Thickness", mat.grid_line_thickness, 8);
    PrintUtils::Field(
      ctx.Out(), "Grid Major Thickness", mat.grid_major_thickness, 8);
    PrintUtils::Field(
      ctx.Out(), "Grid Axis Thickness", mat.grid_axis_thickness, 8);
    PrintUtils::Field(ctx.Out(), "Grid Fade Start", mat.grid_fade_start, 8);
    PrintUtils::Field(ctx.Out(), "Grid Fade End", mat.grid_fade_end, 8);
    PrintUtils::Field(ctx.Out(), "Grid Minor Color",
      fmt::format("[{:.3f}, {:.3f}, {:.3f}, {:.3f}]", mat.grid_minor_color[0],
        mat.grid_minor_color[1], mat.grid_minor_color[2],
        mat.grid_minor_color[3]),
      8);
    PrintUtils::Field(ctx.Out(), "Grid Major Color",
      fmt::format("[{:.3f}, {:.3f}, {:.3f}, {:.3f}]", mat.grid_major_color[0],
        mat.grid_major_color[1], mat.grid_major_color[2],
        mat.grid_major_color[3]),
      8);
    PrintUtils::Field(ctx.Out(), "Grid Axis Color X",
      fmt::format("[{:.3f}, {:.3f}, {:.3f}, {:.3f}]", mat.grid_axis_color_x[0],
        mat.grid_axis_color_x[1], mat.grid_axis_color_x[2],
        mat.grid_axis_color_x[3]),
      8);
    PrintUtils::Field(ctx.Out(), "Grid Axis Color Y",
      fmt::format("[{:.3f}, {:.3f}, {:.3f}, {:.3f}]", mat.grid_axis_color_y[0],
        mat.grid_axis_color_y[1], mat.grid_axis_color_y[2],
        mat.grid_axis_color_y[3]),
      8);
    PrintUtils::Field(ctx.Out(), "Grid Origin Color",
      fmt::format("[{:.3f}, {:.3f}, {:.3f}, {:.3f}]", mat.grid_origin_color[0],
        mat.grid_origin_color[1], mat.grid_origin_color[2],
        mat.grid_origin_color[3]),
      8);
    ctx.Out() << "\n";

    const size_t num_refs = std::popcount(mat.shader_stages);
    if (num_refs == 0) {
      ctx.Out() << "\n";
      co_return;
    }

    const size_t required_bytes
      = sizeof(MaterialAssetDesc) + (num_refs * sizeof(ShaderReferenceDesc));
    if (data->size() < required_bytes) {
      ctx.Out() << "    Shader References (" << num_refs
                << "): (not present in descriptor: need " << required_bytes
                << " bytes, have " << data->size() << ")\n\n";
      co_return;
    }

    ctx.Out() << "    Shader References (" << num_refs << "):\n";

    size_t offset = sizeof(MaterialAssetDesc);
    for (size_t i = 0; i < num_refs; ++i) {
      const auto shader_ref_read = serio::CopyAt<ShaderReferenceDesc>(
        *data, serio::ByteOffset { offset });
      if (!shader_ref_read) {
        ctx.Out() << "      [" << i
                  << "] ShaderReferenceDesc: (insufficient data)\n";
        break;
      }
      const auto& shader_ref = *shader_ref_read;

      ctx.Out() << "      [" << i << "] ShaderReferenceDesc:\n";
      PrintUtils::Field(ctx.Out(), "Shader Type",
        toolkit::EnumString(static_cast<ShaderType>(shader_ref.shader_type)),
        10);
      PrintUtils::Field(ctx.Out(), "Source Path",
        asset_dump_helpers::DescriptorString(
          *data, mat.header, shader_ref.source_path_offset),
        10);
      PrintUtils::Field(ctx.Out(), "Entry Point",
        asset_dump_helpers::DescriptorString(
          *data, mat.header, shader_ref.entry_point_offset),
        10);
      PrintUtils::Field(ctx.Out(), "Defines",
        asset_dump_helpers::DescriptorString(
          *data, mat.header, shader_ref.defines_offset),
        10);
      PrintUtils::Field(ctx.Out(), "Shader Hash",
        asset_dump_helpers::ToHexString(shader_ref.shader_hash), 10);

      if (ctx.show_asset_descriptors) {
        ctx.Out() << "        Hex Dump (offset " << offset << ", size "
                  << sizeof(shader_ref) << "):\n";
        PrintUtils::HexDump(ctx.Out(),
          std::span<const std::byte> { *data }.subspan(
            offset, sizeof(shader_ref)),
          sizeof(shader_ref));
      }

      offset += sizeof(shader_ref);
    }

    ctx.Out() << "\n";
    co_return;
  }
};

} // namespace oxygen::content::pakdump
