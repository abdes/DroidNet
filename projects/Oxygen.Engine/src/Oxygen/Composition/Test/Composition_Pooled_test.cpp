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
  PooledComponent() = default;
  explicit PooledComponent(int v)
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

//! Add and access a pooled component.
NOLINT_TEST_F(PooledComponentTest, AddAndAccessPooledComponent)
{
  // Arrange & Act
  auto& pooled = composition_.AddComponent<PooledComponent>(42);
  // Assert
  EXPECT_EQ(pooled.value_, 42);
  EXPECT_TRUE(composition_.HasComponent<PooledComponent>());
  auto& get_ref = composition_.GetComponent<PooledComponent>();
  EXPECT_EQ(get_ref.value_, 42);
  EXPECT_EQ(&pooled, &get_ref);
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

//! Replace a pooled component and verify new instance.
NOLINT_TEST_F(PooledComponentTest, ReplacePooledComponent)
{
  // Arrange
  composition_.AddComponent<PooledComponent>(5);

  // Act
  auto& pooled2 = composition_.ReplaceComponent<PooledComponent>(10);

  auto& test = composition_.GetComponent<PooledComponent>();
  EXPECT_EQ(test.value_, 10);

  // Assert
  EXPECT_EQ(pooled2.value_, 10);
  EXPECT_EQ(composition_.GetComponent<PooledComponent>().value_, 10);
}
} // namespace
