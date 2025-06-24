//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Composition/Composition.h>
#include <Oxygen/Composition/Object.h>
#include <Oxygen/Testing/GTest.h>
#include <atomic>
#include <thread>
#include <vector>

#include "./BaseCompositionTest.h"

using oxygen::TypeId;

namespace {
//=== ThreadSafetyTest
//===-----------------------------------------------------//
/*!\
 Thread safety scenarios for hybrid (pooled/non-pooled) component storage.\
 Covers concurrent add/remove and access patterns.\
 ### Key Features\
 - **Concurrent pooled/non-pooled add/remove**\
 - **Concurrent hybrid access**\
 @see oxygen::Composition\
*/
class ThreadSafetyTest
  : public oxygen::composition::testing::BaseCompositionTest {
protected:
  // Forward declare pooled component for use in TypeList
  class PooledComponent;
  // Now define the local ResourceTypeList for pooled components
  using LocalResourceTypeList = oxygen::TypeList<PooledComponent>;

  // Pooled test component: must inherit from both Component and Resource
  class PooledComponent final
    : public oxygen::Component,
      public oxygen::Resource<PooledComponent, LocalResourceTypeList> {
    OXYGEN_COMPONENT(PooledComponent)
  public:
    using ResourceTypeList = LocalResourceTypeList;
    PooledComponent() = default;
    explicit PooledComponent(int v)
      : value_(v)
    {
    }
    int value_;
  };

  // Non-pooled test component
  class NonPooledComponent final : public oxygen::Component {
    OXYGEN_COMPONENT(NonPooledComponent)
  public:
    explicit NonPooledComponent(int v)
      : value_(v)
    {
    }
    int value_;
  };

  // Test composition wrapper for easier test access
  class TestComposition : public oxygen::Composition {
  public:
    using Base = Composition;

    // Expose protected methods for testing
    using Base::AddComponent;
    using Base::RemoveComponent;
    using Base::ReplaceComponent;
  };

  TestComposition composition_;
};

//=== ConcurrentPooledAddRemove
//===-----------------------------------------------------//
/*!\
 Test concurrent add/remove of pooled and non-pooled components.\
 Ensures no race conditions or corruption in hybrid storage.\
 @see ThreadSafetyTest, oxygen::Composition\
*/
NOLINT_TEST_F(ThreadSafetyTest, ConcurrentPooledAddRemove)
{
  using ::testing::AllOf;
  using ::testing::SizeIs;
  constexpr int THREADS = 8;
  std::vector<std::thread> threads;
  std::atomic<int> ready { 0 };
  std::atomic<bool> start { false };

  // Arrange: Launch threads for add/remove
  for (int i = 0; i < THREADS; ++i) {
    threads.emplace_back([&, i] {
      ++ready;
      while (!start) { }
      // Act: Each thread adds/removes pooled or non-pooled
      for (int j = 0; j < 10; ++j) {
        try {
          if (i % 2 == 0) {
            composition_.AddComponent<PooledComponent>(i * 100 + j);
            composition_.RemoveComponent<PooledComponent>();
          } else {
            composition_.AddComponent<NonPooledComponent>(i * 100 + j);
            composition_.RemoveComponent<NonPooledComponent>();
          }
        } catch (const oxygen::ComponentError&) {
          // Component exists
          // Move on: another thread is setting up the composition
        } catch (...) {
          FAIL() << "Unexpected exception in thread " << i
                 << " during add/remove operation";
        }
      }
    });
  }
  while (ready < THREADS) { }
  start = true;
  for (auto& t : threads)
    t.join();

  // Assert: No crash or deadlock
  SUCCEED();
}

//=== ConcurrentHybridAccess
//===-----------------------------------------------------//
/*!\
 Test concurrent access to pooled and non-pooled components.\
 Ensures safe hybrid access under multi-threaded reads.\
 @see ThreadSafetyTest, oxygen::Composition\
*/
NOLINT_TEST_F(ThreadSafetyTest, ConcurrentHybridAccess)
{
  using ::testing::AllOf;
  using ::testing::SizeIs;
  constexpr int THREADS = 8;
  std::vector<std::thread> threads;
  std::atomic<int> ready { 0 };
  std::atomic<bool> start { false };

  // Arrange: Add both component types
  composition_.AddComponent<PooledComponent>(1);
  composition_.AddComponent<NonPooledComponent>(2);

  // Act: Launch threads for concurrent access
  for (int i = 0; i < THREADS; ++i) {
    threads.emplace_back([&, i] {
      ++ready;
      while (!start) { }
      for (int j = 0; j < 100; ++j) {
        if (i % 2 == 0) {
          EXPECT_NO_THROW({
            [[maybe_unused]] auto& pooled
              = composition_.GetComponent<PooledComponent>();
          }) << "PooledComponent access should not throw (thread "
             << i << ")";
        } else {
          EXPECT_NO_THROW({
            [[maybe_unused]] auto& nonpooled
              = composition_.GetComponent<NonPooledComponent>();
          }) << "NonPooledComponent access should not throw (thread "
             << i << ")";
        }
      }
    });
  }
  while (ready < THREADS) { }
  start = true;
  for (auto& t : threads)
    t.join();

  // Assert: No crash or deadlock
  SUCCEED();
}

} // namespace
