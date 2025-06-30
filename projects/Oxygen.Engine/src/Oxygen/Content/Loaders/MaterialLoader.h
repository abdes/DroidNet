//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Base/Reader.h>
#include <Oxygen/Base/Stream.h>
#include <Oxygen/Data/MaterialAsset.h>

namespace oxygen::content::loaders {

//! Loader for texture assets.
template <oxygen::serio::Stream S>
auto LoadMaterialAsset(oxygen::serio::Reader<S> reader)
  -> std::unique_ptr<data::MaterialAsset>
{
  LOG_SCOPE_FUNCTION(INFO);

#pragma pack(push, 1)
  struct MaterialAssetHeader {
    uint32_t material_type;
    uint32_t shader_stages;
    uint32_t texture_count;
    uint8_t reserved[52];
    // Followed by:
    // - array of shader asset IDs (uint64_t[count_of(set bits in
    // shader_stages)])
    // - array of texture asset IDs (uint64_t[texture_count])
  };
#pragma pack(pop)
  static_assert(sizeof(MaterialAssetHeader) == 64);

  auto check_result = [](auto&& result, const char* field) {
    if (!result) {
      LOG_F(INFO, "-failed- on {}: {}", field, result.error().message());
      throw std::runtime_error(
        fmt::format("error reading material asset ({}): {}", field,
          result.error().message()));
    }
  };

  // Read material header
  auto result = reader.read<MaterialAssetHeader>();
  check_result(result, "material header");
  const auto& mat_header = result.value();
  LOG_F(INFO, "material type : {}", mat_header.material_type);
  LOG_F(INFO, "shader stages : {}", mat_header.shader_stages);

  std::vector<uint64_t> shader_ids;
  // Shader asset IDs follow header, then texture asset IDs
  {
    const uint32_t shader_id_count = std::popcount(mat_header.shader_stages);
    DCHECK_GT_F(shader_id_count, 0U); // at least one shader
    LOG_SCOPE_F(INFO, fmt::format("Shaders ({})", shader_id_count).c_str());
    shader_ids.resize(shader_id_count);
    if (shader_id_count > 0) {
      auto shader_result = reader.read_blob_to(
        std::as_writable_bytes(std::span<uint64_t>(shader_ids)));
      check_result(shader_result, "shaders");
    }
    for (size_t i = 0; i < shader_ids.size(); ++i) {
      LOG_F(INFO, "{}", shader_ids[i]);
    }
  }

  std::vector<uint64_t> texture_ids(mat_header.texture_count);
  // then texture asset IDs
  if (mat_header.texture_count > 0) {
    LOG_SCOPE_F(
      INFO, fmt::format("Textures ({})", mat_header.texture_count).c_str());
    auto texture_result = reader.read_blob_to(
      std::as_writable_bytes(std::span<uint64_t>(texture_ids)));
    check_result(texture_result, "textures");
    for (size_t i = 0; i < mat_header.texture_count; ++i) {
      LOG_F(INFO, "{}", texture_ids[i]);
    }
  }

  return std::make_unique<data::MaterialAsset>(mat_header.material_type,
    mat_header.shader_stages, mat_header.texture_count, std::move(shader_ids),
    std::move(texture_ids));
}

} // namespace oxygen::content::loaders
