//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>

#include <Oxygen/Core/Bindless/Generated.BindlessAbi.h>
#include <Oxygen/Core/Bindless/Types.h>

namespace oxygen {

//! How a texture repeats outside its 0 to 1 range.
enum class TextureAddress : std::uint8_t {
  kRepeat,
  kMirror,
  kClamp,
};

//! How a texture blends texels when magnified or minified.
enum class TextureFilter : std::uint8_t {
  kNearest,
  kLinear,
};

//! How a texture moves between mip levels. `kNone` stays on the first level.
enum class TextureMipFilter : std::uint8_t {
  kNone,
  kNearest,
  kLinear,
};

//! One texture's sampling state. The defaults are a material's defaults.
struct TextureSamplerState {
  TextureAddress address_u { TextureAddress::kRepeat };
  TextureAddress address_v { TextureAddress::kRepeat };
  TextureFilter mag_filter { TextureFilter::kLinear };
  TextureFilter min_filter { TextureFilter::kLinear };
  TextureMipFilter mip_filter { TextureMipFilter::kLinear };

  auto operator==(const TextureSamplerState&) const -> bool = default;
};

namespace detail {
  inline constexpr std::uint32_t kTextureAddressCount = 3U;
  inline constexpr std::uint32_t kTextureFilterCount = 2U;
  inline constexpr std::uint32_t kTextureMipFilterCount = 3U;
} // namespace detail

//! Samplers in the table: every combination of `TextureSamplerState`.
inline constexpr std::uint32_t kTextureSamplerCount
  = detail::kTextureAddressCount * detail::kTextureAddressCount
  * detail::kTextureFilterCount * detail::kTextureFilterCount
  * detail::kTextureMipFilterCount;
static_assert(
  kTextureSamplerCount == bindless::generated::kTextureSamplerTableCount);

//! Position of a state in the sampler table.
[[nodiscard]] constexpr auto TextureSamplerOffset(
  const TextureSamplerState& state) noexcept -> std::uint32_t
{
  auto offset = static_cast<std::uint32_t>(state.address_u);
  offset = (offset * detail::kTextureAddressCount)
    + static_cast<std::uint32_t>(state.address_v);
  offset = (offset * detail::kTextureFilterCount)
    + static_cast<std::uint32_t>(state.mag_filter);
  offset = (offset * detail::kTextureFilterCount)
    + static_cast<std::uint32_t>(state.min_filter);
  return (offset * detail::kTextureMipFilterCount)
    + static_cast<std::uint32_t>(state.mip_filter);
}

//! The state at a position of the sampler table, the inverse of
//! `TextureSamplerOffset`. Graphics creates the table from it.
[[nodiscard]] constexpr auto TextureSamplerStateAt(
  std::uint32_t offset) noexcept -> TextureSamplerState
{
  auto state = TextureSamplerState {};
  state.mip_filter
    = static_cast<TextureMipFilter>(offset % detail::kTextureMipFilterCount);
  offset /= detail::kTextureMipFilterCount;
  state.min_filter
    = static_cast<TextureFilter>(offset % detail::kTextureFilterCount);
  offset /= detail::kTextureFilterCount;
  state.mag_filter
    = static_cast<TextureFilter>(offset % detail::kTextureFilterCount);
  offset /= detail::kTextureFilterCount;
  state.address_v
    = static_cast<TextureAddress>(offset % detail::kTextureAddressCount);
  offset /= detail::kTextureAddressCount;
  state.address_u = static_cast<TextureAddress>(offset);
  return state;
}

//! What shaders index `SamplerDescriptorHeap` with to sample in `state`.
[[nodiscard]] constexpr auto TextureSamplerIndex(
  const TextureSamplerState& state) noexcept -> ShaderVisibleIndex
{
  return ShaderVisibleIndex { bindless::generated::kTextureSamplerTableIndex
    + TextureSamplerOffset(state) };
}

} // namespace oxygen
