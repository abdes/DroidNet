//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <utility>

#include <Oxygen/Graphics/Common/Types/FenceValue.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/Vortex/Upload/Errors.h>
#include <Oxygen/Vortex/Upload/Internal/UploadTimeline.h>
#include <Oxygen/Vortex/Upload/UploadTicket.h>

namespace oxygen::vortex::upload {

namespace {
  constexpr UploadResult kInvalidResult {
    .success = false, .bytes_uploaded = 0, .error = UploadError::kTicketNotFound
  };
}

UploadTicket::UploadTicket(std::shared_ptr<internal::UploadRecord> record)
  : record_(std::move(record))
{
}

auto UploadTicket::Id() const noexcept -> TicketId
{
  return record_ ? record_->id : TicketId { 0 };
}

auto UploadTicket::Fence() const noexcept -> FenceValue
{
  return record_ ? record_->fence : graphics::fence::kInvalidValue;
}

auto UploadTicket::TryGetResult() const -> std::optional<UploadResult>
{
  if (!record_) {
    return kInvalidResult;
  }
  const std::scoped_lock lock(record_->timeline->mutex);
  return record_->Result();
}

auto UploadTicket::Await() const -> UploadResult
{
  if (!record_) {
    return kInvalidResult;
  }
  const auto record = record_;
  auto& timeline = *record->timeline;
  std::unique_lock lock(timeline.mutex);
  timeline.changed.wait(
    lock, [&record]() { return record->Result().has_value(); });
  if (const auto result = record->Result(); result.has_value()) {
    return *result;
  }
  throw std::logic_error("Upload result wait resumed before a terminal result");
}

auto UploadTicket::Cancel() const -> bool
{
  if (!record_) {
    return false;
  }
  const auto timeline = record_->timeline;
  {
    const std::scoped_lock lock(timeline->mutex);
    if (record_->Result()) {
      return false;
    }
    record_->failure = UploadError::kCanceled;
  }
  timeline->changed.notify_all();
  return true;
}

auto UploadTicket::AwaitGpuCompletionAsync() const -> co::Co<UploadResult>
{
  return AwaitGpuCompletion(record_);
}

auto UploadTicket::AwaitGpuCompletion(
  std::shared_ptr<internal::UploadRecord> record) -> co::Co<UploadResult>
{
  if (!record) {
    co_return kInvalidResult;
  }
  const auto timeline = record->timeline;
  co_await timeline->notification.UntilMatches(
    [fence = record->fence](const internal::UploadProgress& progress) {
      return progress.completed >= fence || progress.closed.has_value();
    });
  const std::scoped_lock lock(timeline->mutex);
  if (const auto result = record->Result(); result.has_value()) {
    co_return *result;
  }
  throw std::logic_error("Upload GPU wait resumed before completion or close");
}

} // namespace oxygen::vortex::upload
