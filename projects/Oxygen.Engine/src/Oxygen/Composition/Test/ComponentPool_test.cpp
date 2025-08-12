//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Base/Resource.h>
#include <Oxygen/Composition/Component.h>
#include <Oxygen/Composition/ComponentPool.h>

using testing::AllOf;
using testing::Contains;
using testing::IsSupersetOf;
using testing::SizeIs;

namespace {

//=== Test Component Types ===------------------------------------------------//

// Forward declarations for test resource types
class TestTransformComponent;
class TestRenderComponent;
class TestPhysicsComponent;

// Test ResourceTypeList for unit tests
using TestResourceTypeList = oxygen::TypeList<TestTransformComponent,
  TestRenderComponent, TestPhysicsComponent>;

//! Test pooled component with basic functionality
class TestTransformComponent : public oxygen::Component {
  OXYGEN_POOLED_COMPONENT(TestTransformComponent, TestResourceTypeList)
public:
  static constexpr std::size_t kExpectedPoolSize = 512;

  explicit TestTransformComponent(int x = 0, int y = 0, int z = 0)
    : x_(x)
    , y_(y)
    , z_(z)
  {
  }

  auto GetX() const noexcept -> int { return x_; }
  auto GetY() const noexcept -> int { return y_; }
  auto GetZ() const noexcept -> int { return z_; }

  auto SetPosition(int x, int y, int z) noexcept -> void
  {
    x_ = x;
    y_ = y;
    z_ = z;
  }

  // Comparison for defragmentation testing
  static auto Compare(
    const TestTransformComponent& a, const TestTransformComponent& b) -> bool
  {
    return a.x_ < b.x_;
  }

  int x_, y_, z_;
};

//! Test pooled component without comparison method
class TestRenderComponent : public oxygen::Component {
  OXYGEN_POOLED_COMPONENT(TestRenderComponent, TestResourceTypeList)
public:
  explicit TestRenderComponent(std::string name = "default")
    : name_(std::move(name))
  {
  }

  auto GetName() const -> const std::string& { return name_; }
  auto SetName(std::string name) -> void { name_ = std::move(name); }

  std::string name_;
};

//! Test pooled component for threading tests
class TestPhysicsComponent : public oxygen::Component {
  OXYGEN_POOLED_COMPONENT(TestPhysicsComponent, TestResourceTypeList)
public:
  explicit TestPhysicsComponent(double mass = 1.0)
    : mass_(mass)
    , velocity_x_(0.0)
    , velocity_y_(0.0)
  {
  }

  auto GetMass() const noexcept -> double { return mass_; }
  auto SetVelocity(double x, double y) noexcept -> void
  {
    velocity_x_ = x;
    velocity_y_ = y;
  }
  auto GetVelocityX() const noexcept -> double { return velocity_x_; }
  auto GetVelocityY() const noexcept -> double { return velocity_y_; }

private:
  double mass_;
  double velocity_x_, velocity_y_;
};

//=== Test Fixtures ===-------------------------------------------------------//

//! Basic ComponentPool fixture for simple operations
class ComponentPoolBasicTest : public testing::Test {
protected:
  auto SetUp() -> void override
  {
    pool_ = std::make_unique<oxygen::ComponentPool<TestTransformComponent>>(64);
  }

  auto TearDown() -> void override { pool_.reset(); }

  std::unique_ptr<oxygen::ComponentPool<TestTransformComponent>> pool_;
};

//! Error handling fixture for testing exceptions and invalid states
class ComponentPoolErrorTest : public ComponentPoolBasicTest { };

//! Threading fixture for concurrent access tests
class ComponentPoolThreadingTest : public testing::Test {
protected:
  auto SetUp() -> void override
  {
    physics_pool_
      = std::make_unique<oxygen::ComponentPool<TestPhysicsComponent>>(1024);
    transform_pool_
      = std::make_unique<oxygen::ComponentPool<TestTransformComponent>>(1024);
  }

  auto TearDown() -> void override
  {
    physics_pool_.reset();
    transform_pool_.reset();
  }

  std::unique_ptr<oxygen::ComponentPool<TestPhysicsComponent>> physics_pool_;
  std::unique_ptr<oxygen::ComponentPool<TestTransformComponent>>
    transform_pool_;
};

//! Complex scenario fixture for edge cases and performance
class ComponentPoolComplexTest : public testing::Test {
protected:
  auto SetUp() -> void override
  {
    small_pool_
      = std::make_unique<oxygen::ComponentPool<TestRenderComponent>>(4);
    large_pool_
      = std::make_unique<oxygen::ComponentPool<TestTransformComponent>>(2048);
  }

