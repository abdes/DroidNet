//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstring>
#include <string>
#include <string_view>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Vortex/Upload/InlineTransfersCoordinator.h>
#include <Oxygen/Vortex/Upload/StagingProvider.h>
#include <Oxygen/Vortex/Upload/TransientStructuredBuffer.h>

namespace oxygen::vortex::internal {

//! Generic publisher for per-view structured payloads backed by transient SRVs.
template <typename Payload> class PerViewStructuredPublisher {
public:
  PerViewStructuredPublisher(observer_ptr<Graphics> gfx,
    upload::StagingProvider& staging,
    observer_ptr<upload::InlineTransfersCoordinator> inline_transfers,
    std::string_view debug_label)
    : debug_label_(
        debug_label.empty() ? "PerViewStructuredPublisher" : debug_label)
    , buffer_(gfx, staging, static_cast<std::uint32_t>(sizeof(Payload)),
        inline_transfers, debug_label_)
  {
  }

  OXYGEN_MAKE_NON_COPYABLE(PerViewStructuredPublisher)
  OXYGEN_DEFAULT_MOVABLE(PerViewStructuredPublisher)

  auto OnFrameStart(frame::SequenceNumber sequence, frame::Slot slot) -> void
  {
    buffer_.OnFrameStart(sequence, slot);
  }

  auto Publish(ViewId view_id, const Payload& payload) -> ShaderVisibleIndex
  {
    auto allocation = buffer_.Allocate(1);
    if (!allocation) {
      LOG_F(ERROR, "{}: failed to allocate payload for view {}: {}",
        debug_label_, view_id.get(), allocation.error().message());
      return kInvalidShaderVisibleIndex;
    }

    if (!allocation->srv.IsValid() || allocation->mapped_ptr == nullptr) {
      LOG_F(ERROR, "{}: invalid allocation for view {}", debug_label_,
        view_id.get());
      return kInvalidShaderVisibleIndex;
    }

    std::memcpy(allocation->mapped_ptr, &payload, sizeof(Payload));
    return allocation->srv;
  }

private:
  std::string debug_label_;
  upload::TransientStructuredBuffer buffer_;
};

} // namespace oxygen::vortex::internal
