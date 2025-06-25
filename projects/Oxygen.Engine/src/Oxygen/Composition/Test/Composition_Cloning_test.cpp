//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Composition/Composition.h>
#include <Oxygen/Composition/Object.h>
#include <memory>
#include <string>

#include "./BaseCompositionTest.h"

using oxygen::TypeId;

namespace {

class CloneablePooledComponent;
using ResourceTypeList = oxygen::TypeList<CloneablePooledComponent>;

//=== CloningTest Components ===----------------------------------------------//

class NonCloneableComponent final : public oxygen::Component {
  OXYGEN_COMPONENT(NonCloneableComponent)
};

class CloneableComponent final : public oxygen::Component {
  OXYGEN_COMPONENT(CloneableComponent)
public:
  int value_ = 0;
  std::string name_;

  CloneableComponent() = default; // NOLINT(*-unused-member-function)
  CloneableComponent(const int v, std::string n)
    : value_(v)
    , name_(std::move(n))
  {
  }

  [[nodiscard]] auto IsCloneable() const noexcept -> bool override
  {
    return true;
  }
  [[nodiscard]] auto Clone() const -> std::unique_ptr<Component> override
  {
    return std::make_unique<CloneableComponent>(*this);
  }
};

class CloneablePooledComponent final : public oxygen::Component {
  OXYGEN_POOLED_COMPONENT(CloneablePooledComponent, ResourceTypeList)
public:
  double data_ = 0.0;
  std::string tag_;

  CloneablePooledComponent() = default; // NOLINT(*-unused-member-function)
  CloneablePooledComponent(const double d, std::string t)
    : data_(d)
    , tag_(std::move(t))
  {
  }

  [[nodiscard]] auto IsCloneable() const noexcept -> bool override
  {
    return true;
  }
  [[nodiscard]] auto Clone() const -> std::unique_ptr<Component> override
  {
    return std::make_unique<CloneablePooledComponent>(*this);
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

//=== Composition for CloningTest ===-----------------------------------------//

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

//=== CloningTest fixture ===-------------------------------------------------//

class CloningTest : public oxygen::composition::testing::BaseCompositionTest {

protected:
  class CloneableComposition final
    : public TestComposition,
      public oxygen::CloneableMixin<CloneableComposition> { };
  CloneableComposition composition_;
};

//=== Test Cases: CloningTest ===---------------------------------------------//

/*!
 Test that a composition with only cloneable components can be cloned,
 and the clone is independent of the original. Also checks that all data
 members are cloned correctly.
 @see oxygen::Composition, oxygen::CloneableMixin
*/
NOLINT_TEST_F(CloningTest, CloneableComponentsSupport)
{
  // Arrange
  composition_.AddComponent<CloneableComponent>(42, "alpha");
  composition_.AddComponent<CloneablePooledComponent>(3.14, "tag1");

  // Act
  const auto clone { composition_.Clone() };

  // Assert
  EXPECT_TRUE(clone->HasComponent<CloneableComponent>());
  EXPECT_TRUE(clone->HasComponent<CloneablePooledComponent>());

  // Check that the data is cloned
  const auto& c1_clone = clone->GetComponent<CloneableComponent>();
  const auto& p1_clone = clone->GetComponent<CloneablePooledComponent>();
  EXPECT_EQ(c1_clone.value_, 42);
  EXPECT_EQ(c1_clone.name_, "alpha");
  EXPECT_EQ(p1_clone.data_, 3.14);
  EXPECT_EQ(p1_clone.tag_, "tag1");

  // Mutate the clone and check independence
  clone->RemoveComponent<CloneableComponent>();
  EXPECT_FALSE(clone->HasComponent<CloneableComponent>());
  EXPECT_TRUE(composition_.HasComponent<CloneableComponent>());
}

/*!
 Test that a non-cloneable component prevents cloning the composition.
 @see oxygen::Composition, oxygen::CloneableMixin
*/
NOLINT_TEST_F(CloningTest, NonCloneableComponentPreventsCloning)
{
  // Arrange
  composition_.AddComponent<NonCloneableComponent>();
  composition_.AddComponent<CloneableComponent>(1, "x");

  // Act & Assert
  EXPECT_THROW(auto clone { composition_.Clone() }, oxygen::ComponentError);
}

/*!
 Test that cloned components have updated dependencies after cloning.
 @see oxygen::Composition, oxygen::CloneableMixin
*/
NOLINT_TEST_F(CloningTest, ClonedComponentsHaveUpdatedDependencies)
{
  // Arrange
  composition_.AddComponent<CloneableComponent>(7, "dep");
  EXPECT_NO_THROW(
    { composition_.AddComponent<CloneableDependentComponent>(); });

  // Act
  const auto clone { composition_.Clone() };

  // Assert
  EXPECT_TRUE(clone->HasComponent<CloneableComponent>());
  EXPECT_TRUE(clone->HasComponent<CloneableDependentComponent>());
  const auto& dependent = clone->GetComponent<CloneableDependentComponent>();
  EXPECT_NE(dependent.dependency_, nullptr);
  // Check that the dependency points to the clone's component, not the original
  const auto& dep = clone->GetComponent<CloneableComponent>();
  EXPECT_EQ(dependent.dependency_, &dep);
}

/*!
 Test that modifying the original after cloning does not affect the clone,
 and vice versa, for both local and pooled components.
*/
NOLINT_TEST_F(CloningTest, CloneIndependenceForAllComponentData)
{
  // Arrange
  auto& c1 = composition_.AddComponent<CloneableComponent>(100, "orig");
  auto& p1
    = composition_.AddComponent<CloneablePooledComponent>(2.71, "origTag");

  // Act
  const auto clone { composition_.Clone() };

  // Mutate original
  c1.value_ = 200;
  c1.name_ = "changed";
  p1.data_ = 1.23;
  p1.tag_ = "changedTag";

  // Assert clone is unchanged
  const auto& c1_clone = clone->GetComponent<CloneableComponent>();
  const auto& p1_clone = clone->GetComponent<CloneablePooledComponent>();
  EXPECT_EQ(c1_clone.value_, 100);
  EXPECT_EQ(c1_clone.name_, "orig");
  EXPECT_EQ(p1_clone.data_, 2.71);
  EXPECT_EQ(p1_clone.tag_, "origTag");

  // Mutate clone
  const_cast<CloneableComponent&>(c1_clone).value_ = 300;
  const_cast<CloneableComponent&>(c1_clone).name_ = "cloneEdit";
  const_cast<CloneablePooledComponent&>(p1_clone).data_ = 9.99;
  const_cast<CloneablePooledComponent&>(p1_clone).tag_ = "cloneTag";

  // Assert original is unchanged
  EXPECT_EQ(c1.value_, 200);
  EXPECT_EQ(c1.name_, "changed");
  EXPECT_EQ(p1.data_, 1.23);
  EXPECT_EQ(p1.tag_, "changedTag");
}

} // namespace
