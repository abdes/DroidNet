//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <chrono>
#include <coroutine>
#include <cstddef>
#include <memory>
#include <type_traits>

#include "Utils/OxCoTestFixture.h"

#include <Oxygen/OxCo/Algorithms.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Event.h>
#include <Oxygen/OxCo/ParkingLot.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/OxCo/Semaphore.h>
#include <Oxygen/Testing/GTest.h>

using namespace std::chrono_literals;
using oxygen::co::AnyOf;
using oxygen::co::Co;
using oxygen::co::ParkingLot;
using oxygen::co::testing::OxCoTestFixture;

// Parked waiters reference their lot, so the primitives must not move.
static_assert(!std::is_move_constructible_v<oxygen::co::Event>);
static_assert(!std::is_move_constructible_v<oxygen::co::Semaphore>);
static_assert(!std::is_move_constructible_v<ParkingLot>);

class MockCoroutine {
public:
  struct promise_type {
    auto get_return_object() -> MockCoroutine { return {}; }
    auto initial_suspend() -> std::suspend_never { return {}; }
    auto final_suspend() noexcept -> std::suspend_never { return {}; }
    void unhandled_exception() { }
    void return_void() { }
  };
};

NOLINT_TEST(ParkingLotTest, ParkAndUnParkOne)
{
  ParkingLot lot;
  constexpr size_t num_coroutines = 10;
  std::array<bool, num_coroutines> parked_flags { true };

  auto create_coroutine = [&](const size_t index) -> MockCoroutine {
    co_await lot.Park();
    parked_flags[index] = false;
  };

  std::array<MockCoroutine, num_coroutines> coroutines {};
  for (size_t i = 0; i < num_coroutines; ++i) {
    parked_flags[i] = true;
    coroutines[i] = create_coroutine(i);
  }

  EXPECT_FALSE(lot.Empty());

  for (size_t i = 0; i < num_coroutines; ++i) {
    lot.UnParkOne();
    EXPECT_EQ(parked_flags[i], false);
  }

  EXPECT_TRUE(lot.Empty());
}

NOLINT_TEST(ParkingLotTest, ParkAndUnParkAll)
{
  ParkingLot lot;
  constexpr size_t num_coroutines = 10;
  std::array<bool, num_coroutines> parked_flags { true };

  auto create_coroutine = [&](const size_t index) -> MockCoroutine {
    co_await lot.Park();
    parked_flags[index] = false;
  };

  std::array<MockCoroutine, num_coroutines> coroutines {};
  for (size_t i = 0; i < num_coroutines; ++i) {
    parked_flags[i] = true;
    coroutines[i] = create_coroutine(i);
  }

  EXPECT_FALSE(lot.Empty());

  lot.UnParkAll();

  EXPECT_TRUE(lot.Empty());
  for (size_t i = 0; i < num_coroutines; ++i) {
    EXPECT_FALSE(parked_flags[i]);
  }
}

NOLINT_TEST(ParkingLotTest, Empty)
{
  ParkingLot lot;
  bool parked { true };

  EXPECT_TRUE(lot.Empty());

  auto coro = [&lot, &parked] -> MockCoroutine {
    co_await lot.Park();
    parked = false;
  };
  coro();
  EXPECT_FALSE(lot.Empty());
  EXPECT_TRUE(parked);

  lot.UnParkOne();
  EXPECT_TRUE(lot.Empty());
  EXPECT_FALSE(parked);
}

//! Peek() returns nullptr on an empty lot, otherwise the waiter that
//! UnParkOne() would wake next.
NOLINT_TEST(ParkingLotTest, PeekReturnsOldestWaiter)
{
  // Arrange
  ParkingLot lot;
  std::array<bool, 2> parked { true, true };
  auto park = [&lot, &parked](const size_t index) -> MockCoroutine {
    co_await lot.Park();
    parked.at(index) = false;
  };
  EXPECT_EQ(lot.Peek(), nullptr);
  park(0);
  park(1);

  // Act
  const auto* oldest = lot.Peek();
  lot.UnParkOne();
  const auto* next = lot.Peek();

  // Assert
  ASSERT_NE(oldest, nullptr);
  ASSERT_NE(next, nullptr);
  EXPECT_NE(next, oldest);
  EXPECT_FALSE(parked[0]);
  EXPECT_TRUE(parked[1]);
  lot.UnParkOne();
  EXPECT_EQ(lot.Peek(), nullptr);
  EXPECT_FALSE(parked[1]);
}

class ParkingLotCancelTest : public OxCoTestFixture { };

//! A cancelled waiter leaves the lot, so destroying the lot afterwards is
//! not a leak.
NOLINT_TEST_F(ParkingLotCancelTest, DestroyAfterCancelledWaiter)
{
  // Arrange
  auto lot = std::make_unique<ParkingLot>();

  // Act
  oxygen::co::Run(*el_,
    [this, &lot] -> Co<> { co_await AnyOf(lot->Park(), el_->Sleep(1ms)); });

  // Assert
  EXPECT_TRUE(lot->Empty());
  lot.reset();
}

#ifndef NDEBUG
//! Destroying a lot while a task is still parked on it is a leak and must
//! assert instead of silently dropping the waiter.
NOLINT_TEST(ParkingLotDeathTest, DestroyWithParkedWaiterAsserts)
{
  EXPECT_DEATH_IF_SUPPORTED(([] -> void {
    auto lot = std::make_unique<ParkingLot>();
    [](ParkingLot& l) -> MockCoroutine { co_await l.Park(); }(*lot);
    lot.reset();
  }()),
    "Still some tasks suspended while destroying a parking lot");
}
#endif
