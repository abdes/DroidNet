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

//=== ResourceTypeList for the test suite ===---------------------------------//

// Forward declare all resource types used in the test suite
class SimplePooledComponent;
class DependentPooledComponent;
class ComplexPooledComponent;
class PooledDep;
class PooledDependent;
class PooledA;
struct PooledTracker;

using PooledTestResourceTypeList
  = oxygen::TypeList<SimplePooledComponent, DependentPooledComponent,
    ComplexPooledComponent, PooledDep, PooledDependent, PooledA>;

//=== Implementation of all pooled test types ===-----------------------------//

struct PooledTracker {
  static inline std::vector<std::string> destroyed;
};
class SimplePooledComponent final : public oxygen::Component {
  OXYGEN_POOLED_COMPONENT(SimplePooledComponent, PooledTestResourceTypeList);

public:
  using ResourceTypeList = PooledTestResourceTypeList;
  SimplePooledComponent() = default;
};
class DependentPooledComponent final : public oxygen::Component {
  OXYGEN_POOLED_COMPONENT(DependentPooledComponent, PooledTestResourceTypeList);
  OXYGEN_COMPONENT_REQUIRES(SimplePooledComponent);

public:
  using ResourceTypeList = PooledTestResourceTypeList;
  DependentPooledComponent() = default;
  auto UpdateDependencies(
    const std::function<Component&(TypeId)>& get_component) noexcept
    -> void override
  {
    simple_ = &static_cast<SimplePooledComponent&>(
      get_component(SimplePooledComponent::ClassTypeId()));
  }
  SimplePooledComponent* simple_ { nullptr };
};
class ComplexPooledComponent final : public oxygen::Component {
  OXYGEN_POOLED_COMPONENT(ComplexPooledComponent, PooledTestResourceTypeList);
  OXYGEN_COMPONENT_REQUIRES(SimplePooledComponent, DependentPooledComponent);

public:
  using ResourceTypeList = PooledTestResourceTypeList;
  ComplexPooledComponent() = default;
  auto UpdateDependencies(
    const std::function<Component&(TypeId)>& get_component) noexcept
    -> void override
  {
    simple_ = &static_cast<SimplePooledComponent&>(
      get_component(SimplePooledComponent::ClassTypeId()));
    dependent_ = &static_cast<DependentPooledComponent&>(
      get_component(DependentPooledComponent::ClassTypeId()));
  }
  SimplePooledComponent* simple_ { nullptr };
  DependentPooledComponent* dependent_ { nullptr };
};
class PooledDep final : public oxygen::Component {
  OXYGEN_POOLED_COMPONENT(PooledDep, PooledTestResourceTypeList);

public:
  using ResourceTypeList = PooledTestResourceTypeList;
  ~PooledDep() override { PooledTracker::destroyed.push_back("Dep"); }
};
class PooledDependent final : public oxygen::Component {
  OXYGEN_POOLED_COMPONENT(PooledDependent, PooledTestResourceTypeList);
  OXYGEN_COMPONENT_REQUIRES(PooledDep);

public:
  using ResourceTypeList = PooledTestResourceTypeList;
  ~PooledDependent() override
  {
    PooledTracker::destroyed.push_back("Dependent");
  }
};
class PooledA final : public oxygen::Component {
  OXYGEN_POOLED_COMPONENT(PooledA, PooledTestResourceTypeList);
  OXYGEN_COMPONENT_REQUIRES(PooledA);

public:
  using ResourceTypeList = PooledTestResourceTypeList;
};

//=== DependencyIntegrityTest ===---------------------------------------------//

