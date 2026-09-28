//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <memory>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Vortex/Upload/UploadTicket.h>
#include <Oxygen/Vortex/api_export.h>

namespace oxygen::vortex::upload {

//! Owns upload progress, without retaining individual ticket records.
class UploadTracker {
public:
  OXGN_VRTX_API UploadTracker();
  OXGN_VRTX_API ~UploadTracker();
  OXYGEN_MAKE_NON_COPYABLE(UploadTracker)
  OXYGEN_MAKE_NON_MOVABLE(UploadTracker)

  OXGN_VRTX_NDAPI auto Register(FenceValue fence, std::uint64_t bytes)
    -> UploadTicket;
  OXGN_VRTX_NDAPI auto RegisterFailedImmediate(UploadError error)
    -> UploadTicket;

  //! Record issued GPU work before any subsequent allocation can fail.
  OXGN_VRTX_API auto RecordSubmission(FenceValue fence) -> void;
  //! Owner-thread progress/close may resume coroutine consumers inline.
  OXGN_VRTX_API auto MarkFenceCompleted(FenceValue completed) -> void;
  OXGN_VRTX_API auto Close(UploadError error = UploadError::kTrackerShutdown)
    -> void;
  OXGN_VRTX_NDAPI auto CompletedFence() const -> FenceValue;
  OXGN_VRTX_NDAPI auto LastSubmittedFence() const -> FenceValue;

private:
  std::shared_ptr<internal::UploadTimeline> timeline_;
};

} // namespace oxygen::vortex::upload
