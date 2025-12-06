//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/OxCo/Value.h>

#include <chrono>

#include <Oxygen/Testing/GTest.h>
#include <gmock/gmock.h>

#include "Utils/OxCoTestFixture.h"
#include "Utils/TestEventLoop.h"
#include <Oxygen/OxCo/Algorithms.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Run.h>

using namespace std::chrono_literals;
using oxygen::co::AllOf;
using oxygen::co::Co;
using oxygen::co::Run;
using oxygen::co::Value;

// NOLINTBEGIN(*-avoid-do-while)
// NOLINTBEGIN(*-avoid-capturing-lambda-coroutines)

namespace {

class ValueTest : public oxygen::co::testing::OxCoTestFixture { };

NOLINT_TEST_F(ValueTest, WakesTasksWhenValueChanges)
{
  ::Run(*el_, [&]() -> Co<> {
    Value v { 0 };

    co_await AllOf(
      [&]() -> Co<> {
        co_await Until(v <= 0);
        EXPECT_EQ(el_->Now(), 0ms);

        auto [from, to] = co_await v.UntilChanged();
        EXPECT_EQ(el_->Now(), 1ms);
        EXPECT_EQ(from, 0);
        EXPECT_EQ(to, 3);

        std::tie(from, to) = co_await v.UntilChanged();
        // Should skip the 3->3 change
        EXPECT_EQ(el_->Now(), 3ms);
        EXPECT_EQ(from, 3);
        EXPECT_EQ(to, 4);

        to = co_await v.UntilEquals(5);
        EXPECT_EQ(el_->Now(), 4ms);
        EXPECT_EQ(to, 5);
        EXPECT_EQ(v, 7);
      },
      [&]() -> Co<> {
        co_await el_->Sleep(1ms);
        v = 3;

        co_await el_->Sleep(1ms);
        v = 3;
        co_await el_->Sleep(1ms);
        ++v;

        co_await el_->Sleep(1ms);
        v = 7;
        v -= 2;
        v += 2;
      });

    EXPECT_EQ(el_->Now(), 4ms);
  });
}

NOLINT_TEST_F(ValueTest, UpdatesWhenSetCalled)
{
  ::Run(*el_, [&]() -> Co<> {
    Value v { 0 };

    co_await AllOf(
      [&]() -> Co<> {
        co_await v.UntilEquals(1);
        EXPECT_EQ(v.Get(), 1);
      },
      [&]() -> Co<> {
        co_await el_->Sleep(1ms);
        v.Set(1);
      });

    EXPECT_EQ(el_->Now(), 1ms);
  });
}

NOLINT_TEST_F(ValueTest, UpdatesWhenAssigned)
{
  ::Run(*el_, [&]() -> Co<> {
    Value v { 0 };

    co_await AllOf(
      [&]() -> Co<> {
        co_await v.UntilEquals(2);
        EXPECT_EQ(v.Get(), 2);
      },
      [&]() -> Co<> {
        co_await el_->Sleep(1ms);
        v = 2;
      });

    EXPECT_EQ(el_->Now(), 1ms);
  });
}

NOLINT_TEST_F(ValueTest, UntilMatchesSuspendsUntilPredicate)
{
  ::Run(*el_, [&]() -> Co<> {
    Value v { 0 };

    co_await AllOf(
      [&]() -> Co<> {
        co_await v.UntilMatches([](const int& value) { return value > 2; });
        EXPECT_EQ(v.Get(), 3);
      },
      [&]() -> Co<> {
        co_await el_->Sleep(1ms);
        v = 1;
        co_await el_->Sleep(1ms);
        v = 2;
        co_await el_->Sleep(1ms);
        v = 3;
      });

    EXPECT_EQ(el_->Now(), 3ms);
  });
}

NOLINT_TEST_F(ValueTest, UntilChangedWithCustomPredicate)
{
  ::Run(*el_, [&]() -> Co<> {
    Value v { 0 };

    co_await AllOf(
      [&]() -> Co<> {
        auto [from, to] = co_await v.UntilChanged(
          [](const int& f, const int& t) { return f < t; });
        EXPECT_EQ(from, 0);
        EXPECT_EQ(to, 2);
      },
      [&]() -> Co<> {
        co_await el_->Sleep(1ms);
        v = 2;
      });

    EXPECT_EQ(el_->Now(), 1ms);
  });
}

NOLINT_TEST_F(ValueTest, UntilChangedWithSpecificFromTo)
{
  ::Run(*el_, [&]() -> Co<> {
    Value v { 0 };

    co_await AllOf(
      [&]() -> Co<> {
        auto [from, to] = co_await v.UntilChanged(0, 3);
        EXPECT_EQ(from, 0);
        EXPECT_EQ(to, 3);
      },
      [&]() -> Co<> {
        co_await el_->Sleep(1ms);
        v = 3;
      });

    EXPECT_EQ(el_->Now(), 1ms);
  });
}

NOLINT_TEST_F(ValueTest, GetMethodReturnsCurrentValue)
{
  Value v { 5 };
  EXPECT_EQ(v.Get(), 5);
}

NOLINT_TEST_F(ValueTest, ConversionOperatorReturnsCurrentValue)
{
  const Value v { 7 };
  const int& value = v;
  EXPECT_EQ(value, 7);
}

} // namespace

// NOLINTEND(*-avoid-capturing-lambda-coroutines)
// NOLINTEND(*-avoid-do-while)
