//===----------------------------------------------------------------------===//
// Common Event Loop Base for OxCo Batch Processing Examples
// Provides a shared event loop implementation that can be inherited
//===----------------------------------------------------------------------===//

#pragma once

#include <atomic>
#include <functional>
#include <iostream>
#include <queue>

#include <Oxygen/OxCo/EventLoop.h>

//===----------------------------------------------------------------------===//
// Base Event Loop Implementation
//===----------------------------------------------------------------------===//

namespace oxygen::scene::testing {

class SimpleEventLoop {
public:
  void Run()
  {
    running_ = true;

    while (!should_stop_ || !tasks_.empty()) {
      if (!tasks_.empty()) {
        auto task = std::move(tasks_.front());
        tasks_.pop();
        task();
      }
    }

    running_ = false;
    std::cout << "Batch Execution EventLoop: Stopped\n";
  }

  void Stop() { should_stop_ = true; }
  bool IsRunning() const noexcept { return running_; }
  void Schedule(std::function<void()> task) { tasks_.push(std::move(task)); }

private:
  std::atomic<bool> running_ { false };
  std::atomic<bool> should_stop_ { false };
  std::queue<std::function<void()>> tasks_;
};

} // namespace oxygen::scene::testing

//===----------------------------------------------------------------------===//
// EventLoopTraits Specializations
//===----------------------------------------------------------------------===//

template <>
struct oxygen::co::EventLoopTraits<oxygen::scene::testing::SimpleEventLoop> {
  static EventLoopID EventLoopId(oxygen::scene::testing::SimpleEventLoop& loop)
  {
    return EventLoopID(&loop);
  }
  static void Run(oxygen::scene::testing::SimpleEventLoop& loop) { loop.Run(); }
  static void Stop(oxygen::scene::testing::SimpleEventLoop& loop)
  {
    loop.Stop();
  }
  static bool IsRunning(
    const oxygen::scene::testing::SimpleEventLoop& loop) noexcept
  {
    return loop.IsRunning();
  }
};
