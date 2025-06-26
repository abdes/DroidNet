//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <atomic>
#include <optional>
#include <ranges>
#include <thread>

#include <Oxygen/Composition/Composition.h>
#include <Oxygen/Composition/Object.h>
#include <Oxygen/Testing/GTest.h>

#include "./BaseCompositionTest.h"

using oxygen::TypeId;
using oxygen::composition::testing::BaseCompositionTest;

namespace {

// TypeList for the test suite
class PooledLocalValueComponent;
using ResourceTypeList = oxygen::TypeList<PooledLocalValueComponent>;

class SimpleComponent final : public oxygen::Component {
  OXYGEN_COMPONENT(SimpleComponent)
};

class LocalValueComponent final : public oxygen::Component {
  OXYGEN_COMPONENT(LocalValueComponent)
public:
  explicit LocalValueComponent(const int value)
    : value_(value)
  {
  }
  [[nodiscard]] auto Value() const -> int { return value_; }

private:
  int value_;
};
using LocalComponent = LocalValueComponent;

class PooledLocalValueComponent final : public oxygen::Component {
  OXYGEN_POOLED_COMPONENT(PooledLocalValueComponent, ResourceTypeList)
public:
  explicit PooledLocalValueComponent(int v)
    : value_(v)
  {
  }
  [[nodiscard]] auto Value() const -> int { return value_; }

private:
  int value_;
};
using PooledComponent = PooledLocalValueComponent;

//=== TestComposition (file scope) ===----------------------------------------//

class TestComposition final : public oxygen::Composition {
public:
  using Base = Composition;

  // Expose protected methods for testing
  using Base::AddComponent;
  using Base::HasComponents;
  using Base::RemoveComponent;
  using Base::ReplaceComponent;

  auto LocalValue() const noexcept -> std::optional<int>
  {
    if (HasComponent<LocalValueComponent>()) {
      return GetComponent<LocalValueComponent>().Value();
    }
    return std::nullopt;
  }

