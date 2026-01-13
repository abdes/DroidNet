//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <memory>
#include <span>
#include <type_traits>
#include <vector>

#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/TextureType.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/TextureResource.h>

namespace oxygen::renderer::testing {

namespace detail {

  [[nodiscard]] inline auto AlignUpSize(const std::size_t value,
    const std::size_t alignment) noexcept -> std::size_t
  {
    if (alignment == 0U) {
      return value;
    }

    const auto mask = alignment - 1U;
    if ((alignment & mask) != 0U) {
      return value;
    }

    return (value + mask) & ~mask;
  }

  inline auto CopyBytes(const std::span<std::byte> destination,
    const std::span<const std::byte> source) noexcept -> void
  {
    std::ranges::copy(source, destination.begin());
  }

  template <typename T>
  [[nodiscard]] inline auto ReadTrivial(
    const std::span<const std::byte> bytes) noexcept -> T
  {
    static_assert(std::is_trivially_copyable_v<T>);

    std::array<std::byte, sizeof(T)> storage {};
    CopyBytes(storage, bytes.subspan(0, storage.size()));
    return std::bit_cast<T>(storage);
  }

  template <typename T>
  inline auto WriteTrivial(const std::span<std::byte> destination,
    const std::size_t offset, const T& value) noexcept -> void
  {
    static_assert(std::is_trivially_copyable_v<T>);

    if (offset + sizeof(T) > destination.size()) {
      return;
    }

    std::array<std::byte, sizeof(T)> storage {};
    storage = std::bit_cast<std::array<std::byte, sizeof(T)>>(value);
    CopyBytes(destination.subspan(offset, storage.size()), storage);
  }

