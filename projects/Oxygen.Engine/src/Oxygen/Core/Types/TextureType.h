//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <bit>
#include <cstdint>
#include <type_traits>

#include <Oxygen/Core/api_export.h>

namespace oxygen {

enum class TextureType : uint8_t {
  // clang-format off
  //!< Unknown texture type (Invalid or uninitialized state)
  kUnknown                    = 0,

  kTexture1D                  = 1,
  kTexture1DArray             = 2,
  kTexture2D                  = 3,
  kTexture2DArray             = 4,
  kTextureCube                = 5,
  kTextureCubeArray           = 6,
  kTexture2DMultiSample       = 7,
  kTexture2DMultiSampleArray  = 8,
  kTexture3D                  = 9,

  //! Sentinel value for the maximum texture type.
  kMaxTextureType             = kTexture3D,
  // clang-format on
};

static_assert(sizeof(std::underlying_type_t<TextureType>) == sizeof(uint8_t),
  "TextureType enum must be 8 bits for binary encoding");

//! The faces of a cube, its array layers: a cube array holds this many layers
//! per cube.
inline constexpr uint32_t kCubeFaceCount = 6U;

//! The extent of a `base`-texel dimension at mip `mip`: halved per level,
//! never below one texel.
[[nodiscard]] constexpr auto MipExtent(
  const uint32_t base, const uint32_t mip) noexcept -> uint32_t
{
  constexpr auto kBits = 32U;
  const auto extent = mip < kBits ? base >> mip : 0U;
  return extent > 0U ? extent : 1U;
}

//! The mips of a full chain from a `width` x `height` x `depth` mip 0 down to
//! one texel: floor(log2(largest extent)) + 1. None for an empty extent.
[[nodiscard]] constexpr auto MipChainLength(const uint32_t width,
  const uint32_t height, const uint32_t depth = 1U) noexcept -> uint32_t
{
  if (width == 0U || height == 0U || depth == 0U) {
    return 0U;
  }
  auto largest = width > height ? width : height;
  largest = largest > depth ? largest : depth;
  return static_cast<uint32_t>(std::bit_width(largest));
}

//! String representation of enum values in `Format`.
OXGN_CORE_NDAPI auto to_string(TextureType value) -> const char*;

} // namespace oxygen
