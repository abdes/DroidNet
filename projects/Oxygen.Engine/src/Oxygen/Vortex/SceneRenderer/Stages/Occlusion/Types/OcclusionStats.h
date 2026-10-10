//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>

#include <Oxygen/Core/Types/Frame.h>

namespace oxygen::vortex {

//! One culling view's occlusion counters for one frame, counted on the GPU.
/*!
 Diagnostics only: they are read back asynchronously, frames late, and
 rendering never reads them.

 ABI: one `uint` per counter, in this order, mirrored by the `OCCLUSION_STAT_*`
 indices of `OcclusionCull.hlsl`.
*/
struct OcclusionCounters {
  std::uint32_t draw_count { 0U };
  std::uint32_t in_frustum_count { 0U };
  //! Inside the clip volume but covering no pixel center.
  std::uint32_t coverage_culled_count { 0U };
  std::uint32_t history_slot_count { 0U };
  std::uint32_t phase1_drawn_count { 0U };
  std::uint32_t phase2_drawn_count { 0U };
  //! In the frustum and behind the phase 1 depth.
  std::uint32_t occluded_count { 0U };
  //! Translucent draws among the occluded ones.
  std::uint32_t translucent_culled_count { 0U };

  auto operator==(const OcclusionCounters&) const -> bool = default;
};
static_assert(sizeof(OcclusionCounters) == 32U); // NOLINT(*-magic-numbers)

//! The latest counters read back for a culling view.
struct OcclusionStats {
  OcclusionCounters counters {};
  //! The frame the counters were recorded in.
  frame::SequenceNumber frame_sequence { 0U };
  //! Occlusion was on for that frame; phase 2 ran.
  bool occlusion_enabled { false };
};

} // namespace oxygen::vortex
