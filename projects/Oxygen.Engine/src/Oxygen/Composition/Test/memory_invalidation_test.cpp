//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Composition/ComponentMacros.h>
#include <Oxygen/Composition/Composition.h>
#include <Oxygen/Composition/Object.h>

using oxygen::TypeId;

namespace {

//=== Test Components ===-----------------------------------------------------//

class BaseComponent final : public oxygen::Component {
  OXYGEN_COMPONENT(BaseComponent)
public:
  explicit BaseComponent(int value)
    : value_(value)
  {
  }
  [[nodiscard]] auto Value() const -> int { return value_; }

private:
  int value_;
};

class DependentComponent final : public oxygen::Component {
  OXYGEN_TYPED(DependentComponent)
  OXYGEN_COMPONENT_REQUIRES(BaseComponent)
public:
  void UpdateDependencies(
    const std::function<Component&(TypeId)>& get_component) override
  {
    base_ptr_ = &static_cast<BaseComponent&>(
      get_component(BaseComponent::ClassTypeId()));
  }

  [[nodiscard]] auto GetBaseValue() const -> int { return base_ptr_->Value(); }

  [[nodiscard]] auto GetBasePtr() const -> const BaseComponent*
  {
    return base_ptr_;
  }

private:
  BaseComponent* base_ptr_ { nullptr };
};

class DummyComponent1 final : public oxygen::Component {
  OXYGEN_COMPONENT(DummyComponent1)
};

class DummyComponent2 final : public oxygen::Component {
  OXYGEN_COMPONENT(DummyComponent2)
};

class DummyComponent3 final : public oxygen::Component {
  OXYGEN_COMPONENT(DummyComponent3)
};

class TestComposition : public oxygen::Composition {
public:
  explicit TestComposition(std::size_t capacity)
    : oxygen::Composition(capacity)
  {
  }

  template <typename T, typename... Args>
  auto AddComponent(Args&&... args) -> T&
  {
    return oxygen::Composition::AddComponent<T>(std::forward<Args>(args)...);
  }
};

//=== Test Case ===-----------------------------------------------------------//

/*!
 * Verifies that component dependency pointers remain valid after vector
 * reallocation when components grow beyond initial capacity.
 */
NOLINT_TEST(DependencyPointerTest, PointersValidAfterVectorReallocation)
{
  // Create composition with small initial capacity to force reallocation
  TestComposition composition(2);

  // Add base component
  auto& base = composition.AddComponent<BaseComponent>(42);

  // Add dependent component - stores pointer to base component
  auto& dependent = composition.AddComponent<DependentComponent>();
  // Verify initial state
  EXPECT_EQ(dependent.GetBaseValue(), 42);
  EXPECT_EQ(dependent.GetBasePtr(), &base);

  // Force vector reallocation by exceeding initial capacity
  composition.AddComponent<DummyComponent1>();
  composition.AddComponent<DummyComponent2>();
  composition.AddComponent<DummyComponent3>();

  // Verify dependency pointer is still valid after potential reallocation
  EXPECT_EQ(dependent.GetBaseValue(), 42)
    << "Dependent component should still access correct value";
  EXPECT_EQ(dependent.GetBasePtr(), &base)
    << "Stored pointer should still point to the same base component";
}

} // anonymous namespace
