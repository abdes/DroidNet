//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <string>
#include <system_error>
#include <type_traits>

namespace oxygen::graphics {

enum class ReadbackError : int {
  kInvalidArgument = 1,
  kNotReady = 2,
  kAlreadyMapped = 3,
  kAlreadyPending = 4,
  kUnsupportedFormat = 5,
  kUnsupportedResource = 6,
  kQueueUnavailable = 7,
  kTicketNotFound = 8,
  kCancelled = 9,
  kBackendFailure = 10,
  kShutdown = 11,
  kWouldDeadlock = 12,
};

inline auto to_string(ReadbackError value) -> const char*
{
  // clang-format off
  switch (value) {
  case ReadbackError::kInvalidArgument: return "kInvalidArgument";
  case ReadbackError::kNotReady: return "kNotReady";
  case ReadbackError::kAlreadyMapped: return "kAlreadyMapped";
  case ReadbackError::kAlreadyPending: return "kAlreadyPending";
  case ReadbackError::kUnsupportedFormat: return "kUnsupportedFormat";
  case ReadbackError::kUnsupportedResource: return "kUnsupportedResource";
  case ReadbackError::kQueueUnavailable: return "kQueueUnavailable";
  case ReadbackError::kTicketNotFound: return "kTicketNotFound";
  case ReadbackError::kCancelled: return "kCancelled";
  case ReadbackError::kBackendFailure: return "kBackendFailure";
  case ReadbackError::kShutdown: return "kShutdown";
  case ReadbackError::kWouldDeadlock: return "kWouldDeadlock";
  }
  // clang-format on
  return "Unknown";
}

class ReadbackErrorCategory : public std::error_category {
public:
  const char* name() const noexcept override { return "Readback Error"; }

  std::string message(int ev) const override
  {
    switch (static_cast<ReadbackError>(ev)) {
    case ReadbackError::kInvalidArgument:
      return "Readback request contains invalid parameters";
    case ReadbackError::kNotReady:
      return "Readback result is not ready yet";
    case ReadbackError::kAlreadyMapped:
      return "Readback resource is already mapped";
    case ReadbackError::kAlreadyPending:
      return "Readback object already has a pending request";
    case ReadbackError::kUnsupportedFormat:
      return "Readback format is not supported by this backend";
    case ReadbackError::kUnsupportedResource:
      return "Readback resource is not supported by this backend";
    case ReadbackError::kQueueUnavailable:
      return "Required queue is not available for readback";
    case ReadbackError::kTicketNotFound:
      return "Readback ticket is invalid or has already been consumed";
    case ReadbackError::kCancelled:
      return "Readback operation was cancelled before completion";
    case ReadbackError::kBackendFailure:
      return "Backend failed while processing readback operation";
    case ReadbackError::kShutdown:
      return "Readback manager is shutting down";
    case ReadbackError::kWouldDeadlock:
      return "Readback await would deadlock because no progress source can "
             "complete the ticket";
    default:
      return "Unknown readback subsystem error";
    }
  }
};

inline auto GetReadbackErrorCategory() noexcept -> const ReadbackErrorCategory&
{
  static ReadbackErrorCategory instance;
  return instance;
}

inline auto make_error_code(ReadbackError e) noexcept -> std::error_code
{
  return { static_cast<int>(e), GetReadbackErrorCategory() };
}

} // namespace oxygen::graphics

template <>
struct std::is_error_code_enum<oxygen::graphics::ReadbackError> : true_type { };
