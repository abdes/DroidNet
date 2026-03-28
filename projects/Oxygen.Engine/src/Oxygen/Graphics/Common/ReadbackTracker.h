//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <atomic>
#include <condition_variable>
#include <expected>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Graphics/Common/ReadbackErrors.h>
#include <Oxygen/Graphics/Common/ReadbackTypes.h>
#include <Oxygen/Graphics/Common/api_export.h>
#include <Oxygen/OxCo/Value.h>

namespace oxygen::graphics {

class ReadbackTracker {
public:
  OXGN_GFX_API ReadbackTracker();

  OXYGEN_MAKE_NON_COPYABLE(ReadbackTracker)
  OXYGEN_DEFAULT_MOVABLE(ReadbackTracker)

  OXGN_GFX_API ~ReadbackTracker();

  OXGN_GFX_API auto Register(FenceValue fence, SizeBytes bytes,
    std::string_view debug_name) -> ReadbackTicket;
  OXGN_GFX_API auto RegisterFailedImmediate(
    std::string_view debug_name, ReadbackError error) -> ReadbackTicket;

  OXGN_GFX_API auto MarkFenceCompleted(FenceValue completed) -> void;

  OXGN_GFX_API auto IsComplete(ReadbackTicketId id) const
    -> std::expected<bool, ReadbackError>;
  OXGN_GFX_API auto TryGetResult(ReadbackTicketId id) const
    -> std::optional<ReadbackResult>;
  OXGN_GFX_API auto Await(ReadbackTicketId id)
    -> std::expected<ReadbackResult, ReadbackError>;
  OXGN_GFX_API auto AwaitAll(std::span<const ReadbackTicket> tickets)
    -> std::expected<std::vector<ReadbackResult>, ReadbackError>;
  OXGN_GFX_API auto AwaitAllPending()
    -> std::expected<std::vector<ReadbackResult>, ReadbackError>;
  OXGN_GFX_API auto Forget(ReadbackTicketId id)
    -> std::expected<bool, ReadbackError>;

  [[nodiscard]] OXGN_GFX_API auto CompletedFence() const noexcept -> FenceValue;
  OXGN_GFX_API auto CompletedFenceValue() noexcept
    -> oxygen::co::Value<FenceValue>&;

  OXGN_GFX_API auto Cancel(ReadbackTicketId id)
    -> std::expected<bool, ReadbackError>;
  [[nodiscard]] OXGN_GFX_API auto HasPending() const -> bool;
  [[nodiscard]] OXGN_GFX_API auto LastRegisteredFence() const -> FenceValue;
  OXGN_GFX_API auto OnFrameStart(frame::Slot slot) -> void;

private:
  struct Entry {
    ReadbackTicket ticket {};
    SizeBytes bytes {};
    std::string name;
    bool completed { false };
    ReadbackResult result {};
    frame::Slot creation_slot { frame::kInvalidSlot };
  };

  auto MarkEntryCompleted(Entry& e) -> void;

  mutable std::mutex mu_;
  std::condition_variable cv_;
  oxygen::co::Value<FenceValue> completed_fence_ { FenceValue { 0 } };
  std::atomic<std::uint64_t> last_registered_fence_raw_ { 0 };
  ReadbackTicketId next_ticket_ { 1 };
  std::unordered_map<ReadbackTicketId, Entry> entries_;
  frame::Slot current_slot_ { frame::kInvalidSlot };
};

} // namespace oxygen::graphics
