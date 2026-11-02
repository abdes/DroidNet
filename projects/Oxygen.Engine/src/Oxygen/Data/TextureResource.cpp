//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

#include <fmt/format.h>

#include <Oxygen/Core/Detail/FormatUtils.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/TextureType.h>
#include <Oxygen/Data/CookedOpenError.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Data/PakFormat_render.h>
#include <Oxygen/Data/TextureResource.h>
#include <Oxygen/Serio/ByteBuffer.h>
#include <Oxygen/Serio/ByteReader.h>
#include <Oxygen/Serio/Types.h>

using oxygen::data::TextureResource;

namespace {

namespace core = oxygen::data::pak::core;
namespace render = oxygen::data::pak::render;
using oxygen::Format;
using oxygen::TextureType;
using oxygen::data::CookedDefect;
using oxygen::data::CookedOpenError;

auto CheckType(const core::TextureResourceDesc& desc) -> void
{
  const auto type = static_cast<TextureType>(desc.texture_type);
  const auto known
    = desc.texture_type > std::to_underlying(TextureType::kUnknown)
    && desc.texture_type <= std::to_underlying(TextureType::kMaxTextureType);
  if (!known) {
    throw CookedOpenError(CookedDefect::kInvalidValue,
      fmt::format("texture type {} is unknown", desc.texture_type));
  }
  const auto multisampled = type == TextureType::kTexture2DMultiSample
    || type == TextureType::kTexture2DMultiSampleArray;
  if (multisampled) {
    throw CookedOpenError(CookedDefect::kInvalidValue,
      "a texture row has no sample count to describe a multisampled texture");
  }
}

auto CheckFormat(const core::TextureResourceDesc& desc) -> void
{
  const auto format = static_cast<Format>(desc.format);
  const auto has_blocks
    = oxygen::graphics::detail::GetFormatInfo(format).bytes_per_block != 0U;
  if (!has_blocks) {
    throw CookedOpenError(CookedDefect::kInvalidValue,
      fmt::format("texture format {} is unknown", desc.format));
  }
  if (!core::HasSupportedCompression(desc)) {
    throw CookedOpenError(CookedDefect::kInvalidValue,
      fmt::format("texture compression {} is not the compression of format {}",
        desc.compression_type, desc.format));
  }
}

//! Whether the extents of `desc` are those its type has.
auto HasExtentsOfItsType(const core::TextureResourceDesc& desc) -> bool
{
  const auto flat = desc.depth == 1U;
  const auto one_row = desc.height == 1U;
  const auto one_layer = desc.array_layers == 1U;
  const auto square = desc.width == desc.height;
  switch (static_cast<TextureType>(desc.texture_type)) {
  case TextureType::kTexture1D:
    return one_row && flat && one_layer;
  case TextureType::kTexture1DArray:
    return one_row && flat;
  case TextureType::kTexture2D:
    return flat && one_layer;
  case TextureType::kTexture2DArray:
    return flat;
  case TextureType::kTextureCube:
    return flat && square && desc.array_layers == oxygen::kCubeFaceCount;
  case TextureType::kTextureCubeArray:
    return flat && square && desc.array_layers % oxygen::kCubeFaceCount == 0U;
  case TextureType::kTexture3D:
    return one_layer;
  default:
    return false;
  }
}

auto CheckExtents(const core::TextureResourceDesc& desc) -> void
{
  const auto empty = desc.width == 0U || desc.height == 0U || desc.depth == 0U
    || desc.array_layers == 0U || desc.mip_levels == 0U;
  if (empty) {
    throw CookedOpenError(CookedDefect::kInvalidValue,
      fmt::format("texture extents {}x{}x{}, {} layers, {} mips are empty",
        desc.width, desc.height, desc.depth, desc.array_layers,
        desc.mip_levels));
  }
  if (!HasExtentsOfItsType(desc)) {
    throw CookedOpenError(CookedDefect::kInconsistent,
      fmt::format(
        "texture extents {}x{}x{}, {} layers are not those of type {}",
        desc.width, desc.height, desc.depth, desc.array_layers,
        desc.texture_type));
  }
  const auto chain
    = oxygen::MipChainLength(desc.width, desc.height, desc.depth);
  if (std::cmp_greater(desc.mip_levels, chain)) {
    throw CookedOpenError(CookedDefect::kInvalidValue,
      fmt::format("texture has {} mips, its chain {}", desc.mip_levels, chain));
  }
}

auto CheckRow(const core::TextureResourceDesc& desc, const size_t payload_size)
  -> void
{
  CheckType(desc);
  CheckFormat(desc);
  CheckExtents(desc);
  if (desc.alignment != TextureResource::kDataAlignment) {
    throw CookedOpenError(CookedDefect::kInvalidValue,
      fmt::format("texture alignment {} is not {}", desc.alignment,
        TextureResource::kDataAlignment));
  }
  if (desc.size_bytes != payload_size) {
    throw CookedOpenError(CookedDefect::kSizeMismatch,
      fmt::format("texture row names {} bytes, its payload holds {}",
        desc.size_bytes, payload_size));
  }
}

auto ReadHeader(const core::TextureResourceDesc& desc,
  const std::span<const std::byte> payload) -> render::TexturePayloadHeader
{
  auto reader = oxygen::serio::ByteReader { payload };
  const auto read = reader.Read<render::TexturePayloadHeader>();
  if (!read) {
    throw CookedOpenError(CookedDefect::kTruncated,
      fmt::format(
        "texture payload of {} bytes holds no header", payload.size()));
  }
  const auto& header = *read;
  if (header.magic != render::kTexturePayloadMagic) {
    throw CookedOpenError(CookedDefect::kWrongMagic,
      fmt::format("texture payload magic 0x{:08X} is not OTX1", header.magic));
  }
  const auto known_policy = header.packing_policy
      == std::to_underlying(render::TexturePackingPolicyId::kD3D12)
    || header.packing_policy
      == std::to_underlying(render::TexturePackingPolicyId::kTightPacked);
  if (!known_policy) {
    throw CookedOpenError(CookedDefect::kInvalidValue,
      fmt::format(
        "texture packing policy {} is unknown", header.packing_policy));
  }
  // Uncompressed tail mips are reserved: no reader lays them out.
  constexpr auto kReadFlags
    = std::to_underlying(render::TexturePayloadFlags::kPremultipliedAlpha);
  const auto unread_flags = header.flags & ~uint32_t { kReadFlags };
  if (unread_flags != 0U) {
    throw CookedOpenError(CookedDefect::kInvalidValue,
      fmt::format("texture payload flags 0x{:02X} are unknown or reserved",
        unread_flags));
  }
  const auto subresources = uint32_t { desc.array_layers } * desc.mip_levels;
  if (header.subresource_count != subresources) {
    throw CookedOpenError(CookedDefect::kInconsistent,
      fmt::format("texture payload has {} subresources, its row {}",
        header.subresource_count, subresources));
  }
  if (header.total_payload_size != payload.size()) {
    throw CookedOpenError(CookedDefect::kSizeMismatch,
      fmt::format("texture payload header names {} bytes, the payload holds {}",
        header.total_payload_size, payload.size()));
  }
  const auto layouts_end = uint64_t { header.layouts_offset_bytes }
    + (uint64_t { header.subresource_count }
      * sizeof(render::SubresourceLayout));
  const auto table_in_payload
    = header.layouts_offset_bytes >= sizeof(render::TexturePayloadHeader)
    && layouts_end <= payload.size();
  if (!table_in_payload) {
    throw CookedOpenError(CookedDefect::kOutOfBounds,
      "texture subresource layouts lie outside the payload");
  }
  const auto data_after_table = header.data_offset_bytes >= layouts_end
    && header.data_offset_bytes <= payload.size();
  if (!data_after_table) {
    throw CookedOpenError(CookedDefect::kOutOfBounds,
      "texture data does not follow its subresource layouts");
  }
  return header;
}

auto ReadLayouts(const render::TexturePayloadHeader& header,
  const std::span<const std::byte> payload)
  -> std::vector<render::SubresourceLayout>
{
  auto reader = oxygen::serio::ByteReader { payload };
  if (!reader.Seek(oxygen::serio::ByteOffset { header.layouts_offset_bytes })) {
    throw CookedOpenError(CookedDefect::kOutOfBounds,
      "texture subresource layouts lie outside the payload");
  }
  auto layouts = std::vector<render::SubresourceLayout> {};
  layouts.reserve(header.subresource_count);
  for (uint32_t index = 0U; index < header.subresource_count; ++index) {
    const auto layout = reader.Read<render::SubresourceLayout>();
    if (!layout) {
      throw CookedOpenError(CookedDefect::kOutOfBounds,
        "texture subresource layouts lie outside the payload");
    }
    layouts.push_back(*layout);
  }
  return layouts;
}

//! Checks that each layout, layer-major, holds the rows of its mip.
auto CheckSubresources(const core::TextureResourceDesc& desc,
  const std::span<const render::SubresourceLayout> layouts,
  const size_t data_size) -> void
{
  const auto format = static_cast<Format>(desc.format);
  for (uint32_t layer = 0U; layer < desc.array_layers; ++layer) {
    for (uint32_t mip = 0U; mip < desc.mip_levels; ++mip) {
      const auto index = (layer * desc.mip_levels) + mip;
      const auto& layout = layouts[index];
      const auto row_bytes = oxygen::graphics::detail::RowBytes(
        format, oxygen::MipExtent(desc.width, mip));
      if (layout.row_pitch_bytes < row_bytes) {
        throw CookedOpenError(CookedDefect::kInconsistent,
          fmt::format("texture layer {} mip {} has a row pitch of {} bytes, "
                      "below its {} row bytes",
            layer, mip, layout.row_pitch_bytes, row_bytes));
      }
      const auto rows = uint64_t {
        oxygen::graphics::detail::BlockRows(
          format, oxygen::MipExtent(desc.height, mip)),
      } * oxygen::MipExtent(desc.depth, mip);
      const auto holds = uint64_t { layout.row_pitch_bytes } * rows;
      if (layout.size_bytes != holds) {
        throw CookedOpenError(CookedDefect::kSizeMismatch,
          fmt::format("texture layer {} mip {} has {} bytes, its rows {}",
            layer, mip, layout.size_bytes, holds));
      }
      const auto in_data
        = uint64_t { layout.offset_bytes } + layout.size_bytes <= data_size;
      if (!in_data) {
        throw CookedOpenError(CookedDefect::kOutOfBounds,
          fmt::format(
            "texture layer {} mip {} lies outside the data", layer, mip));
      }
    }
  }
}

} // namespace

TextureResource::TextureResource(
  pak::core::TextureResourceDesc desc, serio::ByteBuffer payload)
  : desc_(desc)
  , payload_(std::move(payload))
{
  const auto bytes = payload_.Bytes();
  CheckRow(desc_, bytes.size());
  payload_header_ = ReadHeader(desc_, bytes);
  subresource_layouts_ = ReadLayouts(payload_header_, bytes);
  CheckSubresources(desc_, subresource_layouts_, GetDataSize());
}

auto TextureResource::GetTextureType() const noexcept -> TextureType
{
  return static_cast<TextureType>(desc_.texture_type);
}

auto TextureResource::GetFormat() const noexcept -> Format
{
  return static_cast<Format>(desc_.format);
}
