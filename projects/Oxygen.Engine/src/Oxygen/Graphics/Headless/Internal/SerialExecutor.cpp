//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <utility>

#include <Oxygen/Graphics/Headless/Internal/SerialExecutor.h>

namespace oxygen::graphics::headless::internal {

SerialExecutor::SerialExecutor()
  : worker_(&SerialExecutor::WorkerMain, this)
{
}

SerialExecutor::~SerialExecutor() { Stop(); }

auto SerialExecutor::Enqueue(std::function<void()> task) -> std::future<void>
{
  auto prepared = Prepare(std::move(task));
  auto future = prepared->completion.get_future();
  if (!Commit(prepared)) {
    throw std::runtime_error("SerialExecutor is stopped");
  }
  return future;
}

auto SerialExecutor::Prepare(std::function<void()> task) -> Task
{
  auto prepared = std::make_unique<PreparedTask>();
  prepared->callback = std::move(task);
  return prepared;
}

auto SerialExecutor::Commit(Task& task) noexcept -> bool
{
  std::lock_guard lk(mutex_);
  if (stopping_ || !task) {
    return false;
  }
  auto* tail = task.get();
  if (tail_) {
    tail_->next = std::move(task);
  } else {
    tasks_ = std::move(task);
  }
  tail_ = tail;
  cv_.notify_one();
  return true;
}

auto SerialExecutor::Stop() -> void
{
  {
    std::lock_guard lk(mutex_);
    if (stopping_) {
      return;
    }
    stopping_ = true;
  }
  cv_.notify_one();
  if (worker_.joinable()) {
    worker_.join();
  }

  // WorkerMain drains the prepared chain before returning.
}

auto SerialExecutor::WorkerMain() -> void
{
  for (;;) {
    Task item;
    {
      std::unique_lock lk(mutex_);
      cv_.wait(lk, [this] { return stopping_ || tasks_ != nullptr; });
      if (stopping_ && !tasks_) {
        return;
      }
      item = std::move(tasks_);
      tasks_ = std::move(item->next);
      if (!tasks_) {
        tail_ = nullptr;
      }
    }

    try {
      item->callback();
      item->completion.set_value();
    } catch (...) {
      try {
        item->completion.set_exception(std::current_exception());
      } catch (...) {
      }
    }
  }
}

} // namespace oxygen::graphics::headless::internal
