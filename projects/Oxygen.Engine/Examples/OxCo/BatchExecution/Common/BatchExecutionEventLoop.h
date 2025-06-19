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

class BatchExecutionEventLoop {
public:
  void Run()
  {
    running_ = true;
    std::cout << "Batch Execution EventLoop: Starting\n";

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

//===----------------------------------------------------------------------===//
// EventLoopTraits Specializations
//===----------------------------------------------------------------------===//

template <> struct oxygen::co::EventLoopTraits<BatchExecutionEventLoop> {
  static EventLoopID EventLoopId(BatchExecutionEventLoop& loop)
  {
    return EventLoopID(&loop);
  }
  static void Run(BatchExecutionEventLoop& loop) { loop.Run(); }
  static void Stop(BatchExecutionEventLoop& loop) { loop.Stop(); }
  static bool IsRunning(const BatchExecutionEventLoop& loop) noexcept
  {
    return loop.IsRunning();
  }
};
