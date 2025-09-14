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
  // stats: submitted
  stats_.submitted += 1;
  stats_.in_flight += 1;
  stats_.bytes_submitted += bytes;
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
  // stats: counts as submitted+completed immediately (no in-flight)
  stats_.submitted += 1;
  stats_.completed += 1;
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

auto UploadTracker::GetStats() const -> UploadStats
{
  std::lock_guard<std::mutex> lk(mu_);
  return stats_;
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
  // stats: completed, reduce in_flight
  stats_.completed += 1;
  if (stats_.in_flight > 0) {
    stats_.in_flight -= 1;
  }
  cv_.notify_all();
  return true;
}

auto UploadTracker::MarkEntryCompleted(Entry& e) -> void
{
  e.completed = true;
  e.result.success = true;
  e.result.bytes_uploaded = e.bytes;
  e.result.error = std::nullopt; // No error for successful completion
  // stats
  stats_.completed += 1;
  if (stats_.in_flight > 0) {
    stats_.in_flight -= 1;
  }
  stats_.bytes_completed += e.bytes;
}

auto UploadTracker::OnFrameStart(UploaderTag, frame::Slot slot) -> void
{
  std::lock_guard<std::mutex> lk(mu_);
  current_slot_ = slot;

  // Radical cleanup: erase all entries created in this slot
  std::erase_if(entries_,
    [slot](const auto& pair) { return pair.second.creation_slot == slot; });
}

} // namespace oxygen::engine::upload
