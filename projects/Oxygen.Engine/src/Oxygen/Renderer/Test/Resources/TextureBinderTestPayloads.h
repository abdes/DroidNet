//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <vector>

#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/TextureType.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/TextureResource.h>

namespace oxygen::renderer::testing {

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

  TextureResourceDesc desc {};
  std::memcpy(&desc, payload.data(), sizeof(desc));

  const auto data_offset = static_cast<std::size_t>(desc.data_offset);
  const auto data_size = static_cast<std::size_t>(desc.size_bytes);

  if (data_offset > payload.size()
    || payload.size() - data_offset < data_size) {
    return nullptr;
  }

  std::vector<uint8_t> data;
  data.resize(data_size);
  std::memcpy(data.data(), payload.data() + data_offset, data_size);

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
  desc.data_offset = static_cast<data::pak::OffsetT>(sizeof(desc));
  // Cooked data uses a 256-byte row pitch for D3D12-compatible copies.
  desc.size_bytes = static_cast<data::pak::DataBlobSizeT>(256U);
  desc.texture_type = static_cast<uint8_t>(TextureType::kTexture2D);
  desc.compression_type = 0;
  desc.width = 1U;
  desc.height = 1U;
  desc.depth = 1U;
  desc.array_layers = 1U;
  desc.mip_levels = 1U;
  desc.format = static_cast<uint8_t>(Format::kRGBA8UNorm);
  desc.alignment = 256U;

  const std::array<uint8_t, 4> pixel { 0xFF, 0xFF, 0xFF, 0xFF };

  std::vector<uint8_t> bytes;
  bytes.resize(sizeof(desc) + 256U);
  std::memcpy(bytes.data(), &desc, sizeof(desc));
  std::memcpy(bytes.data() + sizeof(desc), pixel.data(), pixel.size());
  return bytes;
}

[[nodiscard]] inline auto MakeInvalidTightPackedTexture1x1Rgba8Payload()
  -> std::vector<uint8_t>
{
  using data::pak::TextureResourceDesc;

  TextureResourceDesc desc {};
  desc.data_offset = static_cast<data::pak::OffsetT>(sizeof(desc));
  // Intentionally tight-packed (row pitch 4, not 256): violates D4.
  desc.size_bytes = static_cast<data::pak::DataBlobSizeT>(4U);
  desc.texture_type = static_cast<uint8_t>(TextureType::kTexture2D);
  desc.compression_type = 0;
  desc.width = 1U;
  desc.height = 1U;
  desc.depth = 1U;
  desc.array_layers = 1U;
  desc.mip_levels = 1U;
  desc.format = static_cast<uint8_t>(Format::kRGBA8UNorm);
  desc.alignment = 256U;

  const std::array<uint8_t, 4> pixel { 0xFF, 0xFF, 0xFF, 0xFF };

  std::vector<uint8_t> bytes;
  bytes.resize(sizeof(desc) + pixel.size());
  std::memcpy(bytes.data(), &desc, sizeof(desc));
  std::memcpy(bytes.data() + sizeof(desc), pixel.data(), pixel.size());
  return bytes;
}

[[nodiscard]] inline auto MakeCookedTexture4x4Bc1Payload()
  -> std::vector<uint8_t>
{
  using data::pak::TextureResourceDesc;

  TextureResourceDesc desc {};
  desc.data_offset = static_cast<data::pak::OffsetT>(sizeof(desc));
  desc.size_bytes = static_cast<data::pak::DataBlobSizeT>(1024U);
  desc.texture_type = static_cast<uint8_t>(TextureType::kTexture2D);
  desc.compression_type = 0;
  desc.width = 4U;
  desc.height = 4U;
  desc.depth = 1U;
  desc.array_layers = 1U;
  desc.mip_levels = 1U;
  desc.format = static_cast<uint8_t>(Format::kBC1UNorm);
  desc.alignment = 256U;

  std::vector<uint8_t> bytes;
  bytes.resize(sizeof(desc) + 1024U);
  std::memcpy(bytes.data(), &desc, sizeof(desc));
  return bytes;
}

} // namespace oxygen::renderer::testing
