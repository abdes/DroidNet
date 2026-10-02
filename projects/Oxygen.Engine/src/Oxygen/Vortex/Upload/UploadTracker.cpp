//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>

#include <Oxygen/Vortex/Upload/Errors.h>
#include <Oxygen/Vortex/Upload/Internal/UploadTimeline.h>
#include <Oxygen/Vortex/Upload/UploadTicket.h>
#include <Oxygen/Vortex/Upload/UploadTracker.h>

namespace oxygen::vortex::upload {

UploadTracker::UploadTracker()
  : timeline_(std::make_shared<internal::UploadTimeline>())
{
}

UploadTracker::~UploadTracker() { Close(); }

auto UploadTracker::Register(const FenceValue fence, const std::uint64_t bytes)
  -> UploadTicket
{
  const auto timeline = timeline_;
  const std::scoped_lock lock(timeline->mutex);
  timeline->submitted = std::max(timeline->submitted, fence);
  const auto id = timeline->next_ticket;
  timeline->next_ticket = TicketId { id.get() + 1 };
  return UploadTicket { std::make_shared<internal::UploadRecord>(
    internal::UploadRecord { .timeline = timeline,
      .id = id,
      .fence = fence,
      .bytes = bytes,
      .failure = timeline->progress.closed }) };
}

auto UploadTracker::RegisterFailedImmediate(const UploadError error)
  -> UploadTicket
{
  const auto timeline = timeline_;
  const std::scoped_lock lock(timeline->mutex);
  const auto id = timeline->next_ticket;
  timeline->next_ticket = TicketId { id.get() + 1 };
  return UploadTicket { std::make_shared<internal::UploadRecord>(
    internal::UploadRecord { .timeline = timeline,
      .id = id,
      .fence = timeline->progress.completed,
      .bytes = 0,
      .failure = error }) };
}

auto UploadTracker::RecordSubmission(const FenceValue fence) -> void
{
  const std::scoped_lock lock(timeline_->mutex);
  timeline_->submitted = std::max(timeline_->submitted, fence);
}

auto UploadTracker::MarkFenceCompleted(const FenceValue completed) -> void
{
  if (completed.get() == std::numeric_limits<std::uint64_t>::max()) {
    Close(UploadError::kDeviceLost);
    return;
  }
  // A resumed coroutine can destroy the tracker; hold no tracker references
  // across notification, and never resume while holding the timeline lock.
  const auto timeline = timeline_;
  {
    const std::scoped_lock lock(timeline->mutex);
    if (timeline->progress.closed
      || completed <= timeline->progress.completed) {
      return;
    }
    timeline->progress.completed = completed;
  }
  timeline->changed.notify_all();
  timeline->Notify();
}

auto UploadTracker::Close(const UploadError error) -> void
{
  const auto timeline = timeline_;
  {
    const std::scoped_lock lock(timeline->mutex);
    if (timeline->progress.closed) {
      return;
    }
    timeline->progress.closed = error;
  }
  timeline->changed.notify_all();
  timeline->Notify();
}

auto UploadTracker::CompletedFence() const -> FenceValue
{
  const std::scoped_lock lock(timeline_->mutex);
  return timeline_->progress.completed;
}

auto UploadTracker::LastSubmittedFence() const -> FenceValue
{
  const std::scoped_lock lock(timeline_->mutex);
  return timeline_->submitted;
}

} // namespace oxygen::vortex::upload
