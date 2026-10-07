//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <atomic>
#include <memory>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Graphics/Common/Types/FenceValue.h>
#include <Oxygen/Vortex/RendererTag.h>
#include <Oxygen/Vortex/Upload/Types.h>
#include <Oxygen/Vortex/api_export.h>

namespace oxygen {
class Graphics;
} // namespace oxygen

namespace oxygen::vortex::upload {

class StagingProvider;

//! Coordinates retirement for direct inline writes recorded on graphics queues.
class InlineTransfersCoordinator {
public:
  OXGN_VRTX_API explicit InlineTransfersCoordinator(observer_ptr<Graphics> gfx);

  OXYGEN_MAKE_NON_COPYABLE(InlineTransfersCoordinator)
  OXYGEN_DEFAULT_MOVABLE(InlineTransfersCoordinator)

  OXGN_VRTX_API ~InlineTransfersCoordinator();

  //! Track a staging provider whose allocations participate in inline writes.
  OXGN_VRTX_API auto RegisterProvider(
    const std::shared_ptr<StagingProvider>& provider) -> void;

  //! Record an inline write so retirement can be driven on the next frame.
  OXGN_VRTX_API auto NotifyInlineWrite(
    SizeBytes size, std::string_view source_label) noexcept -> void;

  //! Called once per frame slot before transient buffers reset their views.
  OXGN_VRTX_API auto OnFrameStart(
    vortex::RendererTag /*unused*/, frame::Slot slot) -> void;

  //! As above, then start frame `sequence` for the providers' transient
  //! consumers. Re-starting the frame in progress does nothing: its slot and
  //! the views published into it stay valid for its earlier work.
  OXGN_VRTX_API auto OnFrameStart(vortex::RendererTag tag,
    frame::SequenceNumber sequence, frame::Slot slot) -> void;

private:
  auto RetireCompleted() -> void;

  std::optional<std::pair<frame::SequenceNumber, frame::Slot>> started_frame_;

  observer_ptr<Graphics> gfx_;
  std::vector<std::weak_ptr<StagingProvider>> providers_;
  std::atomic<uint64_t> synthetic_fence_counter_ { 0 };
  std::atomic<uint64_t> pending_inline_bytes_ { 0 };
  std::atomic<bool> has_pending_inline_writes_ { false };
};

} // namespace oxygen::vortex::upload
