//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Renderer/Upload/UploadTracker.h>

#include <algorithm>

namespace oxygen::engine::upload {

UploadTracker::UploadTracker() = default;
UploadTracker::~UploadTracker() = default;

auto UploadTracker::Register(const FenceValue fence, const uint64_t bytes,
  const std::string_view debug_name) -> UploadTicket
{
  std::lock_guard<std::mutex> lk(mu_);
  const auto id = next_ticket_;
  next_ticket_ = TicketId { id.get() + 1 };

  auto& e = entries_[id];
  e.fence = fence;
  e.bytes = bytes;
  e.name.assign(debug_name);
  e.completed = false;
  e.result = UploadResult {};
  e.creation_slot = current_slot_;
  // Track the raw fence so shutdown can wait for any recorded submissions
  last_registered_fence_raw_.store(fence.get());
  return UploadTicket { id, fence };
}

auto UploadTracker::RegisterFailedImmediate(
  const std::string_view debug_name, const UploadError error) -> UploadTicket
{
  std::lock_guard<std::mutex> lk(mu_);
  const auto id = next_ticket_;
  next_ticket_ = TicketId { id.get() + 1 };

  auto& e = entries_[id];
  e.fence = completed_fence_.Get();
  e.bytes = 0;
  e.name.assign(debug_name);
  e.completed = true;
  e.result.success = false;
  e.result.bytes_uploaded = 0;
  e.result.error = error;
  last_registered_fence_raw_.store(e.fence.get());
  return UploadTicket { id, e.fence };
}

auto UploadTracker::MarkFenceCompleted(const FenceValue completed) -> void
{
  {
    std::lock_guard<std::mutex> lk(mu_);
    if (completed_fence_.Get() < completed) {
      completed_fence_.Set(completed);
    }
    for (auto& [id, e] : entries_) {
      (void)id;
      if (!e.completed && e.fence <= completed) {
        MarkEntryCompleted(e);
      }
    }
  }
  cv_.notify_all();
}

auto UploadTracker::IsComplete(const TicketId id) const
  -> std::expected<bool, UploadError>
{
  std::lock_guard<std::mutex> lk(mu_);
  if (const auto it = entries_.find(id); it != entries_.end()) {
    return it->second.completed;
  }
  return std::unexpected(UploadError::kTicketNotFound);
}

auto UploadTracker::TryGetResult(const TicketId id) const
  -> std::optional<UploadResult>
{
  std::lock_guard<std::mutex> lk(mu_);
  if (const auto it = entries_.find(id); it != entries_.end()) {
    if (it->second.completed) {
      return it->second.result;
    }
  }
  return std::nullopt;
}

auto UploadTracker::Await(const TicketId id)
  -> std::expected<UploadResult, UploadError>
{
  std::unique_lock lk(mu_);

  // Check if ticket exists first
  const auto it = entries_.find(id);
  if (it == entries_.end()) {
    return std::unexpected(UploadError::kTicketNotFound);
  }

  cv_.wait(lk, [&]() noexcept { return it->second.completed; });

  return it->second.result;
}

auto UploadTracker::AwaitAll(const std::span<const UploadTicket> tickets)
  -> std::expected<std::vector<UploadResult>, UploadError>
{
  std::unique_lock lk(mu_);

  for (const auto& t : tickets) {
    if (!entries_.contains(t.id)) {
      return std::unexpected(UploadError::kTicketNotFound);
    }
  }

  cv_.wait(lk, [&]() {
    return std::ranges::all_of(
      tickets, [&](const auto& t) { return entries_[t.id].completed; });
  });

  std::vector<UploadResult> results;
  for (const auto& t : tickets) {
    results.push_back(entries_[t.id].result);
  }
  return results;
}

auto UploadTracker::CompletedFence() const noexcept -> FenceValue
{
  return completed_fence_.Get();
}

auto UploadTracker::CompletedFenceValue() noexcept
  -> oxygen::co::Value<FenceValue>&
{
  return completed_fence_;
}

auto UploadTracker::Cancel(const TicketId id)
  -> std::expected<bool, UploadError>
{
  std::lock_guard<std::mutex> lk(mu_);
  const auto it = entries_.find(id);
  if (it == entries_.end()) {
    return std::unexpected(UploadError::kTicketNotFound);
  }
  auto& e = it->second;
  if (e.completed) {
    return false; // Too late to cancel, but not an error
  }
  // Mark as completed canceled
  e.completed = true;
  e.result.success = false;
  e.result.bytes_uploaded = 0;
  e.result.error = UploadError::kCanceled;
  cv_.notify_all();
  return true;
}

auto UploadTracker::HasPending() const -> bool
{
  std::lock_guard<std::mutex> lk(mu_);
  return std::ranges::any_of(
    entries_, [](const auto& p) { return !p.second.completed; });
}

auto UploadTracker::LastRegisteredFence() const -> FenceValue
{
  const auto raw = last_registered_fence_raw_.load();
  return FenceValue { raw };
}

auto UploadTracker::MarkEntryCompleted(Entry& e) -> void
{
  e.completed = true;
  e.result.success = true;
  e.result.bytes_uploaded = e.bytes;
  e.result.error = std::nullopt; // No error for successful completion
}

auto UploadTracker::OnFrameStart(UploaderTag, frame::Slot slot) -> void
{
  std::lock_guard<std::mutex> lk(mu_);
  current_slot_ = slot;

  // Radical cleanup: erase all entries created in this slot
  std::erase_if(entries_,
    [slot](const auto& pair) { return pair.second.creation_slot == slot; });
}

auto UploadTracker::AwaitAllPending()
  -> std::expected<std::vector<UploadResult>, UploadError>
{
  // Loop until there are no pending entries. If entries are erased
  // underneath us (OnFrameStart cleanup), retry until none remain.
  while (true) {
    std::vector<UploadTicket> pending;
    {
      std::lock_guard<std::mutex> lk(mu_);
      for (const auto& p : entries_) {
        if (!p.second.completed) {
          pending.emplace_back(p.first, p.second.fence);
        }
      }
    }

    if (pending.empty()) {
      // No pending tickets
      return std::vector<UploadResult> {};
    }

    // AwaitAll will block until the pending set completes. If entries are
    // removed while waiting we may receive TicketNotFound; treat that as a
    // reason to retry collection and wait again until nothing remains.
    auto res = AwaitAll(pending);
    if (res) {
      return res;
    }
    if (res.error() == UploadError::kTicketNotFound) {
      // Some tickets vanished; loop and build a fresh pending list.
      continue;
    }
    return std::unexpected(res.error());
  }
}

} // namespace oxygen::engine::upload
