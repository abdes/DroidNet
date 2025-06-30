//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Composition/TypedObject.h>

namespace oxygen::data {

//! A material asset as stored in the PAK file.
/*!
 ### Binary Encoding:
 ```text
 offset size name           description
 ------ ---- -------------- ----------------------------------------------------
 0x00   4    material_type  Material type (e.g. Opaque, Transparent)
 0x04   4    shader_stages  32 bit Bitset, each bit maps to a shader stage
 0x08   4    texture_count  Number of bound textures
 0x0C   52   reserved       Reserved/padding to 64 bytes
 0x40   ...  shader asset IDs (uint64_t[count_of(set bits in shader_stages)])
 0x...  ...  texture asset IDs (uint64_t[texture_count])
 ```

 @note Packed to 64 bytes total. Not aligned.
 @note Each bit in `shader_flags`, when set indicates that this material applies
 in that particular stage, and a corresponding shader ID will be present in the
 shader assetIDs table that follows this MaterialAssetHeader.

 @see TextureAsset, ShaderAsset
*/
class MaterialAsset : public oxygen::Object {
  OXYGEN_TYPED(MaterialAsset)

public:
  MaterialAsset() = default;
  MaterialAsset(uint32_t material_type, uint32_t shader_stages,
    uint32_t texture_count, std::vector<uint64_t> shader_ids,
    std::vector<uint64_t> texture_ids)
    : material_type_(material_type)
    , shader_stages_(shader_stages)
    , texture_count_(texture_count)
    , shader_ids_(std::move(shader_ids))
    , texture_ids_(std::move(texture_ids))
  {
  }
  ~MaterialAsset() override = default;

  OXYGEN_MAKE_NON_COPYABLE(MaterialAsset)
  OXYGEN_DEFAULT_MOVABLE(MaterialAsset)

  [[nodiscard]] auto GetMaterialType() const noexcept -> uint32_t
  {
    return material_type_;
  }
  [[nodiscard]] auto GetShaderStages() const noexcept -> uint32_t
  {
    return shader_stages_;
  }
  [[nodiscard]] auto GetTextureCount() const noexcept -> uint32_t
  {
    return texture_count_;
  }
  [[nodiscard]] auto GetShaderIds() const noexcept -> std::span<const uint64_t>
  {
    return shader_ids_;
  }
  [[nodiscard]] auto GetTextureIds() const noexcept -> std::span<const uint64_t>
  {
    return texture_ids_;
  }

private:
  uint32_t material_type_;
  uint32_t shader_stages_;
  uint32_t texture_count_;
  std::vector<uint64_t> shader_ids_;
  std::vector<uint64_t> texture_ids_;
};

} // namespace oxygen::data
