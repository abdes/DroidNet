//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Vortex/Internal/StructuredGpuBuffer.h>
#include <Oxygen/Vortex/PreparedSceneFrame.h>
#include <Oxygen/Vortex/SceneRenderer/Stages/Occlusion/Internal/DrawCullPass.h>
#include <Oxygen/Vortex/SceneRenderer/Stages/Occlusion/Internal/HistorySlotAllocator.h>
#include <Oxygen/Vortex/api_export.h>

namespace oxygen {
class Graphics;
} // namespace oxygen

namespace oxygen::graphics {
class CommandRecorder;
} // namespace oxygen::graphics

namespace oxygen::vortex::upload {
class TransientStructuredBuffer;
} // namespace oxygen::vortex::upload

namespace oxygen::vortex::occlusion::internal {

//! The visibility history of one culling view, camera or shadow.
/*!
 Owns the view's history slots and its GPU history, one `uint` per slot.
 Each frame, `Prepare` assigns the draws' slots, uploads their slot words and
 grows the history, copying it so it survives. The history is valid for the
 next frame only when that frame's phase 2 wrote it (`SetWritten`).
*/
class CullingViewHistory {
public:
  OXGN_VRTX_API explicit CullingViewHistory(std::string debug_name);
  OXGN_VRTX_API ~CullingViewHistory();

  OXYGEN_MAKE_NON_COPYABLE(CullingViewHistory)
  OXYGEN_DEFAULT_MOVABLE(CullingViewHistory)

  //! Binds this frame's history for `draws`; nothing when an upload or an
  //! allocation fails, and the view then culls by frustum only.
  /*!
   `reset` discards the history: phase 1 then draws every draw in the
   frustum. `slot_words` is the caller's per-frame upload buffer, already
   started for this frame.
  */
  OXGN_VRTX_API auto Prepare(graphics::CommandRecorder& recorder,
    const std::shared_ptr<Graphics>& gfx,
    upload::TransientStructuredBuffer& slot_words,
    std::span<const PreparedSceneFrame::DrawSource> draws,
    std::uint32_t draw_count, bool reset) -> std::optional<DrawCullHistory>;

  //! Records whether this frame's phase 2 wrote the history.
  auto SetWritten(const bool written) noexcept -> void { written_ = written; }

private:
  auto EnsureCapacity(graphics::CommandRecorder& recorder,
    const std::shared_ptr<Graphics>& gfx) -> bool;

  std::string debug_name_;
  HistorySlotAllocator slots_;
  //! Replaced, with its contents copied, when the slot capacity grows.
  std::unique_ptr<vortex::internal::StructuredGpuBuffer> history_;
  std::vector<std::uint32_t> slot_words_;
  bool written_ { false };
};

} // namespace oxygen::vortex::occlusion::internal
