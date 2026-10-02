//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>

#include <Oxygen/Base/ScopeGuard.h>
#include <Oxygen/OxCo/Value.h>
#include <Oxygen/Vortex/Upload/UploadTicket.h>

namespace oxygen::vortex::upload::internal {

struct UploadProgress {
  FenceValue completed { 0 };
  std::optional<UploadError> closed;
};

struct UploadTimeline {
  std::mutex mutex;
  std::condition_variable changed;
  UploadProgress progress {};
  FenceValue submitted { 0 };
  TicketId next_ticket { 1 };
  // Only the upload-owner thread accesses coroutine notifications.
  co::Value<UploadProgress> notification { UploadProgress {} };
  bool notifying { false };

  // Caller retains this timeline. Nested progress is delivered after Value
  // reparks any waiters detached by the outer notification.
  auto Notify() -> void
  {
    if (notifying) {
      return;
    }
    notifying = true;
    const ScopeGuard reset([this]() noexcept { notifying = false; });
    while (true) {
      UploadProgress latest {};
      {
        const std::scoped_lock lock(mutex);
        latest = progress;
      }
      const auto& published = notification.Get();
      if (latest.completed == published.completed
        && latest.closed == published.closed) {
        return;
      }
      notification.Set(latest);
    }
  }
};

struct UploadRecord {
  std::shared_ptr<UploadTimeline> timeline;
  TicketId id { 0 };
  FenceValue fence { 0 };
  std::uint64_t bytes { 0 };
  std::optional<UploadError> failure;

  //! Caller holds timeline->mutex.
  [[nodiscard]] auto Result() const -> std::optional<UploadResult>
  {
    if (failure) {
      return UploadResult {
        .success = false, .bytes_uploaded = 0, .error = failure
      };
    }
    if (fence <= timeline->progress.completed) {
      return UploadResult {
        .success = true, .bytes_uploaded = bytes, .error = std::nullopt
      };
    }
    if (timeline->progress.closed) {
      return UploadResult { .success = false,
        .bytes_uploaded = 0,
        .error = timeline->progress.closed };
    }
    return std::nullopt;
  }
};

} // namespace oxygen::vortex::upload::internal
