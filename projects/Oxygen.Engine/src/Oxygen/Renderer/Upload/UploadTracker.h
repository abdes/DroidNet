//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <condition_variable>
#include <expected>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/OxCo/Value.h>
#include <Oxygen/Renderer/Upload/Types.h>
#include <Oxygen/Renderer/Upload/UploaderTag.h>
#include <Oxygen/Renderer/api_export.h>

namespace oxygen::engine::upload {

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
  OXGN_RNDR_API auto RegisterFailedImmediate(
    std::string_view debug_name, UploadError error) -> UploadTicket;

  // Advance completed fence and mark all eligible tickets as completed.
  OXGN_RNDR_API auto MarkFenceCompleted(FenceValue completed) -> void;

  // Queries
  OXGN_RNDR_API auto IsComplete(TicketId id) const
    -> std::expected<bool, UploadError>;
  OXGN_RNDR_API auto TryGetResult(TicketId id) const
    -> std::optional<UploadResult>;
  OXGN_RNDR_API auto Await(TicketId id)
    -> std::expected<UploadResult, UploadError>;
  OXGN_RNDR_API auto AwaitAll(std::span<const UploadTicket> tickets)
    -> std::expected<std::vector<UploadResult>, UploadError>;

  // Wait for all currently pending (non-completed) tickets tracked by the
  // UploadTracker. This is a best-effort helper for shutdown: it collects
  // the set of outstanding tickets and waits until they complete. If ticket
  // entries are erased while waiting (frame lifecycle cleanup), the method
  // will retry until no pending tickets remain.
  OXGN_RNDR_API auto AwaitAllPending()
    -> std::expected<std::vector<UploadResult>, UploadError>;

  // Coroutine helper accessors
  OXGN_RNDR_API auto CompletedFence() const noexcept -> FenceValue;
  OXGN_RNDR_API auto CompletedFenceValue() noexcept
    -> oxygen::co::Value<FenceValue>&;

  // Best-effort cancellation: if found and not yet completed, mark canceled.
  OXGN_RNDR_API auto Cancel(TicketId id) -> std::expected<bool, UploadError>;
  // Query whether there are any pending (not yet completed) entries.
  OXGN_RNDR_API auto HasPending() const -> bool;
  // Returns the highest fence value that has been registered. Use during
  // shutdown to wait for any recorded submissions even when per-ticket
  // entries are erased due to frame lifecycle cleanup.
  OXGN_RNDR_API auto LastRegisteredFence() const -> FenceValue;
  // Frame lifecycle management: cleanup entries for cycling slot
  OXGN_RNDR_API auto OnFrameStart(UploaderTag, frame::Slot slot) -> void;

private:
  struct Entry {
    FenceValue fence { oxygen::graphics::fence::kInvalidValue };
    uint64_t bytes { 0 };
    std::string name;
    bool completed { false };
    UploadResult result {};
    frame::Slot creation_slot { frame::kInvalidSlot };
  };

  auto MarkEntryCompleted(Entry& e) -> void;

  mutable std::mutex mu_;
  std::condition_variable cv_;

  // Monotonic completed fence for coroutine waits.
  oxygen::co::Value<FenceValue> completed_fence_ { FenceValue { 0 } };

  // Last fence value observed during registration. This allows shutdown to
  // wait for any recorded submissions even if individual ticket entries are
  // later removed due to frame-slot cleanup.
  std::atomic<std::uint64_t> last_registered_fence_raw_ { 0 };

  TicketId next_ticket_ { 1 };
  std::unordered_map<TicketId, Entry> entries_;
  frame::Slot current_slot_ { frame::kInvalidSlot };
};

} // namespace oxygen::engine::upload
