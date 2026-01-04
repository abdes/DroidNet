//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <bit>
#include <cstddef>
#include <cstring>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include <fmt/format.h>

#include <Oxygen/Data/PakFormat.h>

#include "AssetDumpHelpers.h"
#include "AssetDumper.h"

namespace oxygen::content::pakdump {

//! Dumps material asset descriptors.
class MaterialAssetDumper final : public AssetDumper {
public:
  void Dump(const oxygen::content::PakFile& pak,
    const oxygen::data::pak::v2::AssetDirectoryEntry& entry, DumpContext& ctx,
    const size_t idx) const override
  {
    using oxygen::data::pak::MaterialAssetDesc;
    using oxygen::data::pak::ShaderReferenceDesc;

    std::cout << "Asset #" << idx << ":\n";
    asset_dump_helpers::PrintAssetKey(entry.asset_key, ctx);
    asset_dump_helpers::PrintAssetMetadata(entry);

    const auto data = asset_dump_helpers::ReadDescriptorBytes(pak, entry);
    if (!data) {
      std::cout << "    Failed to read asset descriptor data\n\n";
      return;
    }

    asset_dump_helpers::PrintAssetDescriptorHexPreview(*data, ctx);
    if (data->size() < sizeof(MaterialAssetDesc)) {
      std::cout << "    MaterialAssetDesc: (insufficient data)\n\n";
      return;
    }

    MaterialAssetDesc mat {};
    std::memcpy(&mat, data->data(), sizeof(mat));

    std::cout << "    --- Material Descriptor Fields ---\n";
    asset_dump_helpers::PrintAssetHeaderFields(mat.header);

    PrintUtils::Field(
      "Material Domain", static_cast<int>(mat.material_domain), 8);
    PrintUtils::Field("Flags", asset_dump_helpers::ToHexString(mat.flags), 8);
    PrintUtils::Field(
      "Shader Stages", asset_dump_helpers::ToHexString(mat.shader_stages), 8);
    PrintUtils::Field("Base Color",
      fmt::format("[{:.3f}, {:.3f}, {:.3f}, {:.3f}]", mat.base_color[0],
        mat.base_color[1], mat.base_color[2], mat.base_color[3]),
      8);
    PrintUtils::Field("Normal Scale", mat.normal_scale, 8);
    PrintUtils::Field("Metalness", mat.metalness.ToFloat(), 8);
    PrintUtils::Field("Roughness", mat.roughness.ToFloat(), 8);
    PrintUtils::Field("Ambient Occlusion", mat.ambient_occlusion.ToFloat(), 8);
    PrintUtils::Field("Base Color Texture", mat.base_color_texture, 8);
    PrintUtils::Field("Normal Texture", mat.normal_texture, 8);
    PrintUtils::Field("Metallic Texture", mat.metallic_texture, 8);
    PrintUtils::Field("Roughness Texture", mat.roughness_texture, 8);
    PrintUtils::Field(
      "Ambient Occlusion Texture", mat.ambient_occlusion_texture, 8);

    PrintUtils::Field("Emissive Texture", mat.emissive_texture, 8);
    PrintUtils::Field("Specular Texture", mat.specular_texture, 8);
    PrintUtils::Field("Sheen Color Texture", mat.sheen_color_texture, 8);
    PrintUtils::Field("Clearcoat Texture", mat.clearcoat_texture, 8);
    PrintUtils::Field(
      "Clearcoat Normal Texture", mat.clearcoat_normal_texture, 8);
    PrintUtils::Field("Transmission Texture", mat.transmission_texture, 8);
    PrintUtils::Field("Thickness Texture", mat.thickness_texture, 8);

    PrintUtils::Field("Emissive Factor",
      fmt::format("[{:.3f}, {:.3f}, {:.3f}]", mat.emissive_factor[0].ToFloat(),
        mat.emissive_factor[1].ToFloat(), mat.emissive_factor[2].ToFloat()),
      8);
    PrintUtils::Field("Alpha Cutoff", mat.alpha_cutoff.ToFloat(), 8);
    PrintUtils::Field("IOR", mat.ior, 8);
    PrintUtils::Field("Specular Factor", mat.specular_factor.ToFloat(), 8);
    PrintUtils::Field("Sheen Color Factor",
      fmt::format("[{:.3f}, {:.3f}, {:.3f}]",
        mat.sheen_color_factor[0].ToFloat(),
        mat.sheen_color_factor[1].ToFloat(),
        mat.sheen_color_factor[2].ToFloat()),
      8);
    PrintUtils::Field("Clearcoat Factor", mat.clearcoat_factor.ToFloat(), 8);
    PrintUtils::Field(
      "Clearcoat Roughness", mat.clearcoat_roughness.ToFloat(), 8);
    PrintUtils::Field(
      "Transmission Factor", mat.transmission_factor.ToFloat(), 8);
    PrintUtils::Field("Thickness Factor", mat.thickness_factor.ToFloat(), 8);
    PrintUtils::Field("Attenuation Color",
      fmt::format("[{:.3f}, {:.3f}, {:.3f}]",
        mat.attenuation_color[0].ToFloat(), mat.attenuation_color[1].ToFloat(),
        mat.attenuation_color[2].ToFloat()),
      8);
    PrintUtils::Field("Attenuation Distance", mat.attenuation_distance, 8);
    std::cout << "\n";

    const size_t num_refs = std::popcount(mat.shader_stages);
    if (num_refs == 0) {
      std::cout << "\n";
      return;
    }

    const size_t required_bytes
      = sizeof(MaterialAssetDesc) + num_refs * sizeof(ShaderReferenceDesc);
    if (data->size() < required_bytes) {
      std::cout << "    Shader References (" << num_refs
                << "): (not present in descriptor: need " << required_bytes
                << " bytes, have " << data->size() << ")\n\n";
      return;
    }

    std::cout << "    Shader References (" << num_refs << "):\n";

    size_t offset = sizeof(MaterialAssetDesc);
    for (size_t i = 0; i < num_refs; ++i) {
      ShaderReferenceDesc shader_ref {};
      if (!asset_dump_helpers::ReadStructAt(
            *data, offset, sizeof(shader_ref), &shader_ref)) {
        std::cout << "      [" << i
                  << "] ShaderReferenceDesc: (insufficient data)\n";
        break;
      }

      std::cout << "      [" << i << "] ShaderReferenceDesc:\n";
      PrintUtils::Field(
        "Shader Type", static_cast<int>(shader_ref.shader_type), 10);
      PrintUtils::Field("Source Path",
        std::string(shader_ref.source_path,
          strnlen(shader_ref.source_path, sizeof(shader_ref.source_path))),
        10);
      PrintUtils::Field("Entry Point",
        std::string(shader_ref.entry_point,
          strnlen(shader_ref.entry_point, sizeof(shader_ref.entry_point))),
        10);
      PrintUtils::Field("Defines",
        std::string(shader_ref.defines,
          strnlen(shader_ref.defines, sizeof(shader_ref.defines))),
        10);
      PrintUtils::Field("Shader Hash",
        asset_dump_helpers::ToHexString(shader_ref.shader_hash), 10);

      if (ctx.show_asset_descriptors) {
        std::cout << "        Hex Dump (offset " << offset << ", size "
                  << sizeof(shader_ref) << "):\n";
        PrintUtils::HexDump(
          reinterpret_cast<const uint8_t*>(data->data()) + offset,
          sizeof(shader_ref), sizeof(shader_ref));
      }

      offset += sizeof(shader_ref);
    }

    std::cout << "\n";
  }
};

} // namespace oxygen::content::pakdump
