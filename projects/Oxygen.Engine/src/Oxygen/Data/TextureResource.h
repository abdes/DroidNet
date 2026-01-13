//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
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

 ### Resource Descriptor Encoding (PAK v4, 40 bytes)

 ```text
 offset size name             description
 ------ ---- ---------------- ---------------------------------------------
 0x00   8    data_offset      Absolute offset to texture data in PAK file
 0x08   4    size_bytes        Size of cooked texture payload in bytes
 0x0C   1    texture_type     Texture type/dimension (enum)
 0x0D   1    compression_type Compression type (enum)
 0x0E   4    width            Texture width in pixels
 0x12   4    height           Texture height in pixels
 0x16   2    depth            Texture depth (3D/volume), otherwise 1
 0x18   2    array_layers     Array/cubemap layers, otherwise 1
 0x1A   2    mip_levels       Number of mipmap levels
 0x1C   1    format           Texture format enum value
 0x1D   2    alignment        Required alignment (default 256)
 0x1F   8    content_hash     First 8 bytes of SHA256 of pixel/block data
 0x27   1    reserved         Reserved for future use (must be 0)
 ```

 ### Texture Payload Encoding (PAK v4)

 `data_offset` points at a cooked texture payload stored in the textures
 resource data blob. Payloads are **v4-only** and start with a
 `pak::TexturePayloadHeader` (magic `pak::kTexturePayloadMagic`, "OTX1").

 ```text
 offset size name
 ------ ---- -----------------------------
 0x00   28   TexturePayloadHeader
 0x1C   ...  SubresourceLayout[subresource_count]
 ...    ...  Padding up to data_offset_bytes
 ...    ...  Pixel\/block data region
 ```

 `subresource_count` is expected to be `array_layers * mip_levels`.

 @note `GetPayload()` returns the full payload (header + layouts + data).
 @note `GetData()` returns only the pixel\/block data region.

 @see TextureResourceDesc, MaterialAssetDesc
*/
class TextureResource : public oxygen::Object {
  OXYGEN_TYPED(TextureResource)

public:
  //! Type alias for the descriptor type used by this resource.
  using DescT = pak::TextureResourceDesc;

  /*! Constructs a TextureResource with descriptor and exclusive payload
      ownership.
      @param desc Texture resource descriptor from PAK file.
      @param data Cooked texture payload buffer (ownership transferred).
  */
  TextureResource(pak::TextureResourceDesc desc, std::vector<uint8_t> data)
    : desc_(std::move(desc))
    , payload_(std::move(data))
  {
    Validate();
  }

  ~TextureResource() override = default;

  OXYGEN_MAKE_NON_COPYABLE(TextureResource)

  TextureResource(TextureResource&& other) noexcept
    : Object(std::move(other))
    , desc_(std::move(other.desc_))
    , payload_header_(other.payload_header_)
    , subresource_layouts_(std::move(other.subresource_layouts_))
    , payload_(std::move(other.payload_))
    , payload_data_offset_bytes_(other.payload_data_offset_bytes_)
    , payload_data_size_bytes_(other.payload_data_size_bytes_)
  {
    other.ResetMovedFromState();
  }

  auto operator=(TextureResource&& other) noexcept -> TextureResource&
  {
    if (this != &other) {
      Object::operator=(std::move(other));
      desc_ = std::move(other.desc_);
      payload_header_ = other.payload_header_;
      subresource_layouts_ = std::move(other.subresource_layouts_);
      payload_ = std::move(other.payload_);
      payload_data_offset_bytes_ = other.payload_data_offset_bytes_;
      payload_data_size_bytes_ = other.payload_data_size_bytes_;
      other.ResetMovedFromState();
    }
    return *this;
  }

  [[nodiscard]] auto GetDataOffset() const noexcept
  {
    return desc_.data_offset;
  }

  //! Returns the size in bytes of the pixel/block data region (excludes the
  //! payload header and layout table).
  [[nodiscard]] auto GetDataSize() const noexcept -> std::size_t
  {
    return payload_data_size_bytes_;
  }

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

  //! Returns the per-resource content hash from the descriptor.
  [[nodiscard]] auto GetContentHash() const noexcept -> uint64_t
  {
    return desc_.content_hash;
  }

  //! Returns an immutable span of the pixel/block data region.
  [[nodiscard]] auto GetData() const noexcept -> std::span<const uint8_t>
  {
    return std::span<const uint8_t>(
      payload_.data() + payload_data_offset_bytes_, payload_data_size_bytes_);
  }

  //! Returns the full cooked payload bytes (header + layouts + data).
  [[nodiscard]] auto GetPayload() const noexcept -> std::span<const uint8_t>
  {
    return std::span<const uint8_t>(payload_.data(), payload_.size());
  }

