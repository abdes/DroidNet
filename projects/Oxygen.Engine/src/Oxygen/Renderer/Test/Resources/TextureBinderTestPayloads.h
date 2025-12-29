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
  using data::pak::TextureResourceDesc;

  TextureResourceDesc desc {};
  desc.data_offset = sizeof(desc);
  // Cooked data uses a 256-byte row pitch for D3D12-compatible copies.
  desc.size_bytes = 256U; // NOLINT(*-magic-numbers)
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

  std::vector<uint8_t> bytes;
  bytes.resize(sizeof(desc) + 256U); // NOLINT(*-magic-numbers)
  const auto bytes_span = std::as_writable_bytes(std::span { bytes });
  detail::CopyBytes(
    bytes_span.subspan(0, sizeof(desc)), std::as_bytes(std::span { &desc, 1 }));
  detail::CopyBytes(bytes_span.subspan(sizeof(desc), pixel.size()),
    std::as_bytes(std::span { pixel }));
  return bytes;
}

[[nodiscard]] inline auto MakeInvalidTightPackedTexture1x1Rgba8Payload()
  -> std::vector<uint8_t>
{
  using data::pak::TextureResourceDesc;

  TextureResourceDesc desc {};
  desc.data_offset = sizeof(desc);
  // Intentionally tight-packed (row pitch 4, not 256): violates D4.
  desc.size_bytes = 4U;
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

  std::vector<uint8_t> bytes;
  bytes.resize(sizeof(desc) + pixel.size());
  const auto bytes_span = std::as_writable_bytes(std::span { bytes });
  detail::CopyBytes(
    bytes_span.subspan(0, sizeof(desc)), std::as_bytes(std::span { &desc, 1 }));
  detail::CopyBytes(bytes_span.subspan(sizeof(desc), pixel.size()),
    std::as_bytes(std::span { pixel }));
  return bytes;
}

[[nodiscard]] inline auto MakeCookedTexture4x4Bc1Payload()
  -> std::vector<uint8_t>
{
  using data::pak::TextureResourceDesc;

  TextureResourceDesc desc {};
  desc.data_offset = sizeof(desc);
  desc.size_bytes = 1024U; // NOLINT(*-magic-numbers)
  desc.texture_type = static_cast<uint8_t>(TextureType::kTexture2D);
  desc.compression_type = 0;
  desc.width = 4U;
  desc.height = 4U;
  desc.depth = 1U;
  desc.array_layers = 1U;
  desc.mip_levels = 1U;
  desc.format = static_cast<uint8_t>(Format::kBC1UNorm);
  desc.alignment = 256U; // NOLINT(*-magic-numbers)

  std::vector<uint8_t> bytes;
  bytes.resize(sizeof(desc) + 1024U); // NOLINT(*-magic-numbers)
  const auto bytes_span = std::as_writable_bytes(std::span { bytes });
  detail::CopyBytes(
    bytes_span.subspan(0, sizeof(desc)), std::as_bytes(std::span { &desc, 1 }));
  return bytes;
}

} // namespace oxygen::renderer::testing
