//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Internal/ImportEventLoop.cpp

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

#include <Oxygen/Cooker/Import/Internal/ImportEventLoop.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/OxCo/ThreadPool.h>
#include <Oxygen/Testing/GTest.h>

using namespace std::chrono_literals;
using namespace oxygen::content::import;
using namespace oxygen::co;
namespace co = oxygen::co;

namespace {

//=== Basic Functionality Tests
//===--------------------------------------------//

class ImportEventLoopBasicTest : public testing::Test {
protected:
  ImportEventLoop loop_;
};

NOLINT_TEST_F(ImportEventLoopBasicTest, RunAndStopViaPostSucceeds)
{
  std::atomic<bool> callback_ran { false };

  loop_.Post([&]() {
    callback_ran = true;
    loop_.Stop();
  });
  loop_.Run();

  EXPECT_TRUE(callback_ran);
}

NOLINT_TEST_F(ImportEventLoopBasicTest, PostMultipleCallbacksExecuteInOrder)
{
  std::vector<int> order;

  loop_.Post([&]() { order.push_back(1); });
  loop_.Post([&]() { order.push_back(2); });
  loop_.Post([&]() {
    order.push_back(3);
    loop_.Stop();
  });
  loop_.Run();

  using testing::ElementsAre;
  EXPECT_THAT(order, ElementsAre(1, 2, 3));
}

NOLINT_TEST_F(ImportEventLoopBasicTest, StopFromOtherThreadSucceeds)
{
  std::thread stopper([&]() {
    std::this_thread::sleep_for(50ms);
    loop_.Stop();
  });

  // Act & Assert (should not hang)
  loop_.Run();
  stopper.join();
}

NOLINT_TEST_F(ImportEventLoopBasicTest, IsRunningReturnsCorrectState)
{
  std::atomic<bool> was_running_inside { false };

  EXPECT_FALSE(loop_.IsRunning());

  loop_.Post([&]() {
    was_running_inside = loop_.IsRunning();
    loop_.Stop();
  });
  loop_.Run();

  EXPECT_TRUE(was_running_inside);
  EXPECT_FALSE(loop_.IsRunning());
}

//=== EventLoopTraits Tests
//===------------------------------------------------//

class ImportEventLoopTraitsTest : public testing::Test {
protected:
  ImportEventLoop loop_;
};

NOLINT_TEST_F(ImportEventLoopTraitsTest, EventLoopIdReturnsValidId)
{
  auto id = EventLoopTraits<ImportEventLoop>::EventLoopId(loop_);

  EXPECT_NE(id.Get(), nullptr);
}

NOLINT_TEST_F(ImportEventLoopTraitsTest, RunWithCoRunWorks)
{
  std::atomic<bool> coroutine_ran { false };

  co::Run(loop_, [&]() -> Co<> {
    coroutine_ran = true;
    co_return;
  });

  EXPECT_TRUE(coroutine_ran);
}

//=== ThreadNotification Tests
//===---------------------------------------------//

class ImportEventLoopThreadNotificationTest : public testing::Test {
protected:
  ImportEventLoop loop_;
};

NOLINT_TEST_F(ImportEventLoopThreadNotificationTest,
  PostFromWorkerThreadExecutesOnEventLoop)
{
  std::atomic<bool> callback_ran { false };
  std::thread::id callback_thread_id {};
  std::thread::id main_thread_id {};

  std::thread worker([&]() {
    // Create notification (normally done by ThreadPool)
    ThreadNotification<ImportEventLoop> notification(loop_, nullptr, nullptr);

    // Post from worker thread
    notification.Post(
      loop_,
      [](void* arg) {
        auto& data
          = *static_cast<std::pair<std::atomic<bool>*, std::thread::id*>*>(arg);
        *data.first = true;
        *data.second = std::this_thread::get_id();
      },
      new std::pair<std::atomic<bool>*, std::thread::id*>(
        &callback_ran, &callback_thread_id));
  });

  loop_.Post([&]() { main_thread_id = std::this_thread::get_id(); });

  // Let worker post, then stop
  loop_.Post([&]() {
    std::this_thread::sleep_for(50ms);
    loop_.Stop();
  });

  loop_.Run();
  worker.join();

  EXPECT_TRUE(callback_ran);
  EXPECT_EQ(callback_thread_id, main_thread_id);
}

//=== ThreadPool Integration Tests
//===-----------------------------------------//

class ImportEventLoopThreadPoolTest : public testing::Test {
protected:
  auto SetUp() -> void override
  {
    loop_ = std::make_unique<ImportEventLoop>();
    pool_ = std::make_unique<ThreadPool>(*loop_, 4);
  }

