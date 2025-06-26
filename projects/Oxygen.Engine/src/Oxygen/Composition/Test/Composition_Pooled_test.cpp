//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <memory>

#include <Oxygen/Base/ResourceHandle.h>
#include <Oxygen/Base/ResourceTypeList.h>
#include <Oxygen/Composition/Composition.h>
#include <Oxygen/Composition/Object.h>
#include <Oxygen/Testing/GTest.h>

#include "./BaseCompositionTest.h"

using oxygen::TypeId;

namespace {
// Forward declare pooled type
class PooledComponent;
// ResourceTypeList after forward declaration
using ResourceTypeList = oxygen::TypeList<PooledComponent>;
// Full definition of pooled type
class PooledComponent final : public oxygen::Component {
  OXYGEN_POOLED_COMPONENT(PooledComponent, ResourceTypeList)
public:
  explicit PooledComponent(const int v)
    : value_(v)
  {
  }
  int value_;
};

class PooledComponentTest
  : public oxygen::composition::testing::BaseCompositionTest {
protected:
  class DependentOnPooled final : public oxygen::Component {
    OXYGEN_COMPONENT(DependentOnPooled)
    OXYGEN_COMPONENT_REQUIRES(PooledComponent)
  public:
    auto UpdateDependencies(
      const std::function<Component&(TypeId)>& get_component) noexcept
      -> void override
    {
      pooled_ = &static_cast<PooledComponent&>(
        get_component(PooledComponent::ClassTypeId()));
    }
    PooledComponent* pooled_ { nullptr };
  };
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

//! Add a pooled component with in-place construction.
NOLINT_TEST_F(PooledComponentTest, AddPooledComponentInPlace)
{
  const auto& pooled = composition_.AddComponent<PooledComponent>(123);
  EXPECT_TRUE(composition_.HasComponent<PooledComponent>());
  EXPECT_EQ(pooled.value_, 123);
  EXPECT_EQ(composition_.GetComponent<PooledComponent>().value_, 123);
}

//! Add a pooled component by value.
NOLINT_TEST_F(PooledComponentTest, AddPooledComponentByValue)
{
  PooledComponent new_comp(456);
  const auto& pooled = composition_.AddComponent<PooledComponent>(new_comp);
  EXPECT_TRUE(composition_.HasComponent<PooledComponent>());
  EXPECT_EQ(pooled.value_, 456);
  EXPECT_EQ(composition_.GetComponent<PooledComponent>().value_, 456);
}

//! Add a pooled component from a unique_ptr.
NOLINT_TEST_F(PooledComponentTest, AddPooledComponentFromUniquePtr)
{
  const auto& pooled = composition_.AddComponent<PooledComponent>(
    std::make_unique<PooledComponent>(789));
  EXPECT_TRUE(composition_.HasComponent<PooledComponent>());
  EXPECT_EQ(pooled.value_, 789);
  EXPECT_EQ(composition_.GetComponent<PooledComponent>().value_, 789);
}

//! Remove a pooled component and verify it is gone.
NOLINT_TEST_F(PooledComponentTest, RemovePooledComponent)
{
  // Arrange
  composition_.AddComponent<PooledComponent>(7);
  // Act
  composition_.RemoveComponent<PooledComponent>();
  // Assert
  EXPECT_FALSE(composition_.HasComponent<PooledComponent>())
    << "Pooled component should be removed";
}

//! Removing a pooled component with dependents should throw.
NOLINT_TEST_F(PooledComponentTest, DependencyAwareRemovalThrows)
{
  // Arrange
  composition_.AddComponent<PooledComponent>(1);
  composition_.AddComponent<DependentOnPooled>();
  // Act & Assert
  EXPECT_THROW(
    composition_.RemoveComponent<PooledComponent>(), oxygen::ComponentError)
    << "Should throw when removing pooled component with dependents";
}

//! Remove dependent then dependency should succeed.
NOLINT_TEST_F(PooledComponentTest, RemoveDependentThenDependency)
{
  // Arrange
  composition_.AddComponent<PooledComponent>(2);
  composition_.AddComponent<DependentOnPooled>();
  // Act
  composition_.RemoveComponent<DependentOnPooled>();
  // Assert
  EXPECT_NO_THROW(composition_.RemoveComponent<PooledComponent>())
    << "Should allow removing pooled component after dependents are gone";
}

//! Replace a pooled component with in-place construction.
NOLINT_TEST_F(PooledComponentTest, ReplacePooledComponentInPlace)
{
  composition_.AddComponent<PooledComponent>(5);
  const auto& replaced = composition_.ReplaceComponent<PooledComponent>(10);
  EXPECT_TRUE(composition_.HasComponent<PooledComponent>());
  EXPECT_EQ(replaced.value_, 10);
  EXPECT_EQ(composition_.GetComponent<PooledComponent>().value_, 10);
}

//! Replace a pooled component by value.
NOLINT_TEST_F(PooledComponentTest, ReplacePooledComponentByValue)
{
  composition_.AddComponent<PooledComponent>(7);
  PooledComponent new_comp(42);
  const auto& replaced
    = composition_.ReplaceComponent<PooledComponent>(new_comp);
  EXPECT_TRUE(composition_.HasComponent<PooledComponent>());
  EXPECT_EQ(replaced.value_, 42);
  EXPECT_EQ(composition_.GetComponent<PooledComponent>().value_, 42);
}

//! Replace a pooled component with a unique_ptr.
NOLINT_TEST_F(PooledComponentTest, ReplacePooledComponentFromUniquePtr)
{
  composition_.AddComponent<PooledComponent>(8);
  const auto& replaced = composition_.ReplaceComponent<PooledComponent>(
    std::make_unique<PooledComponent>(99));
  EXPECT_TRUE(composition_.HasComponent<PooledComponent>());
  EXPECT_EQ(replaced.value_, 99);
  EXPECT_EQ(composition_.GetComponent<PooledComponent>().value_, 99);
}

} // namespace
