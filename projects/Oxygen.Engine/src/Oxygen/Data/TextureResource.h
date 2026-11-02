//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Composition/TypedObject.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/TextureType.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/api_export.h>
#include <Oxygen/Serio/ByteBuffer.h>
#include <Oxygen/Serio/Types.h>

namespace oxygen::data {

//! Texture resource as described in the PAK file resource table.
/*!
 Represents a texture resource referenced by assets in the PAK file. This is not
 a first-class asset: it is not named or globally identified, but is referenced
 by index in the textures resource table from materials or geometry.

 @note `GetPayload()` returns the full payload (header + layouts + data).
 @note `GetData()` returns only the pixel\/block data region.

 @see TextureResourceDesc, MaterialAssetDesc
*/
class TextureResource : public oxygen::Object {
  OXYGEN_TYPED(TextureResource)

public:
  //! Type alias for the descriptor type used by this resource.
  using DescT = pak::core::TextureResourceDesc;

  //! The alignment, in bytes, every texture row records for its payload.
  static constexpr uint16_t kDataAlignment = 256U;

  /*! Constructs a TextureResource that owns `payload`, the buffer its loader
      read the cooked payload into.

      The row must declare a known type that is not multisampled (a row has
      no sample count), a known format and the compression it uses, extents
      its type has, no more mips than its full chain, and `kDataAlignment`.
      The payload must hold its header, its known packing policy and flags,
      one layout per layer and mip (layer-major), and data after the layout
      table. Each layout's row pitch holds at least the row bytes of its
      mip's width in the format, and its size is that pitch times the rows
      of blocks of the mip's height times the mip's depth, within the data.

      @param desc Texture resource descriptor from PAK file.
      @param payload Cooked texture payload (header + layouts + data).
      @throw CookedOpenError When the row or the payload breaks a rule.
  */
  OXGN_DATA_API TextureResource(
    pak::core::TextureResourceDesc desc, serio::ByteBuffer payload);

  ~TextureResource() override = default;

  OXYGEN_MAKE_NON_COPYABLE(TextureResource)

  TextureResource(TextureResource&& other) noexcept
    : Object(std::move(static_cast<Object&>(other)))
    , desc_(other.desc_)
    , payload_header_(other.payload_header_)
    , subresource_layouts_(std::move(other.subresource_layouts_))
    , payload_(std::move(other.payload_))
  {
    other.ResetMovedFromState();
  }

  auto operator=(TextureResource&& other) noexcept -> TextureResource&
  {
    if (this != &other) {
      Object::operator=(std::move(static_cast<Object&>(other)));
      desc_ = other.desc_;
      payload_header_ = other.payload_header_;
      subresource_layouts_ = std::move(other.subresource_layouts_);
      payload_ = std::move(other.payload_);
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
    return GetData().size();
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

  //! Returns the SHA-256 of the resource data, from the descriptor.
  [[nodiscard]] auto GetContentHash() const noexcept
    -> const pak::core::ContentHashDigest&
  {
    return desc_.content_hash;
  }

  //! Returns an immutable span of the pixel/block data region.
  [[nodiscard]] auto GetData() const noexcept -> std::span<const uint8_t>
  {
    return Payload().subspan(payload_header_.data_offset_bytes);
  }

  //! Returns the full cooked payload bytes (header + layouts + data).
  [[nodiscard]] auto GetPayload() const noexcept -> std::span<const uint8_t>
  {
    return Payload();
  }

  //! Returns the parsed payload header.
  [[nodiscard]] auto GetPayloadHeader() const noexcept
    -> const pak::render::TexturePayloadHeader&
  {
    return payload_header_;
  }

  //! Returns the parsed subresource layouts stored in the payload.
  [[nodiscard]] auto GetSubresourceLayouts() const noexcept
    -> std::span<const pak::render::SubresourceLayout>
  {
    return subresource_layouts_;
  }

private:
  pak::core::TextureResourceDesc desc_ {};
  pak::render::TexturePayloadHeader payload_header_ {};
  std::vector<pak::render::SubresourceLayout> subresource_layouts_;
  serio::ByteBuffer payload_;

  //! The payload bytes, as the byte type texture consumers read.
  [[nodiscard]] auto Payload() const noexcept -> std::span<const uint8_t>
  {
    return payload_.Unsigned();
  }

  void ResetMovedFromState() noexcept
  {
    desc_ = {};
    payload_header_ = {};
    subresource_layouts_.clear();
    payload_ = {};
  }
};

} // namespace oxygen::data