class DependencyIntegrityTest
  : public oxygen::composition::testing::BaseCompositionTest {
protected:
  class SimpleComponent final : public oxygen::Component {
    OXYGEN_COMPONENT(SimpleComponent)
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
  class ComplexComponent final : public oxygen::Component {
    OXYGEN_COMPONENT(ComplexComponent)
    OXYGEN_COMPONENT_REQUIRES(SimpleComponent, DependentComponent)
  public:
    auto UpdateDependencies(
      const std::function<Component&(TypeId)>& get_component) noexcept
      -> void override
    {
      simple_ = &static_cast<SimpleComponent&>(
        get_component(SimpleComponent::ClassTypeId()));
      dependent_ = &static_cast<DependentComponent&>(
        get_component(DependentComponent::ClassTypeId()));
    }
    SimpleComponent* simple_ { nullptr };
    DependentComponent* dependent_ { nullptr };
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

//=== Destruction order and circular dependency ===---------------------------//

struct Tracker {
  static inline std::vector<std::string> destroyed;
};
class Dep final : public oxygen::Component {
  OXYGEN_COMPONENT(Dep)
public:
  ~Dep() override { Tracker::destroyed.push_back("Dep"); }
};
class Dependent final : public oxygen::Component {
  OXYGEN_COMPONENT(Dependent)
  OXYGEN_COMPONENT_REQUIRES(Dep)
public:
  ~Dependent() override { Tracker::destroyed.push_back("Dependent"); }
};
class LocalComp : public oxygen::Composition {
public:
  using Base = Composition;
  template <typename T, typename... Args>
  auto AddComponent(Args&&... args) -> T&
  {
    return Base::AddComponent<T>(std::forward<Args>(args)...);
  }
};
class A final : public oxygen::Component {
  OXYGEN_COMPONENT(A)
  OXYGEN_COMPONENT_REQUIRES(A)
};
class TestComp : public oxygen::Composition {
public:
  using Base = Composition;
  template <typename T, typename... Args>
  auto AddComponent(Args&&... args) -> T&
  {
    return Base::AddComponent<T>(std::forward<Args>(args)...);
  }
};

//=== Test Cases: DependencyIntegrityTest ===---------------------------------//

//! Test that adding a dependent component requires its dependency.
NOLINT_TEST_F(DependencyIntegrityTest, DependencyValidation)
{
  // Arrange
  composition_.AddComponent<SimpleComponent>();
  // Act & Assert
  EXPECT_NO_THROW(composition_.AddComponent<DependentComponent>())
    << "Should allow adding DependentComponent when dependency present";
  const auto& dependent = composition_.GetComponent<DependentComponent>();
  EXPECT_NE(dependent.simple_, nullptr)
    << "DependentComponent should have valid dependency pointer";
}

//! Test that missing dependency throws on add.
NOLINT_TEST_F(DependencyIntegrityTest, MissingDependencyThrows)
{
  // Arrange & Act & Assert
  EXPECT_THROW(
    composition_.AddComponent<DependentComponent>(), oxygen::ComponentError);
}

//! Test that removing a required dependency throws if dependents exist.
NOLINT_TEST_F(DependencyIntegrityTest, RemoveRequiredComponentThrows)
{
  // Arrange
  composition_.AddComponent<SimpleComponent>();
  composition_.AddComponent<DependentComponent>();
  // Act & Assert
  EXPECT_THROW(
    composition_.RemoveComponent<SimpleComponent>(), oxygen::ComponentError);
}

//! Test complex dependency chains.
NOLINT_TEST_F(DependencyIntegrityTest, ComplexDependencyChains)
{
  // Arrange
  composition_.AddComponent<SimpleComponent>();
  composition_.AddComponent<DependentComponent>();
  // Act & Assert
  EXPECT_NO_THROW(composition_.AddComponent<ComplexComponent>())
    << "Should allow adding ComplexComponent when all dependencies present";
  const auto& complex = composition_.GetComponent<ComplexComponent>();
  EXPECT_NE(complex.simple_, nullptr)
    << "ComplexComponent should have valid SimpleComponent pointer";
  EXPECT_NE(complex.dependent_, nullptr)
    << "ComplexComponent should have valid DependentComponent pointer";
}

//! Test that removing dependent then dependency is allowed.
NOLINT_TEST_F(DependencyIntegrityTest, RemoveDependentThenDependency)
{
  // Arrange
  composition_.AddComponent<SimpleComponent>();
  composition_.AddComponent<DependentComponent>();
  // Act
  composition_.RemoveComponent<DependentComponent>();
  // Assert
  EXPECT_NO_THROW(composition_.RemoveComponent<SimpleComponent>())
    << "Should allow removing dependency after dependents are gone";
}

//! Test destruction order: dependents destroyed before dependencies.
NOLINT_TEST_F(
  DependencyIntegrityTest, DestructionOrderDependentsBeforeDependencies)
{
  // Arrange
  Tracker::destroyed.clear();
  {
    LocalComp comp;
    comp.AddComponent<Dep>();
    comp.AddComponent<Dependent>();
  }
  // Assert
  ASSERT_EQ(Tracker::destroyed.size(), 2);
  EXPECT_EQ(Tracker::destroyed[0], "Dependent")
    << "Dependent should be destroyed before dependency";
  EXPECT_EQ(Tracker::destroyed[1], "Dep")
    << "Dependency should be destroyed after dependents";
}

//! Test that circular dependency throws.
NOLINT_TEST_F(DependencyIntegrityTest, CircularDependencyThrows)
{
  // Arrange
  TestComp comp;
  // Act & Assert
  EXPECT_THROW(comp.AddComponent<A>(), oxygen::ComponentError);
}

//=== PooledDependencyIntegrityTest ===---------------------------------------//

// Pooled composition helpers
class PooledLocalComp : public oxygen::Composition {
public:
  using Base = Composition;
  using Base::AddComponent;
};
class PooledTestComp : public oxygen::Composition {
public:
  using Base = Composition;
  using Base::AddComponent;
  using Base::RemoveComponent;
};

//=== PooledDependencyIntegrityTest Fixture ===-------------------------------//

class PooledDependencyIntegrityTest
  : public oxygen::composition::testing::BaseCompositionTest {
protected:
  PooledTestComp composition_;
};

//=== Test Cases: PooledDependencyIntegrityTest ===---------------------------//

//! Test that adding a pooled dependent component requires its dependency.
NOLINT_TEST_F(PooledDependencyIntegrityTest, PooledDependencyValidation)
{
  // Arrange
  composition_.AddComponent<SimplePooledComponent>();
  // Act & Assert
  [[maybe_unused]] auto& _
    = composition_.AddComponent<DependentPooledComponent>();
  const auto& dependent = composition_.GetComponent<DependentPooledComponent>();
  EXPECT_NE(dependent.simple_, nullptr)
    << "DependentPooledComponent should have valid dependency pointer";
}

//! Test that missing pooled dependency throws on add.
NOLINT_TEST_F(PooledDependencyIntegrityTest, PooledMissingDependencyThrows)
{
  // Arrange & Act & Assert
  EXPECT_THROW(composition_.AddComponent<DependentPooledComponent>(),
    oxygen::ComponentError);
}

//! Test that removing a required pooled dependency throws if dependents exist.
NOLINT_TEST_F(
  PooledDependencyIntegrityTest, PooledRemoveRequiredComponentThrows)
{
  // Arrange
  composition_.AddComponent<SimplePooledComponent>();
  composition_.AddComponent<DependentPooledComponent>();
  // Act & Assert
  EXPECT_THROW(composition_.RemoveComponent<SimplePooledComponent>(),
    oxygen::ComponentError);
}

//! Test complex dependency chains with pooled components.
NOLINT_TEST_F(PooledDependencyIntegrityTest, PooledComplexDependencyChains)
{
  // Arrange
  composition_.AddComponent<SimplePooledComponent>();
  composition_.AddComponent<DependentPooledComponent>();
  // Act & Assert
  EXPECT_NO_THROW(composition_.AddComponent<ComplexPooledComponent>())
    << "Should allow adding ComplexPooledComponent when all dependencies "
       "present";
  const auto& complex = composition_.GetComponent<ComplexPooledComponent>();
  EXPECT_NE(complex.simple_, nullptr)
    << "ComplexPooledComponent should have valid SimplePooledComponent pointer";
  EXPECT_NE(complex.dependent_, nullptr)
    << "ComplexPooledComponent should have valid DependentPooledComponent "
       "pointer";
}

//! Test that dependents are destroyed before dependencies for pooled
//! components.
NOLINT_TEST_F(PooledDependencyIntegrityTest,
  PooledDestructionOrderDependentsBeforeDependencies)
{
  // Arrange
  PooledTracker::destroyed.clear();
  {
    PooledLocalComp comp;
    comp.AddComponent<PooledDep>();
    comp.AddComponent<PooledDependent>();
  }
  // Assert
  ASSERT_EQ(PooledTracker::destroyed.size(), 2);
  EXPECT_EQ(PooledTracker::destroyed[0], "Dependent")
    << "Dependent should be destroyed before dependency (pooled)";
  EXPECT_EQ(PooledTracker::destroyed[1], "Dep")
    << "Dependency should be destroyed after dependents (pooled)";
}

//! Test that circular dependency in pooled components throws.
NOLINT_TEST_F(PooledDependencyIntegrityTest, PooledCircularDependencyThrows)
{
  // Arrange
  PooledTestComp comp;
  // Act & Assert
  EXPECT_THROW(comp.AddComponent<PooledA>(), oxygen::ComponentError);
}

} // namespace