  [[nodiscard]] inline auto BuildV4TexturePayload(
    const data::pak::TextureResourceDesc& desc,
    const std::span<const data::pak::SubresourceLayout> layouts,
    const std::span<const std::uint8_t> data_region) -> std::vector<uint8_t>
  {
    using data::pak::SubresourceLayout;
    using data::pak::TexturePayloadHeader;

    constexpr std::size_t kDataOffsetAlignment = 256U;

    const auto layout_bytes = layouts.size() * sizeof(SubresourceLayout);
    const auto layouts_offset = sizeof(TexturePayloadHeader);
    const auto unaligned_data_offset = layouts_offset + layout_bytes;
    const auto data_offset
      = AlignUpSize(unaligned_data_offset, kDataOffsetAlignment);

    const auto total_payload_size = data_offset + data_region.size();

    TexturePayloadHeader header {};
    header.magic = data::pak::kTexturePayloadMagic;
    header.packing_policy = 0U;
    header.flags = 0U;
    header.subresource_count = static_cast<std::uint16_t>(layouts.size());
    header.total_payload_size = static_cast<std::uint32_t>(total_payload_size);
    header.layouts_offset_bytes = static_cast<std::uint32_t>(layouts_offset);
    header.data_offset_bytes = static_cast<std::uint32_t>(data_offset);
    header.content_hash = desc.content_hash;

    std::vector<std::uint8_t> payload(total_payload_size);
    const auto payload_bytes = std::as_writable_bytes(std::span { payload });

    WriteTrivial(payload_bytes, 0U, header);

    if (!layouts.empty()) {
      CopyBytes(payload_bytes.subspan(layouts_offset, layout_bytes),
        std::as_bytes(layouts));
    }

    if (!data_region.empty()) {
      CopyBytes(payload_bytes.subspan(data_offset, data_region.size()),
        std::as_bytes(data_region));
    }

    return payload;
  }

} // namespace detail

//! Decode a cooked TextureResource payload into a CPU-side TextureResource.
/*! The payload must begin with a `data::pak::TextureResourceDesc` and contain
    `desc.size_bytes` bytes of data starting at `desc.data_offset`.
*/
[[nodiscard]] inline auto DecodeCookedTexturePayload(
  const std::span<const uint8_t> payload)
  -> std::shared_ptr<data::TextureResource>
{
  using data::pak::TextureResourceDesc;

  if (payload.size() < sizeof(TextureResourceDesc)) {
    return nullptr;
  }

  const auto payload_bytes = std::as_bytes(payload);
  const auto desc = detail::ReadTrivial<TextureResourceDesc>(
    payload_bytes.subspan(0, sizeof(TextureResourceDesc)));

  const auto data_offset = desc.data_offset;
  const auto data_size = static_cast<std::size_t>(desc.size_bytes);

  if (data_offset > payload.size()
    || payload.size() - data_offset < data_size) {
    return nullptr;
  }
  std::vector<uint8_t> data(data_size);
  const auto slice = payload.subspan(data_offset, data_size);
  detail::CopyBytes(
    std::as_writable_bytes(std::span { data }), std::as_bytes(slice));

  try {
    return std::make_shared<data::TextureResource>(desc, std::move(data));
  } catch (...) {
    return nullptr;
  }
}

[[nodiscard]] inline auto MakeCookedTexture1x1Rgba8Payload()
  -> std::vector<uint8_t>
{
  using data::pak::SubresourceLayout;
  using data::pak::TextureResourceDesc;

  TextureResourceDesc desc {};
  desc.data_offset = sizeof(desc);
  // desc.size_bytes set after v4 payload is built.
  desc.texture_type = static_cast<uint8_t>(TextureType::kTexture2D);
  desc.compression_type = 0;
  desc.width = 1U;
  desc.height = 1U;
  desc.depth = 1U;
  desc.array_layers = 1U;
  desc.mip_levels = 1U;
  desc.format = static_cast<uint8_t>(Format::kRGBA8UNorm);
  desc.alignment = 256U; // NOLINT(*-magic-numbers)

  constexpr std::array<uint8_t, 4> pixel { 0xFF, 0xFF, 0xFF, 0xFF };

  // Cooked data uses a 256-byte row pitch for D3D12-compatible copies.
  std::vector<std::uint8_t> data_region(256U); // NOLINT(*-magic-numbers)
  detail::CopyBytes(
    std::as_writable_bytes(std::span { data_region }).subspan(0, pixel.size()),
    std::as_bytes(std::span { pixel }));

  const std::array<SubresourceLayout, 1> layouts {
    SubresourceLayout {
      .offset_bytes = 0U,
      .row_pitch_bytes = 256U, // NOLINT(*-magic-numbers)
      .size_bytes = 256U, // NOLINT(*-magic-numbers)
    },
  };

  auto payload = detail::BuildV4TexturePayload(desc, layouts, data_region);
  desc.size_bytes = static_cast<std::uint32_t>(payload.size());

  std::vector<uint8_t> bytes;
  bytes.resize(sizeof(desc) + payload.size());
  const auto bytes_span = std::as_writable_bytes(std::span { bytes });
  detail::CopyBytes(
    bytes_span.subspan(0, sizeof(desc)), std::as_bytes(std::span { &desc, 1 }));
  detail::CopyBytes(bytes_span.subspan(sizeof(desc), payload.size()),
    std::as_bytes(std::span { payload }));
  return bytes;
}

[[nodiscard]] inline auto MakeTightPackedTexture1x1Rgba8Payload()
  -> std::vector<uint8_t>
{
  using data::pak::SubresourceLayout;
  using data::pak::TextureResourceDesc;

  TextureResourceDesc desc {};
  desc.data_offset = sizeof(desc);
  // desc.size_bytes set after v4 payload is built.
  desc.texture_type = static_cast<uint8_t>(TextureType::kTexture2D);
  desc.compression_type = 0;
  desc.width = 1U;
  desc.height = 1U;
  desc.depth = 1U;
  desc.array_layers = 1U;
  desc.mip_levels = 1U;
  desc.format = static_cast<uint8_t>(Format::kRGBA8UNorm);
  desc.alignment = 256U; // NOLINT(*-magic-numbers)

  constexpr std::array<uint8_t, 4> pixel { 0xFF, 0xFF, 0xFF, 0xFF };

  // Tight-packed (row pitch 4): valid cooked payload.
  const std::vector<std::uint8_t> data_region(pixel.begin(), pixel.end());
  const std::array<SubresourceLayout, 1> layouts {
    SubresourceLayout {
      .offset_bytes = 0U,
      .row_pitch_bytes = 4U,
      .size_bytes = 4U,
    },
  };

  auto payload = detail::BuildV4TexturePayload(desc, layouts, data_region);
  desc.size_bytes = static_cast<std::uint32_t>(payload.size());

  std::vector<uint8_t> bytes;
  bytes.resize(sizeof(desc) + payload.size());
  const auto bytes_span = std::as_writable_bytes(std::span { bytes });
  detail::CopyBytes(
    bytes_span.subspan(0, sizeof(desc)), std::as_bytes(std::span { &desc, 1 }));
  detail::CopyBytes(bytes_span.subspan(sizeof(desc), payload.size()),
    std::as_bytes(std::span { payload }));
  return bytes;
}

[[nodiscard]] inline auto MakeInvalidTexture1x1Rgba8Payload_RowPitchTooSmall()
  -> std::vector<uint8_t>
{
  using data::pak::SubresourceLayout;
  using data::pak::TextureResourceDesc;

  TextureResourceDesc desc {};
  desc.data_offset = sizeof(desc);
  // desc.size_bytes set after v4 payload is built.
  desc.texture_type = static_cast<uint8_t>(TextureType::kTexture2D);
  desc.compression_type = 0;
  desc.width = 1U;
  desc.height = 1U;
  desc.depth = 1U;
  desc.array_layers = 1U;
  desc.mip_levels = 1U;
  desc.format = static_cast<uint8_t>(Format::kRGBA8UNorm);
  desc.alignment = 256U; // NOLINT(*-magic-numbers)

  // Invalid: row pitch is smaller than bytes_per_row (RGBA8 => 4 bytes).
  const std::vector<std::uint8_t> data_region { 0xFF, 0xFF, 0xFF };
  const std::array<SubresourceLayout, 1> layouts {
    SubresourceLayout {
      .offset_bytes = 0U,
      .row_pitch_bytes = 3U,
      .size_bytes = 3U,
    },
  };

  auto payload = detail::BuildV4TexturePayload(desc, layouts, data_region);
  desc.size_bytes = static_cast<std::uint32_t>(payload.size());

  std::vector<uint8_t> bytes;
  bytes.resize(sizeof(desc) + payload.size());
  const auto bytes_span = std::as_writable_bytes(std::span { bytes });
  detail::CopyBytes(
    bytes_span.subspan(0, sizeof(desc)), std::as_bytes(std::span { &desc, 1 }));
  detail::CopyBytes(bytes_span.subspan(sizeof(desc), payload.size()),
    std::as_bytes(std::span { payload }));
  return bytes;
}

[[nodiscard]] inline auto MakeCookedTexture8x8Bc7MipChainPayload()
  -> std::vector<uint8_t>
{
  using data::pak::SubresourceLayout;
  using data::pak::TextureResourceDesc;

  TextureResourceDesc desc {};
  desc.data_offset = sizeof(desc);
  // desc.size_bytes set after v4 payload is built.
  desc.texture_type = static_cast<uint8_t>(TextureType::kTexture2D);
  desc.compression_type = 0;
  desc.width = 8U;
  desc.height = 8U;
  desc.depth = 1U;
  desc.array_layers = 1U;
  desc.mip_levels = 4U; // 8x8, 4x4, 2x2, 1x1
  desc.format = static_cast<uint8_t>(Format::kBC7UNorm);
  desc.alignment = 256U; // NOLINT(*-magic-numbers)

  // BC7: 4x4 blocks, 16 bytes per block.
  // D3D12-style cooked layout: row pitch aligned to 256, placement aligned to
  // 512. For 8x8: blocks_x=2, blocks_y=2 => bytes_per_row=32 -> row_pitch=256
  // => size=512. For smaller mips: blocks_x=1, blocks_y=1 => size=256.
  constexpr std::array<SubresourceLayout, 4> layouts {
    SubresourceLayout {
      .offset_bytes = 0U, .row_pitch_bytes = 256U, .size_bytes = 512U },
    SubresourceLayout {
      .offset_bytes = 512U, .row_pitch_bytes = 256U, .size_bytes = 256U },
    SubresourceLayout {
      .offset_bytes = 1024U, .row_pitch_bytes = 256U, .size_bytes = 256U },
    SubresourceLayout {
      .offset_bytes = 1536U, .row_pitch_bytes = 256U, .size_bytes = 256U },
  };

  constexpr std::size_t kDataRegionSize = 1792U;
  std::vector<std::uint8_t> data_region(kDataRegionSize, 0U);

  // Populate the first block of each mip with a distinct pattern.
  for (std::size_t mip = 0; mip < layouts.size(); ++mip) {
    const auto base = static_cast<std::size_t>(layouts[mip].offset_bytes);
    for (std::size_t i = 0; i < 16U; ++i) {
      data_region[base + i]
        = static_cast<std::uint8_t>(0xA0U + (mip * 0x10U) + i);
    }
  }

  auto payload = detail::BuildV4TexturePayload(desc, layouts, data_region);
  desc.size_bytes = static_cast<std::uint32_t>(payload.size());

  std::vector<uint8_t> bytes;
  bytes.resize(sizeof(desc) + payload.size());
  const auto bytes_span = std::as_writable_bytes(std::span { bytes });
  detail::CopyBytes(
    bytes_span.subspan(0, sizeof(desc)), std::as_bytes(std::span { &desc, 1 }));
  detail::CopyBytes(bytes_span.subspan(sizeof(desc), payload.size()),
    std::as_bytes(std::span { payload }));
  return bytes;
}

[[nodiscard]] inline auto MakeCookedTexture4x4Bc1Payload()
  -> std::vector<uint8_t>
{
  using data::pak::SubresourceLayout;
  using data::pak::TextureResourceDesc;

  TextureResourceDesc desc {};
  desc.data_offset = sizeof(desc);
  // desc.size_bytes set after v4 payload is built.
  desc.texture_type = static_cast<uint8_t>(TextureType::kTexture2D);
  desc.compression_type = 0;
  desc.width = 4U;
  desc.height = 4U;
  desc.depth = 1U;
  desc.array_layers = 1U;
  desc.mip_levels = 1U;
  desc.format = static_cast<uint8_t>(Format::kBC1UNorm);
  desc.alignment = 256U; // NOLINT(*-magic-numbers)

  std::vector<std::uint8_t> data_region(1024U); // NOLINT(*-magic-numbers)
  const std::array<SubresourceLayout, 1> layouts {
    SubresourceLayout {
      .offset_bytes = 0U,
      .row_pitch_bytes = 256U, // NOLINT(*-magic-numbers)
      .size_bytes = 1024U, // NOLINT(*-magic-numbers)
    },
  };

  auto payload = detail::BuildV4TexturePayload(desc, layouts, data_region);
  desc.size_bytes = static_cast<std::uint32_t>(payload.size());

  std::vector<uint8_t> bytes;
  bytes.resize(sizeof(desc) + payload.size());
  const auto bytes_span = std::as_writable_bytes(std::span { bytes });
  detail::CopyBytes(
    bytes_span.subspan(0, sizeof(desc)), std::as_bytes(std::span { &desc, 1 }));
  detail::CopyBytes(bytes_span.subspan(sizeof(desc), payload.size()),
    std::as_bytes(std::span { payload }));
  return bytes;
}

} // namespace oxygen::renderer::testing
