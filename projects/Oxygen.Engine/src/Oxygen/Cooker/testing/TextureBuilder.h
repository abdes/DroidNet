//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/TextureType.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Data/PakFormat_render.h>

namespace oxygen::cooker::testing {

//! Builds a well-formed cooked texture: the payload, laid out as the cook
//! lays it out by its packing policy, and the resource table row of it.
/*!
 The default is a 1x1 RGBA8 2D texture, one layer and one mip, packed for
 D3D12. Each data byte holds its offset in the data region, modulo 256.

 ```cpp
 const auto texture
   = TextureBuilder {}.Size({ .width = 8U, .height = 8U }).Mips(4U);
 auto payload = texture.Payload();
 auto layout = TextureBuilder::SubresourceAt(payload, 1U);
 layout.row_pitch_bytes = 4U;
 TextureBuilder::SetSubresource(payload, 1U, layout);
 EXPECT_EQ(TextureRefusal(texture.Record(payload), payload),
   CookedDefect::kInconsistent);
 ```
*/
class TextureBuilder final {
public:
  //! The texels of mip 0.
  struct Extent final {
    uint32_t width = 1U;
    uint32_t height = 1U;
    uint16_t depth = 1U;
  };

  auto Type(TextureType type) -> TextureBuilder&;
  auto PixelFormat(Format format) -> TextureBuilder&;
  auto Size(const Extent& extent) -> TextureBuilder&;
  auto Layers(uint16_t layers) -> TextureBuilder&;
  auto Mips(uint16_t mips) -> TextureBuilder&;
  auto Policy(data::pak::render::TexturePackingPolicyId policy)
    -> TextureBuilder&;

  //! The payload: header, subresource layouts and data.
  [[nodiscard]] auto Payload() const -> std::vector<std::byte>;

  //! The resource table row of this texture, at data offset 0, with the size
  //! and SHA-256 of `payload` and the compression of its format.
  [[nodiscard]] auto Record(std::span<const std::byte> payload) const
    -> data::pak::core::TextureResourceDesc;

  //! The header of `payload`.
  [[nodiscard]] static auto HeaderAt(std::span<const std::byte> payload)
    -> data::pak::render::TexturePayloadHeader;

  //! Replaces the header of `payload`.
  static auto SetHeader(std::vector<std::byte>& payload,
    const data::pak::render::TexturePayloadHeader& header) -> void;

  //! The layout of subresource `index`, layer-major, of `payload`.
  [[nodiscard]] static auto SubresourceAt(std::span<const std::byte> payload,
    uint32_t index) -> data::pak::render::SubresourceLayout;

  //! Replaces the layout of subresource `index` of `payload`.
  static auto SetSubresource(std::vector<std::byte>& payload, uint32_t index,
    const data::pak::render::SubresourceLayout& layout) -> void;

private:
  TextureType type_ = TextureType::kTexture2D;
  Format format_ = Format::kRGBA8UNorm;
  Extent extent_ {};
  uint16_t layers_ = 1U;
  uint16_t mips_ = 1U;
  data::pak::render::TexturePackingPolicyId policy_
    = data::pak::render::TexturePackingPolicyId::kD3D12;
};

} // namespace oxygen::cooker::testing
