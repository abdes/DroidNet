//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <unordered_map>
#include <vector>

#include <Oxygen/Vortex/PreparedSceneFrame.h>
#include <Oxygen/Vortex/api_export.h>

namespace oxygen::vortex::occlusion::internal {

//! The draw has no visibility history slot.
inline constexpr std::uint32_t kNoHistorySlot
  = std::numeric_limits<std::uint32_t>::max();

//! Marks a slot word whose slot is new this frame. Mirrored by
//! `OCCLUSION_CULL_FRESH_HISTORY_SLOT` in `OcclusionCull.hlsl`.
inline constexpr std::uint32_t kFreshHistorySlotBit = 1U << 31U;

//! Maps draws to stable visibility-history slots for one culling view.
/*!
 Draw indices follow each frame's sort order, so a draw's identity across
 frames is its `DrawSource` (node, LOD, submesh, mesh view). Each frame,
 `Update`:

 1. keeps the slot of every key seen last frame,
 2. gives each new key the lowest free slot and marks it fresh (its history
    reads as not visible),
 3. frees the slots of keys absent this frame,
 4. gives no slot to a key that produced more than one draw this frame.

 Slots are dense from 0, so `Capacity()` sizes the GPU history buffer. The
 capacity only grows, and growth never moves an existing slot. Slots stay
 below `kFreshHistorySlotBit`.
*/
class HistorySlotAllocator {
public:
  //! A draw's history slot this frame.
  struct Assignment {
    std::uint32_t slot { kNoHistorySlot };
    //! The slot is new this frame; its history must read as not visible.
    bool fresh { false };

    auto operator==(const Assignment&) const -> bool = default;

    //! The word the cull kernels read: the slot, with `kFreshHistorySlotBit`
    //! when fresh, or `kNoHistorySlot`.
    [[nodiscard]] constexpr auto ToSlotWord() const noexcept -> std::uint32_t
    {
      if (slot == kNoHistorySlot) {
        return kNoHistorySlot;
      }
      return fresh ? (slot | kFreshHistorySlotBit) : slot;
    }
  };

  //! Assigns one slot per draw, in the order of `draws`.
  OXGN_VRTX_NDAPI auto Update(
    std::span<const PreparedSceneFrame::DrawSource> draws)
    -> std::vector<Assignment>;

  //! Forgets every key, so the next update treats all draws as fresh.
  OXGN_VRTX_API auto Reset() -> void;

  //! Number of slots the GPU history buffer must hold.
  [[nodiscard]] auto Capacity() const noexcept -> std::uint32_t
  {
    return capacity_;
  }

private:
  struct KeyHash {
    auto operator()(const PreparedSceneFrame::DrawSource& key) const noexcept
      -> std::size_t;
  };
  struct KeyEqual {
    auto operator()(const PreparedSceneFrame::DrawSource& lhs,
      const PreparedSceneFrame::DrawSource& rhs) const noexcept -> bool;
  };

  auto AllocateSlot() -> std::uint32_t;

  std::unordered_map<PreparedSceneFrame::DrawSource, std::uint32_t, KeyHash,
    KeyEqual>
    slots_;
  //! Freed slots, kept sorted descending so the lowest is taken first.
  std::vector<std::uint32_t> free_slots_;
  std::uint32_t capacity_ { 0U };
};

} // namespace oxygen::vortex::occlusion::internal
