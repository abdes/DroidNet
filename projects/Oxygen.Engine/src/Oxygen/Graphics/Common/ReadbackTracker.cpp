//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <chrono>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Graphics/Common/ReadbackTracker.h>

namespace oxygen::graphics {

ReadbackTracker::ReadbackTracker() = default;
ReadbackTracker::~ReadbackTracker() = default;

auto ReadbackTracker::Register(const FenceValue fence, const SizeBytes bytes,
  const std::string_view debug_name) -> ReadbackTicket
{
  std::lock_guard<std::mutex> lk(mu_);
  const auto id = next_ticket_;
  next_ticket_ = ReadbackTicketId { id.get() + 1 };

  auto& e = entries_[id];
  e.ticket = ReadbackTicket { id, fence };
  e.bytes = bytes;
  e.name.assign(debug_name);
  e.completed = false;
  e.result.ticket = e.ticket;
  e.result.bytes_copied = SizeBytes { 0 };
  e.result.error = std::nullopt;
  e.creation_slot = current_slot_;
  last_registered_fence_raw_.store(fence.get());
  return e.ticket;
}

auto ReadbackTracker::RegisterFailedImmediate(const std::string_view debug_name,
  const ReadbackError error) -> ReadbackTicket
{
  std::lock_guard<std::mutex> lk(mu_);
  const auto id = next_ticket_;
  next_ticket_ = ReadbackTicketId { id.get() + 1 };

  auto& e = entries_[id];
  e.ticket = ReadbackTicket { id, completed_fence_.Get() };
  e.bytes = SizeBytes { 0 };
  e.name.assign(debug_name);
  e.completed = true;
  e.result.ticket = e.ticket;
  e.result.bytes_copied = SizeBytes { 0 };
  e.result.error = error;
  e.creation_slot = current_slot_;
  last_registered_fence_raw_.store(e.ticket.fence.get());
  return e.ticket;
}

auto ReadbackTracker::MarkFenceCompleted(const FenceValue completed) -> void
{
  {
    std::lock_guard<std::mutex> lk(mu_);
    if (completed_fence_.Get() < completed) {
      completed_fence_.Set(completed);
    }
    for (auto& [id, e] : entries_) {
      (void)id;
      if (!e.completed && e.ticket.fence <= completed) {
        MarkEntryCompleted(e);
      }
    }
  }
  cv_.notify_all();
}

auto ReadbackTracker::IsComplete(const ReadbackTicketId id) const
  -> std::expected<bool, ReadbackError>
{
  std::lock_guard<std::mutex> lk(mu_);
  if (const auto it = entries_.find(id); it != entries_.end()) {
    return it->second.completed;
  }
  return std::unexpected(ReadbackError::kTicketNotFound);
}

auto ReadbackTracker::TryGetResult(const ReadbackTicketId id) const
  -> std::optional<ReadbackResult>
{
  std::lock_guard<std::mutex> lk(mu_);
  if (const auto it = entries_.find(id); it != entries_.end()) {
    if (it->second.completed) {
      return it->second.result;
    }
  }
  return std::nullopt;
}

auto ReadbackTracker::Await(const ReadbackTicketId id)
  -> std::expected<ReadbackResult, ReadbackError>
{
  std::unique_lock lk(mu_);
  constexpr auto kAwaitWarningInterval = std::chrono::seconds { 1 };
  uint64_t warning_count = 0;

  while (true) {
    const auto it = entries_.find(id);
    if (it == entries_.end()) {
      return std::unexpected(ReadbackError::kTicketNotFound);
    }
    if (it->second.completed) {
      return it->second.result;
    }

    const auto woke_for_completion
      = cv_.wait_for(lk, kAwaitWarningInterval, [&]() noexcept {
          const auto current = entries_.find(id);
          return current == entries_.end() || current->second.completed;
        });
    if (woke_for_completion) {
      continue;
    }

    const auto current = entries_.find(id);
    if (current == entries_.end()) {
      return std::unexpected(ReadbackError::kTicketNotFound);
    }

    ++warning_count;
    const auto waited_ms
      = std::chrono::duration_cast<std::chrono::milliseconds>(
        kAwaitWarningInterval * static_cast<int64_t>(warning_count))
          .count();
    LOG_F(WARNING,
      "ReadbackTracker::Await still waiting for ticket {} (`{}`) after {} ms "
      "(ticket_fence={} completed_fence={} creation_slot={}). If no queue "
      "completion can advance this ticket, the caller is deadlocked.",
      current->second.ticket.id.get(), current->second.name.c_str(), waited_ms,
      current->second.ticket.fence.get(), completed_fence_.Get().get(),
      current->second.creation_slot.get());
  }
}

auto ReadbackTracker::AwaitAll(const std::span<const ReadbackTicket> tickets)
  -> std::expected<std::vector<ReadbackResult>, ReadbackError>
{
  std::unique_lock lk(mu_);

  for (const auto& t : tickets) {
    if (!entries_.contains(t.id)) {
      return std::unexpected(ReadbackError::kTicketNotFound);
    }
  }

  cv_.wait(lk, [&]() {
    return std::ranges::all_of(
      tickets, [&](const auto& t) { return entries_[t.id].completed; });
  });

  std::vector<ReadbackResult> results;
  for (const auto& t : tickets) {
    results.push_back(entries_[t.id].result);
  }
  return results;
}

auto ReadbackTracker::CompletedFence() const noexcept -> FenceValue
{
  return completed_fence_.Get();
}

auto ReadbackTracker::CompletedFenceValue() noexcept
  -> oxygen::co::Value<FenceValue>&
{
  return completed_fence_;
}

auto ReadbackTracker::Cancel(const ReadbackTicketId id)
  -> std::expected<bool, ReadbackError>
{
  std::lock_guard<std::mutex> lk(mu_);
  const auto it = entries_.find(id);
  if (it == entries_.end()) {
    return std::unexpected(ReadbackError::kTicketNotFound);
  }
  auto& e = it->second;
  if (e.completed) {
    return false;
  }
  e.completed = true;
  e.result.error = ReadbackError::kCancelled;
  e.result.bytes_copied = SizeBytes { 0 };
  cv_.notify_all();
  return true;
}

auto ReadbackTracker::HasPending() const -> bool
{
  std::lock_guard<std::mutex> lk(mu_);
  return std::ranges::any_of(
    entries_, [](const auto& p) { return !p.second.completed; });
}

auto ReadbackTracker::LastRegisteredFence() const -> FenceValue
{
  const auto raw = last_registered_fence_raw_.load();
  return FenceValue { raw };
}

auto ReadbackTracker::MarkEntryCompleted(Entry& e) -> void
{
  e.completed = true;
  e.result.ticket = e.ticket;
  e.result.bytes_copied = e.bytes;
  e.result.error = std::nullopt;
}

auto ReadbackTracker::OnFrameStart(const frame::Slot slot) -> void
{
  CHECK_LT_F(slot, frame::kMaxSlot, "Frame slot out of bounds");

  std::lock_guard<std::mutex> lk(mu_);
  current_slot_ = slot;

  std::erase_if(entries_,
    [slot](const auto& pair) { return pair.second.creation_slot == slot; });
}

auto ReadbackTracker::AwaitAllPending()
  -> std::expected<std::vector<ReadbackResult>, ReadbackError>
{
  while (true) {
    std::vector<ReadbackTicket> pending;
    {
      std::lock_guard<std::mutex> lk(mu_);
      for (const auto& p : entries_) {
        if (!p.second.completed) {
          pending.emplace_back(p.second.ticket);
        }
      }
    }

    if (pending.empty()) {
      return std::vector<ReadbackResult> {};
    }

    auto res = AwaitAll(pending);
    if (res) {
      return res;
    }
    if (res.error() == ReadbackError::kTicketNotFound) {
      continue;
    }
    return std::unexpected(res.error());
  }
}

} // namespace oxygen::graphics
