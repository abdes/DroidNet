//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

#include <Oxygen/Content/Import/Async/ImportEventLoop.h>
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

class ImportEventLoopBasicTest : public ::testing::Test {
protected:
  ImportEventLoop loop_;
};

//! Verify event loop runs and stops correctly via Post.
NOLINT_TEST_F(ImportEventLoopBasicTest, RunAndStop_ViaPost_Succeeds)
{
  // Arrange
  std::atomic<bool> callback_ran { false };

  // Act
  loop_.Post([&]() {
    callback_ran = true;
    loop_.Stop();
  });
  loop_.Run();

  // Assert
  EXPECT_TRUE(callback_ran);
}

//! Verify multiple callbacks execute in order.
NOLINT_TEST_F(ImportEventLoopBasicTest, Post_MultipleCallbacks_ExecuteInOrder)
{
  // Arrange
  std::vector<int> order;

  // Act
  loop_.Post([&]() { order.push_back(1); });
  loop_.Post([&]() { order.push_back(2); });
  loop_.Post([&]() {
    order.push_back(3);
    loop_.Stop();
  });
  loop_.Run();

  // Assert
  using ::testing::ElementsAre;
  EXPECT_THAT(order, ElementsAre(1, 2, 3));
}

//! Verify Stop() can be called from a different thread.
NOLINT_TEST_F(ImportEventLoopBasicTest, Stop_FromOtherThread_Succeeds)
{
  // Arrange
  std::thread stopper([&]() {
    std::this_thread::sleep_for(50ms);
    loop_.Stop();
  });

  // Act & Assert (should not hang)
  loop_.Run();
  stopper.join();
}

//! Verify IsRunning() returns correct state.
NOLINT_TEST_F(ImportEventLoopBasicTest, IsRunning_ReturnsCorrectState)
{
  // Arrange
  std::atomic<bool> was_running_inside { false };

  // Act
  EXPECT_FALSE(loop_.IsRunning());

  loop_.Post([&]() {
    was_running_inside = loop_.IsRunning();
    loop_.Stop();
  });
  loop_.Run();

  // Assert
  EXPECT_TRUE(was_running_inside);
  EXPECT_FALSE(loop_.IsRunning());
}

//=== EventLoopTraits Tests
//===------------------------------------------------//

class ImportEventLoopTraitsTest : public ::testing::Test {
protected:
  ImportEventLoop loop_;
};

//! Verify EventLoopTraits::EventLoopId returns a valid ID.
NOLINT_TEST_F(ImportEventLoopTraitsTest, EventLoopId_ReturnsValidId)
{
  // Arrange & Act
  auto id = EventLoopTraits<ImportEventLoop>::EventLoopId(loop_);

  // Assert
  EXPECT_NE(id.Get(), nullptr);
}

//! Verify EventLoopTraits can be used with Run.
NOLINT_TEST_F(ImportEventLoopTraitsTest, Run_WithCoRun_Works)
{
  // Arrange
  std::atomic<bool> coroutine_ran { false };

  // Act
  co::Run(loop_, [&]() -> Co<> {
    coroutine_ran = true;
    co_return;
  });

  // Assert
  EXPECT_TRUE(coroutine_ran);
}

//=== ThreadNotification Tests
//===---------------------------------------------//

class ImportEventLoopThreadNotificationTest : public ::testing::Test {
protected:
  ImportEventLoop loop_;
};

//! Verify ThreadNotification posts callback to event loop from worker thread.
NOLINT_TEST_F(ImportEventLoopThreadNotificationTest,
  Post_FromWorkerThread_ExecutesOnEventLoop)
{
  // Arrange
  std::atomic<bool> callback_ran { false };
  std::thread::id callback_thread_id {};
  std::thread::id main_thread_id {};

  // Act
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

  // Assert
  EXPECT_TRUE(callback_ran);
  EXPECT_EQ(callback_thread_id, main_thread_id);
}

//=== ThreadPool Integration Tests
//===-----------------------------------------//

class ImportEventLoopThreadPoolTest : public ::testing::Test {
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

//! Verify ThreadPool works with ImportEventLoop.
NOLINT_TEST_F(ImportEventLoopThreadPoolTest, Run_CpuBoundTask_ReturnsResult)
{
  // Arrange & Act
  int result = 0;

  co::Run(*loop_,
    [&]() -> Co<> { result = co_await pool_->Run([]() { return 42; }); });

  // Assert
  EXPECT_EQ(result, 42);
}

//! Verify ThreadPool executes on worker thread, not event loop thread.
NOLINT_TEST_F(
  ImportEventLoopThreadPoolTest, Run_CpuBoundTask_ExecutesOnWorkerThread)
{
  // Arrange
  std::thread::id event_loop_thread_id {};
  std::thread::id worker_thread_id {};

  // Act
  co::Run(*loop_, [&]() -> Co<> {
    event_loop_thread_id = std::this_thread::get_id();

    worker_thread_id
      = co_await pool_->Run([]() { return std::this_thread::get_id(); });
  });

  // Assert
  EXPECT_NE(event_loop_thread_id, std::thread::id {});
  EXPECT_NE(worker_thread_id, std::thread::id {});
  EXPECT_NE(event_loop_thread_id, worker_thread_id);
}

//! Verify ThreadPool result returns to event loop thread.
NOLINT_TEST_F(
  ImportEventLoopThreadPoolTest, Run_CpuBoundTask_ResumesOnEventLoopThread)
{
  // Arrange
  std::thread::id before_thread_id {};
  std::thread::id after_thread_id {};

  // Act
  co::Run(*loop_, [&]() -> Co<> {
    before_thread_id = std::this_thread::get_id();
    co_await pool_->Run([]() { return 0; });
    after_thread_id = std::this_thread::get_id();
  });

  // Assert
  EXPECT_EQ(before_thread_id, after_thread_id);
}

//! Verify ThreadPool with CancelToken receives valid token.
NOLINT_TEST_F(ImportEventLoopThreadPoolTest, Run_WithCancelToken_Completes)
{
  // Arrange
  std::atomic<bool> task_started { false };

  // Act
  int result = 0;
  co::Run(*loop_, [&]() -> Co<> {
    result = co_await pool_->Run([&](ThreadPool::CancelToken cancelled) {
      task_started = true;
      // Short task that doesn't actually get cancelled
      if (cancelled) {
        return -1;
      }
      return 42;
    });
  });

  // Assert
  EXPECT_TRUE(task_started);
  EXPECT_EQ(result, 42);
}

//! Verify multiple ThreadPool tasks complete correctly.
NOLINT_TEST_F(ImportEventLoopThreadPoolTest, Run_MultipleTasks_AllComplete)
{
  // Arrange
  constexpr int kTaskCount = 10;
  std::atomic<int> completed_count { 0 };

  // Act
  co::Run(*loop_, [&]() -> Co<> {
    for (int i = 0; i < kTaskCount; ++i) {
      auto result = co_await pool_->Run([i]() { return i * i; });
      EXPECT_EQ(result, i * i);
      ++completed_count;
    }
  });

  // Assert
  EXPECT_EQ(completed_count, kTaskCount);
}

} // namespace
