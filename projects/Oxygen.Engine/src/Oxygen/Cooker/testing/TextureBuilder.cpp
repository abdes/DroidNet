//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <stdexcept>
#include <vector>

#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Cooker/Import/TexturePackingPolicy.h>
#include <Oxygen/Cooker/testing/TextureBuilder.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/TextureType.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Data/PakFormat_render.h>

namespace oxygen::cooker::testing {

namespace {

  namespace render = data::pak::render;

  auto PolicyOf(const render::TexturePackingPolicyId policy)
    -> const content::import::ITexturePackingPolicy&
  {
    switch (policy) {
    case render::TexturePackingPolicyId::kD3D12:
      return content::import::D3D12PackingPolicy::Instance();
    case render::TexturePackingPolicyId::kTightPacked:
      return content::import::TightPackedPolicy::Instance();
    }
    throw std::logic_error("TextureBuilder: unknown packing policy");
  }

  //! The offset of the layout of subresource `index` in a payload.
  auto LayoutOffset(const std::size_t index) -> std::size_t
  {
    return sizeof(render::TexturePayloadHeader)
      + (index * sizeof(render::SubresourceLayout));
  }

  template <typename T>
  auto ReadAt(
    const std::span<const std::byte> payload, const std::size_t offset) -> T
  {
    if (payload.size() < offset + sizeof(T)) {
      throw std::logic_error("TextureBuilder: the payload is too small");
    }
    auto value = T {};
    std::memcpy(&value, payload.subspan(offset, sizeof(T)).data(), sizeof(T));
    return value;
  }

  template <typename T>
  auto WriteAt(std::vector<std::byte>& payload, const std::size_t offset,
    const T& value) -> void
  {
    if (payload.size() < offset + sizeof(T)) {
      throw std::logic_error("TextureBuilder: the payload is too small");
    }
    std::memcpy(std::span { payload }.subspan(offset, sizeof(T)).data(), &value,
      sizeof(T));
  }

} // namespace

auto TextureBuilder::Type(const TextureType type) -> TextureBuilder&
{
  type_ = type;
  return *this;
}

auto TextureBuilder::PixelFormat(const Format format) -> TextureBuilder&
{
  format_ = format;
  return *this;
}

auto TextureBuilder::Size(const Extent& extent) -> TextureBuilder&
{
  extent_ = extent;
  return *this;
}

auto TextureBuilder::Layers(const uint16_t layers) -> TextureBuilder&
{
  layers_ = layers;
  return *this;
}

auto TextureBuilder::Mips(const uint16_t mips) -> TextureBuilder&
{
  mips_ = mips;
  return *this;
}

auto TextureBuilder::Policy(const render::TexturePackingPolicyId policy)
  -> TextureBuilder&
{
  policy_ = policy;
  return *this;
}

auto TextureBuilder::Payload() const -> std::vector<std::byte>
{
  const auto& policy = PolicyOf(policy_);
  const auto layouts = content::import::ComputeSubresourceLayouts(
    {
      .texture_type = type_,
      .width = extent_.width,
      .height = extent_.height,
      .depth = extent_.depth,
      .array_layers = layers_,
      .mip_levels = mips_,
      .format = format_,
    },
    policy);
  const auto data_bytes = content::import::ComputeTotalPayloadSize(layouts);
  const auto data_offset
    = policy.AlignSubresourceOffset(LayoutOffset(layouts.size()));

  auto payload = std::vector<std::byte>(data_offset + data_bytes);
  WriteAt(payload, 0U,
    render::TexturePayloadHeader {
      .packing_policy = static_cast<uint8_t>(policy_),
      .subresource_count = static_cast<uint16_t>(layouts.size()),
      .total_payload_size = static_cast<uint32_t>(payload.size()),
      .layouts_offset_bytes = sizeof(render::TexturePayloadHeader),
      .data_offset_bytes = static_cast<uint32_t>(data_offset),
    });
  for (uint32_t index = 0U; index < layouts.size(); ++index) {
    const auto& layout = layouts[index];
    SetSubresource(payload, index,
      {
        .offset_bytes = static_cast<uint32_t>(layout.offset),
        .row_pitch_bytes = static_cast<uint32_t>(layout.row_pitch),
        .size_bytes = static_cast<uint32_t>(layout.size_bytes),
      });
  }
  for (std::size_t at = 0U; at < data_bytes; ++at) {
    payload[data_offset + at] = static_cast<std::byte>(at & 0xFFU);
  }
  return payload;
}

auto TextureBuilder::Record(const std::span<const std::byte> payload) const
  -> data::pak::core::TextureResourceDesc
{
  return {
    .data_offset = 0U,
    .size_bytes = static_cast<uint32_t>(payload.size()),
    .texture_type = static_cast<uint8_t>(type_),
    .compression_type = data::pak::core::TextureCompressionOf(format_),
    .width = extent_.width,
    .height = extent_.height,
    .depth = extent_.depth,
    .array_layers = layers_,
    .mip_levels = mips_,
    .format = static_cast<uint8_t>(format_),
    .alignment = 256U,
    .content_hash = base::ComputeSha256(payload),
  };
}

auto TextureBuilder::HeaderAt(const std::span<const std::byte> payload)
  -> render::TexturePayloadHeader
{
  return ReadAt<render::TexturePayloadHeader>(payload, 0U);
}

auto TextureBuilder::SetHeader(std::vector<std::byte>& payload,
  const render::TexturePayloadHeader& header) -> void
{
  WriteAt(payload, 0U, header);
}

auto TextureBuilder::SubresourceAt(const std::span<const std::byte> payload,
  const uint32_t index) -> render::SubresourceLayout
{
  return ReadAt<render::SubresourceLayout>(payload, LayoutOffset(index));
}

auto TextureBuilder::SetSubresource(std::vector<std::byte>& payload,
  const uint32_t index, const render::SubresourceLayout& layout) -> void
{
  WriteAt(payload, LayoutOffset(index), layout);
}

} // namespace oxygen::cooker::testing
