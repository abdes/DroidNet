//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Composition/Composition.h>
#include <Oxygen/Composition/Object.h>
#include <memory>

#include "./BaseCompositionTest.h"

using oxygen::TypeId;

namespace {
//=== CloningTest Components (file scope for test compatibility)
//===----------------------------------------------------------//
class NonCloneableComponent final : public oxygen::Component {
  OXYGEN_TYPED(NonCloneableComponent)
};
class CloneableComponent final : public oxygen::Component {
  OXYGEN_TYPED(CloneableComponent)
public:
  [[nodiscard]] auto IsCloneable() const noexcept -> bool override
  {
    return true;
  }
  [[nodiscard]] auto Clone() const -> std::unique_ptr<Component> override
  {
    return std::make_unique<CloneableComponent>(*this);
  }
};
class CloneableDependentComponent final : public oxygen::Component {
  OXYGEN_TYPED(CloneableDependentComponent)
  OXYGEN_COMPONENT_REQUIRES(CloneableComponent)
public:
  [[nodiscard]] auto IsCloneable() const noexcept -> bool override
  {
    return true;
  }
  [[nodiscard]] auto Clone() const -> std::unique_ptr<Component> override
  {
    return std::make_unique<CloneableDependentComponent>(*this);
  }
  auto UpdateDependencies(
    const std::function<Component&(TypeId)>& get_component) noexcept
    -> void override
  {
    dependency_ = &static_cast<CloneableComponent&>(
      get_component(CloneableComponent::ClassTypeId()));
  }
  CloneableComponent* dependency_ { nullptr };
};
//=== Local TestComposition for CloningTest
//===----------------------------------------------------------//
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
};
//=== CloningTest
//===----------------------------------------------------------//
class CloningTest : public oxygen::composition::testing::BaseCompositionTest {

protected:
  class CloneableComposition final
    : public TestComposition,
      public oxygen::CloneableMixin<CloneableComposition> { };
  CloneableComposition composition_;
};
//=== Test Cases: CloningTest
//===----------------------------------------------------------//

/*!\
 Test that a composition with only cloneable components can be cloned,
 and the clone is independent of the original.
 @see oxygen::Composition, oxygen::CloneableMixin
*/
NOLINT_TEST_F(CloningTest, CloneableComponentsSupport)
{
  // Arrange
  composition_.AddComponent<CloneableComponent>();

  // Act
  const auto clone { composition_.Clone() };

  // Assert
  EXPECT_TRUE(clone->HasComponent<CloneableComponent>())
    << "Clone should have CloneableComponent";
  clone->RemoveComponent<CloneableComponent>();
  EXPECT_FALSE(clone->HasComponent<CloneableComponent>())
    << "Clone should not have CloneableComponent after removal";
  EXPECT_TRUE(composition_.HasComponent<CloneableComponent>())
    << "Original should still have CloneableComponent";
}

/*!\
 Test that a non-cloneable component prevents cloning the composition.
 @see oxygen::Composition, oxygen::CloneableMixin
*/
NOLINT_TEST_F(CloningTest, NonCloneableComponentPreventsCloning)
{
  // Arrange
  composition_.AddComponent<NonCloneableComponent>();
  composition_.AddComponent<CloneableComponent>();

  // Act & Assert
  EXPECT_THROW(auto clone { composition_.Clone() }, oxygen::ComponentError);
}

/*!\
 Test that cloned components have updated dependencies after cloning.
 @see oxygen::Composition, oxygen::CloneableMixin
*/
NOLINT_TEST_F(CloningTest, ClonedComponentsHaveUpdatedDependencies)
{
  // Arrange
  composition_.AddComponent<CloneableComponent>();
  EXPECT_NO_THROW(
    { composition_.AddComponent<CloneableDependentComponent>(); });

  // Act
  const auto clone { composition_.Clone() };

  // Assert
  EXPECT_TRUE(clone->HasComponent<CloneableComponent>())
    << "Clone should have CloneableComponent";
  EXPECT_TRUE(clone->HasComponent<CloneableDependentComponent>())
    << "Clone should have CloneableDependentComponent";
  const auto& dependent = clone->GetComponent<CloneableDependentComponent>();
  EXPECT_NE(dependent.dependency_, nullptr)
    << "Cloned dependent should have updated dependency pointer";
}
// TODO: Add pooled/hybrid cloning tests
} // namespace
