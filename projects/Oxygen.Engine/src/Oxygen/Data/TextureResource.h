//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <algorithm>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Composition/TypedObject.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/TextureType.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/api_export.h>

namespace oxygen::data {

//! Texture resource as described in the PAK file resource table.
/*!
 Represents a texture resource referenced by assets in the PAK file. This is not
 a first-class asset: it is not named or globally identified, but is referenced
 by index in the textures resource table from materials or geometry.

 ### Binary Encoding (PAK v1, 40 bytes)

 ```text
 offset size name             description
 ------ ---- ---------------- ---------------------------------------------
 0x00   8    data_offset      Absolute offset to texture data in PAK file
 0x08   4    size_bytes        Size of texture data in bytes
 0x0C   1    texture_type     Texture type/dimension (enum)
 0x0D   1    compression_type Compression type (enum)
 0x0E   4    width            Texture width in pixels
 0x12   4    height           Texture height in pixels
 0x16   2    depth            Texture depth (3D/volume), otherwise 1
 0x18   2    array_layers     Array/cubemap layers, otherwise 1
 0x1A   2    mip_levels       Number of mipmap levels
 0x1C   1    format           Texture format enum value
 0x1D   2    alignment        Required alignment (default 256)
 texture_type) 0x1F   9    reserved         Reserved for future use
 ```

 @see TextureResourceDesc, MaterialAssetDesc
*/
class TextureResource : public oxygen::Object {
  OXYGEN_TYPED(TextureResource)

public:
  //! Type alias for the descriptor type used by this resource.
  using DescT = pak::TextureResourceDesc;

  /*! Constructs a TextureResource with descriptor and exclusive data ownership.
      @param desc Texture resource descriptor from PAK file.
      @param data Raw texture data buffer (ownership transferred).
  */
  TextureResource(pak::TextureResourceDesc desc, std::vector<uint8_t> data)
    : desc_(std::move(desc))
    , data_(std::move(data))
  {
    Validate();
  }

  ~TextureResource() override = default;

  OXYGEN_MAKE_NON_COPYABLE(TextureResource)
  OXYGEN_DEFAULT_MOVABLE(TextureResource)

  [[nodiscard]] auto GetDataOffset() const noexcept
  {
    return desc_.data_offset;
  }

  [[nodiscard]] auto GetDataSize() const noexcept { return data_.size(); }

  OXGN_DATA_NDAPI auto GetTextureType() const noexcept -> TextureType;

  [[nodiscard]] auto GetCompressionType() const noexcept -> uint8_t
  {
    return desc_.compression_type;
  }

  [[nodiscard]] auto GetWidth() const noexcept -> uint32_t
  {
    return desc_.width;
  }

  [[nodiscard]] auto GetHeight() const noexcept -> uint32_t
  {
    return desc_.height;
  }

  [[nodiscard]] auto GetDepth() const noexcept -> uint16_t
  {
    return desc_.depth;
  }

  [[nodiscard]] auto GetArrayLayers() const noexcept -> uint16_t
  {
    return desc_.array_layers;
  }

  [[nodiscard]] auto GetMipCount() const noexcept -> uint16_t
  {
    return desc_.mip_levels;
  }

  OXGN_DATA_NDAPI auto GetFormat() const noexcept -> Format;

  [[nodiscard]] auto GetDataAlignment() const noexcept -> uint16_t
  {
    return desc_.alignment;
  }

  //! Returns an immutable span of the loaded texture data.
  [[nodiscard]] auto GetData() const noexcept -> std::span<const uint8_t>
  {
    return std::span<const uint8_t>(data_.data(), data_.size());
  }

private:
  pak::TextureResourceDesc desc_ {};
  std::vector<uint8_t> data_;

  void Validate()
  {
    // Alignment invariant (spec states 256 for textures)
    if (desc_.alignment != 256) {
      throw std::invalid_argument("TextureResource: alignment must be 256");
    }

    // Basic dimension checks
    if (desc_.width == 0) {
      throw std::invalid_argument("TextureResource: width must be > 0");
    }
    if (desc_.mip_levels == 0) {
      throw std::invalid_argument("TextureResource: mip_levels must be > 0");
    }

    // Height/depth rules per texture type (only enforce obvious invariants)
    switch (static_cast<TextureType>(desc_.texture_type)) {
    case TextureType::kTexture1D:
    case TextureType::kTexture1DArray:
      if (desc_.height != 1) {
        throw std::invalid_argument(
          "TextureResource: 1D textures must have height == 1");
      }
      if (desc_.depth != 1) {
        throw std::invalid_argument(
          "TextureResource: 1D textures must have depth == 1");
      }
      break;
    case TextureType::kTexture2D:
    case TextureType::kTexture2DArray:
    case TextureType::kTexture2DMultiSample:
    case TextureType::kTexture2DMultiSampleArray:
    case TextureType::kTextureCube:
    case TextureType::kTextureCubeArray:
      if (desc_.height == 0) {
        throw std::invalid_argument(
          "TextureResource: 2D-like textures must have height > 0");
      }
      if (desc_.depth != 1) {
        throw std::invalid_argument(
          "TextureResource: 2D-like textures must have depth == 1");
      }
      break;
    case TextureType::kTexture3D:
      if (desc_.height == 0) {
        throw std::invalid_argument(
          "TextureResource: 3D textures must have height > 0");
      }
      if (desc_.depth == 0) {
        throw std::invalid_argument(
          "TextureResource: 3D textures must have depth > 0");
      }
      break;
    default:
      // Unknown enum is allowed (will map to kUnknown); enforce minimal height
      if (desc_.height == 0) {
        throw std::invalid_argument(
          "TextureResource: height must be > 0 for unknown texture type");
      }
      if (desc_.depth == 0) {
        throw std::invalid_argument(
          "TextureResource: depth must be > 0 for unknown texture type");
      }
      break;
    }

    // Array layers must be >= 1
    if (desc_.array_layers == 0) {
      throw std::invalid_argument("TextureResource: array_layers must be >= 1");
    }

    // Mip level upper bound: floor(log2(max_dim)) + 1
    const auto max_dim = (std::max)({ desc_.width, desc_.height,
      static_cast<uint32_t>(desc_.depth == 0 ? 1 : desc_.depth) });
    uint16_t max_mip_levels = 1;
    uint32_t tmp = max_dim;
    while (tmp > 1) {
      tmp >>= 1u;
      ++max_mip_levels;
    }
    if (desc_.mip_levels > max_mip_levels) {
      throw std::invalid_argument("TextureResource: mip_levels exceed limit");
    }

    // Data size consistency
    if (desc_.size_bytes != data_.size()) {
      throw std::invalid_argument(
        "TextureResource: descriptor size_bytes mismatch with data size");
    }
  }
};

} // namespace oxygen::data
