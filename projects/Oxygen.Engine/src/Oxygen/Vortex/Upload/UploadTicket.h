//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/NamedType.h>
#include <Oxygen/Graphics/Common/Types/FenceValue.h>
#include <Oxygen/Vortex/Upload/Errors.h>
#include <Oxygen/Vortex/api_export.h>

namespace oxygen::co {
template <class T> class Co;
}

namespace oxygen::vortex::upload {

using TicketId = NamedType<std::uint64_t, struct TicketIdTag,
  DefaultInitialized, Comparable, Printable, Hashable>;

inline auto to_string(const TicketId& ticket)
{
  return std::to_string(ticket.get());
}

using graphics::FenceValue;

struct UploadResult {
  bool success { false };
  std::uint64_t bytes_uploaded { 0 };
  std::optional<UploadError> error;
};

namespace internal {
  struct UploadRecord;
  struct UploadTimeline;
}

//! Retains a CPU upload result independently of the coordinator and GPU
//! resources.
class UploadTicket {
public:
  UploadTicket() = delete;
  ~UploadTicket() = default;
  OXYGEN_DEFAULT_COPYABLE(UploadTicket)
  OXYGEN_DEFAULT_MOVABLE(UploadTicket)

  OXGN_VRTX_NDAPI auto Id() const noexcept -> TicketId;
  OXGN_VRTX_NDAPI auto Fence() const noexcept -> FenceValue;
  //! Null means pending. A moved-from ticket reports kTicketNotFound.
  OXGN_VRTX_NDAPI auto TryGetResult() const -> std::optional<UploadResult>;
  OXGN_VRTX_NDAPI auto Await() const -> UploadResult;
  //! Cancels the logical result, without canceling submitted GPU work.
  OXGN_VRTX_NDAPI auto Cancel() const -> bool;
  //! Retains state immediately; owner-thread execution waits for fence or
  //! close.
  OXGN_VRTX_NDAPI auto AwaitGpuCompletionAsync() const -> co::Co<UploadResult>;

private:
  friend class UploadTracker;
  explicit UploadTicket(std::shared_ptr<internal::UploadRecord> record);
  static auto AwaitGpuCompletion(std::shared_ptr<internal::UploadRecord> record)
    -> co::Co<UploadResult>;

  std::shared_ptr<internal::UploadRecord> record_;
};

} // namespace oxygen::vortex::upload
