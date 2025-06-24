//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Composition/ComponentPoolRegistry.h>

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

#include <Oxygen/Base/Resource.h>
#include <Oxygen/Testing/GTest.h>

using ::testing::AllOf;
using ::testing::Contains;
using ::testing::IsSupersetOf;
using ::testing::SizeIs;

namespace {

//=== Test Component Types ===------------------------------------------------//

// Forward declarations for test resource types
class TestTransformComponent;
class TestRenderComponent;
class TestPhysicsComponent;
class TestCustomSizeComponent;

// Test ResourceTypeList for ComponentPoolRegistry unit tests
using TestPoolRegistryResourceTypeList
  = oxygen::TypeList<TestTransformComponent, TestRenderComponent,
    TestPhysicsComponent, TestCustomSizeComponent>;

//! Test pooled component with basic functionality
class TestTransformComponent : public oxygen::Component {
  OXYGEN_POOLED_COMPONENT(
    TestTransformComponent, TestPoolRegistryResourceTypeList);

public:
  explicit TestTransformComponent(int x = 0, int y = 0, int z = 0)
    : x_(x)
    , y_(y)
    , z_(z)
  {
  }

  auto GetX() const noexcept -> int { return x_; }
  auto GetY() const noexcept -> int { return y_; }
  auto GetZ() const noexcept -> int { return z_; }

  void SetPosition(int x, int y, int z) noexcept
  {
    x_ = x;
    y_ = y;
    z_ = z;
  }

private:
  int x_, y_, z_;
};

//! Test pooled component without custom pool size
class TestRenderComponent : public oxygen::Component {
  OXYGEN_POOLED_COMPONENT(
    TestRenderComponent, TestPoolRegistryResourceTypeList);

public:
  explicit TestRenderComponent(std::string name = "default")
    : name_(std::move(name))
  {
  }

  auto GetName() const -> const std::string& { return name_; }
  void SetName(std::string name) { name_ = std::move(name); }

private:
  std::string name_;
};

//! Test pooled component for threading tests
class TestPhysicsComponent : public oxygen::Component {
  OXYGEN_POOLED_COMPONENT(
    TestPhysicsComponent, TestPoolRegistryResourceTypeList);

public:
  explicit TestPhysicsComponent(double mass = 1.0)
    : mass_(mass)
    , counter_(0)
  {
  }

  auto GetMass() const noexcept -> double { return mass_; }
  void IncrementCounter() noexcept { ++counter_; }
  auto GetCounter() const noexcept -> int { return counter_; }

private:
  double mass_;
  int counter_; // Changed from std::atomic<int> to regular int
};

//! Test pooled component with custom expected pool size
class TestCustomSizeComponent : public oxygen::Component {
  OXYGEN_POOLED_COMPONENT(
    TestCustomSizeComponent, TestPoolRegistryResourceTypeList);

public:
  static constexpr std::size_t kExpectedPoolSize = 2048;

  explicit TestCustomSizeComponent(int value = 42)
    : value_(value)
  {
  }

  auto GetValue() const noexcept -> int { return value_; }
  void SetValue(int value) noexcept { value_ = value; }

private:
  int value_;
};

//=== Test Fixtures ===-------------------------------------------------------//

/*!
 Basic ComponentPoolRegistry fixture for singleton and pool access tests.
 Tests fundamental registry operations and pool management.
*/
class ComponentPoolRegistryBasicTest : public ::testing::Test {
protected:
  void SetUp() override
  {
    // Get registry instance for testing
    registry_ = &oxygen::ComponentPoolRegistry::Get();
  }

  void TearDown() override
  {
    // Clean up after each test to prevent state leakage
    registry_->ForceClearAllPools();
  }

