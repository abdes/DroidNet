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
  std::scoped_lock lk(mu_);
  const auto id = next_ticket_;
  next_ticket_ = TicketId { id.get() + 1 };

  auto& e = entries_[id];
  e.fence = fence;
  e.bytes = bytes;
  e.name.assign(debug_name);
  e.completed = false;
  e.result = UploadResult {};
  // stats: submitted
  stats_.submitted += 1;
  stats_.in_flight += 1;
  stats_.bytes_submitted += bytes;
  return UploadTicket { id, fence };
}

auto UploadTracker::RegisterFailedImmediate(const std::string_view debug_name,
  const UploadError error, const std::string_view message) -> UploadTicket
{
  std::scoped_lock lk(mu_);
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
  e.result.message.assign(message);
  // stats: counts as submitted+completed immediately (no in-flight)
  stats_.submitted += 1;
  stats_.completed += 1;
  return UploadTicket { id, e.fence };
}

auto UploadTracker::MarkFenceCompleted(const FenceValue completed) -> void
{
  {
    std::scoped_lock lk(mu_);
    if (completed_fence_.Get() < completed) {
      completed_fence_.Set(completed);
    }
    for (auto& [id, e] : entries_) {
      (void)id;
      if (!e.completed && e.fence <= completed) {
        MarkEntryCompleted_(e);
      }
    }
  }
  cv_.notify_all();
}

auto UploadTracker::IsComplete(const TicketId id) const -> bool
{
  std::scoped_lock lk(mu_);
  if (const auto it = entries_.find(id); it != entries_.end()) {
    return it->second.completed;
  }
  return false;
}

auto UploadTracker::TryGetResult(const TicketId id) const
  -> std::optional<UploadResult>
{
  std::scoped_lock lk(mu_);
  if (const auto it = entries_.find(id); it != entries_.end()) {
    if (it->second.completed)
      return it->second.result;
  }
  return std::nullopt;
}

auto UploadTracker::Await(const TicketId id) -> UploadResult
{
  std::unique_lock lk(mu_);
  cv_.wait(lk, [&]() noexcept {
    if (const auto it = entries_.find(id); it != entries_.end()) {
      return it->second.completed;
    }
    return false;
  });
  return entries_.at(id).result;
}

auto UploadTracker::AwaitAll(const std::span<const UploadTicket> tickets)
  -> std::vector<UploadResult>
{
  std::vector<UploadResult> results;
  results.reserve(tickets.size());

  std::unique_lock lk(mu_);
  cv_.wait(lk, [&]() noexcept {
    for (const auto& t : tickets) {
      const auto it = entries_.find(t.id);
      if (it == entries_.end() || !it->second.completed)
        return false;
    }
    return true;
  });
  for (const auto& t : tickets) {
    results.emplace_back(entries_.at(t.id).result);
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
  std::scoped_lock lk(mu_);
  return stats_;
}

auto UploadTracker::Cancel(const TicketId id) -> bool
{
  std::scoped_lock lk(mu_);
  const auto it = entries_.find(id);
  if (it == entries_.end())
    return false;
  auto& e = it->second;
  if (e.completed)
    return false;
  // Mark as completed canceled
  e.completed = true;
  e.result.success = false;
  e.result.bytes_uploaded = 0;
  e.result.error = UploadError::kCanceled;
  e.result.message = "Canceled";
  // stats: completed, reduce in_flight
  stats_.completed += 1;
  if (stats_.in_flight > 0)
    stats_.in_flight -= 1;
  cv_.notify_all();
  return true;
}

auto UploadTracker::MarkEntryCompleted_(Entry& e) -> void
{
  e.completed = true;
  e.result.success = true;
  e.result.bytes_uploaded = e.bytes;
  e.result.error = UploadError::kNone;
  e.result.message = {};
  // stats
  stats_.completed += 1;
  if (stats_.in_flight > 0)
    stats_.in_flight -= 1;
  stats_.bytes_completed += e.bytes;
}

} // namespace oxygen::engine::upload
