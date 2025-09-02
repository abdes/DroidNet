//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <Oxygen/Graphics/Common/Internal/CommandListPool.h>
#include <Oxygen/Graphics/Common/Types/QueueRole.h>
#include <Oxygen/Testing/GTest.h>

using namespace oxygen::graphics;
using namespace oxygen::graphics::internal;
using ::testing::AllOf;
using ::testing::Contains;
using ::testing::IsSupersetOf;
using ::testing::SizeIs;

//===----------------------------------------------------------------------===//
// Test Fixtures and Helper Classes
//===----------------------------------------------------------------------===//

namespace {

//! Mock CommandList for testing purposes.
class MockCommandList : public CommandList {
public:
  MockCommandList(std::string_view name, QueueRole role)
    : CommandList(name, role)
  {
  }

  OXYGEN_MAKE_NON_COPYABLE(MockCommandList)
  OXYGEN_MAKE_NON_MOVABLE(MockCommandList)

  auto GetCreationCount() const -> int { return creation_count_; }
  auto IncrementCreationCount() -> void { ++creation_count_; }

private:
  int creation_count_ = 0;
};

//! Basic test fixture for CommandListPool tests.
class CommandListPoolBasicTest : public ::testing::Test {
protected:
  auto SetUp() -> void override
  {
    // Setup factory that creates mock command lists
    factory_ = [this](QueueRole role, std::string_view name) {
      ++total_created_count_;
      auto cmd_list = std::make_unique<MockCommandList>(name, role);
      cmd_list->IncrementCreationCount();
      return std::unique_ptr<CommandList>(cmd_list.release());
    };

    pool_ = std::make_unique<CommandListPool>(factory_);
  }

  auto TearDown() -> void override
  {
    pool_.reset();
    total_created_count_ = 0;
  }

  //! Helper to verify command list properties.
  auto ExpectCommandListProperties(const std::shared_ptr<CommandList>& cmd_list,
    QueueRole expected_role, std::string_view expected_name) -> void
  {
    EXPECT_NE(cmd_list, nullptr);
    EXPECT_EQ(cmd_list->GetQueueRole(), expected_role);
    EXPECT_EQ(cmd_list->GetName(), expected_name);
  }

  CommandListPool::CommandListFactory factory_;
  std::unique_ptr<CommandListPool> pool_;
  int total_created_count_ = 0;
};

//! Test fixture for error scenarios.
class CommandListPoolErrorTest : public ::testing::Test {
protected:
  auto SetUp() -> void override
  {
    // Valid factory for basic setup
    valid_factory_ = [](QueueRole role, std::string_view name) {
      return std::make_unique<MockCommandList>(name, role);
    };

    // Invalid factory that returns nullptr
    null_factory_ = [](QueueRole /*role*/, std::string_view /*name*/) {
      return std::unique_ptr<CommandList>(nullptr);
    };
  }

  CommandListPool::CommandListFactory valid_factory_;
  CommandListPool::CommandListFactory null_factory_;
};

//! Test fixture for concurrency scenarios.
class CommandListPoolConcurrencyTest : public ::testing::Test {
protected:
  auto SetUp() -> void override
  {
    factory_ = [this](QueueRole role, std::string_view name) {
      std::lock_guard lock(creation_mutex_);
      ++creation_count_;
      return std::make_unique<MockCommandList>(name, role);
    };

    pool_ = std::make_unique<CommandListPool>(factory_);
  }

  auto TearDown() -> void override
  {
    pool_.reset();
    creation_count_ = 0;
  }

  //! Helper to acquire command lists concurrently.
  auto AcquireCommandListsConcurrently(int thread_count, int lists_per_thread)
    -> std::vector<std::shared_ptr<CommandList>>
  {
    std::vector<std::shared_ptr<CommandList>> all_command_lists;
    std::vector<std::thread> threads;
    std::mutex results_mutex;

    for (int i = 0; i < thread_count; ++i) {
      threads.emplace_back([&, i]() {
        std::vector<std::shared_ptr<CommandList>> thread_lists;
        for (int j = 0; j < lists_per_thread; ++j) {
          auto cmd_list = pool_->AcquireCommandList(QueueRole::kGraphics,
            "Thread" + std::to_string(i) + "_List" + std::to_string(j));
          thread_lists.push_back(cmd_list);
        }

        std::lock_guard lock(results_mutex);
        all_command_lists.insert(
          all_command_lists.end(), thread_lists.begin(), thread_lists.end());
      });
    }

    for (auto& thread : threads) {
      thread.join();
    }

    return all_command_lists;
  }

