//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at <https://opensource.org/licenses/BSD-3-Clause>.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//
#pragma once
#include <Oxygen/Graphics/Common/api_export.h>
namespace oxygen::graphics {
class CommandQueue;
namespace internal {
  //! Internal deterministic failure seam shared by native backend
  //! qualification. One-shot; never enabled by renderer code or runtime
  //! configuration.
  enum class SubmissionFailurePoint {
    kNone,
    kBeforeIssue,
    kAfterFirstList,
    kAfterIssueBeforeMarker
  };
  class SubmissionFaultTestAccess final {
  public:
    OXGN_GFX_API static auto LoseDevice(CommandQueue& queue) noexcept -> void;
    OXGN_GFX_API static auto FailNext(
      CommandQueue& queue, SubmissionFailurePoint point) -> void;
  };
}
}
