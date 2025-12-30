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
    const oxygen::data::pak::v1::AssetDirectoryEntry& entry, DumpContext& ctx,
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
    PrintUtils::Field("Metalness", mat.metalness, 8);
    PrintUtils::Field("Roughness", mat.roughness, 8);
    PrintUtils::Field("Ambient Occlusion", mat.ambient_occlusion, 8);
    PrintUtils::Field("Base Color Texture", mat.base_color_texture, 8);
    PrintUtils::Field("Normal Texture", mat.normal_texture, 8);
    PrintUtils::Field("Metallic Texture", mat.metallic_texture, 8);
    PrintUtils::Field("Roughness Texture", mat.roughness_texture, 8);
    PrintUtils::Field(
      "Ambient Occlusion Texture", mat.ambient_occlusion_texture, 8);
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
      PrintUtils::Field("Unique ID",
        std::string(shader_ref.shader_unique_id,
          strnlen(
            shader_ref.shader_unique_id, sizeof(shader_ref.shader_unique_id))),
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
