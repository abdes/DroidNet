//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <condition_variable>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>

namespace oxygen::graphics::headless::internal {

class SerialExecutor {
public:
  struct PreparedTask {
    std::function<void()> callback;
    std::promise<void> completion;
    std::unique_ptr<PreparedTask> next;
  };
  using Task = std::unique_ptr<PreparedTask>;
  SerialExecutor();
  ~SerialExecutor();

  // Enqueue a task to run serially. Returns a future<void> that will be
  // satisfied when the task completes (or exceptional if the task throws).
  auto Enqueue(std::function<void()> task) -> std::future<void>;
  static auto Prepare(std::function<void()> task) -> Task;
  //! Transfers an already allocated task; false means no work was issued.
  auto Commit(Task& task) noexcept -> bool;

  // Stop the executor and join the worker thread. After Stop(), Enqueue
  // will throw std::runtime_error.
  auto Stop() -> void;

private:
  auto WorkerMain() -> void;

  std::mutex mutex_;
  std::condition_variable cv_;
  bool stopping_ { false };
  Task tasks_;
  PreparedTask* tail_ { nullptr };
  std::thread worker_;
};

} // namespace oxygen::graphics::headless::internal