  //! Returns the parsed payload header.
  [[nodiscard]] auto GetPayloadHeader() const noexcept
    -> const pak::TexturePayloadHeader&
  {
    return payload_header_;
  }

  //! Returns the parsed subresource layouts stored in the payload.
  [[nodiscard]] auto GetSubresourceLayouts() const noexcept
    -> std::span<const pak::SubresourceLayout>
  {
    return subresource_layouts_;
  }

private:
  pak::TextureResourceDesc desc_ {};
  pak::TexturePayloadHeader payload_header_ {};
  std::vector<pak::SubresourceLayout> subresource_layouts_ {};
  std::vector<uint8_t> payload_;
  std::size_t payload_data_offset_bytes_ = 0;
  std::size_t payload_data_size_bytes_ = 0;

  void ResetMovedFromState() noexcept
  {
    desc_ = {};
    payload_header_ = {};
    subresource_layouts_.clear();
    payload_.clear();
    payload_data_offset_bytes_ = 0;
    payload_data_size_bytes_ = 0;
  }

  //! Parses a v4 format payload (with TexturePayloadHeader).
  void ParseV4Payload()
  {
    const auto payload_size = payload_.size();
    std::memcpy(
      &payload_header_, payload_.data(), sizeof(pak::TexturePayloadHeader));

    const auto expected_subresources = static_cast<std::uint32_t>(
      static_cast<std::uint64_t>(desc_.array_layers) * desc_.mip_levels);
    if (payload_header_.subresource_count != expected_subresources) {
      throw std::invalid_argument(
        "TextureResource: subresource count mismatch");
    }

    if (payload_header_.total_payload_size != payload_size) {
      throw std::invalid_argument(
        "TextureResource: payload size mismatch with header");
    }

    const auto layouts_offset
      = static_cast<std::size_t>(payload_header_.layouts_offset_bytes);
    const auto data_offset
      = static_cast<std::size_t>(payload_header_.data_offset_bytes);
    const auto layout_count
      = static_cast<std::size_t>(payload_header_.subresource_count);
    const auto layouts_bytes = layout_count * sizeof(pak::SubresourceLayout);

    if (layouts_offset < sizeof(pak::TexturePayloadHeader)
      || layouts_offset > payload_size) {
      throw std::invalid_argument("TextureResource: invalid layouts offset");
    }

    if (layouts_bytes > payload_size
      || layouts_offset > payload_size - layouts_bytes) {
      throw std::invalid_argument(
        "TextureResource: layout table exceeds payload bounds");
    }

    if (data_offset < layouts_offset + layouts_bytes
      || data_offset > payload_size) {
      throw std::invalid_argument(
        "TextureResource: invalid data offset in payload header");
    }

    subresource_layouts_.resize(layout_count);
    std::memcpy(subresource_layouts_.data(), payload_.data() + layouts_offset,
      layouts_bytes);

    payload_data_offset_bytes_ = data_offset;
    payload_data_size_bytes_ = payload_size - payload_data_offset_bytes_;

    std::size_t required_data_size = 0;
    for (const auto& layout : subresource_layouts_) {
      const auto offset_in_data = static_cast<std::size_t>(layout.offset_bytes);
      const auto size_bytes = static_cast<std::size_t>(layout.size_bytes);

      if (offset_in_data > payload_data_size_bytes_
        || size_bytes > payload_data_size_bytes_ - offset_in_data) {
        throw std::invalid_argument(
          "TextureResource: subresource layout exceeds payload bounds");
      }

      required_data_size
        = (std::max)(required_data_size, offset_in_data + size_bytes);
    }

    if (required_data_size > payload_data_size_bytes_) {
      throw std::invalid_argument(
        "TextureResource: payload data truncated for subresources");
    }
  }

  void ParsePayload()
  {
    if (payload_.size() < sizeof(pak::TexturePayloadHeader)) {
      throw std::invalid_argument("TextureResource: payload too small");
    }

    uint32_t magic = 0;
    std::memcpy(&magic, payload_.data(), sizeof(magic));
    if (magic != pak::kTexturePayloadMagic) {
      throw std::invalid_argument("TextureResource: invalid payload magic");
    }

    ParseV4Payload();
  }

  void Validate()
  {
    constexpr uint16_t kExpectedTextureAlignmentBytes = 256;
    if (desc_.alignment != kExpectedTextureAlignmentBytes) {
      throw std::invalid_argument(
        "TextureResource: alignment must be 256 bytes");
    }

    ParsePayload();

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

    if (desc_.size_bytes != payload_.size()) {
      throw std::invalid_argument(
        "TextureResource: descriptor size_bytes mismatch with payload size");
    }
  }
};

} // namespace oxygen::data
