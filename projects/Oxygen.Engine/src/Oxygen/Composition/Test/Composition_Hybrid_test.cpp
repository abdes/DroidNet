//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Composition/Composition.h>
#include <Oxygen/Composition/Object.h>
#include <Oxygen/Testing/GTest.h>
#include <memory>
#include <thread>
#include <vector>

#include "./BaseCompositionTest.h"

using oxygen::TypeId;

namespace {
// Forward declare pooled type
class PooledComponent;
// ResourceTypeList after forward declaration
using ResourceTypeList = oxygen::TypeList<PooledComponent>;
// Full definition of pooled type
class PooledComponent final : public oxygen::Component,
                              public oxygen::Resource<PooledComponent,
                                oxygen::TypeList<PooledComponent>> {
  OXYGEN_COMPONENT(PooledComponent)
public:
  using ResourceTypeList = oxygen::TypeList<PooledComponent>;
  PooledComponent() = default;
  explicit PooledComponent(int v)
    : value_(v)
  {
  }
  int value_;
};

class HybridStorageTest
  : public oxygen::composition::testing::BaseCompositionTest {
protected:
  class NonPooledComponent final : public oxygen::Component {
    OXYGEN_COMPONENT(NonPooledComponent)
  public:
    explicit NonPooledComponent(int v)
      : value_(v)
    {
    }
    int value_;
  };
  class TestComposition : public oxygen::Composition {
  public:
    using Base = Composition;
    template <typename T, typename... Args>
    auto AddComponent(Args&&... args) -> T&
    {
      return Base::AddComponent<T>(std::forward<Args>(args)...);
    }
    template <typename T> auto RemoveComponent() -> void
    {
      Base::RemoveComponent<T>();
    }
    template <typename OldT, typename NewT = OldT, typename... Args>
    auto ReplaceComponent(Args&&... args) -> NewT&
    {
      return Base::ReplaceComponent<OldT, NewT>(std::forward<Args>(args)...);
    }
    template <typename T> [[nodiscard]] auto HasComponent() const -> bool
    {
      return Base::HasComponent<T>();
    }
    template <typename T> [[nodiscard]] auto GetComponent() const -> T&
    {
      return Base::GetComponent<T>();
    }
  };
  TestComposition composition_;
};

//! Add both pooled and non-pooled components and verify presence.
NOLINT_TEST_F(HybridStorageTest, AddBothPooledAndNonPooled)
{
  // Arrange & Act
  auto& pooled = composition_.AddComponent<PooledComponent>(1);
  auto& nonpooled = composition_.AddComponent<NonPooledComponent>(2);
  // Assert
  EXPECT_EQ(pooled.value_, 1);
  EXPECT_EQ(nonpooled.value_, 2);
  EXPECT_TRUE(composition_.HasComponent<PooledComponent>())
    << "Should have pooled component";
  EXPECT_TRUE(composition_.HasComponent<NonPooledComponent>())
    << "Should have non-pooled component";
}

//! Remove pooled then non-pooled component and verify state.
NOLINT_TEST_F(HybridStorageTest, RemovePooledThenNonPooled)
{
  // Arrange
  composition_.AddComponent<PooledComponent>(3);
  composition_.AddComponent<NonPooledComponent>(4);
  // Act
  composition_.RemoveComponent<PooledComponent>();
  // Assert
  EXPECT_FALSE(composition_.HasComponent<PooledComponent>())
    << "Pooled component should be removed";
  EXPECT_TRUE(composition_.HasComponent<NonPooledComponent>())
    << "Non-pooled component should still exist";
  composition_.RemoveComponent<NonPooledComponent>();
  EXPECT_FALSE(composition_.HasComponent<NonPooledComponent>())
    << "Non-pooled component should be removed";
}

//! Remove non-pooled then pooled component and verify state.
NOLINT_TEST_F(HybridStorageTest, RemoveNonPooledThenPooled)
{
  // Arrange
  composition_.AddComponent<PooledComponent>(5);
  composition_.AddComponent<NonPooledComponent>(6);
  // Act
  composition_.RemoveComponent<NonPooledComponent>();
  // Assert
  EXPECT_FALSE(composition_.HasComponent<NonPooledComponent>())
    << "Non-pooled component should be removed";
  EXPECT_TRUE(composition_.HasComponent<PooledComponent>())
    << "Pooled component should still exist";
  composition_.RemoveComponent<PooledComponent>();
  EXPECT_FALSE(composition_.HasComponent<PooledComponent>())
    << "Pooled component should be removed";
}

} // namespace
