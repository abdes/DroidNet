//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Composition/Composition.h>
#include <Oxygen/Composition/Object.h>
#include <Oxygen/Testing/GTest.h>
#include <functional>

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
  using ResourceTypeList = ::ResourceTypeList;
  PooledComponent() = default;
  explicit PooledComponent(int v)
    : value_(v)
  {
  }
  int value_;
};

class ErrorHandlingTest
  : public oxygen::composition::testing::BaseCompositionTest {
protected:
  class SimpleComponent final : public oxygen::Component {
    OXYGEN_COMPONENT(SimpleComponent)
  };
  class AnotherSimpleComponent final : public oxygen::Component {
    OXYGEN_COMPONENT(AnotherSimpleComponent)
  };
  class DependentComponent final : public oxygen::Component {
    OXYGEN_COMPONENT(DependentComponent)
    OXYGEN_COMPONENT_REQUIRES(SimpleComponent)
  public:
    auto UpdateDependencies(
      const std::function<Component&(TypeId)>& get_component) noexcept
      -> void override
    {
      simple_ = &static_cast<SimpleComponent&>(
        get_component(SimpleComponent::ClassTypeId()));
    }
    SimpleComponent* simple_ { nullptr };
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

//! Remove a non-existent component should not throw.
NOLINT_TEST_F(ErrorHandlingTest, RemoveNonExistentComponent)
{
  // Act & Assert
  EXPECT_NO_THROW(composition_.RemoveComponent<SimpleComponent>())
    << "Removing non-existent component should not throw";
}

//! Replace a non-existent component should throw.
NOLINT_TEST_F(ErrorHandlingTest, ReplaceNonExistentComponent)
{
  // Act & Assert
  EXPECT_THROW(
    composition_.ReplaceComponent<SimpleComponent>(), oxygen::ComponentError)
    << "Replacing non-existent component should throw";
}

//! Removing a component twice should not throw.
NOLINT_TEST_F(ErrorHandlingTest, RemoveComponentTwice)
{
  // Arrange
  composition_.AddComponent<SimpleComponent>();
  composition_.RemoveComponent<SimpleComponent>();
  // Act & Assert
  EXPECT_NO_THROW(composition_.RemoveComponent<SimpleComponent>())
    << "Removing already-removed component should not throw";
}

//! Add, remove, and re-add a component should succeed.
NOLINT_TEST_F(ErrorHandlingTest, AddRemoveReAddComponent)
{
  // Arrange
  composition_.AddComponent<SimpleComponent>();
  composition_.RemoveComponent<SimpleComponent>();
  // Act & Assert
  EXPECT_NO_THROW(composition_.AddComponent<SimpleComponent>())
    << "Should be able to re-add a component after removal";
}

//! Replace a component with a different type should succeed if not required by
//! others.
NOLINT_TEST_F(
  ErrorHandlingTest, ReplaceComponentWithDifferentTypeAllowedIfNoDependents)
{
  // Arrange
  composition_.AddComponent<SimpleComponent>();
  // Act & Assert
  // Use another non-pooled type for replacement
  EXPECT_NO_THROW(
    (composition_.ReplaceComponent<SimpleComponent, AnotherSimpleComponent>()))
    << "Replacing a component with a different type (both non-pooled) should "
       "succeed if not required by others";

  // But if we try replacing with DependentComponent, it should throw
  // since it requires SimpleComponent
  EXPECT_THROW(
    (composition_.ReplaceComponent<SimpleComponent, DependentComponent>()),
    oxygen::ComponentError)
    << "Replacing a component with another one that depends on it should throw";
}

//! Replace a component with a different type should throw if required by
//! others.
NOLINT_TEST_F(
  ErrorHandlingTest, ReplaceComponentWithDifferentTypeThrowsIfRequired)
{
  // Arrange
  composition_.AddComponent<SimpleComponent>();
  composition_.AddComponent<DependentComponent>();
  // Act & Assert
  // Use another non-pooled type for replacement
  EXPECT_THROW(
    (composition_.ReplaceComponent<SimpleComponent, DependentComponent>()),
    oxygen::ComponentError)
    << "Replacing a component with a different type (both non-pooled) should "
       "throw if required "
       "by others";
}

//! Adding a dependent with missing dependency should throw.
NOLINT_TEST_F(ErrorHandlingTest, AddDependentWithMissingDependencyThrows)
{
  // Act & Assert
  EXPECT_THROW(
    composition_.AddComponent<DependentComponent>(), oxygen::ComponentError)
    << "Adding dependent without dependency should throw";
}

//! Adding a duplicate component should throw.
NOLINT_TEST_F(ErrorHandlingTest, AddDuplicateComponentThrows)
{
  // Arrange
  composition_.AddComponent<SimpleComponent>();
  // Act & Assert
  EXPECT_THROW(
    composition_.AddComponent<SimpleComponent>(), oxygen::ComponentError)
    << "Adding duplicate component should throw";
}

//! Getting a non-existent component should throw.
NOLINT_TEST_F(ErrorHandlingTest, GetNonExistentComponentThrows)
{
  // Act & Assert
  EXPECT_THROW(
    [[maybe_unused]] auto& _ = composition_.GetComponent<SimpleComponent>(),
    oxygen::ComponentError)
    << "Getting non-existent component should throw";
}

} // namespace
