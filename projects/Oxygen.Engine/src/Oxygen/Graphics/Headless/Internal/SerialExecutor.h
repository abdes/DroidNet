//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <condition_variable>
#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <thread>

namespace oxygen::graphics::headless::internal {

class SerialExecutor {
public:
  SerialExecutor();
  ~SerialExecutor();

  // Enqueue a task to run serially. Returns a future<void> that will be
  // satisfied when the task completes (or exceptional if the task throws).
  std::future<void> Enqueue(std::function<void()> task);

  // Stop the executor and join the worker thread. After Stop(), Enqueue
  // will throw std::runtime_error.
  void Stop();

private:
  void WorkerMain();

  std::mutex mutex_;
  std::condition_variable cv_;
  bool stopping_ { false };
  std::queue<std::pair<std::function<void()>, std::promise<void>>> tasks_;
  std::thread worker_;
};

} // namespace oxygen::graphics::headless::internal
