//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at <https://opensource.org/licenses/BSD-3-Clause>.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <exception>
#include <limits>
#include <optional>

#include <Oxygen/Graphics/Common/BackendLifetime.h>
#include <Oxygen/Graphics/Common/SubmissionCallback.h>

namespace oxygen::graphics {
using QueueIdentity
  = NamedType<uint64_t, struct QueueIdentityTag, Comparable, Hashable>;

//! A copied queue-completion value; it owns no resource or native fence.
class CompletionReceipt final {
public:
  CompletionReceipt() = default;
  [[nodiscard]] auto Backend() const noexcept -> BackendIncarnationId
  {
    return backend_;
  }
  [[nodiscard]] auto Queue() const noexcept -> QueueIdentity { return queue_; }
  [[nodiscard]] auto Value() const noexcept -> uint64_t { return value_; }
  [[nodiscard]] auto IsValid() const noexcept -> bool
  {
    return backend_.get() != 0 && queue_.get() != 0 && value_ != 0
      && value_ != (std::numeric_limits<uint64_t>::max)();
  }
  auto operator==(const CompletionReceipt&) const -> bool = default;

private:
  friend class CommandQueue;
  CompletionReceipt(
    BackendIncarnationId backend, QueueIdentity queue, uint64_t value)
    : backend_(backend)
    , queue_(queue)
    , value_(value)
  {
  }
  BackendIncarnationId backend_ { 0 };
  QueueIdentity queue_ { 0 };
  uint64_t value_ { 0 };
};

enum class CompletionStatus : uint8_t {
  kPending,
  kComplete,
  kDeviceLost,
  kInvalid
};
struct SubmissionResult {
  SubmissionOutcome outcome { SubmissionOutcome::kDiscarded };
  std::optional<CompletionReceipt> receipt;
};
//! Legacy void-submit error with an already finalized, allocation-free outcome.
class SubmissionException final : public std::exception {
public:
  explicit SubmissionException(SubmissionResult result) noexcept
    : result_(result)
  {
  }
  auto what() const noexcept -> const char* override
  {
    return result_.outcome == SubmissionOutcome::kExecutionUncertain
      ? "Command execution is uncertain; backend recovery is required"
      : "Command submission rejected before issue";
  }
  [[nodiscard]] auto Result() const noexcept -> SubmissionResult
  {
    return result_;
  }

private:
  SubmissionResult result_;
};
enum class UseReleaseReason : uint8_t {
  kDiscarded,
  kCompleted,
  kDeviceLost,
  kAdmissionResolved
};
} // namespace oxygen::graphics
