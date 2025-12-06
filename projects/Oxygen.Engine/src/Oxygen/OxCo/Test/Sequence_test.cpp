//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/OxCo/Detail/Sequence.h>

#include <chrono>

#include <Oxygen/Testing/GTest.h>
#include <gmock/gmock.h>

#include "Utils/OxCoTestFixture.h"
#include "Utils/TestEventLoop.h"
#include <Oxygen/Base/Unreachable.h>
#include <Oxygen/OxCo/Algorithms.h>
#include <Oxygen/OxCo/Awaitables.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Event.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/OxCo/Semaphore.h>

using namespace std::chrono_literals;
using oxygen::co::AllOf;
using oxygen::co::AnyOf;
using oxygen::co::Co;
using oxygen::co::Event;
using oxygen::co::Just;
using oxygen::co::kYield;
using oxygen::co::NoOp;
using oxygen::co::Run;
using oxygen::co::Semaphore;
using oxygen::co::Then;
using oxygen::co::testing::kNonCancellable;
using oxygen::co::testing::TestEventLoop;

// NOLINTBEGIN(*-avoid-do-while)
// NOLINTBEGIN(*-avoid-capturing-lambda-coroutines)

namespace {

// ReSharper disable CppMemberFunctionMayBeStatic
struct LValueQualifiedImm {
  [[nodiscard]] auto await_ready() const noexcept -> bool { return true; }
  [[noreturn]] auto await_suspend(oxygen::co::detail::Handle) noexcept -> void
  {
    oxygen::Unreachable();
  }
  auto await_resume() & -> int { return 42; }
};

struct RValueQualifiedImm {
  [[nodiscard]] auto await_ready() const noexcept -> bool { return true; }
  [[noreturn]] void await_suspend(oxygen::co::detail::Handle) noexcept
  {
    oxygen::Unreachable();
  }
  auto await_resume() && -> int { return 42; }
};
// ReSharper restore CppMemberFunctionMayBeStatic

struct LValueQualified {
  auto operator co_await() const& { return RValueQualifiedImm {}; }
};
struct RValueQualified {
  auto operator co_await() const&& { return RValueQualifiedImm {}; }
};

class SequenceTest : public oxygen::co::testing::OxCoTestFixture { };

NOLINT_TEST_F(SequenceTest, BasicOperation)
{
  ::Run(*el_, [&]() -> Co<> {
    co_await (el_->Sleep(2ms) | Then([&] { return el_->Sleep(3ms); }));
    EXPECT_EQ(el_->Now(), 5ms);
  });
}

NOLINT_TEST_F(SequenceTest, WithValueReturnType)
{
  ::Run(*el_, [&]() -> Co<> {
    auto r
      = co_await (Just(42) | Then([](const int v) { return Just(v + 1); }));
    EXPECT_EQ(r, 43);
  });
}

NOLINT_TEST_F(SequenceTest, WithReferenceReturnType)
{
  ::Run(*el_, [&]() -> Co<> {
    int arr[2] = { 1, 2 };
    auto r = co_await (
      Just<int&>(arr[0]) | Then([](int& v) { return Just(&v + 1); }));
    EXPECT_EQ(r, &arr[1]);
  });
}

NOLINT_TEST_F(SequenceTest, WithVoidReturnType)
{
  ::Run(
    *el_, [&]() -> Co<> { co_await (NoOp() | Then([] { return NoOp(); })); });
}

NOLINT_TEST_F(SequenceTest, WithTaskReturnType)
{
  ::Run(*el_, [&]() -> Co<> {
    auto r = co_await ([]() -> Co<int> { co_return 42; }
        | Then([](const int v) -> Co<int> { co_return v + 1; }));
    EXPECT_EQ(r, 43);
  });
}

NOLINT_TEST_F(SequenceTest, ExceptionThrowInFirstTask)
{
  ::Run(*el_, [&]() -> Co<> {
    try {
      co_await ([]() -> Co<> {
        co_await kYield;
        throw std::runtime_error("test");
      } | Then([] { return Just(42); }));
      ADD_FAILURE() << "expected exception";
    } catch (const std::runtime_error&) {
      // expected
    }
  });
}

NOLINT_TEST_F(SequenceTest, ExceptionThrowInThen)
{
  ::Run(*el_, [&]() -> Co<> {
    try {
      co_await (
        Just(42) | Then([](int) -> Co<> { throw std::runtime_error("test"); }));
      ADD_FAILURE() << "expected exception";
    } catch (const std::runtime_error&) {
      // expected
    }
  });
}

NOLINT_TEST_F(SequenceTest, ExceptionThrowAfterYield)
{
  ::Run(*el_, [&]() -> Co<> {
    try {
      co_await (Just(42) | Then([](int) -> Co<> {
        co_await kYield;
        throw std::runtime_error("test");
      }));
      ADD_FAILURE() << "expected exception";
    } catch (const std::runtime_error&) {
      // expected
    }
  });
}

NOLINT_TEST_F(SequenceTest, CancellationFirstTaskCancelled)
{
  ::Run(*el_, [&]() -> Co<> {
    auto [r, _] = co_await AnyOf(el_->Sleep(3ms) | Then([] {
      ADD_FAILURE() << "should not reach here";
      return NoOp();
    }),
      el_->Sleep(1ms));
    EXPECT_EQ(el_->Now(), 1ms);
    EXPECT_FALSE(r);
  });
}

NOLINT_TEST_F(SequenceTest, CancellationSecondTaskCancelled)
{
  ::Run(*el_, [&]() -> Co<> {
    auto [r, _] = co_await AnyOf(
      el_->Sleep(1ms) | Then([&] { return el_->Sleep(3ms); }) | Then([] {
        ADD_FAILURE() << "should not reach here";
        return NoOp();
      }),
      el_->Sleep(2ms));
    EXPECT_EQ(el_->Now(), 2ms);
    EXPECT_FALSE(r);
  });
}

NOLINT_TEST_F(SequenceTest, CancellationWithEventTrigger)
{
  ::Run(*el_, [&]() -> Co<> {
    Event evt;
    co_await AnyOf(evt, el_->Sleep(1ms) | Then([&] {
      evt.Trigger();
      return el_->Sleep(3ms);
    }));
  });
}

NOLINT_TEST_F(SequenceTest, CancellationFirstTaskNonCancellable)
{
  ::Run(*el_, [&]() -> Co<> {
    auto [r, _] = co_await AnyOf(
      el_->Sleep(2ms, kNonCancellable) | Then([] { return Just(42); }),
      el_->Sleep(1ms));
    EXPECT_EQ(el_->Now(), 2ms);
    EXPECT_EQ(*r, 42);
  });
}

NOLINT_TEST_F(SequenceTest, CancellationSecondTaskNonCancellable)
{
  ::Run(*el_, [&]() -> Co<> {
    auto [r, _] = co_await AnyOf(
      el_->Sleep(1ms) | Then([&] { return el_->Sleep(3ms, kNonCancellable); }),
      el_->Sleep(2ms));
    EXPECT_EQ(el_->Now(), 4ms);
    EXPECT_TRUE(r);
  });
}

NOLINT_TEST_F(SequenceTest, CancellationSecondTaskEarlyCancelable)
{
  ::Run(*el_, [&]() -> Co<> {
    bool started = false;
    auto [r, _] = co_await AnyOf(el_->Sleep(2ms, kNonCancellable) | Then([&] {
      started = true;
      return el_->Sleep(2ms);
    }),
      el_->Sleep(1ms));
    EXPECT_EQ(el_->Now(), 2ms);
    EXPECT_TRUE(started);
    EXPECT_FALSE(r);
  });
}

NOLINT_TEST_F(SequenceTest, Lifetime)
{
  ::Run(*el_, [&]() -> Co<> {
    Semaphore sem { 1 };
    co_await AllOf(
      sem.Lock() | Then([&] { return el_->Sleep(5ms); }), [&]() -> Co<> {
        co_await el_->Sleep(1ms);
        auto lk = co_await sem.Lock();
        EXPECT_EQ(el_->Now(), 5ms);
      });
  });
}

NOLINT_TEST_F(SequenceTest, ChainingThenThen)
{
  ::Run(*el_, [&]() -> Co<> {
    auto r = co_await (Just(42) | Then([](const int v) { return Just(v + 1); })
      | Then([](const int v) { return Just(v + 1); }));
    EXPECT_EQ(r, 44);
  });
}

NOLINT_TEST_F(SequenceTest, ChainingGroupedThenThen)
{
  ::Run(*el_, [&]() -> Co<> {
    auto r = co_await (Just(42) | (Then([](const int v) {
      return Just(v + 1);
    }) | Then([](const int v) { return Just(v + 1); })));
    EXPECT_EQ(r, 44);
  });
}

NOLINT_TEST_F(SequenceTest, OperationWithDifferentQualifications)
{
  ::Run(*el_, [&]() -> Co<> {
    LValueQualifiedImm lvqi;
    LValueQualified lvq;
    int r = co_await (NoOp() | Then([&]() -> auto& { return lvqi; }));
    EXPECT_EQ(r, 42);
    r = co_await (NoOp() | Then([] { return RValueQualifiedImm {}; }));
    EXPECT_EQ(r, 42);
    r = co_await (NoOp() | Then([&]() -> auto& { return lvq; }));
    EXPECT_EQ(r, 42);
    r = co_await (NoOp() | Then([] { return RValueQualified {}; }));
    EXPECT_EQ(r, 42);
  });
}

NOLINT_TEST_F(SequenceTest, WithCustomAwaitable)
{
  ::Run(*el_, [&]() -> Co<> {
    struct CustomAwaitable {
      static auto await_ready() noexcept -> bool { return true; }
      static auto await_suspend(std::coroutine_handle<>) noexcept -> void { }
      static auto await_resume() noexcept -> int { return 99; }
    };

    auto r = co_await (
      CustomAwaitable {} | Then([](const int v) { return Just(v + 1); }));
    EXPECT_EQ(r, 100);
  });
}

NOLINT_TEST_F(SequenceTest, WithEmptySequence)
{
  ::Run(
    *el_, [&]() -> Co<> { co_await (NoOp() | Then([] { return NoOp(); })); });
}

} // namespace

// NOLINTEND(*-avoid-capturing-lambda-coroutines)
// NOLINTEND(*-avoid-do-while)
