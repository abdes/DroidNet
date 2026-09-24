//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <atomic>
#include <limits>
#include <stdexcept>
#include <vector>

#include <Oxygen/Graphics/Common/Internal/QueueSubmission.h>
#include <Oxygen/Graphics/Headless/CommandList.h>
#include <Oxygen/Graphics/Headless/CommandQueue.h>
#include <Oxygen/Graphics/Headless/Internal/CommandExecutor.h>

namespace oxygen::graphics::headless {
namespace {
  constexpr auto kLost = (std::numeric_limits<uint64_t>::max)();
}

struct CommandQueue::Timeline {
  mutable std::mutex mutex;
  mutable std::condition_variable changed;
  std::atomic<uint64_t> completed { 0 };
  auto Publish(uint64_t value) noexcept -> void
  {
    std::lock_guard lock(mutex);
    if (completed.load() != kLost) {
      completed.store(value, std::memory_order_release);
    }
    changed.notify_all();
  }
  auto Wait(uint64_t value, const Timeline* cancellation = nullptr) const
    -> void
  {
    std::unique_lock lock(mutex);
    while (completed.load(std::memory_order_acquire) < value) {
      if (cancellation && cancellation->completed.load() == kLost) {
        throw std::runtime_error("Headless dependency wait cancelled");
      }
      changed.wait_for(lock, std::chrono::milliseconds(10));
    }
    if (completed.load() == kLost) {
      throw std::runtime_error("Headless device lost");
    }
  }
};

struct CommandQueue::PreparedSubmission final
  : graphics::internal::NativeSubmission {
  CommandQueue& owner;
  internal::CommandExecutor::Task task;
  size_t count;
  uint64_t last_signal;
  bool private_marker;
  bool fail_before_private_marker;
  PreparedSubmission(CommandQueue& queue,
    internal::CommandExecutor::Task prepared, size_t list_count,
    uint64_t signal, bool marker, bool fail)
    : owner(queue)
    , task(std::move(prepared))
    , count(list_count)
    , last_signal(signal)
    , private_marker(marker)
    , fail_before_private_marker(fail)
  {
  }
  auto Execute(graphics::internal::NativeSubmissionProgress& progress)
    -> void override
  {
    {
      std::lock_guard lock(owner.mutex_);
      owner.current_value_ = std::max(owner.current_value_, last_signal);
      ++owner.pending_submissions_;
    }
    if (!owner.executor_->Commit(task)) {
      owner.CompleteSubmission();
      throw std::runtime_error("Headless executor is stopped");
    }
    progress.issued_lists = count;
    progress.last_legacy_signal
      = std::max(progress.last_legacy_signal, last_signal);
    if (fail_before_private_marker) {
      throw graphics::SubmissionException(
        { graphics::SubmissionOutcome::kExecutionUncertain, {} });
    }
    progress.private_marker_emitted = private_marker;
  }
};

CommandQueue::CommandQueue(std::string_view name, QueueRole role)
  : graphics::CommandQueue(name)
  , private_timeline_(std::make_shared<Timeline>())
  , executor_(new internal::CommandExecutor())
  , queue_role_(role)
{
}
CommandQueue::~CommandQueue() { delete executor_; }

auto CommandQueue::PrepareNativeSubmission(
  const graphics::internal::NativeSubmissionRequest& request,
  std::unique_ptr<graphics::internal::NativeSubmission> reusable)
  -> std::unique_ptr<graphics::internal::NativeSubmission>
{
  reusable.reset();
  struct Dependency {
    std::shared_ptr<Timeline> timeline;
    uint64_t value;
  };
  std::vector<Dependency> dependencies;
  dependencies.reserve(request.dependencies.size());
  for (const auto& dependency : request.dependencies) {
    const auto& producer
      = static_cast<const CommandQueue&>(*dependency.producer);
    dependencies.push_back(
      { producer.private_timeline_, dependency.receipt.Value() });
  }
  std::vector<internal::SubmissionChunk> chunks;
  chunks.reserve(request.lists.size());
  uint64_t last_signal = request.legacy_marker.value_or(0);
  for (const auto& list : request.lists) {
    const auto actions = list->SubmitActions();
    chunks.push_back({ { actions.begin(), actions.end() },
      static_cast<CommandList&>(*list).StealCommands() });
    for (const auto& action : actions) {
      if (action.kind
        == graphics::CommandList::SubmitQueueActionKind::kSignal) {
        last_signal = std::max(last_signal, action.value);
      }
    }
  }
  if (request.fail_after_first_list && !chunks.empty()) {
    chunks.resize(1);
  }
  const auto timeline = private_timeline_;
  auto task = executor_->Prepare(
    this, std::move(chunks),
    [dependencies = std::move(dependencies), timeline] {
      if (timeline->completed.load() == kLost) {
        throw std::runtime_error("Headless device lost");
      }
      for (const auto& dependency : dependencies) {
        dependency.timeline->Wait(dependency.value, timeline.get());
      }
    },
    [this, timeline,
      marker
      = (request.fail_before_private_marker || request.fail_after_first_list)
        ? std::nullopt
        : request.private_marker,
      legacy = request.legacy_marker] {
      if (legacy) {
        SignalImmediate(*legacy);
      }
      if (marker) {
        timeline->Publish(*marker);
      }
    },
    [this] { TearDownUncertainDevice(); });
  return std::make_unique<PreparedSubmission>(*this, std::move(task),
    request.fail_after_first_list ? std::min(size_t { 1 }, request.lists.size())
                                  : request.lists.size(),
    last_signal, request.private_marker.has_value(),
    request.fail_before_private_marker || request.fail_after_first_list);
}

auto CommandQueue::EnqueueLegacyMarker(uint64_t value) -> void
{
  auto prepared = PrepareNativeSubmission({ {}, {}, {}, value }, {});
  graphics::internal::NativeSubmissionProgress progress;
  prepared->Execute(progress);
}

auto CommandQueue::QueryPrivateCompletion() const noexcept -> uint64_t
{
  return private_timeline_->completed.load(std::memory_order_acquire);
}
auto CommandQueue::WaitPrivateCompletion(uint64_t value) const -> void
{
  private_timeline_->Wait(value);
}
auto CommandQueue::WaitForNativeStop() noexcept -> void { executor_->Stop(); }
auto CommandQueue::TearDownUncertainDevice() noexcept -> void
{
  private_timeline_->Publish(kLost);
  if (auto lifetime = BackendLifetimeState()) {
    lifetime->MarkSubmissionFault();
  }
  std::lock_guard lock(mutex_);
  completed_value_ = kLost;
  cv_.notify_all();
}

auto CommandQueue::Signal(uint64_t value) const -> void
{
  std::lock_guard lock(mutex_);
  if (value <= current_value_ || value == kLost) {
    throw std::invalid_argument("Invalid queue signal reservation");
  }
  current_value_ = value;
}
auto CommandQueue::Signal() const -> uint64_t
{
  std::lock_guard lock(mutex_);
  if (current_value_ >= kLost - 1) {
    throw std::overflow_error("Queue timeline exhausted");
  }
  return ++current_value_;
}
auto CommandQueue::SignalImmediate(uint64_t value) const -> void
{
  std::lock_guard lock(mutex_);
  if (completed_value_ == kLost) {
    throw std::runtime_error("Headless device lost");
  }
  if (value <= completed_value_) {
    throw std::invalid_argument("Queue signal went backwards");
  }
  current_value_ = std::max(current_value_, value);
  completed_value_ = value;
  cv_.notify_all();
}
auto CommandQueue::QueueWaitImmediate(uint64_t value) const -> void
{
  Wait(value);
}
auto CommandQueue::Wait(uint64_t value, std::chrono::milliseconds timeout) const
  -> void
{
  std::unique_lock lock(mutex_);
  if (!cv_.wait_for(lock, timeout, [&] { return completed_value_ >= value; })) {
    throw std::runtime_error("Headless queue wait timed out");
  }
  if (completed_value_ == kLost) {
    throw std::runtime_error("Headless device lost");
  }
}
auto CommandQueue::Wait(uint64_t value) const -> void
{
  std::unique_lock lock(mutex_);
  cv_.wait(lock, [&] { return completed_value_ >= value; });
  if (completed_value_ == kLost) {
    throw std::runtime_error("Headless device lost");
  }
}
auto CommandQueue::GetCompletedValue() const -> uint64_t
{
  std::lock_guard lock(mutex_);
  return completed_value_;
}
auto CommandQueue::GetCurrentValue() const -> uint64_t
{
  std::lock_guard lock(mutex_);
  return current_value_;
}
auto CommandQueue::CompleteSubmission() const -> void
{
  std::lock_guard lock(mutex_);
  --pending_submissions_;
  cv_.notify_all();
}
} // namespace oxygen::graphics::headless
