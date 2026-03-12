//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <algorithm>
#include <cstdint>

namespace oxygen::renderer {

inline constexpr std::uint32_t kVirtualShadowPageTableTileCoordMask = 0x0FFFU;
inline constexpr std::uint32_t kVirtualShadowPageTableTileXShift = 0U;
inline constexpr std::uint32_t kVirtualShadowPageTableTileYShift = 12U;
inline constexpr std::uint32_t kVirtualShadowPageTableFallbackLodOffsetShift
  = 24U;
inline constexpr std::uint32_t kVirtualShadowPageTableFallbackLodOffsetMask
  = 0x0FU;
inline constexpr std::uint32_t kVirtualShadowPageTableCurrentLodValidBit
  = (1U << 28U);
inline constexpr std::uint32_t kVirtualShadowPageTableAnyLodValidBit
  = (1U << 29U);
inline constexpr std::uint32_t kVirtualShadowPageTableRequestedThisFrameBit
  = (1U << 30U);

struct VirtualShadowPageTableEntryDecoded {
  std::uint16_t tile_x { 0U };
  std::uint16_t tile_y { 0U };
  std::uint8_t fallback_lod_offset { 0U };
  bool current_lod_valid { false };
  bool any_lod_valid { false };
  bool requested_this_frame { false };
};

[[nodiscard]] constexpr auto PackVirtualShadowPageTableEntry(
  const std::uint32_t tile_x, const std::uint32_t tile_y,
  const std::uint32_t fallback_lod_offset = 0U,
  const bool current_lod_valid = true, const bool any_lod_valid = true,
  const bool requested_this_frame = true) -> std::uint32_t
{
  const bool any_valid = any_lod_valid || current_lod_valid;
  return ((tile_x & kVirtualShadowPageTableTileCoordMask)
           << kVirtualShadowPageTableTileXShift)
    | ((tile_y & kVirtualShadowPageTableTileCoordMask)
       << kVirtualShadowPageTableTileYShift)
    | ((fallback_lod_offset & kVirtualShadowPageTableFallbackLodOffsetMask)
       << kVirtualShadowPageTableFallbackLodOffsetShift)
    | (current_lod_valid ? kVirtualShadowPageTableCurrentLodValidBit : 0U)
    | (any_valid ? kVirtualShadowPageTableAnyLodValidBit : 0U)
    | (requested_this_frame ? kVirtualShadowPageTableRequestedThisFrameBit
                            : 0U);
}

[[nodiscard]] constexpr auto DecodeVirtualShadowPageTableEntry(
  const std::uint32_t packed_entry) -> VirtualShadowPageTableEntryDecoded
{
  return VirtualShadowPageTableEntryDecoded {
    .tile_x = static_cast<std::uint16_t>(
      (packed_entry >> kVirtualShadowPageTableTileXShift)
      & kVirtualShadowPageTableTileCoordMask),
    .tile_y = static_cast<std::uint16_t>(
      (packed_entry >> kVirtualShadowPageTableTileYShift)
      & kVirtualShadowPageTableTileCoordMask),
    .fallback_lod_offset = static_cast<std::uint8_t>(
      (packed_entry >> kVirtualShadowPageTableFallbackLodOffsetShift)
      & kVirtualShadowPageTableFallbackLodOffsetMask),
    .current_lod_valid
    = (packed_entry & kVirtualShadowPageTableCurrentLodValidBit) != 0U,
    .any_lod_valid
    = (packed_entry & kVirtualShadowPageTableAnyLodValidBit) != 0U,
    .requested_this_frame
    = (packed_entry & kVirtualShadowPageTableRequestedThisFrameBit) != 0U,
  };
}

[[nodiscard]] constexpr auto VirtualShadowPageTableEntryHasCurrentLod(
  const std::uint32_t packed_entry) -> bool
{
  return DecodeVirtualShadowPageTableEntry(packed_entry).current_lod_valid;
}

[[nodiscard]] constexpr auto VirtualShadowPageTableEntryHasAnyLod(
  const std::uint32_t packed_entry) -> bool
{
  return DecodeVirtualShadowPageTableEntry(packed_entry).any_lod_valid;
}

[[nodiscard]] constexpr auto ResolveVirtualShadowFallbackClipIndex(
  const std::uint32_t clip_index, const std::uint32_t clip_level_count,
  const std::uint32_t packed_entry) -> std::uint32_t
{
  const auto decoded = DecodeVirtualShadowPageTableEntry(packed_entry);
  if (!decoded.any_lod_valid || clip_level_count == 0U) {
    return clip_level_count;
  }

  return std::min(clip_level_count - 1U,
    clip_index + static_cast<std::uint32_t>(decoded.fallback_lod_offset));
}

} // namespace oxygen::renderer