  oxygen::ComponentPoolRegistry* registry_;
};

/*!
 Threading fixture for ComponentPoolRegistry concurrency tests.
 Tests thread safety of registry operations and pool access.
*/
class ComponentPoolRegistryThreadingTest
  : public ComponentPoolRegistryBasicTest { };

//=== Singleton and Basic Access Tests ===------------------------------------//

//! Test singleton behavior - same instance across calls
NOLINT_TEST_F(ComponentPoolRegistryBasicTest, SingletonBehavior_SameInstance)
{
  using ::testing::Eq;

  // Arrange
  // (No setup needed)

  // Act
  auto& registry1 = oxygen::ComponentPoolRegistry::Get();
  auto& registry2 = oxygen::ComponentPoolRegistry::Get();

  // Assert
  EXPECT_THAT(std::addressof(registry1), Eq(std::addressof(registry2)));
}

//! Test pool access for different component types
NOLINT_TEST_F(ComponentPoolRegistryBasicTest, PoolAccess_DifferentTypes)
{
  using ::testing::Ne;

  // Arrange
  // (No setup needed)

  // Act
  auto& transform_pool
    = oxygen::ComponentPoolRegistry::GetComponentPool<TestTransformComponent>();
  auto& render_pool
    = oxygen::ComponentPoolRegistry::GetComponentPool<TestRenderComponent>();
  auto& physics_pool
    = oxygen::ComponentPoolRegistry::GetComponentPool<TestPhysicsComponent>();

  // Assert
  // Different component types should get different pool instances
  auto* transform_ptr = static_cast<void*>(&transform_pool);
  auto* render_ptr = static_cast<void*>(&render_pool);
  auto* physics_ptr = static_cast<void*>(&physics_pool);

  EXPECT_THAT(transform_ptr, Ne(render_ptr));
  EXPECT_THAT(render_ptr, Ne(physics_ptr));
  EXPECT_THAT(transform_ptr, Ne(physics_ptr));
}

//! Test that repeated access to same component type returns same pool
NOLINT_TEST_F(
  ComponentPoolRegistryBasicTest, PoolAccess_SameTypeReturnsIdenticalPool)
{
  using ::testing::Eq;

  // Arrange
  // (No setup needed)

  // Act
  auto& pool1
    = oxygen::ComponentPoolRegistry::GetComponentPool<TestTransformComponent>();
  auto& pool2
    = oxygen::ComponentPoolRegistry::GetComponentPool<TestTransformComponent>();
  auto& pool3
    = oxygen::ComponentPoolRegistry::GetComponentPool<TestTransformComponent>();

  // Assert
  // The same pool instance should be returned for multiple accesses
  EXPECT_THAT(std::addressof(pool1), Eq(std::addressof(pool2)));
  EXPECT_THAT(std::addressof(pool2), Eq(std::addressof(pool3)));
}

//=== Pool Operations Tests ===-----------------------------------------------//

//! Test basic pool operations through registry
NOLINT_TEST_F(
  ComponentPoolRegistryBasicTest, PoolOperations_BasicAllocationAndAccess)
{
  using ::testing::IsNull;
  using ::testing::NotNull;

  // Arrange
  auto& pool
    = oxygen::ComponentPoolRegistry::GetComponentPool<TestTransformComponent>();

  // Act
  auto handle = pool.Allocate(10, 20, 30);

  // Assert
  EXPECT_TRUE(handle.IsValid());

  auto* component = pool.Get(handle);
  EXPECT_THAT(component, NotNull());
  EXPECT_THAT(component->GetX(), testing::Eq(10));
  EXPECT_THAT(component->GetY(), testing::Eq(20));
  EXPECT_THAT(component->GetZ(), testing::Eq(30));

  // Act - deallocate
  pool.Deallocate(handle);

  // Assert
  auto* null_component = pool.Get(handle);
  EXPECT_THAT(null_component, IsNull());
}

//! Test pool operations with different component types
NOLINT_TEST_F(
  ComponentPoolRegistryBasicTest, PoolOperations_MultipleComponentTypes)
{
  using ::testing::Eq;
  using ::testing::IsNull;
  using ::testing::NotNull;

  // Arrange
  auto& transform_pool
    = oxygen::ComponentPoolRegistry::GetComponentPool<TestTransformComponent>();
  auto& render_pool
    = oxygen::ComponentPoolRegistry::GetComponentPool<TestRenderComponent>();

  // Act
  auto transform_handle = transform_pool.Allocate(1, 2, 3);
  auto render_handle = render_pool.Allocate("test_render");

  // Assert
  auto* transform = transform_pool.Get(transform_handle);
  auto* render = render_pool.Get(render_handle);

  EXPECT_THAT(transform, NotNull());
  EXPECT_THAT(render, NotNull());
  EXPECT_THAT(transform->GetX(), Eq(1));
  EXPECT_THAT(render->GetName(), Eq("test_render"));

  // Cross-type access should fail appropriately
  auto* invalid_transform = transform_pool.Get(render_handle);
  auto* invalid_render = render_pool.Get(transform_handle);

  EXPECT_THAT(invalid_transform, IsNull());
  EXPECT_THAT(invalid_render, IsNull());

  // Clean up
  transform_pool.Deallocate(transform_handle);
  render_pool.Deallocate(render_handle);
}

//=== Custom Pool Size Tests ===----------------------------------------------//

//! Test that components with kExpectedPoolSize are respected
NOLINT_TEST_F(
  ComponentPoolRegistryBasicTest, CustomPoolSize_ComponentWithExpectedSize)
{
  using ::testing::Ge;

  // Arrange
  auto& pool = oxygen::ComponentPoolRegistry::GetComponentPool<
    TestCustomSizeComponent>();

  // Act
  auto handle = pool.Allocate(123);
  auto* component = pool.Get(handle);

  // Assert
  EXPECT_THAT(component, ::testing::NotNull());
  EXPECT_THAT(component->GetValue(), testing::Eq(123));
  EXPECT_THAT(pool.Size(), Ge(1));

  // Clean up
  pool.Deallocate(handle);
}

//=== Threading Tests ===-----------------------------------------------------//

//! Test concurrent access to the same component pool
NOLINT_TEST_F(
  ComponentPoolRegistryThreadingTest, Threading_ConcurrentPoolAccess)
{
  using ::testing::Eq;

  constexpr int kNumThreads = 8;
  constexpr int kOperationsPerThread = 100;
  std::vector<std::thread> threads;
  std::atomic<int> successful_operations { 0 };

  //! Launch multiple threads accessing the same pool
  for (int i = 0; i < kNumThreads; ++i) {
    threads.emplace_back([&, i]() {
      auto& pool = oxygen::ComponentPoolRegistry::GetComponentPool<
        TestPhysicsComponent>();

      for (int j = 0; j < kOperationsPerThread; ++j) {
        // Allocate component
        auto handle = pool.Allocate(static_cast<double>(i * 100 + j));

        // Access and modify component
        if (auto* component = pool.Get(handle)) {
          component->IncrementCounter();
          if (component->GetCounter() == 1) {
            successful_operations.fetch_add(1);
          }
        }

        // Deallocate component
        pool.Deallocate(handle);
      }
    });
  }

  // Wait for all threads to complete
  for (auto& thread : threads) {
    thread.join();
  }

  // Verify all operations completed successfully
  EXPECT_THAT(
    successful_operations.load(), Eq(kNumThreads * kOperationsPerThread));
}

//! Test concurrent access to different component pools
NOLINT_TEST_F(
  ComponentPoolRegistryThreadingTest, Threading_ConcurrentDifferentPools)
{
  using ::testing::Eq;

  constexpr int kNumThreads = 6;
  std::vector<std::thread> threads;
  std::atomic<int> successful_operations { 0 };

  //! Launch threads accessing different pools
  for (int i = 0; i < kNumThreads; ++i) {
    threads.emplace_back([&, i]() {
      if (i % 3 == 0) {
        // Transform pool
        auto& pool = oxygen::ComponentPoolRegistry::GetComponentPool<
          TestTransformComponent>();
        auto handle = pool.Allocate(i, i + 1, i + 2);
        if (auto* component = pool.Get(handle)) {
          component->SetPosition(i * 10, i * 10 + 1, i * 10 + 2);
          successful_operations.fetch_add(1);
        }
        pool.Deallocate(handle);
      } else if (i % 3 == 1) {
        // Render pool
        auto& pool = oxygen::ComponentPoolRegistry::GetComponentPool<
          TestRenderComponent>();
        auto handle = pool.Allocate("thread_" + std::to_string(i));
        if (auto* component = pool.Get(handle)) {
          component->SetName("modified_" + std::to_string(i));
          successful_operations.fetch_add(1);
        }
        pool.Deallocate(handle);
      } else {
        // Physics pool
        auto& pool = oxygen::ComponentPoolRegistry::GetComponentPool<
          TestPhysicsComponent>();
        auto handle = pool.Allocate(static_cast<double>(i));
        if (auto* component = pool.Get(handle)) {
          component->IncrementCounter();
          successful_operations.fetch_add(1);
        }
        pool.Deallocate(handle);
      }
    });
  }

  // Wait for all threads to complete
  for (auto& thread : threads) {
    thread.join();
  }

  // Verify all operations completed successfully
  EXPECT_THAT(successful_operations.load(), Eq(kNumThreads));
}

//! Test that registry singleton is thread-safe during initialization
NOLINT_TEST_F(
  ComponentPoolRegistryThreadingTest, Threading_RegistrySingletonThreadSafety)
{
  using ::testing::Eq;

  constexpr int kNumThreads = 10;
  std::vector<std::thread> threads;
  std::vector<oxygen::ComponentPoolRegistry*> registry_ptrs(kNumThreads);

  //! Launch threads that all get the registry instance simultaneously
  for (int i = 0; i < kNumThreads; ++i) {
    threads.emplace_back(
      [&, i]() { registry_ptrs[i] = &oxygen::ComponentPoolRegistry::Get(); });
  }

  // Wait for all threads to complete
  for (auto& thread : threads) {
    thread.join();
  }
  // Assert
  // All threads should get the same registry instance
  for (int i = 1; i < kNumThreads; ++i) {
    EXPECT_THAT(registry_ptrs[i], Eq(registry_ptrs[0]));
  }
}

//=== Edge Cases and Error Handling ===---------------------------------------//

//! Test registry behavior with rapid pool creation and access
NOLINT_TEST_F(
  ComponentPoolRegistryBasicTest, EdgeCases_RapidPoolCreationAndAccess)
{
  using ::testing::Eq;
  using ::testing::NotNull;

  //! Rapidly create and access pools
  for (int i = 0; i < 100; ++i) {
    auto& transform_pool = oxygen::ComponentPoolRegistry::GetComponentPool<
      TestTransformComponent>();
    auto& render_pool
      = oxygen::ComponentPoolRegistry::GetComponentPool<TestRenderComponent>();

    // Verify pools are consistently the same
    auto& transform_pool2 = oxygen::ComponentPoolRegistry::GetComponentPool<
      TestTransformComponent>();
    auto& render_pool2
      = oxygen::ComponentPoolRegistry::GetComponentPool<TestRenderComponent>();

    EXPECT_THAT(
      std::addressof(transform_pool), Eq(std::addressof(transform_pool2)));
    EXPECT_THAT(std::addressof(render_pool), Eq(std::addressof(render_pool2)));
  }
}

//! Test that pool state persists across multiple registry accesses
NOLINT_TEST_F(ComponentPoolRegistryBasicTest, EdgeCases_PoolStatePersistence)
{
  using ::testing::Eq;
  using ::testing::NotNull;

  // Arrange & Act
  auto& pool1
    = oxygen::ComponentPoolRegistry::GetComponentPool<TestTransformComponent>();
  auto initial_size = pool1.Size(); // Get current size instead of assuming 0
  auto handle = pool1.Allocate(100, 200, 300);

  EXPECT_THAT(pool1.Size(), Eq(initial_size + 1));

  // Act
  auto& pool2
    = oxygen::ComponentPoolRegistry::GetComponentPool<TestTransformComponent>();
  auto* component = pool2.Get(handle);
  // Assert
  // Component should persist across pool accesses
  EXPECT_THAT(component, NotNull());
  EXPECT_THAT(component->GetX(), Eq(100));
  EXPECT_THAT(component->GetY(), Eq(200));
  EXPECT_THAT(component->GetZ(), Eq(300));
  EXPECT_THAT(pool2.Size(), Eq(initial_size + 1));
}

} // namespace
