//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at <https://opensource.org/licenses/BSD-3-Clause>.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cassert>
#include <mutex>
#include <stdexcept>
#include <utility>

#include <Oxygen/Graphics/Common/BackendLifetime.h>

namespace oxygen::graphics {
thread_local BackendOperation* BackendOperation::current_ = nullptr;

BackendOperation::BackendOperation(const BackendLifetime& lifetime)
  : lifetime_(&lifetime)
  , previous_(current_)
{
  bool nested = false;
  for (auto* operation = current_; operation;
    operation = operation->previous_) {
    if (operation->lifetime_ == &lifetime) {
      nested = true;
      break;
    }
  }
  if (!nested) {
    lock_ = std::shared_lock(lifetime.gate_);
  }
  if (lifetime.State() != BackendLifecycle::kActive || lifetime.IsFaulted()) {
    throw std::logic_error("Backend is closing or retired");
  }
  current_ = this;
}

BackendOperation::~BackendOperation()
{
  if (lifetime_) {
    assert(current_ == this);
    current_ = previous_;
  }
}

BackendLifetime::BackendLifetime() = default;
BackendLifetime::~BackendLifetime() = default;

auto BackendLifetime::Install(
  BackendIncarnationId id, std::shared_ptr<void> module) -> void
{
  std::unique_lock lock(gate_);
  if (id_.get() != 0 || id.get() == 0 || State() != BackendLifecycle::kActive) {
    throw std::logic_error("Backend lifetime already installed or closed");
  }
  module_ = std::move(module);
  id_ = id;
}

auto BackendLifetime::AcquireOperation() const -> BackendOperation
{
  return BackendOperation { *this };
}

auto BackendLifetime::BeginClose() noexcept -> bool
{
  std::unique_lock lock(gate_);
  if (State() != BackendLifecycle::kActive) {
    return false;
  }
  state_.store(BackendLifecycle::kClosing, std::memory_order_release);
  return true;
}

auto BackendLifetime::FinishClose() noexcept -> void
{
  state_.store(BackendLifecycle::kRetiring, std::memory_order_release);
}

auto BackendLifetime::MarkSubmissionFault() noexcept -> void
{
  if (!faulted_.exchange(true, std::memory_order_acq_rel)) {
    recording_epoch_.fetch_add(1, std::memory_order_acq_rel);
  }
}
auto BackendLifetime::ClearSubmissionFault() noexcept -> void
{
  faulted_.store(false, std::memory_order_release);
}
auto BackendLifetime::SynchronizeOperations() -> void
{
  std::unique_lock lock(gate_);
}
auto BackendLifetime::RegisterQueue(
  uint64_t id, std::weak_ptr<CommandQueue> queue) -> void
{
  std::lock_guard lock(queue_mutex_);
  std::erase_if(
    queues_, [](const auto& entry) { return entry.second.expired(); });
  queues_.insert_or_assign(id, std::move(queue));
}
auto BackendLifetime::FindQueue(uint64_t id) const
  -> std::shared_ptr<CommandQueue>
{
  std::lock_guard lock(queue_mutex_);
  const auto found = queues_.find(id);
  return found == queues_.end() ? nullptr : found->second.lock();
}
} // namespace oxygen::graphics
