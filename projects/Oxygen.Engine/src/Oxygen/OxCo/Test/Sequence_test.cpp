//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/OxCo/Detail/Sequence.h>

#include <chrono>

#include <catch2/catch_test_macros.hpp>

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

TEST_CASE("Sequence")
{
  TestEventLoop el;
  Run(el, [&]() -> Co<> {
    SECTION("basic operation")
    {
      co_await (el.Sleep(2ms) | Then([&] { return el.Sleep(3ms); }));
      CHECK(el.Now() == 5ms);
    }

    SECTION("with value return type")
    {
      auto r
        = co_await (Just(42) | Then([](const int v) { return Just(v + 1); }));
      CHECK(r == 43);
    }

    SECTION("with reference return type")
    {
      int arr[2] = { 1, 2 };
      auto r = co_await (
        Just<int&>(arr[0]) | Then([](int& v) { return Just(&v + 1); }));
      CHECK(r == &arr[1]);
    }

    SECTION("with void return type")
    {
      co_await (NoOp() | Then([] { return NoOp(); }));
    }

    SECTION("with task return type")
    {
      auto r = co_await ([]() -> Co<int> { co_return 42; }
          | Then([](const int v) -> Co<int> { co_return v + 1; }));
      CHECK(r == 43);
    }

    SECTION("with exception (throw in first task)")
    {
      CHECK_THROWS(co_await ([]() -> Co<> {
        co_await kYield;
        throw std::runtime_error("test");
      } | Then([] { return Just(42); })));
    }

    SECTION("with exception (throw in then)")
    {
      CHECK_THROWS(co_await (Just(42)
        | Then([](int) -> Co<> { throw std::runtime_error("test"); })));
    }

    SECTION("with exception (throw after yield)")
    {
      CHECK_THROWS(co_await (Just(42) | Then([](int) -> Co<> {
        co_await kYield;
        throw std::runtime_error("test");
      })));
    }

    SECTION("cancellation (first task cancelled)")
    {
      auto [r, _] = co_await AnyOf(el.Sleep(3ms) | Then([] {
        FAIL_CHECK("should not reach here");
        return NoOp();
      }),
        el.Sleep(1ms));
      CHECK(el.Now() == 1ms);
      CHECK(!r);
    }

    SECTION("cancellation (second task cancelled)")
    {
      auto [r, _] = co_await AnyOf(
        el.Sleep(1ms) | Then([&el] { return el.Sleep(3ms); }) | Then([] {
          FAIL_CHECK("should not reach here");
          return NoOp();
        }),
        el.Sleep(2ms));
      CHECK(el.Now() == 2ms);
      CHECK(!r);
    }

    SECTION("cancellation with event trigger from second task")
    {
      Event evt;
      co_await AnyOf(evt, el.Sleep(1ms) | Then([&] {
        evt.Trigger();
        return el.Sleep(3ms);
      }));
    }

    SECTION("cancellation (first task non cancellable)")
    {
      auto [r, _] = co_await AnyOf(
        el.Sleep(2ms, kNonCancellable) | Then([] { return Just(42); }),
        el.Sleep(1ms));
      CHECK(el.Now() == 2ms);
      CHECK(*r == 42);
    }

    SECTION("cancellation (second task non cancellable)")
    {
      auto [r, _] = co_await AnyOf(
        el.Sleep(1ms) | Then([&el] { return el.Sleep(3ms, kNonCancellable); }),
        el.Sleep(2ms));
      CHECK(el.Now() == 4ms);
      CHECK(r);
    }

    SECTION("cancellation (second task is early cancelable)")
    {
      // The second awaitable is non-cancellable, so should complete,
      // and the lambda should be invoked. However, as the awaitable
      // returned by the lambda is early-cancellable, it should not be
      // suspended on.
      bool started = false;
      auto [r, _] = co_await AnyOf(el.Sleep(2ms, kNonCancellable) | Then([&] {
        started = true;
        return el.Sleep(2ms);
      }),
        el.Sleep(1ms));
      CHECK(el.Now() == 2ms);
      CHECK(started);
      CHECK(!r);
    }

    SECTION("lifetime")
    {
      Semaphore sem { 1 };
      co_await AllOf(
        sem.Lock() | Then([&el] { return el.Sleep(5ms); }), [&]() -> Co<> {
          co_await el.Sleep(1ms);
          auto lk = co_await sem.Lock();
          CHECK(el.Now() == 5ms);
        });
    }

    SECTION("chaining -> then -> then")
    {
      auto r = co_await (Just(42) | Then([](const int v) {
        return Just(v + 1);
      }) | Then([](const int v) { return Just(v + 1); }));
      CHECK(r == 44);
    }

    SECTION("chaining -> (then -> then)")
    {
      auto r = co_await (Just(42) | (Then([](const int v) {
        return Just(v + 1);
      }) | Then([](const int v) { return Just(v + 1); })));
      CHECK(r == 44);
    }

    SECTION("operation with different qualifications")
    {
      LValueQualifiedImm lvqi;
      LValueQualified lvq;
      int r = co_await (NoOp() | Then([&]() -> auto& { return lvqi; }));
      CHECK(r == 42);
      r = co_await (NoOp() | Then([] { return RValueQualifiedImm {}; }));
      CHECK(r == 42);
      r = co_await (NoOp() | Then([&]() -> auto& { return lvq; }));
      CHECK(r == 42);
      r = co_await (NoOp() | Then([] { return RValueQualified {}; }));
      CHECK(r == 42);
    }

    SECTION("with custom awaitable")
    {
      struct CustomAwaitable {
        static auto await_ready() noexcept -> bool { return true; }
        static auto await_suspend(std::coroutine_handle<>) noexcept -> void { }
        static auto await_resume() noexcept -> int { return 99; }
      };

      auto r = co_await (
        CustomAwaitable {} | Then([](const int v) { return Just(v + 1); }));
      CHECK(r == 100);
    }

    SECTION("with empty sequence")
    {
      co_await (NoOp() | Then([] { return NoOp(); }));
    }
  });
}

} // namespace

// NOLINTEND(*-avoid-capturing-lambda-coroutines)
// NOLINTEND(*-avoid-do-while)
