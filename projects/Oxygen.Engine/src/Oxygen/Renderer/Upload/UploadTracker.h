//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <condition_variable>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <Oxygen/OxCo/Value.h>
#include <Oxygen/Renderer/Upload/Types.h>
#include <Oxygen/Renderer/Upload/UploadDiagnostics.h>
#include <Oxygen/Renderer/api_export.h>

namespace oxygen::engine::upload {

//=== UploadTracker ---------------------------------------------------------//
// Tracks submitted uploads by TicketId against a monotonic GPU fence value.
// Provides both coroutine-friendly waiting via co::Value<FenceValue> and
// blocking waits for synchronous paths.
class UploadTracker {
public:
  OXGN_RNDR_API UploadTracker();
  OXYGEN_MAKE_NON_COPYABLE(UploadTracker)
  OXYGEN_DEFAULT_MOVABLE(UploadTracker)
  OXGN_RNDR_API ~UploadTracker();

  // Register a new ticket that will complete when the given fence value is
  // reached. Returns the assigned TicketId and the same fence.
  OXGN_RNDR_API auto Register(FenceValue fence, uint64_t bytes,
    std::string_view debug_name) -> UploadTicket;

  // Register an immediate failed ticket (used when planning/fill fails).
  // The ticket is marked completed with the provided error/message.
  OXGN_RNDR_API auto RegisterFailedImmediate(std::string_view debug_name,
    UploadError error, std::string_view message = {}) -> UploadTicket;

  // Advance completed fence and mark all eligible tickets as completed.
  OXGN_RNDR_API auto MarkFenceCompleted(FenceValue completed) -> void;

  // Queries
  OXGN_RNDR_API auto IsComplete(TicketId id) const -> bool;
  OXGN_RNDR_API auto TryGetResult(TicketId id) const
    -> std::optional<UploadResult>;
  OXGN_RNDR_API auto Await(TicketId id) -> UploadResult;
  OXGN_RNDR_API auto AwaitAll(std::span<const UploadTicket> tickets)
    -> std::vector<UploadResult>;

  // Coroutine helper accessors
  OXGN_RNDR_API auto CompletedFence() const noexcept -> FenceValue;
  OXGN_RNDR_API auto CompletedFenceValue() noexcept
    -> oxygen::co::Value<FenceValue>&;

  // Diagnostics and control
  OXGN_RNDR_API auto GetStats() const -> UploadStats;
  // Best-effort cancellation: if found and not yet completed, mark canceled.
  OXGN_RNDR_API auto Cancel(TicketId id) -> bool;

private:
  struct Entry {
    FenceValue fence { oxygen::graphics::fence::kInvalidValue };
    uint64_t bytes { 0 };
    std::string name;
    bool completed { false };
    UploadResult result {};
  };

  auto MarkEntryCompleted_(Entry& e) -> void;

  mutable std::mutex mu_;
  std::condition_variable cv_;

  // Monotonic completed fence for coroutine waits.
  oxygen::co::Value<FenceValue> completed_fence_ { FenceValue { 0 } };

  TicketId next_ticket_ { 1 };
  std::unordered_map<TicketId, Entry> entries_;

  // Stats (not atomic; protected by mu_)
  UploadStats stats_ {};
};

} // namespace oxygen::engine::upload