  auto PooledValue() const noexcept -> std::optional<int>
  {
    if (HasComponent<PooledLocalValueComponent>()) {
      return GetComponent<PooledLocalValueComponent>().Value();
    }
    return std::nullopt;
  }
};

//=== BasicCompositionTest (test fixture) ===---------------------------------//

class BasicCompositionTest : public BaseCompositionTest {
public:
  TestComposition composition_;
  static constexpr int local_v_ = 5;
  static constexpr int pooled_v_ = 10;
};

//=== Test Cases: BasicCompositionTest ===------------------------------------//

//! Verify empty composition operations: Has, Get, Remove on missing component.
NOLINT_TEST_F(BasicCompositionTest, EmptyCompositionOperations)
{
  // Arrange & Act
  // Assert
  EXPECT_FALSE(composition_.HasComponents());
  EXPECT_FALSE(composition_.HasComponent<LocalComponent>());
  EXPECT_FALSE(composition_.HasComponent<PooledComponent>());
  NOLINT_EXPECT_THROW(
    [[maybe_unused]] auto _ = composition_.GetComponent<LocalComponent>(),
    oxygen::ComponentError);
  NOLINT_EXPECT_THROW(
    [[maybe_unused]] auto _ = composition_.GetComponent<PooledComponent>(),
    oxygen::ComponentError);
  EXPECT_NO_THROW(composition_.RemoveComponent<SimpleComponent>());
}

//! Add and verify a component is present and retrievable.
NOLINT_TEST_F(BasicCompositionTest, AddComponentInPlace)
{
  // Arrange
  // Act
  auto& local_c = composition_.AddComponent<LocalComponent>(local_v_);
  auto& pooled_c = composition_.AddComponent<PooledComponent>(pooled_v_);
  // Assert
  EXPECT_TRUE(composition_.HasComponent<LocalComponent>());
  EXPECT_EQ(&local_c, &composition_.GetComponent<LocalComponent>());
  EXPECT_EQ(local_c.Value(), local_v_);

  EXPECT_TRUE(composition_.HasComponent<PooledComponent>());
  EXPECT_EQ(&pooled_c, &composition_.GetComponent<PooledComponent>());
  EXPECT_EQ(pooled_c.Value(), pooled_v_);
}

//! Add and verify a pre-created component passed by value.
NOLINT_TEST_F(BasicCompositionTest, AddComponentByValue)
{
  // Arrange

  // Act
  auto& local_c
    = composition_.AddComponent<LocalComponent>(LocalComponent(local_v_));
  auto& pooled_c
    = composition_.AddComponent<PooledComponent>(PooledComponent(pooled_v_));
  // Assert
  EXPECT_TRUE(composition_.HasComponent<LocalComponent>());
  EXPECT_EQ(&local_c, &composition_.GetComponent<LocalComponent>());
  EXPECT_EQ(local_c.Value(), local_v_);

  EXPECT_TRUE(composition_.HasComponent<PooledComponent>());
  EXPECT_EQ(&pooled_c, &composition_.GetComponent<PooledComponent>());
  EXPECT_EQ(pooled_c.Value(), pooled_v_);
}

//! Add and verify a pre-created component passed by value.
NOLINT_TEST_F(BasicCompositionTest, AddComponentFromUniquePtr)
{
  // Arrange

  // Act
  auto& local_c = composition_.AddComponent<LocalComponent>(
    std::make_unique<LocalComponent>(local_v_));
  auto& pooled_c = composition_.AddComponent<PooledComponent>(
    std::make_unique<PooledComponent>(pooled_v_));
  // Assert
  EXPECT_TRUE(composition_.HasComponent<LocalComponent>());
  EXPECT_EQ(&local_c, &composition_.GetComponent<LocalComponent>());
  EXPECT_EQ(local_c.Value(), local_v_);

  EXPECT_TRUE(composition_.HasComponent<PooledComponent>());
  EXPECT_EQ(&pooled_c, &composition_.GetComponent<PooledComponent>());
  EXPECT_EQ(pooled_c.Value(), pooled_v_);
}

//! Remove a component and verify it is no longer present.
NOLINT_TEST_F(BasicCompositionTest, RemoveComponent)
{
  // Arrange
  composition_.AddComponent<SimpleComponent>();
  // Act
  composition_.RemoveComponent<SimpleComponent>();
  // Assert
  EXPECT_FALSE(composition_.HasComponent<SimpleComponent>());
  EXPECT_FALSE(composition_.HasComponents());
}

//! Copy constructor copies all components.
NOLINT_TEST_F(BasicCompositionTest, CopyConstructor)
{
  // Arrange
  composition_.AddComponent<LocalComponent>(local_v_);
  composition_.AddComponent<PooledComponent>(pooled_v_);
  // Act
  const TestComposition copy(composition_);
  // Assert
  EXPECT_TRUE(composition_.HasComponent<LocalComponent>());
  EXPECT_TRUE(composition_.HasComponent<PooledComponent>());
  EXPECT_TRUE(copy.HasComponent<LocalComponent>());
  EXPECT_EQ(copy.LocalValue(), local_v_);
  EXPECT_TRUE(copy.HasComponent<PooledComponent>());
  EXPECT_EQ(copy.PooledValue(), pooled_v_);
}

//! Move constructor moves all components.
NOLINT_TEST_F(BasicCompositionTest, MoveConstructor)
{
  // Arrange
  composition_.AddComponent<SimpleComponent>();
  // Act
  const TestComposition moved(std::move(composition_));
  // Assert
  EXPECT_TRUE(moved.HasComponent<SimpleComponent>());
}

//! Copy assignment operator copies all components.
NOLINT_TEST_F(BasicCompositionTest, CopyAssignmentOperator)
{
  // Arrange
  composition_.AddComponent<LocalComponent>(local_v_);
  composition_.AddComponent<PooledComponent>(pooled_v_);

  TestComposition copy;
  // Act
  copy = composition_;

  // Assert
  EXPECT_TRUE(copy.HasComponent<LocalComponent>());
  EXPECT_TRUE(copy.HasComponent<PooledComponent>());
  EXPECT_EQ(copy.LocalValue(), local_v_);
  EXPECT_EQ(copy.PooledValue(), pooled_v_);

  // Original remains unchanged
  EXPECT_TRUE(composition_.HasComponent<LocalComponent>());
  EXPECT_TRUE(composition_.HasComponent<PooledComponent>());
}

//! Move assignment operator moves all components and leaves the source empty.
NOLINT_TEST_F(BasicCompositionTest, MoveAssignmentOperator)
{
  // Arrange
  composition_.AddComponent<LocalComponent>(local_v_);
  composition_.AddComponent<PooledComponent>(pooled_v_);

  TestComposition moved;
  // Act
  moved = std::move(composition_);

  // Assert
  EXPECT_TRUE(moved.HasComponent<LocalComponent>());
  EXPECT_TRUE(moved.HasComponent<PooledComponent>());
  EXPECT_EQ(moved.LocalValue(), local_v_);
  EXPECT_EQ(moved.PooledValue(), pooled_v_);

  // Source should be empty after move
  EXPECT_FALSE(composition_.HasComponent<LocalComponent>());
  EXPECT_FALSE(composition_.HasComponent<PooledComponent>());
  EXPECT_FALSE(composition_.HasComponents());
}

//! Adding a duplicate component throws an error.
NOLINT_TEST_F(BasicCompositionTest, DuplicateComponentThrows)
{
  // Arrange
  composition_.AddComponent<SimpleComponent>();
  // Act & Assert
  NOLINT_EXPECT_THROW(
    composition_.AddComponent<SimpleComponent>(), oxygen::ComponentError);
}

//! Getting a non-existent component throws an error.
NOLINT_TEST_F(BasicCompositionTest, GetNonExistentComponent)
{
  // Arrange & Act & Assert
  NOLINT_EXPECT_THROW(
    [[maybe_unused]] auto& _ = composition_.GetComponent<SimpleComponent>(),
    oxygen::ComponentError);
}

//! Add multiple components and verify their presence.
NOLINT_TEST_F(BasicCompositionTest, MultipleComponents)
{
  // Arrange
  composition_.AddComponent<SimpleComponent>();
  // Act
  EXPECT_NO_THROW(composition_.AddComponent<LocalValueComponent>(42));
  // Assert
  EXPECT_TRUE(composition_.HasComponent<SimpleComponent>());
  EXPECT_TRUE(composition_.HasComponent<LocalValueComponent>());
}

//! Replace a local component with in-place construction.
NOLINT_TEST_F(BasicCompositionTest, ReplaceLocalComponentInPlace)
{
  composition_.AddComponent<LocalComponent>(local_v_);
  int new_value = 42;
  const auto& replaced
    = composition_.ReplaceComponent<LocalComponent>(new_value);
  EXPECT_TRUE(composition_.HasComponent<LocalComponent>());
  EXPECT_EQ(replaced.Value(), new_value);
}

//! Replace a local component by value.
NOLINT_TEST_F(BasicCompositionTest, ReplaceLocalComponentByValue)
{
  composition_.AddComponent<LocalComponent>(local_v_);
  LocalComponent new_comp(123);
  const auto& replaced
    = composition_.ReplaceComponent<LocalComponent>(new_comp);
  EXPECT_TRUE(composition_.HasComponent<LocalComponent>());
  EXPECT_EQ(replaced.Value(), 123);
}

//! Replace a local component with a unique_ptr.
NOLINT_TEST_F(BasicCompositionTest, ReplaceLocalComponentFromUniquePtr)
{
  composition_.AddComponent<LocalComponent>(local_v_);
  const auto& replaced = composition_.ReplaceComponent<LocalComponent>(
    std::make_unique<LocalComponent>(321));
  EXPECT_TRUE(composition_.HasComponent<LocalComponent>());
  EXPECT_EQ(replaced.Value(), 321);
}

} // namespace
