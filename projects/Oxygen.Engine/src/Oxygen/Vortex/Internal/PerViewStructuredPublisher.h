//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <expected>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

#include <fmt/format.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Vortex/Upload/Errors.h>
#include <Oxygen/Vortex/Upload/InlineTransfersCoordinator.h>
#include <Oxygen/Vortex/Upload/StagingProvider.h>
#include <Oxygen/Vortex/Upload/TransientStructuredBuffer.h>

namespace oxygen::vortex::internal {

//! The published slot, or an invalid one after logging which `record` failed
//! for `view_id`. For callers whose own result already reports an invalid slot
//! as a rejection.
inline auto SlotOrInvalid(
  const std::expected<ShaderVisibleIndex, std::error_code>& published,
  const ViewId view_id, const std::string_view record) -> ShaderVisibleIndex
{
  if (published) {
    return *published;
  }
  LOG_F(ERROR, "View {}: {} was not published: {}", view_id.get(), record,
    published.error().message());
  return kInvalidShaderVisibleIndex;
}

//! The published slot of a record the current recording cannot do without.
/*!
 Throws when `record` was not published, so the view's recording fails and the
 scene renderer rejects the view with this reason.
*/
inline auto RequirePublished(
  const std::expected<ShaderVisibleIndex, std::error_code>& published,
  const ViewId view_id, const std::string_view record) -> ShaderVisibleIndex
{
  if (!published) {
    throw std::runtime_error(fmt::format("{} was not published for view {}: {}",
      record, view_id.get(), published.error().message()));
  }
  return *published;
}

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

  //! Publish `payload` as a new record for `view_id`.
  /*!
   Fails with the allocation error. The buffer logs budget rejections once per
   frame; callers decide whether the record was required and reject the view.
  */
  auto Publish(ViewId view_id, const Payload& payload)
    -> std::expected<ShaderVisibleIndex, std::error_code>
  {
    auto allocation = buffer_.Allocate(1);
    if (!allocation) {
      return std::unexpected(allocation.error());
    }
    if (!allocation->srv.IsValid() || !allocation->TryWriteObject(payload)) {
      LOG_F(ERROR, "{}: payload for view {} was not written", debug_label_,
        view_id.get());
      return std::unexpected(
        make_error_code(upload::UploadError::kStagingAllocFailed));
    }
    return allocation->srv;
  }

private:
  std::string debug_label_;
  upload::TransientStructuredBuffer buffer_;
};

} // namespace oxygen::vortex::internal