  auto TearDown() -> void override
  {
    small_pool_.reset();
    large_pool_.reset();
  }

  // Helper to create multiple components for testing
  auto CreateMultipleComponents(std::size_t count)
    -> std::vector<oxygen::ResourceHandle>
  {
    std::vector<oxygen::ResourceHandle> handles;
    handles.reserve(count);

    for (std::size_t i = 0; i < count; ++i) {
      handles.push_back(large_pool_->Allocate(
        static_cast<int>(i), static_cast<int>(i * 2), static_cast<int>(i * 3)));
    }

    return handles;
  }

  std::unique_ptr<oxygen::ComponentPool<TestRenderComponent>> small_pool_;
  std::unique_ptr<oxygen::ComponentPool<TestTransformComponent>> large_pool_;
};

//=== Basic Functionality Tests ===-------------------------------------------//

//! Test ComponentPool construction and initial state
NOLINT_TEST_F(ComponentPoolBasicTest, Construction_InitialStateIsValid)
{
  // Arrange - pool created in SetUp()

  // Act & Assert
  EXPECT_TRUE(pool_->IsEmpty());
  EXPECT_EQ(pool_->Size(), 0);

  auto type_id = pool_->GetComponentType();
  EXPECT_EQ(type_id, TestTransformComponent::GetResourceType());
}

//! Test single component allocation and access
NOLINT_TEST_F(ComponentPoolBasicTest, AllocateAndGet_SingleComponent_Success)
{
  // Arrange
  constexpr int x = 10, y = 20, z = 30;

  // Act
  auto handle = pool_->Allocate(x, y, z);
  auto* component = pool_->Get(handle);

  // Assert
  EXPECT_TRUE(handle.IsValid());
  EXPECT_NE(component, nullptr);
  EXPECT_EQ(component->GetX(), x);
  EXPECT_EQ(component->GetY(), y);
  EXPECT_EQ(component->GetZ(), z);
  EXPECT_EQ(pool_->Size(), 1);
  EXPECT_FALSE(pool_->IsEmpty());
}

//! Test Allocate(&&) with correct type
NOLINT_TEST_F(ComponentPoolBasicTest, Allocate_RValue_CorrectType_Success)
{
  // Arrange
  TestTransformComponent comp(42, 43, 44);

  // Act
  auto handle = pool_->Allocate(std::move(comp));
  auto* component = pool_->Get(handle);

  // Assert
  EXPECT_TRUE(handle.IsValid());
  ASSERT_NE(component, nullptr);
  EXPECT_EQ(component->GetX(), 42);
  EXPECT_EQ(component->GetY(), 43);
  EXPECT_EQ(component->GetZ(), 44);
}

//! Test Allocate(&&) with correct type
NOLINT_TEST_F(ComponentPoolBasicTest, Allocate_UniquePtr_CorrectType_Success)
{
  // Arrange
  std::unique_ptr<oxygen::Component> comp
    = std::make_unique<TestTransformComponent>(42, 43, 44);

  // Act
  auto handle = pool_->Allocate(std::move(comp));
  auto* component = pool_->Get(handle);

  // Assert
  EXPECT_TRUE(handle.IsValid());
  ASSERT_NE(component, nullptr);
  EXPECT_EQ(component->GetX(), 42);
  EXPECT_EQ(component->GetY(), 43);
  EXPECT_EQ(component->GetZ(), 44);
}

//! Test const access to components
NOLINT_TEST_F(ComponentPoolBasicTest, ConstGet_ValidHandle_ReturnsConstPointer)
{
  // Arrange
  auto handle = pool_->Allocate(1, 2, 3);

  // Act
  const auto* const_pool = pool_.get();
  const auto* component = const_pool->Get(handle);

  // Assert
  EXPECT_NE(component, nullptr);
  EXPECT_EQ(component->GetX(), 1);
}

//! Test component deallocation
NOLINT_TEST_F(ComponentPoolBasicTest, Deallocate_ValidHandle_RemovesComponent)
{
  // Arrange
  auto handle = pool_->Allocate(5, 10, 15);
  EXPECT_NE(pool_->Get(handle), nullptr);

  // Act
  pool_->Deallocate(handle);

  // Assert
  EXPECT_EQ(pool_->Get(handle), nullptr);
  EXPECT_EQ(pool_->Size(), 0);
  EXPECT_TRUE(pool_->IsEmpty());
}

//=== Handle Management and Validation Tests ===------------------------------//

//! Test multiple allocations with different handles
NOLINT_TEST_F(ComponentPoolBasicTest, MultipleAllocations_ProduceUniqueHandles)
{
  // Arrange
  constexpr std::size_t count = 10;
  std::vector<oxygen::ResourceHandle> handles;

  // Act
  for (std::size_t i = 0; i < count; ++i) {
    handles.push_back(pool_->Allocate(
      static_cast<int>(i), static_cast<int>(i * 2), static_cast<int>(i * 3)));
  }

  // Assert
  EXPECT_EQ(pool_->Size(), count);

  // Verify all handles are unique and valid
  for (std::size_t i = 0; i < count; ++i) {
    EXPECT_TRUE(handles[i].IsValid());
    auto* component = pool_->Get(handles[i]);
    EXPECT_NE(component, nullptr);
    EXPECT_EQ(component->GetX(), static_cast<int>(i));
  }

  // Verify handles are unique
  for (std::size_t i = 0; i < count; ++i) {
    for (std::size_t j = i + 1; j < count; ++j) {
      EXPECT_NE(handles[i], handles[j]);
    }
  }
}

//! Test handle reuse after deallocation
NOLINT_TEST_F(
  ComponentPoolBasicTest, HandleReuse_AfterDeallocation_WorksCorrectly)
{
  // Arrange
  auto handle1 = pool_->Allocate(1, 2, 3);
  auto handle2 = pool_->Allocate(4, 5, 6);

  // Act - deallocate first, then allocate new
  pool_->Deallocate(handle1);
  auto handle3 = pool_->Allocate(7, 8, 9);

  // Assert
  EXPECT_EQ(pool_->Get(handle1), nullptr);
  EXPECT_NE(pool_->Get(handle2), nullptr);
  EXPECT_NE(pool_->Get(handle3), nullptr);
  EXPECT_EQ(pool_->Size(), 2);
}

//=== Defragmentation Tests ===-----------------------------------------------//

/*!
 IMPORTANT: ResourceTable's defragmentation implements reverse insertion sort.
 "a < b" comparison results in DESCENDING order (not ascending as expected).
 Defragmentation only works on fragmented tables (after deletions create gaps).
 */

//! Test defragmentation with built-in Compare method
NOLINT_TEST_F(ComponentPoolBasicTest, DefragmentWithComparison_OrdersComponents)
{
  // Arrange
  std::vector<oxygen::ResourceHandle> handles;
  for (int i = 5; i >= 1; --i) {
    handles.push_back(pool_->Allocate(i, 0, 0));
  }

  // Create fragmentation (required for defragmentation to work)
  pool_->Deallocate(handles[1]); // Delete component with x=4
  pool_->Deallocate(handles[3]); // Delete component with x=2

  handles.push_back(pool_->Allocate(0, 0, 0)); // x=0
  handles.push_back(pool_->Allocate(6, 0, 0)); // x=6

  // Act
  auto swaps_performed
    = pool_->Defragment(); // Uses TestTransformComponent::Compare

  // Assert - descending order due to reverse insertion sort
  EXPECT_GT(swaps_performed, 0);
  std::size_t count = 0;
  int prev_x = (std::numeric_limits<int>::max)();
  pool_->ForEach([&](const TestTransformComponent& component) {
    if (count > 0) {
      EXPECT_GE(prev_x, component.GetX());
    }
    prev_x = component.GetX();
    ++count;
  });
  EXPECT_EQ(count, 5);
}

//! Test defragmentation with custom comparison lambda
NOLINT_TEST_F(
  ComponentPoolBasicTest, DefragmentWithCustomComparison_UsesProvidedOrder)
{
  // Arrange
  std::vector<oxygen::ResourceHandle> handles;
  handles.push_back(pool_->Allocate(1, 30, 0)); // y=30
  handles.push_back(pool_->Allocate(2, 10, 0)); // y=10
  handles.push_back(pool_->Allocate(3, 20, 0)); // y=20
  handles.push_back(pool_->Allocate(4, 40, 0)); // y=40
  handles.push_back(pool_->Allocate(5, 5, 0)); // y=5

  // Create fragmentation
  pool_->Deallocate(handles[0]); // Delete y=30
  pool_->Deallocate(handles[3]); // Delete y=40

  // Act - custom comparison by Y coordinate
  auto swaps_performed = pool_->Defragment(
    [](const TestTransformComponent& a, const TestTransformComponent& b) {
      return a.GetY() < b.GetY(); // "Ascending" comparison
    });

  // Assert - still produces descending order
  EXPECT_GT(swaps_performed, 0);
  std::size_t count = 0;
  int prev_y = (std::numeric_limits<int>::max)();
  pool_->ForEach([&](const TestTransformComponent& component) {
    if (count == 0) {
      EXPECT_EQ(component.GetY(), 20); // Largest first
    } else if (count == 1) {
      EXPECT_EQ(component.GetY(), 10);
    } else if (count == 2) {
      EXPECT_EQ(component.GetY(), 5); // Smallest last
    }
    if (count > 0) {
      EXPECT_GE(prev_y, component.GetY());
    }
    prev_y = component.GetY();
    ++count;
  });
  EXPECT_EQ(count, 3);
}

//=== Threading and Concurrency Tests ===-------------------------------------//

//! Test concurrent allocations from multiple threads
NOLINT_TEST_F(ComponentPoolThreadingTest, ConcurrentAllocations_ThreadSafe)
{
  // Arrange
  constexpr std::size_t thread_count = 4;
  constexpr std::size_t allocations_per_thread = 100;
  std::vector<std::thread> threads;
  std::vector<std::vector<oxygen::ResourceHandle>> thread_handles(thread_count);

  // Act - allocate from multiple threads
  for (std::size_t t = 0; t < thread_count; ++t) {
    threads.emplace_back([this, t, allocations_per_thread, &thread_handles]() {
      for (std::size_t i = 0; i < allocations_per_thread; ++i) {
        auto mass = static_cast<double>(t * 1000 + i);
        thread_handles[t].push_back(physics_pool_->Allocate(mass));
      }
    });
  }

  // Wait for all threads
  for (auto& thread : threads) {
    thread.join();
  }

  // Assert
  EXPECT_EQ(physics_pool_->Size(), thread_count * allocations_per_thread);

  // Verify all handles are valid and unique
  std::vector<oxygen::ResourceHandle> all_handles;
  for (const auto& handles : thread_handles) {
    for (const auto& handle : handles) {
      EXPECT_TRUE(handle.IsValid());
      EXPECT_NE(physics_pool_->Get(handle), nullptr);
      all_handles.push_back(handle);
    }
  }

  // Check uniqueness
  std::sort(all_handles.begin(), all_handles.end());
  auto unique_end = std::unique(all_handles.begin(), all_handles.end());
  EXPECT_EQ(unique_end, all_handles.end());
}

//! Test concurrent read/write operations
/*!
 Tests ComponentPool's thread safety under concurrent access patterns.

 This test verifies that ComponentPool's shared_mutex implementation correctly
 handles multiple readers and writers accessing the same pool simultaneously:
 - Reader threads: Use shared_lock for Get() operations (multiple readers
   allowed)
 - Writer threads: Use exclusive lock for SetPosition() calls via Get() + modify

 The test pattern:
 1. Pre-allocate components in single thread (avoids allocation contention)
 2. Start multiple reader/writer threads
 3. Let them run concurrently for short duration
 4. Verify no data races or corruption occurred

 NOTE: This tests the locking behavior of ComponentPool, not ResourceTable's
 internal thread safety (which is tested separately).
 */
TEST_F(ComponentPoolThreadingTest, ConcurrentReadWrite_ThreadSafe)
{ // NOLINT_NEXTLINE(misc-use-anonymous-namespace)
  // Arrange - pre-allocate components
  constexpr std::size_t component_count = 200;
  std::vector<oxygen::ResourceHandle> handles;
  for (std::size_t i = 0; i < component_count; ++i) {
    handles.push_back(transform_pool_->Allocate(static_cast<int>(i), 0, 0));
  }

  std::atomic<bool> stop_flag { false };
  std::atomic<std::size_t> read_operations { 0 };
  std::atomic<std::size_t> write_operations { 0 };

  // Act - concurrent readers and writers
  std::vector<std::thread> threads;

  // Reader threads
  for (int t = 0; t < 2; ++t) {
    threads.emplace_back([&]() {
      while (!stop_flag.load()) {
        for (const auto& handle : handles) {
          if (auto* component = transform_pool_->Get(handle)) {
            volatile int x = component->GetX(); // Force read
            (void)x;
            read_operations.fetch_add(1);
          }
        }
      }
    });
  }

  // Writer threads
  for (int t = 0; t < 2; ++t) {
    threads.emplace_back([&]() {
      int counter = 0;
      while (!stop_flag.load()) {
        for (const auto& handle : handles) {
          if (auto* component = transform_pool_->Get(handle)) {
            component->SetPosition(counter, counter + 1, counter + 2);
            write_operations.fetch_add(1);
            ++counter;
          }
        }
      }
    });
  }

  // Let threads run for a short time
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  stop_flag.store(true);

  // Wait for threads to finish
  for (auto& thread : threads) {
    thread.join();
  }
  // Assert
  EXPECT_GT(read_operations.load(), 0);
  EXPECT_GT(write_operations.load(), 0);
  EXPECT_EQ(transform_pool_->Size(), component_count);
}

//=== Edge Cases ===----------------------------------------------------------//

//! Test pool growth beyond initial capacity
NOLINT_TEST_F(ComponentPoolComplexTest, GrowthBeyondCapacity_HandledCorrectly)
{
  // Arrange - small_pool_ has capacity of 4
  std::vector<oxygen::ResourceHandle> handles;

  // Act - allocate beyond initial capacity
  for (std::size_t i = 0; i < 10; ++i) {
    handles.push_back(small_pool_->Allocate("component_" + std::to_string(i)));
  }

  // Assert
  EXPECT_EQ(small_pool_->Size(), 10);

  // Verify all components are accessible
  std::size_t verified_count = 0;
  small_pool_->ForEach([&](const TestRenderComponent& component) {
    EXPECT_EQ(
      component.GetName(), "component_" + std::to_string(verified_count));
    ++verified_count;
  });
  EXPECT_EQ(verified_count, handles.size());
}

//! Test dense component access with large dataset
NOLINT_TEST_F(ComponentPoolComplexTest, DenseAccess_LargeDataset_PerformsWell)
{
  // Arrange
  constexpr std::size_t large_count = 1000;
  auto handles = CreateMultipleComponents(large_count);

  // Act
  // Assert
  EXPECT_EQ(large_pool_->Size(), large_count);

  // Verify data integrity in dense access
  std::size_t verified_count = 0;
  large_pool_->ForEach([&](const TestTransformComponent& component) {
    // Components were created with pattern: x=i, y=i*2, z=i*3
    auto x = component.GetX();
    auto expected_y = x * 2;
    auto expected_z = x * 3;
    EXPECT_EQ(component.GetY(), expected_y);
    EXPECT_EQ(component.GetZ(), expected_z);
    ++verified_count;
  });
  EXPECT_EQ(verified_count, large_count);
}

//! Test mixed allocation and deallocation patterns
NOLINT_TEST_F(ComponentPoolComplexTest, MixedOperations_MaintainsIntegrity)
{
  // Arrange
  std::vector<oxygen::ResourceHandle> handles;

  // Act - complex allocation/deallocation pattern Phase 1: Allocate 20
  // components
  for (std::size_t i = 0; i < 20; ++i) {
    handles.push_back(large_pool_->Allocate(static_cast<int>(i), 0, 0));
  }

  // Phase 2: Deallocate every 3rd component
  for (std::size_t i = 2; i < handles.size(); i += 3) {
    large_pool_->Deallocate(handles[i]);
    handles[i]
      .Invalidate(); // Mark as invalid instead of using CreateInvalidHandle
  }

  // Phase 3: Allocate 10 more components
  for (std::size_t i = 100; i < 110; ++i) {
    handles.push_back(large_pool_->Allocate(static_cast<int>(i), 0, 0));
  }

  // Assert
  std::size_t valid_handles = 0;
  std::size_t accessible_components = 0;

  for (const auto& handle : handles) {
    if (handle.IsValid()) {
      ++valid_handles;
      if (large_pool_->Get(handle) != nullptr) {
        ++accessible_components;
      }
    }
  }

  EXPECT_EQ(valid_handles, accessible_components);
  EXPECT_EQ(large_pool_->Size(), accessible_components);
}

//=== Error Handling Tests ===------------------------------------------------//

//! Test Allocate(&&) with wrong type
NOLINT_TEST_F(ComponentPoolErrorTest, Allocate_RValue_WrongType_ThrowsOrDies)
{
  // Arrange
  TestRenderComponent wrong_type("bad");
  auto* wrong_type_ptr = static_cast<oxygen::Component*>(&wrong_type);
#ifdef NDEBUG
  // In release, expect exception
  EXPECT_THROW(pool_->Allocate(std::move(*wrong_type_ptr)), std::exception);
#else
  // In debug, expect death
  EXPECT_DEATH(pool_->Allocate(std::move(*wrong_type_ptr)), "");
#endif
}

//! Test Allocate(&&) with wrong type
NOLINT_TEST_F(ComponentPoolErrorTest, Allocate_UniquePtr_WrongType_ThrowsOrDies)
{
  // Arrange
  std::unique_ptr<oxygen::Component> wrong_type
    = std::make_unique<TestRenderComponent>("bad");
#ifdef NDEBUG
  // In release, expect exception
  EXPECT_THROW(pool_->Allocate(std::move(*wrong_type)), std::exception);
#else
  // In debug, expect death
  EXPECT_DEATH(pool_->Allocate(std::move(wrong_type)), "");
#endif
}

} // anonymous namespace
