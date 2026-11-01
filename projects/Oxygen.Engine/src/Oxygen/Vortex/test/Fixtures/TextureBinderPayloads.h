//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/Detail/FormatUtils.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/TextureType.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/TextureResource.h>
#include <Oxygen/Serio/ByteBuffer.h>

namespace oxygen::vortex::testing {

namespace detail {

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
    const data::pak::core::TextureResourceDesc& desc,
    const std::span<const data::pak::render::SubresourceLayout> layouts,
    const std::span<const std::uint8_t> data_region) -> std::vector<uint8_t>
  {
    using data::pak::render::SubresourceLayout;
    using data::pak::render::TexturePayloadHeader;

    constexpr std::size_t kDataOffsetAlignment = 256U;

    const auto layout_bytes = layouts.size() * sizeof(SubresourceLayout);
    const auto layouts_offset = sizeof(TexturePayloadHeader);
    const auto unaligned_data_offset = layouts_offset + layout_bytes;
    const auto data_offset
      = (unaligned_data_offset + (kDataOffsetAlignment - 1U))
      & ~(kDataOffsetAlignment - 1U);

    const auto total_payload_size = data_offset + data_region.size();

    TexturePayloadHeader header {};
    header.magic = data::pak::render::kTexturePayloadMagic;
    header.packing_policy = 0U;
    header.flags = 0U;
    header.subresource_count = static_cast<std::uint16_t>(layouts.size());
    header.total_payload_size = static_cast<std::uint32_t>(total_payload_size);
    header.layouts_offset_bytes = static_cast<std::uint32_t>(layouts_offset);
    header.data_offset_bytes = static_cast<std::uint32_t>(data_offset);
    header.content_hash = desc.content_hash;

    std::vector<std::uint8_t> payload(total_payload_size);
    const auto payload_bytes = std::as_writable_bytes(std::span {
      payload,
    });

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
/*! The payload must begin with a `data::pak::core::TextureResourceDesc` and
   contain `desc.size_bytes` bytes of data starting at `desc.data_offset`.
*/
[[nodiscard]] inline auto DecodeCookedTexturePayload(
  const std::span<const uint8_t> payload)
  -> std::shared_ptr<data::TextureResource>
{
  using data::pak::core::TextureResourceDesc;

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
  auto data = serio::ByteBuffer::CopyOf(
    std::as_bytes(payload.subspan(data_offset, data_size)));
  if (!data) {
    return nullptr;
  }
  try {
    return std::make_shared<data::TextureResource>(
      desc, std::move(data).value());
  } catch (...) {
    return nullptr;
  }
}

[[nodiscard]] inline auto MakeCookedTexture1x1Rgba8Payload()
  -> std::vector<uint8_t>
{
  using data::pak::core::TextureResourceDesc;
  using data::pak::render::SubresourceLayout;

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
  desc.alignment = 256U;

  constexpr std::array<uint8_t, 4> pixel {
    0xFF,
    0xFF,
    0xFF,
    0xFF,
  };

  // Cooked data uses a 256-byte row pitch for D3D12-compatible copies.
  std::vector<std::uint8_t> data_region(256U);
  detail::CopyBytes(std::as_writable_bytes(std::span {
                                             data_region,
                                           })
                      .subspan(0, pixel.size()),
    std::as_bytes(std::span {
      pixel,
    }));

  const std::array<SubresourceLayout, 1> layouts {
    SubresourceLayout {
      .offset_bytes = 0U,
      .row_pitch_bytes = 256U,
      .size_bytes = 256U,
    },
  };

  auto payload = detail::BuildV4TexturePayload(desc, layouts, data_region);
  desc.size_bytes = static_cast<std::uint32_t>(payload.size());

  std::vector<uint8_t> bytes;
  bytes.resize(sizeof(desc) + payload.size());
  const auto bytes_span = std::as_writable_bytes(std::span {
    bytes,
  });
  detail::CopyBytes(bytes_span.subspan(0, sizeof(desc)),
    std::as_bytes(std::span {
      &desc,
      1,
    }));
  detail::CopyBytes(bytes_span.subspan(sizeof(desc), payload.size()),
    std::as_bytes(std::span {
      payload,
    }));
  return bytes;
}

[[nodiscard]] inline auto MakeTightPackedTexture1x1Rgba8Payload()
  -> std::vector<uint8_t>
{
  using data::pak::core::TextureResourceDesc;
  using data::pak::render::SubresourceLayout;

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
  desc.alignment = 256U;

  constexpr std::array<uint8_t, 4> pixel {
    0xFF,
    0xFF,
    0xFF,
    0xFF,
  };

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
  const auto bytes_span = std::as_writable_bytes(std::span {
    bytes,
  });
  detail::CopyBytes(bytes_span.subspan(0, sizeof(desc)),
    std::as_bytes(std::span {
      &desc,
      1,
    }));
  detail::CopyBytes(bytes_span.subspan(sizeof(desc), payload.size()),
    std::as_bytes(std::span {
      payload,
    }));
  return bytes;
}

//! A tight-packed 1x1 cube: six 4-byte faces, one mip each. `format` other
//! than RGBA8 describes the same bytes, to exercise format rejection.
[[nodiscard]] inline auto MakeTightPackedTextureCube1x1Rgba8Payload(
  const Format format = Format::kRGBA8UNorm) -> std::vector<uint8_t>
{
  using data::pak::core::TextureResourceDesc;
  using data::pak::render::SubresourceLayout;

  constexpr std::uint32_t kFaceCount = 6U;
  constexpr std::uint32_t kFaceBytes = 4U;

  TextureResourceDesc desc {};
  desc.data_offset = sizeof(desc);
  // desc.size_bytes set after v4 payload is built.
  desc.texture_type = static_cast<uint8_t>(TextureType::kTextureCube);
  desc.compression_type = 0;
  desc.width = 1U;
  desc.height = 1U;
  desc.depth = 1U;
  desc.array_layers = kFaceCount;
  desc.mip_levels = 1U;
  desc.format = static_cast<uint8_t>(format);
  desc.alignment = 256U;

  const std::vector<std::uint8_t> data_region(
    static_cast<std::size_t>(kFaceCount) * kFaceBytes, 0xFFU);
  std::array<SubresourceLayout, kFaceCount> layouts {};
  for (std::uint32_t face = 0U; face < kFaceCount; ++face) {
    layouts.at(face) = SubresourceLayout {
      .offset_bytes = face * kFaceBytes,
      .row_pitch_bytes = kFaceBytes,
      .size_bytes = kFaceBytes,
    };
  }

  auto payload = detail::BuildV4TexturePayload(desc, layouts, data_region);
  desc.size_bytes = static_cast<std::uint32_t>(payload.size());

  std::vector<uint8_t> bytes;
  bytes.resize(sizeof(desc) + payload.size());
  const auto bytes_span = std::as_writable_bytes(std::span {
    bytes,
  });
  detail::CopyBytes(bytes_span.subspan(0, sizeof(desc)),
    std::as_bytes(std::span {
      &desc,
      1,
    }));
  detail::CopyBytes(bytes_span.subspan(sizeof(desc), payload.size()),
    std::as_bytes(std::span {
      payload,
    }));
  return bytes;
}

[[nodiscard]] inline auto MakeInvalidTexture1x1Rgba8Payload_RowPitchTooSmall()
  -> std::vector<uint8_t>
{
  using data::pak::core::TextureResourceDesc;
  using data::pak::render::SubresourceLayout;

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
  desc.alignment = 256U;

  // Invalid: row pitch is smaller than bytes_per_row (RGBA8 => 4 bytes).
  const std::vector<std::uint8_t> data_region {
    0xFF,
    0xFF,
    0xFF,
  };
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
  const auto bytes_span = std::as_writable_bytes(std::span {
    bytes,
  });
  detail::CopyBytes(bytes_span.subspan(0, sizeof(desc)),
    std::as_bytes(std::span {
      &desc,
      1,
    }));
  detail::CopyBytes(bytes_span.subspan(sizeof(desc), payload.size()),
    std::as_bytes(std::span {
      payload,
    }));
  return bytes;
}

[[nodiscard]] inline auto MakeCookedTexture8x8Bc7MipChainPayload()
  -> std::vector<uint8_t>
{
  using data::pak::core::TextureResourceDesc;
  using data::pak::render::SubresourceLayout;

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
  desc.alignment = 256U;

  // BC7: 4x4 blocks, 16 bytes per block.
  // D3D12-style cooked layout: row pitch aligned to 256, placement aligned to
  // 512. For 8x8: blocks_x=2, blocks_y=2 => bytes_per_row=32 -> row_pitch=256
  // => size=512. For smaller mips: blocks_x=1, blocks_y=1 => size=256.
  constexpr std::array<SubresourceLayout, 4> layouts {
    SubresourceLayout {
      .offset_bytes = 0U,
      .row_pitch_bytes = 256U,
      .size_bytes = 512U,
    },
    SubresourceLayout {
      .offset_bytes = 512U,
      .row_pitch_bytes = 256U,
      .size_bytes = 256U,
    },
    SubresourceLayout {
      .offset_bytes = 1024U,
      .row_pitch_bytes = 256U,
      .size_bytes = 256U,
    },
    SubresourceLayout {
      .offset_bytes = 1536U,
      .row_pitch_bytes = 256U,
      .size_bytes = 256U,
    },
  };

  constexpr std::size_t kDataRegionSize = 1792U;
  std::vector<std::uint8_t> data_region(kDataRegionSize, 0U);

  // Populate the first block of each mip with a distinct pattern.
  for (std::size_t mip = 0; mip < layouts.size(); ++mip) {
    const auto base = static_cast<std::size_t>(layouts.at(mip).offset_bytes);
    for (std::size_t i = 0; i < 16U; ++i) {
      data_region.at(base + i)
        = static_cast<std::uint8_t>(0xA0U + (mip * 0x10U) + i);
    }
  }

  auto payload = detail::BuildV4TexturePayload(desc, layouts, data_region);
  desc.size_bytes = static_cast<std::uint32_t>(payload.size());

  std::vector<uint8_t> bytes;
  bytes.resize(sizeof(desc) + payload.size());
  const auto bytes_span = std::as_writable_bytes(std::span {
    bytes,
  });
  detail::CopyBytes(bytes_span.subspan(0, sizeof(desc)),
    std::as_bytes(std::span {
      &desc,
      1,
    }));
  detail::CopyBytes(bytes_span.subspan(sizeof(desc), payload.size()),
    std::as_bytes(std::span {
      payload,
    }));
  return bytes;
}

[[nodiscard]] inline auto MakeCookedTexture4x4Bc1Payload()
  -> std::vector<uint8_t>
{
  using data::pak::core::TextureResourceDesc;
  using data::pak::render::SubresourceLayout;

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
  desc.alignment = 256U;

  std::vector<std::uint8_t> data_region(1024U);
  const std::array<SubresourceLayout, 1> layouts {
    SubresourceLayout {
      .offset_bytes = 0U,
      .row_pitch_bytes = 256U,
      .size_bytes = 1024U,
    },
  };

  auto payload = detail::BuildV4TexturePayload(desc, layouts, data_region);
  desc.size_bytes = static_cast<std::uint32_t>(payload.size());

  std::vector<uint8_t> bytes;
  bytes.resize(sizeof(desc) + payload.size());
  const auto bytes_span = std::as_writable_bytes(std::span {
    bytes,
  });
  detail::CopyBytes(bytes_span.subspan(0, sizeof(desc)),
    std::as_bytes(std::span {
      &desc,
      1,
    }));
  detail::CopyBytes(bytes_span.subspan(sizeof(desc), payload.size()),
    std::as_bytes(std::span {
      payload,
    }));
  return bytes;
}

//! Shape of a synthetic single-mip, uncompressed texture.
struct SyntheticTextureShape {
  TextureType type { TextureType::kTexture2D };
  std::uint32_t width { 1U };
  std::uint32_t height { 1U };
  std::uint16_t layers { 1U };
  Format format { Format::kRGBA32Float };
  //! Identity of the texel data; the loader key when the texture is registered.
  std::uint64_t content_hash { 0U };
};

//! The shape of a cube with square faces of `size` texels.
[[nodiscard]] inline auto CubeShape(const std::uint32_t size,
  const Format format = Format::kRGBA32Float) -> SyntheticTextureShape
{
  return {
    .type = TextureType::kTextureCube,
    .width = size,
    .height = size,
    .layers = 6U,
    .format = format,
    .content_hash = 0U,
  };
}

//! Builds a texture of `shape` from its tightly packed `texels`, layer by
//! layer.
[[nodiscard]] inline auto MakeSyntheticTexture(
  const SyntheticTextureShape& shape,
  const std::span<const std::uint8_t> texels)
  -> std::shared_ptr<data::TextureResource>
{
  const auto texel_bytes
    = oxygen::graphics::detail::GetFormatInfo(shape.format).bytes_per_block;
  const auto row_bytes = std::size_t { shape.width } * texel_bytes;
  const auto layer_bytes = row_bytes * shape.height;
  CHECK_EQ_F(texels.size(), layer_bytes * shape.layers,
    "texels must hold every texel of every layer");

  data::pak::core::TextureResourceDesc desc {};
  desc.texture_type = static_cast<std::uint8_t>(shape.type);
  desc.width = shape.width;
  desc.height = shape.height;
  desc.depth = desc.mip_levels = 1U;
  desc.array_layers = shape.layers;
  desc.format = static_cast<std::uint8_t>(shape.format);
  desc.alignment = 256U;
  desc.content_hash = shape.content_hash;
  std::vector<data::pak::render::SubresourceLayout> layouts;
  layouts.reserve(shape.layers);
  for (std::uint16_t layer = 0U; layer < shape.layers; ++layer) {
    layouts.push_back({
      .offset_bytes = static_cast<std::uint32_t>(layer * layer_bytes),
      .row_pitch_bytes = static_cast<std::uint32_t>(row_bytes),
      .size_bytes = static_cast<std::uint32_t>(layer_bytes),
    });
  }
  auto payload = detail::BuildV4TexturePayload(desc, layouts, texels);
  desc.size_bytes = static_cast<std::uint32_t>(payload.size());
  return std::make_shared<data::TextureResource>(desc,
    serio::ByteBuffer::CopyOf(std::as_bytes(std::span { payload })).value());
}

//! The RGBA value of the texel at column `x` and row `y` of layer `layer`.
using SyntheticTexel = std::function<std::array<float, 4>(
  std::uint32_t layer, std::uint32_t x, std::uint32_t y)>;

//! Builds an RGBA32Float or RGBA8UNorm texture of `shape` from `texel`.
/*!
 RGBA8UNorm texels are clamped to [0, 1] and rounded to the nearest code.
*/
[[nodiscard]] inline auto MakeSyntheticTexture(
  const SyntheticTextureShape& shape, const SyntheticTexel& texel)
  -> std::shared_ptr<data::TextureResource>
{
  const bool unorm8 = shape.format == Format::kRGBA8UNorm;
  CHECK_F(unorm8 || shape.format == Format::kRGBA32Float,
    "texel functions encode only RGBA32Float and RGBA8UNorm");
  const std::size_t texel_bytes = unorm8 ? 4U : sizeof(std::array<float, 4>);
  std::vector<std::uint8_t> texels(
    std::size_t { shape.layers } * shape.height * shape.width * texel_bytes);
  auto destination = std::as_writable_bytes(std::span { texels });
  for (std::uint32_t layer = 0U; layer < shape.layers; ++layer) {
    for (std::uint32_t y = 0U; y < shape.height; ++y) {
      for (std::uint32_t x = 0U; x < shape.width; ++x) {
        const auto value = texel(layer, x, y);
        if (unorm8) {
          for (std::size_t c = 0U; c < 4U; ++c) {
            destination[c] = static_cast<std::byte>(
              std::lround(std::clamp(value.at(c), 0.0F, 1.0F) * 255.0F));
          }
        } else {
          detail::CopyBytes(destination, std::as_bytes(std::span { value }));
        }
        destination = destination.subspan(texel_bytes);
      }
    }
  }
  return MakeSyntheticTexture(shape, texels);
}

} // namespace oxygen::vortex::testing
