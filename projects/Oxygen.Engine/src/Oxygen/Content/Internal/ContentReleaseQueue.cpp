//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <atomic>
#include <cstddef>
#include <memory>
#include <memory_resource>
#include <optional>
#include <stdexcept>
#include <utility>

#include <Oxygen/Base/Finally.h>
#include <Oxygen/Content/Internal/ContentReleaseQueue.h>
#include <Oxygen/Content/OperationCancelledException.h>

namespace oxygen::content::internal {

auto ContentReleaseQueue::MakeOwner(Cache& cache,
  std::shared_ptr<const void> value, std::optional<Cache::UsageTicket> usage)
  -> std::shared_ptr<Record>
{
  const auto rollback = Finally([&]() noexcept {
    if (usage && usage->IsActive()) {
      static_cast<void>(cache.Release(std::move(*usage)));
    }
  });
  if (closed_) {
    throw OperationCancelledException("Content release queue is closed");
  }
  const auto queue = weak_from_this();
  if (queue.expired()) {
    throw std::logic_error("Content release queue requires shared ownership");
  }
  auto record = std::make_unique<Record>();
  record->value = std::move(value);
  std::shared_ptr<Record> owner(record.release(), ReturnRecord { queue },
    std::pmr::polymorphic_allocator<Record>(control_allocator_));
  if (usage) {
    owner->usage.emplace(std::move(*usage));
  }
  return owner;
}

auto ContentReleaseQueue::Pin(Cache& cache, Cache::UsageTicket usage)
  -> std::shared_ptr<void>
{
  return MakeOwner(
    cache, {}, std::optional<Cache::UsageTicket> { std::move(usage) });
}

ContentReleaseQueue::~ContentReleaseQueue()
{
  Close();
  // The loader normally drains first. Destruction never calls a retired loader.
  while (auto record = Pop()) {
    record.reset();
  }
}

auto ContentReleaseQueue::ReturnRecord::operator()(
  Record* record) const noexcept -> void
{
  if (const auto live_queue = queue.lock()) {
    live_queue->Enqueue(record);
  } else {
    delete record;
  }
}

auto ContentReleaseQueue::Enqueue(Record* record) noexcept -> void
{
  auto* head = incoming_.load(std::memory_order_relaxed);
  for (;;) {
    if (head == &closed_sentinel_) {
      delete record;
      return;
    }
    record->next = head;
    if (incoming_.compare_exchange_weak(
          head, record, std::memory_order_release, std::memory_order_relaxed)) {
      return;
    }
  }
}

auto ContentReleaseQueue::Pop() noexcept -> std::unique_ptr<Record>
{
  if (!pending_) {
    pending_ = closed_ ? std::exchange(closing_batch_, nullptr)
                       : incoming_.exchange(nullptr, std::memory_order_acquire);
  }
  if (!pending_) {
    return {};
  }
  auto* const record = pending_;
  pending_ = record->next;
  record->next = nullptr;
  return std::unique_ptr<Record>(record);
}

auto ContentReleaseQueue::Drain(Cache& cache, const std::size_t limit)
  -> std::size_t
{
  const auto keep_alive = shared_from_this();
  if (draining_) {
    return 0U;
  }
  draining_ = true;
  const auto reset = Finally([&]() noexcept { draining_ = false; });
  std::size_t processed = 0;
  while (processed < limit) {
    auto record = Pop();
    if (!record) {
      break;
    }
    if (!closed_ && record->usage) {
      static_cast<void>(cache.Release(std::move(*record->usage)));
    }
    // Reentrant shutdown may destroy the cache. Closed records only dispose
    // CPU storage and never access cache accounting.
    record.reset();
    ++processed;
  }
  return processed;
}

auto ContentReleaseQueue::Close() noexcept -> void
{
  if (!closed_) {
    closing_batch_
      = incoming_.exchange(&closed_sentinel_, std::memory_order_acq_rel);
    closed_ = true;
  }
}

auto ContentReleaseQueue::HasPending() const noexcept -> bool
{
  if (pending_ || closing_batch_) {
    return true;
  }
  const auto* const head = incoming_.load(std::memory_order_relaxed);
  return head && head != &closed_sentinel_;
}

} // namespace oxygen::content::internal