  auto TearDown() -> void override
  {
    pool_.reset();
    loop_.reset();
  }

  std::unique_ptr<ImportEventLoop> loop_;
  std::unique_ptr<ThreadPool> pool_;
};

NOLINT_TEST_F(ImportEventLoopThreadPoolTest, RunCpuBoundTaskReturnsResult)
{
  int result = 0;

  co::Run(*loop_,
    [&]() -> Co<> { result = co_await pool_->Run([]() { return 42; }); });

  EXPECT_EQ(result, 42);
}

//! A completed run must not let a later run exit before worker results arrive.
NOLINT_TEST_F(
  ImportEventLoopThreadPoolTest, RepeatedRunsWaitForWorkerCompletion)
{
  for (int cycle = 0; cycle < 3; ++cycle) {
    int result = -1;
    co::Run(*loop_, [&]() -> Co<> {
      result = co_await pool_->Run([cycle]() {
        std::this_thread::sleep_for(10ms);
        return cycle;
      });
    });
    EXPECT_EQ(result, cycle);
    EXPECT_FALSE(loop_->IsRunning());
  }
}

NOLINT_TEST_F(
  ImportEventLoopThreadPoolTest, RunCpuBoundTaskExecutesOnWorkerThread)
{
  std::thread::id event_loop_thread_id {};
  std::thread::id worker_thread_id {};

  co::Run(*loop_, [&]() -> Co<> {
    event_loop_thread_id = std::this_thread::get_id();

    worker_thread_id
      = co_await pool_->Run([]() { return std::this_thread::get_id(); });
  });

  EXPECT_NE(event_loop_thread_id, std::thread::id {});
  EXPECT_NE(worker_thread_id, std::thread::id {});
  EXPECT_NE(event_loop_thread_id, worker_thread_id);
}

NOLINT_TEST_F(
  ImportEventLoopThreadPoolTest, RunCpuBoundTaskResumesOnEventLoopThread)
{
  std::thread::id before_thread_id {};
  std::thread::id after_thread_id {};

  co::Run(*loop_, [&]() -> Co<> {
    before_thread_id = std::this_thread::get_id();
    co_await pool_->Run([]() { return 0; });
    after_thread_id = std::this_thread::get_id();
  });

  EXPECT_EQ(before_thread_id, after_thread_id);
}

NOLINT_TEST_F(ImportEventLoopThreadPoolTest, RunWithCancelTokenCompletes)
{
  std::atomic<bool> task_started { false };

  int result = 0;
  co::Run(*loop_, [&]() -> Co<> {
    result = co_await pool_->Run([&](ThreadPool::CancelToken canceled) {
      task_started = true;
      // Short task that doesn't actually get canceled
      if (canceled) {
        return -1;
      }
      return 42;
    });
  });

  EXPECT_TRUE(task_started);
  EXPECT_EQ(result, 42);
}

NOLINT_TEST_F(ImportEventLoopThreadPoolTest, RunMultipleTasksAllComplete)
{
  constexpr int kTaskCount = 10;
  std::atomic<int> completed_count { 0 };

  co::Run(*loop_, [&]() -> Co<> {
    for (int i = 0; i < kTaskCount; ++i) {
      auto result = co_await pool_->Run([i]() { return i * i; });
      EXPECT_EQ(result, i * i);
      ++completed_count;
    }
  });

  EXPECT_EQ(completed_count, kTaskCount);
}

} // namespace
