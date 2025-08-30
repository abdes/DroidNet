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

std::future<void> SerialExecutor::Enqueue(std::function<void()> task)
{
  std::lock_guard lk(mutex_);
  if (stopping_) {
    throw std::runtime_error("SerialExecutor is stopped");
  }

  std::promise<void> p;
  auto f = p.get_future();
  tasks_.emplace(std::make_pair(std::move(task), std::move(p)));
  cv_.notify_one();
  return f;
}

void SerialExecutor::Stop()
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

  // If there are pending tasks, set exceptions so futures don't hang.
  while (!tasks_.empty()) {
    auto& pr = tasks_.front().second;
    try {
      pr.set_exception(
        std::make_exception_ptr(std::runtime_error("executor stopped")));
    } catch (...) {
    }
    tasks_.pop();
  }
}

void SerialExecutor::WorkerMain()
{
  for (;;) {
    std::pair<std::function<void()>, std::promise<void>> item;
    {
      std::unique_lock lk(mutex_);
      cv_.wait(lk, [this] { return stopping_ || !tasks_.empty(); });
      if (stopping_ && tasks_.empty()) {
        return;
      }
      item = std::move(tasks_.front());
      tasks_.pop();
    }

    try {
      item.first();
      item.second.set_value();
    } catch (...) {
      try {
        item.second.set_exception(std::current_exception());
      } catch (...) {
      }
    }
  }
}

} // namespace oxygen::graphics::headless::internal
