//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/OxCo/Nursery.h>

#include <chrono>

#include <Oxygen/Testing/GTest.h>
#include <gmock/gmock.h>

#include "Utils/OxCoTestFixture.h"
#include "Utils/TestEventLoop.h"
#include <Oxygen/OxCo/Algorithms.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Event.h>
#include <Oxygen/OxCo/Run.h>

using std::chrono::milliseconds;
using namespace std::chrono_literals;

using oxygen::co::AnyOf;
using oxygen::co::Co;
using oxygen::co::Event;
using oxygen::co::kCancel;
using oxygen::co::kJoin;
using oxygen::co::kSuspendForever;
using oxygen::co::kYield;
using oxygen::co::NonCancellable;
using oxygen::co::NoOp;
using oxygen::co::Nursery;
using oxygen::co::OpenNursery;
using oxygen::co::Run;
using oxygen::co::TaskStarted;
using oxygen::co::detail::NurseryBodyRetVal;
using oxygen::co::testing::kNonCancellable;
using oxygen::co::testing::TestEventLoop;

// NOLINTBEGIN(*-avoid-reference-coroutine-parameters)
// NOLINTBEGIN(*-avoid-capturing-lambda-coroutines)
// NOLINTBEGIN(*-avoid-do-while)

namespace {

class NurseryTest : public oxygen::co::testing::OxCoTestFixture { };

NOLINT_TEST_F(NurseryTest, ScopeForStartedTasks)
{
  ::Run(*el_, [&]() -> Co<> {
    size_t count = 0;
    auto increment_after = [&](const milliseconds delay) -> Co<> {
      co_await el_->Sleep(delay);
      ++count;
    };
    co_yield Nursery::Factory {} % [&](Nursery& n) -> Co<NurseryBodyRetVal> {
      n.Start(increment_after, 2ms);
      n.Start(increment_after, 3ms);
      n.Start(increment_after, 5ms);

      co_await el_->Sleep(4ms);
      EXPECT_EQ(count, 2);

      co_await el_->Sleep(2ms);
      EXPECT_EQ(count, 3);

      co_return kJoin;
    };
  });
}

NOLINT_TEST_F(NurseryTest, StartEnsuresArgsImplicitlyConstructed)
{
  ::Run(*el_, [&]() -> Co<> {
    auto func = [&](const std::string& s) -> Co<> {
      co_await el_->Sleep(1ms);
      EXPECT_EQ(s, "hello world! I am a long(ish) string.");
    };

    OXCO_WITH_NURSERY(n)
    {
      n.Start(func, "hello world! I am a long(ish) string.");
      co_return kJoin;
    };
  });
}

NOLINT_TEST_F(NurseryTest, StartEnsuresArgsExistingObjects)
{
  ::Run(*el_, [&]() -> Co<> {
    auto func = [&](const std::string& s) -> Co<> {
      co_await el_->Sleep(1ms);
      EXPECT_EQ(s, "hello world! I am a long(ish) string.");
    };

    const std::string str = "hello world! I am a long(ish) string.";
    OXCO_WITH_NURSERY(n)
    {
      n.Start(func, str);
      co_return kJoin;
    };
  });
}

NOLINT_TEST_F(NurseryTest, StartEnsuresArgsPassedByCref)
{
  ::Run(*el_, [&]() -> Co<> {
    auto func = [&](const std::string& s) -> Co<> {
      co_await el_->Sleep(1ms);
      EXPECT_EQ(s, "hello world! I am a long(ish) string.");
    };

    const std::string ext = "hello world! I am a long(ish) string.";
    OXCO_WITH_NURSERY(n)
    {
      n.Start(func, std::cref(ext));
      co_return kJoin;
    };
  });
}

NOLINT_TEST_F(NurseryTest, StartWithMemberFunction_Pointer)
{
  constexpr int kInitialValue = 42;
  constexpr int kModifiedValue = 43;

  struct Test {
    int x = kInitialValue;
    auto Func(TestEventLoop& el, const int expected) const -> Co<>
    {
      co_await el.Sleep(1ms);
      EXPECT_EQ(x, expected);
    }
  };

  Test obj;
  ::Run(*el_, [&]() -> Co<> {
    OXCO_WITH_NURSERY(n)
    {
      n.Start(&Test::Func, &obj, std::ref(*el_), kModifiedValue);
      obj.x = kModifiedValue;
      co_return kJoin;
    };
  });
}

NOLINT_TEST_F(NurseryTest, StartWithMemberFunction_Ref)
{
  constexpr int kInitialValue = 42;
  constexpr int kModifiedValue = 43;

  struct Test {
    int x = kInitialValue;
    auto Func(TestEventLoop& el, const int expected) const -> Co<>
    {
      co_await el.Sleep(1ms);
      EXPECT_EQ(x, expected);
    }
  };

  Test obj;
  ::Run(*el_, [&]() -> Co<> {
    OXCO_WITH_NURSERY(n)
    {
      n.Start(&Test::Func, std::ref(obj), std::ref(*el_), kModifiedValue);
      obj.x = kModifiedValue;
      co_return kJoin;
    };
  });
}

NOLINT_TEST_F(NurseryTest, StartWithMemberFunction_ByValue)
{
  constexpr int kInitialValue = 42;
  constexpr int kModifiedValue = 43;

  struct Test {
    int x = kInitialValue;
    auto Func(TestEventLoop& el, const int expected) const -> Co<>
    {
      co_await el.Sleep(1ms);
      EXPECT_EQ(x, expected);
    }
  };

  Test obj;
  ::Run(*el_, [&]() -> Co<> {
    OXCO_WITH_NURSERY(n)
    {
      n.Start(&Test::Func, obj, std::ref(*el_), kInitialValue);
      obj.x = kModifiedValue;
      co_return kJoin;
    };
    EXPECT_EQ(el_->Now(), 1ms);
  });
}

NOLINT_TEST_F(NurseryTest, CompletionPolicies_Join)
{
  ::Run(*el_, [&]() -> Co<> {
    auto sleep
      = [&](const milliseconds delay) -> Co<> { co_await el_->Sleep(delay); };
    OXCO_WITH_NURSERY(n)
    {
      n.Start(sleep, 5ms);
      co_return kJoin;
    };
    EXPECT_EQ(el_->Now(), 5ms);
  });
}

NOLINT_TEST_F(NurseryTest, CompletionPolicies_Cancel)
{
  ::Run(*el_, [&]() -> Co<> {
    auto sleep
      = [&](const milliseconds delay) -> Co<> { co_await el_->Sleep(delay); };
    OXCO_WITH_NURSERY(n)
    {
      n.Start(sleep, 5ms);
      co_return kCancel;
    };
    EXPECT_EQ(el_->Now(), 0ms);
  });
}

NOLINT_TEST_F(NurseryTest, CompletionPolicies_CancelledFromOutside)
{
  ::Run(*el_, [&]() -> Co<> {
    auto sleep
      = [&](const milliseconds delay) -> Co<> { co_await el_->Sleep(delay); };
    co_await AnyOf(sleep(5ms), [&]() -> Co<> {
      OXCO_WITH_NURSERY(/*unused*/)
      {
        co_await kSuspendForever;
        co_return kJoin;
      };
    });
    EXPECT_EQ(el_->Now(), 5ms);
  });
}

NOLINT_TEST_F(NurseryTest, EarlyCancelsTasks)
{
  ::Run(*el_, [&]() -> Co<> {
    bool started = false;
    OXCO_WITH_NURSERY(nursery)
    {
      ScopeGuard guard([&]() noexcept {
        nursery.Start([&]() -> Co<> {
          started = true;
          co_await kYield;
          ADD_FAILURE() << "should never reach here";
          co_return;
        });
      });
      co_return kCancel;
    };
    EXPECT_TRUE(started);
  });
}

NOLINT_TEST_F(NurseryTest, SynchronousCancellation)
{
  ::Run(*el_, [&]() -> Co<> {
    bool cancelled = false;
    OXCO_WITH_NURSERY(n)
    {
      n.Start([&]() -> Co<> {
        ScopeGuard guard([&]() noexcept { cancelled = true; });
        co_await el_->Sleep(5ms);
      });
      co_await el_->Sleep(1ms);
      EXPECT_FALSE(cancelled);
      n.Cancel();
      EXPECT_FALSE(cancelled);
      co_await kYield;
      ADD_FAILURE() << "should not reach here";
      co_return kCancel;
    };
    EXPECT_TRUE(cancelled);
  });
}

NOLINT_TEST_F(NurseryTest, MultipleCancelledTasks)
{
  ::Run(*el_, [&]() -> Co<> {
    auto task = [&](Nursery& n) -> Co<> {
      co_await el_->Sleep(1ms, kNonCancellable);
      n.Cancel();
    };
    OXCO_WITH_NURSERY(n)
    {
      n.Start(task, std::ref(n));
      n.Start(task, std::ref(n));
      n.Start(task, std::ref(n));
      co_return kJoin;
    };
  });
}

NOLINT_TEST_F(NurseryTest, MultipleExceptions)
{
  ::Run(*el_, [&]() -> Co<> {
    auto task = [&]([[maybe_unused]] Nursery& n) -> Co<> {
      co_await el_->Sleep(1ms, kNonCancellable);
      throw std::runtime_error("boo!");
    };
    try {
      OXCO_WITH_NURSERY(n)
      {
        n.Start(task, std::ref(n));
        n.Start(task, std::ref(n));
        n.Start(task, std::ref(n));
        co_return kJoin;
      };
    } catch (std::runtime_error& /*ex*/) {
      // expected
    }
  });
}

NOLINT_TEST_F(NurseryTest, CancelAndException)
{
  ::Run(*el_, [&]() -> Co<> {
    try {
      OXCO_WITH_NURSERY(n)
      {
        n.Start([&]() -> Co<> {
          co_await el_->Sleep(2ms, kNonCancellable);
          throw std::runtime_error("boo!");
        });
        n.Start([&]() -> Co<> { co_await el_->Sleep(3ms, kNonCancellable); });
        co_await el_->Sleep(1ms);
        co_return kCancel;
      };
      // Should have thrown
      ADD_FAILURE() << "Expected exception was not thrown";
    } catch (const std::runtime_error& ex) {
      EXPECT_EQ(std::string_view(ex.what()), "boo!");
    }
    EXPECT_EQ(el_->Now(), 3ms);
  });
}

NOLINT_TEST_F(NurseryTest, CancelFromOutside)
{
  ::Run(*el_, [&]() -> Co<> {
    OXCO_WITH_NURSERY(n)
    {
      n.Start([&]() -> Co<> { co_await el_->Sleep(10ms); });
      el_->Schedule(1ms, [&] { n.Cancel(); });
      co_return kJoin;
    };

    EXPECT_EQ(el_->Now(), 1ms);
  });
}

NOLINT_TEST_F(NurseryTest, PropagatesExceptions)
{
  ::Run(*el_, [&]() -> Co<> {
    auto t1 = [&]() -> Co<> { co_await el_->Sleep(2ms); };
    auto t2 = [&]() -> Co<> {
      co_await std::suspend_never();
      throw std::runtime_error("boo!");
    };

    try {
      OXCO_WITH_NURSERY(n)
      {
        n.Start(t1);
        n.Start(t2);
        co_return kJoin;
      };
      ADD_FAILURE() << "Expected exception was not thrown";
    } catch (const std::runtime_error& ex) {
      EXPECT_EQ(std::string_view(ex.what()), "boo!");
    }

    EXPECT_EQ(el_->Now(), 0ms);
  });
}

NOLINT_TEST_F(NurseryTest, Start_TaskStarted_CoawaitInit)
{
  ::Run(*el_, [&]() -> Co<> {
    OXCO_WITH_NURSERY(n)
    {
      co_await n.Start(
        [&](const milliseconds delay, TaskStarted<> started) -> Co<> {
          co_await el_->Sleep(delay);
          started();
          co_await el_->Sleep(5ms);
        },
        2ms);
      EXPECT_EQ(el_->Now(), 2ms);
      co_return kJoin;
    };
    EXPECT_EQ(el_->Now(), 7ms);
  });
}

NOLINT_TEST_F(NurseryTest, Start_TaskStarted_NoCoawaitInit)
{
  ::Run(*el_, [&]() -> Co<> {
    OXCO_WITH_NURSERY(n)
    {
      n.Start(
        [&](const milliseconds delay, TaskStarted<> started) -> Co<> {
          co_await el_->Sleep(delay);
          started();
        },
        2ms);
      co_return kJoin;
    };
    EXPECT_EQ(el_->Now(), 2ms);
  });
}

NOLINT_TEST_F(NurseryTest, Start_TaskStarted_OptionalArg_CoawaitInit)
{
  ::Run(*el_, [&]() -> Co<> {
    OXCO_WITH_NURSERY(n)
    {
      co_await n.Start(
        [&](const milliseconds delay, TaskStarted<> started = {}) -> Co<> {
          co_await el_->Sleep(delay);
          started();
        },
        2ms);
      co_return kJoin;
    };
    EXPECT_EQ(el_->Now(), 2ms);
  });
}

NOLINT_TEST_F(NurseryTest, Start_TaskStarted_OptionalArg_NoCoawaitInit)
{
  ::Run(*el_, [&]() -> Co<> {
    OXCO_WITH_NURSERY(n)
    {
      n.Start(
        [&](const milliseconds delay, TaskStarted<> started = {}) -> Co<> {
          co_await el_->Sleep(delay);
          started();
        },
        2ms);
      co_return kJoin;
    };
    EXPECT_EQ(el_->Now(), 2ms);
  });
}

NOLINT_TEST_F(NurseryTest, Start_TaskStarted_WithCombiners)
{
  ::Run(*el_, [&]() -> Co<> {
    auto task = [&](const milliseconds delay, TaskStarted<> started) -> Co<> {
      co_await el_->Sleep(delay);
      started();
      co_await el_->Sleep(delay);
    };
    OXCO_WITH_NURSERY(n)
    {
      co_await AllOf(n.Start(task, 2ms), n.Start(task, 3ms));
      EXPECT_EQ(el_->Now(), 3ms);
      co_return kJoin;
    };
    EXPECT_EQ(el_->Now(), 6ms);
  });
}

NOLINT_TEST_F(NurseryTest, Start_TaskStarted_ReturnValue)
{
  ::Run(*el_, [&]() -> Co<> {
    OXCO_WITH_NURSERY(n)
    {
      const int ret = co_await n.Start([](TaskStarted<int> started) -> Co<> {
        co_await kYield; // make this a coroutine
        started(42);
      });
      EXPECT_EQ(ret, 42);
      co_return kJoin;
    };
  });
}

NOLINT_TEST_F(NurseryTest, Start_TaskStarted_PassesArguments)
{
  ::Run(*el_, [&]() -> Co<> {
    OXCO_WITH_NURSERY(n)
    {
      const int ret = co_await n.Start<int>(
        [](auto arg, TaskStarted<int> started) -> Co<> {
          co_await kYield; // make this a coroutine
          started(arg);
        },
        42);
      EXPECT_EQ(ret, 42);
      co_return kJoin;
    };
  });
}

NOLINT_TEST_F(NurseryTest, Start_TaskStarted_HandleInitException)
{
  ::Run(*el_, [&]() -> Co<> {
    OXCO_WITH_NURSERY(n)
    {
      try {
        co_await n.Start([]([[maybe_unused]] TaskStarted<> started) -> Co<> {
          co_await kYield; // make this a coroutine
          throw std::runtime_error("boo!");
        });
        ADD_FAILURE() << "should never reach here";
      } catch (const std::runtime_error& e) {
        EXPECT_EQ(e.what(), std::string_view("boo!"));
      }
      co_return kJoin;
    };
  });
}

NOLINT_TEST_F(NurseryTest, Start_TaskStarted_CancelBeforeInit)
{
  ::Run(*el_, [&]() -> Co<> {
    OXCO_WITH_NURSERY(n)
    {
      auto [done, timedOut] = co_await AnyOf(
        n.Start([&]([[maybe_unused]] TaskStarted<> started) -> Co<> {
          co_await el_->Sleep(5ms);
          ADD_FAILURE() << "should never reach here";
        }),
        el_->Sleep(2ms));
      EXPECT_FALSE(done);
      EXPECT_EQ(el_->Now(), 2ms);
      co_return kJoin;
    };
  });
}

NOLINT_TEST_F(NurseryTest, Start_TaskStarted_RejectedCancellation)
{
  ::Run(*el_, [&]() -> Co<> {
    OXCO_WITH_NURSERY(n)
    {
      auto [done, timedOut]
        = co_await AnyOf(n.Start([&](TaskStarted<> started) -> Co<> {
            co_await NonCancellable(el_->Sleep(5ms));
            started();
          }),
          // This task completion will cause cancellation request of
          // the other task, which will be rejected.
          el_->Sleep(2ms));
      EXPECT_FALSE(done);
      EXPECT_EQ(el_->Now(), 5ms);
      co_return kJoin;
    };
  });
}

NOLINT_TEST_F(NurseryTest, Start_TaskStarted_InnerNurseryCancelled)
{
  ::Run(*el_, [&]() -> Co<> {
    Nursery* inner = nullptr;
    Event cancelInner;
    OXCO_WITH_NURSERY(outer)
    {
      co_await outer.Start([&](TaskStarted<> started) -> Co<> {
        co_await AnyOf(
          OpenNursery(std::ref(inner), std::move(started)), cancelInner);
      });

      if (!inner) {
        ADD_FAILURE() << "Inner nursery was not created";
        co_return kJoin;
      }

      outer.Start([&]() -> Co<> {
        co_await inner->Start([&](TaskStarted<> started) -> Co<> {
          co_await el_->Sleep(5ms);
          started();
          co_await el_->Sleep(1ms);
        });
      });

      co_await el_->Sleep(1ms);
      cancelInner.Trigger();
      co_await el_->Sleep(1ms);
      EXPECT_NE(inner, nullptr);
      co_await el_->Sleep(5ms);
      EXPECT_EQ(inner, nullptr);

      co_return kJoin;
    };
  });
}

NOLINT_TEST_F(NurseryTest, Start_TaskStarted_ImmediatelyReady)
{
  ::Run(*el_, [&]() -> Co<> {
    OXCO_WITH_NURSERY(n)
    {
      co_await n.Start([](TaskStarted<> started) -> Co<> {
        started();
        return NoOp();
      });
      co_return kJoin;
    };
    EXPECT_EQ(el_->Now(), 0ms);
  });
}

NOLINT_TEST_F(NurseryTest, Start_TaskStarted_CancelBeforeHandoff)
{
  ::Run(*el_, [&]() -> Co<> {
    co_await AnyOf(el_->Sleep(1ms), [&]() -> Co<> {
      OXCO_WITH_NURSERY(n)
      {
        co_await n.Start([&](TaskStarted<> started) -> Co<> {
          co_await el_->Sleep(5ms, kNonCancellable);
          started();

          co_await el_->Sleep(1ms, kNonCancellable);

          co_await kYield;
          ADD_FAILURE() << "should never reach here";
        });

        ADD_FAILURE() << "should never reach here";

        co_return kJoin;
      };
    });
  });
}

NOLINT_TEST_F(NurseryTest, Start_TaskStarted_CancelBeforeHandoff2)
{
  ::Run(*el_, [&]() -> Co<> {
    OXCO_WITH_NURSERY(n)
    {
      co_await AnyOf(
        el_->Sleep(1ms), n.Start([&](TaskStarted<> started) -> Co<> {
          co_await el_->Sleep(2ms, kNonCancellable);
          started();
          co_await el_->Sleep(2ms, kNonCancellable);
        }));
      co_return kCancel;
    };
  });
}

NOLINT_TEST_F(NurseryTest, OpenInnerNursery)
{
  ::Run(*el_, [&]() -> Co<> {
    Nursery* inner = nullptr;
    OXCO_WITH_NURSERY(outer)
    {
      co_await outer.Start(OpenNursery, std::ref(inner));
      inner->Start([&]() -> Co<> { co_return; });
      co_return kCancel;
    };
  });
}

NOLINT_TEST_F(NurseryTest, OpenInnerNurseryAndCancel)
{
  ::Run(*el_, [&]() -> Co<> {
    Nursery* n = nullptr;
    OXCO_WITH_NURSERY(n2)
    {
      co_await n2.Start(OpenNursery, std::ref(n));
      n->Start([&]() -> Co<> {
        co_await el_->Sleep(1ms, kNonCancellable);
        n->Start([&]() -> Co<> { co_return; });
      });
      co_return kCancel;
    };
    EXPECT_EQ(el_->Now(), 1ms);
  });
}

} // namespace

// NOLINTEND(*-avoid-do-while)
// NOLINTEND(*-avoid-capturing-lambda-coroutines)
// NOLINTEND(*-avoid-reference-coroutine-parameters)