  CommandListPool::CommandListFactory factory_;
  std::unique_ptr<CommandListPool> pool_;
  std::atomic<int> creation_count_ = 0;
  std::mutex creation_mutex_;
};

//===----------------------------------------------------------------------===//
// Constructor and Factory Tests
//===----------------------------------------------------------------------===//

//! Test CommandListPool construction with valid factory.
NOLINT_TEST_F(
  CommandListPoolBasicTest, Constructor_ValidFactory_CreatesPoolSuccessfully)
{
  // Arrange & Act - pool created in SetUp

  // Assert
  EXPECT_NO_THROW({
    auto cmd_list = pool_->AcquireCommandList(QueueRole::kGraphics, "TestList");
  });
}

//! Test CommandListPool construction with null factory throws exception.
NOLINT_TEST_F(
  CommandListPoolErrorTest, Constructor_NullFactory_ThrowsInvalidArgument)
{
  // Arrange
  CommandListPool::CommandListFactory test_null_factory = nullptr;

  // Act & Assert
  EXPECT_THROW(
    { auto pool = CommandListPool(test_null_factory); }, std::invalid_argument);
}

//===----------------------------------------------------------------------===//
// Command List Acquisition Tests
//===----------------------------------------------------------------------===//

//! Test acquiring command list from empty pool creates new instance.
NOLINT_TEST_F(
  CommandListPoolBasicTest, AcquireCommandList_EmptyPool_CreatesNewCommandList)
{
  // Arrange
  const auto queue_role = QueueRole::kGraphics;
  const auto name = "TestCommandList";

  // Act
  auto cmd_list = pool_->AcquireCommandList(queue_role, name);

  // Assert
  GCHECK_F(ExpectCommandListProperties(cmd_list, queue_role, name));
  EXPECT_EQ(total_created_count_, 1);
}

//! Test acquiring multiple command lists with different queue roles.
NOLINT_TEST_F(CommandListPoolBasicTest,
  AcquireCommandList_DifferentQueueRoles_CreatesAppropriateCommandLists)
{
  // Arrange
  const std::vector<QueueRole> queue_roles = { QueueRole::kGraphics,
    QueueRole::kCompute, QueueRole::kTransfer, QueueRole::kPresent };

  // Act
  std::vector<std::shared_ptr<CommandList>> command_lists;
  for (size_t i = 0; i < queue_roles.size(); ++i) {
    auto cmd_list = pool_->AcquireCommandList(
      queue_roles[i], "TestList" + std::to_string(i));
    command_lists.push_back(cmd_list);
  }

  // Assert
  EXPECT_EQ(command_lists.size(), queue_roles.size());
  for (size_t i = 0; i < command_lists.size(); ++i) {
    GCHECK_F(ExpectCommandListProperties(
      command_lists[i], queue_roles[i], "TestList" + std::to_string(i)));
  }
  EXPECT_EQ(total_created_count_, static_cast<int>(queue_roles.size()));
}

//! Test command list recycling when returned to pool.
NOLINT_TEST_F(CommandListPoolBasicTest,
  AcquireCommandList_RecycledCommandList_ReusesExistingInstance)
{
  // Arrange
  const auto queue_role = QueueRole::kGraphics;
  auto first_cmd_list = pool_->AcquireCommandList(queue_role, "FirstList");

  // Act - Release first command list and acquire a new one
  first_cmd_list.reset(); // This should return the command list to the pool
  auto second_cmd_list = pool_->AcquireCommandList(queue_role, "SecondList");

  // Assert
  GCHECK_F(
    ExpectCommandListProperties(second_cmd_list, queue_role, "SecondList"));
  EXPECT_EQ(
    total_created_count_, 1); // Only one command list should have been created
}

//! Test acquiring command lists with different names updates the name
//! correctly.
NOLINT_TEST_F(CommandListPoolBasicTest,
  AcquireCommandList_DifferentNames_UpdatesNameCorrectly)
{
  // Arrange
  const auto queue_role = QueueRole::kGraphics;
  const std::vector<std::string> names
    = { "FirstName", "SecondName", "ThirdName" };

  // Act & Assert
  for (const auto& name : names) {
    auto cmd_list = pool_->AcquireCommandList(queue_role, name);
    GCHECK_F(ExpectCommandListProperties(cmd_list, queue_role, name));
    cmd_list.reset(); // Return to pool for next iteration
  }

  EXPECT_EQ(total_created_count_, 1); // Should reuse the same command list
}

//===----------------------------------------------------------------------===//
// Pool Management Tests
//===----------------------------------------------------------------------===//

//! Test Clear method empties all pools.
TEST_F(CommandListPoolBasicTest, Clear_WithCommandListsInPool_EmptiesAllPools)
{
  // Arrange
  auto cmd_list1 = pool_->AcquireCommandList(QueueRole::kGraphics, "List1");
  auto cmd_list2 = pool_->AcquireCommandList(QueueRole::kCompute, "List2");
  cmd_list1.reset(); // Return to pool
  cmd_list2.reset(); // Return to pool

  // Act
  pool_->Clear();

  // Assert
  // After clearing, new acquisitions should create new command lists
  auto new_cmd_list
    = pool_->AcquireCommandList(QueueRole::kGraphics, "NewList");
  EXPECT_EQ(total_created_count_, 3); // 2 original + 1 new after clear
}

//! Test destructor clears pools properly.
TEST_F(CommandListPoolBasicTest,
  Destructor_WithCommandListsInPool_ClearsPoolsSuccessfully)
{
  // Arrange
  auto cmd_list = pool_->AcquireCommandList(QueueRole::kGraphics, "TestList");
  cmd_list.reset(); // Return to pool

  // Act & Assert
  EXPECT_NO_THROW({
    pool_.reset(); // Destroy the pool
  });
}

//===----------------------------------------------------------------------===//
// Edge Case and Error Handling Tests
//===----------------------------------------------------------------------===//

//! Test acquiring command list with empty name.
TEST_F(CommandListPoolBasicTest, AcquireCommandList_EmptyName_HandlesGracefully)
{
  // Arrange & Act
  auto cmd_list = pool_->AcquireCommandList(QueueRole::kGraphics, "");

  // Assert
  GCHECK_F(ExpectCommandListProperties(cmd_list, QueueRole::kGraphics, ""));
}

//! Test multiple acquisitions without releasing create separate instances.
TEST_F(CommandListPoolBasicTest,
  AcquireCommandList_MultipleWithoutRelease_CreatesSeparateInstances)
{
  // Arrange & Act
  auto cmd_list1 = pool_->AcquireCommandList(QueueRole::kGraphics, "List1");
  auto cmd_list2 = pool_->AcquireCommandList(QueueRole::kGraphics, "List2");
  auto cmd_list3 = pool_->AcquireCommandList(QueueRole::kGraphics, "List3");

  // Assert
  EXPECT_NE(cmd_list1.get(), cmd_list2.get());
  EXPECT_NE(cmd_list2.get(), cmd_list3.get());
  EXPECT_NE(cmd_list1.get(), cmd_list3.get());
  EXPECT_EQ(total_created_count_, 3);
}

//===----------------------------------------------------------------------===//
// Concurrency Tests
//===----------------------------------------------------------------------===//

//! Test concurrent acquisition of command lists is thread-safe.
TEST_F(CommandListPoolConcurrencyTest,
  AcquireCommandList_ConcurrentAccess_IsThreadSafe)
{
  // Arrange
  const int thread_count = 4;
  const int lists_per_thread = 5;
  const int expected_total_lists = thread_count * lists_per_thread;

  // Act
  auto all_command_lists
    = AcquireCommandListsConcurrently(thread_count, lists_per_thread);

  // Assert
  EXPECT_EQ(all_command_lists.size(), expected_total_lists);

  // Verify all command lists are unique (no double-allocation)
  std::set<CommandList*> unique_pointers;
  for (const auto& cmd_list : all_command_lists) {
    unique_pointers.insert(cmd_list.get());
  }
  EXPECT_EQ(unique_pointers.size(), expected_total_lists);
}

//! Test concurrent acquisition and release operations.
TEST_F(CommandListPoolConcurrencyTest,
  AcquireAndRelease_ConcurrentOperations_IsThreadSafe)
{
  // Arrange
  const int thread_count = 3;
  const int operations_per_thread = 10;
  std::vector<std::thread> threads;

  // Act
  for (int i = 0; i < thread_count; ++i) {
    threads.emplace_back([&, i]() {
      for (int j = 0; j < operations_per_thread; ++j) {
        auto cmd_list = pool_->AcquireCommandList(QueueRole::kGraphics,
          "Thread" + std::to_string(i) + "_Op" + std::to_string(j));

        // Hold briefly then release
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        cmd_list.reset();
      }
    });
  }

  for (auto& thread : threads) {
    thread.join();
  }

  // Assert
  // The creation count should be less than or equal to total operations
  // due to recycling
  const int total_operations = thread_count * operations_per_thread;
  EXPECT_LE(creation_count_.load(), total_operations);
  EXPECT_GT(creation_count_.load(), 0);
}

//! Test Clear operation during concurrent access.
TEST_F(
  CommandListPoolConcurrencyTest, Clear_DuringConcurrentAccess_IsThreadSafe)
{
  // Arrange
  std::atomic<bool> should_continue { true };
  std::vector<std::thread> worker_threads;

  // Start worker threads that continuously acquire and release command lists
  for (int i = 0; i < 3; ++i) {
    worker_threads.emplace_back([&, i]() {
      int operation_count = 0;
      while (should_continue.load()) {
        auto cmd_list = pool_->AcquireCommandList(QueueRole::kGraphics,
          "Worker" + std::to_string(i) + "_"
            + std::to_string(operation_count++));

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        cmd_list.reset();
      }
    });
  }

  // Act
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  EXPECT_NO_THROW({ pool_->Clear(); });

  // Assert & Cleanup
  should_continue.store(false);
  for (auto& thread : worker_threads) {
    thread.join();
  }
}

} // namespace
