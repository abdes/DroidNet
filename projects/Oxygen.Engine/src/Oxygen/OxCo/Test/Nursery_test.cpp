//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Oxygen/OxCo/Nursery.h"

#include <chrono>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "Oxygen/OxCo/Algorithms.h"
#include "Oxygen/OxCo/Co.h"
#include "Oxygen/OxCo/Event.h"
#include "Oxygen/OxCo/Run.h"
#include "Utils/TestEventLoop.h"

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

TEST_CASE("Nursery is a scope for its started tasks")
{
    TestEventLoop el;
    Run(el, [&]() -> Co<> {
        size_t count = 0;
        auto increment_after = [&](const milliseconds delay) -> Co<> {
            co_await el.Sleep(delay);
            ++count;
        };
        co_yield Nursery::Factory {} % [&](Nursery& n) -> Co<NurseryBodyRetVal> {
            n.Start(increment_after, 2ms);
            n.Start(increment_after, 3ms);
            n.Start(increment_after, 5ms);

            co_await el.Sleep(4ms);
            CHECK(count == 2);

            co_await el.Sleep(2ms);
            CHECK(count == 3);

            co_return kJoin;
        };
    });
}

TEST_CASE("Nursery::Start() ensures args live as long as task")
{
    TestEventLoop el;
    Run(el, [&]() -> Co<> {
        auto func = [&](const std::string& s) -> Co<> {
            co_await el.Sleep(1ms);
            CHECK(s == "hello world! I am a long(ish) string.");
        };

        const std::string ext = "hello world! I am a long(ish) string.";

        OXCO_WITH_NURSERY(n)
        {
            SECTION("for implicitly constructed objects")
            {
                n.Start(func, "hello world! I am a long(ish) string.");
            }

            SECTION("for existing objects")
            {
                const std::string str = "hello world! I am a long(ish) string.";
                n.Start(func, str);
            }

            SECTION("for objects passed by std::cref()")
            {
                // Passing a string defined outside the nursery block by reference
                n.Start(func, std::cref(ext));
            }

            co_return kJoin;
        };
    });
}

TEST_CASE("Nursery can start a task with a member function")
{
    constexpr int kInitialValue = 42;
    constexpr int kModifiedValue = 43;

    struct Test {
        int x = kInitialValue;
        auto Func(TestEventLoop& el, const int expected) const -> Co<>
        {
            co_await el.Sleep(1ms);
            CHECK(x == expected);
        }
    };

    Test obj;
    TestEventLoop el;
    Run(el, [&]() -> Co<> {
        OXCO_WITH_NURSERY(n)
        {
            SECTION("passing the object as a pointer")
            {
                n.Start(&Test::Func, &obj, std::ref(el), kModifiedValue);
                obj.x = kModifiedValue;
            }
            SECTION("passing the object as a std::ref()")
            {
                n.Start(&Test::Func, std::ref(obj), std::ref(el), kModifiedValue);
                obj.x = kModifiedValue;
            }
            SECTION("passing the object by value")
            {
                // `this` passed by value, so subsequent changes to `obj`
                // should not be visible by the spawned task
                n.Start(&Test::Func, obj, std::ref(el), kInitialValue);
                obj.x = kModifiedValue;
            }
            co_return kJoin;
        };
        CHECK(el.Now() == 1ms);
    });
}

TEST_CASE("Nursery completion policies")
{
    TestEventLoop el;
    Run(el, [&]() -> Co<> {
        auto sleep = [&](const milliseconds delay) -> Co<> {
            co_await el.Sleep(delay);
        };

        SECTION("normally with `co_await kJoin`")
        {
            OXCO_WITH_NURSERY(n)
            {
                n.Start(sleep, 5ms);
                co_return kJoin;
            };
            CHECK(el.Now() == 5ms);
        }

        SECTION("through cancellation of its tasks with `co_await kCancel`")
        {
            OXCO_WITH_NURSERY(n)
            {
                n.Start(sleep, 5ms);
                co_return kCancel;
            };
            CHECK(el.Now() == 0ms);
        }

        SECTION("by getting cancelled from outside")
        {
            co_await AnyOf(
                sleep(5ms),
                [&]() -> Co<> {
                    OXCO_WITH_NURSERY(/*unused*/)
                    {
                        co_await kSuspendForever;
                        co_return kJoin;
                    };
                });
            CHECK(el.Now() == 5ms);
        }
    });
}

TEST_CASE("Nursery early cancels tasks")
{
    TestEventLoop el;
    Run(el, [&]() -> Co<> {
        bool started = false;
        OXCO_WITH_NURSERY(nursery)
        {
            ScopeGuard guard([&]() noexcept {
                nursery.Start([&]() -> Co<> {
                    started = true;
                    co_await kYield;
                    FAIL_CHECK("should never reach here");
                    co_return;
                });
            });
            co_return kCancel;
        };
        CHECK(started);
    });
}

TEST_CASE("Nursery synchronous cancellation")
{
    TestEventLoop el;
    Run(el, [&]() -> Co<> {
        bool cancelled = false;
        OXCO_WITH_NURSERY(n)
        {
            n.Start([&]() -> Co<> {
                ScopeGuard guard([&]() noexcept { cancelled = true; });
                co_await el.Sleep(5ms);
            });
            co_await el.Sleep(1ms);
            CHECK(!cancelled);
            n.Cancel();
            CHECK(!cancelled);
            co_await kYield;
            FAIL_CHECK("should not reach here");
            co_return kCancel;
        };
        CHECK(cancelled);
    });
}

TEST_CASE("Nursery can handle multiple cancelled tasks")
{
    TestEventLoop el;
    Run(el, [&]() -> Co<> {
        auto task = [&](Nursery& n) -> Co<> {
            co_await el.Sleep(1ms, kNonCancellable);
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

TEST_CASE("Nursery can handle multiple exceptions")
{
    TestEventLoop el;
    Run(el, [&]() -> Co<> {
        auto task = [&]([[maybe_unused]] Nursery& n) -> Co<> {
            co_await el.Sleep(1ms, kNonCancellable);
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
        } catch (std::runtime_error& ex) {
            // expected
            (void)ex;
        }
    });
}

TEST_CASE("Nursery cancel and exception")
{
    TestEventLoop el;
    Run(el, [&]() -> Co<> {
        CHECK_THROWS_WITH(
            OXCO_WITH_NURSERY(n) {
                // This task will throw an exception that get propagated to the
                // nursery
                n.Start([&]() -> Co<> {
                    co_await el.Sleep(2ms, kNonCancellable);
                    throw std::runtime_error("boo!");
                });
                // This task cannot be cancelled and will complete
                n.Start([&]() -> Co<> {
                    co_await el.Sleep(3ms, kNonCancellable);
                });
                co_await el.Sleep(1ms);
                // Requesting cancellation of the nursery tasks will not cancel
                // the 3ms task
                co_return kCancel;
            },
            // We should get the exception
            Catch::Matchers::Equals("boo!"));
        // and we should finish after the 3ms task is done
        CHECK(el.Now() == 3ms);
    });
}

TEST_CASE("Nursery cancel from outside")
{
    TestEventLoop el;
    Run(el, [&]() -> Co<> {
        OXCO_WITH_NURSERY(n)
        {
            n.Start([&]() -> Co<> { co_await el.Sleep(10ms); });
            el.Schedule(1ms, [&] { n.Cancel(); });
            co_return kJoin;
        };

        CHECK(el.Now() == 1ms);
    });
}

TEST_CASE("Nursery propagates exceptions")
{
    TestEventLoop el;
    Run(el, [&]() -> Co<> {
        // This task can be cancelled, and will be early cancelled due to the
        // early exception thrown in the next task
        auto t1 = [&]() -> Co<> { co_await el.Sleep(2ms); };
        // This task throws immediately and should cause the nursery to get the
        // exception immediately
        auto t2 = [&]() -> Co<> {
            co_await std::suspend_never();
            throw std::runtime_error("boo!");
        };

        CHECK_THROWS_WITH(
            OXCO_WITH_NURSERY(n) {
                n.Start(t1);
                n.Start(t2);
                co_return kJoin;
            },
            Catch::Matchers::Equals("boo!"));

        // Early cancellation and early exception
        CHECK(el.Now() == 0ms);
    });
}

TEST_CASE("Nursery `Start` with `TaskStarted`")
{
    TestEventLoop el;
    Run(el, [&]() -> Co<> {
        SECTION("started, co_await initialization")
        {
            OXCO_WITH_NURSERY(n)
            {
                co_await n.Start(
                    [&](const milliseconds delay,
                        TaskStarted<> started) -> Co<> {
                        co_await el.Sleep(delay);
                        started();
                        co_await el.Sleep(5ms);
                    },
                    2ms);
                CHECK(el.Now() == 2ms);
                co_return kJoin;
            };
            CHECK(el.Now() == 7ms);
        }

        SECTION("started, no co_await initialization")
        {
            OXCO_WITH_NURSERY(n)
            {
                // Not waiting for the initialization
                n.Start(
                    [&](const milliseconds delay,
                        TaskStarted<> started) -> Co<> {
                        co_await el.Sleep(delay);
                        started();
                    },
                    2ms);
                co_return kJoin;
            };
            CHECK(el.Now() == 2ms);
        }

        SECTION("optional arg, co_await initialization")
        {
            OXCO_WITH_NURSERY(n)
            {
                // The default constructed `TaskStarted` performs no operation
                // when it is called.
                co_await n.Start(
                    [&](const milliseconds delay,
                        TaskStarted<> started = {}) -> Co<> {
                        co_await el.Sleep(delay);
                        started();
                    },
                    2ms);
                co_return kJoin;
            };
            CHECK(el.Now() == 2ms);
        }

        SECTION("optional arg, no co_await initialization")
        {
            OXCO_WITH_NURSERY(n)
            {
                // Behaves like "optional arg, co_await initialization"
                n.Start(
                    [&](const milliseconds delay,
                        TaskStarted<> started = {}) -> Co<> {
                        co_await el.Sleep(delay);
                        started();
                    },
                    2ms);
                co_return kJoin;
            };
            CHECK(el.Now() == 2ms);
        }

        SECTION("works with combiners")
        {
            auto task = [&](const milliseconds delay,
                            TaskStarted<> started) -> Co<> {
                co_await el.Sleep(delay);
                started();
                co_await el.Sleep(delay);
            };
            OXCO_WITH_NURSERY(n)
            {
                co_await AllOf(n.Start(task, 2ms), n.Start(task, 3ms));
                CHECK(el.Now() == 3ms);
                co_return kJoin;
            };
            CHECK(el.Now() == 6ms);
        }

        SECTION("gets the return value from started")
        {
            OXCO_WITH_NURSERY(n)
            {
                const int ret = co_await n.Start([](
                                                     TaskStarted<int> started) -> Co<> {
                    co_await kYield; // make this a coroutine
                    started(42);
                });
                CHECK(ret == 42);
                co_return kJoin;
            };
        }

        SECTION("passes arguments to the callable")
        {
            OXCO_WITH_NURSERY(n)
            {
                const int ret = co_await n.Start<int>(
                    [](auto arg, TaskStarted<int> started) -> Co<> {
                        co_await kYield; // make this a coroutine
                        started(arg);
                    },
                    42);
                CHECK(ret == 42);
                co_return kJoin;
            };
        }

        SECTION("handle exception thrown during initialization")
        {
            OXCO_WITH_NURSERY(n)
            {
                try {
                    co_await n.Start([]([[maybe_unused]] TaskStarted<> started) -> Co<> {
                        co_await kYield; // make this a coroutine
                        throw std::runtime_error("boo!");
                    });
                    FAIL_CHECK("should never reach here");
                } catch (const std::runtime_error& e) {
                    CHECK(e.what() == std::string_view("boo!"));
                }
                co_return kJoin;
            };
        }

        SECTION("works when task cancelled before initialization completes")
        {
            OXCO_WITH_NURSERY(n)
            {
                auto [done, timedOut] = co_await AnyOf(
                    n.Start([&]([[maybe_unused]] TaskStarted<> started) -> Co<> {
                        co_await el.Sleep(5ms);
                        FAIL_CHECK("should never reach here");
                    }),
                    el.Sleep(2ms));
                CHECK(!done);
                CHECK(el.Now() == 2ms);
                co_return kJoin;
            };
        }

        SECTION("works when task rejects cancellation")
        {
            OXCO_WITH_NURSERY(n)
            {
                auto [done, timedOut] = co_await AnyOf(
                    n.Start([&](TaskStarted<> started) -> Co<> {
                        co_await NonCancellable(el.Sleep(5ms));
                        started();
                    }),
                    // This task completion will cause cancellation request of
                    // the other task, which will be rejected.
                    el.Sleep(2ms));
                // Task completes normally after 5ms
                CHECK(done);
                CHECK(el.Now() == 5ms);
                co_return kJoin;
            };
        }

        SECTION("works when inner nursery is cancelled")
        {
            Nursery* inner = nullptr;
            Event cancelInner;
            OXCO_WITH_NURSERY(outer)
            {
                // Start task with OpenNursery
                co_await outer.Start([&](TaskStarted<> started) -> Co<> {
                    co_await AnyOf(
                        OpenNursery(std::ref(inner), std::move(started)),
                        cancelInner);
                });

                REQUIRE(inner);

                // Start task in outer nursery
                outer.Start([&]() -> Co<> {
                    co_await inner->Start([&](TaskStarted<> started) -> Co<> {
                        co_await el.Sleep(5ms);
                        started();
                        co_await el.Sleep(1ms);
                    });
                });

                // Cancel inner nursery after 1ms
                co_await el.Sleep(1ms);
                cancelInner.Trigger();
                co_await el.Sleep(1ms);
                CHECK(inner);
                co_await el.Sleep(5ms);
                // outer task completes
                CHECK(!inner);

                co_return kJoin;
            };
        }

        SECTION("works with task that is immediately ready")
        {
            OXCO_WITH_NURSERY(n)
            {
                co_await n.Start([](TaskStarted<> started) -> Co<> {
                    started();
                    return NoOp();
                });
                // Everything completes immediately
                co_return kJoin;
            };
            CHECK(el.Now() == 0ms);
        }
    });
}

TEST_CASE("Open inner nursery")
{
    TestEventLoop el;
    Run(el, [&]() -> Co<> {
        Nursery* inner = nullptr;
        OXCO_WITH_NURSERY(outer)
        {
            co_await outer.Start(OpenNursery, std::ref(inner));
            inner->Start([&]() -> Co<> { co_return; });
            co_return kCancel;
        };
    });
}

TEST_CASE("Open inner nursery and cancel")
{
    TestEventLoop el;
    Run(el, [&]() -> Co<> {
        Nursery* n = nullptr;
        OXCO_WITH_NURSERY(n2)
        {
            co_await n2.Start(OpenNursery, std::ref(n));
            // This task is not early cancellable
            n->Start([&]() -> Co<> {
                co_await el.Sleep(1ms, kNonCancellable);
                n->Start([&]() -> Co<> { co_return; });
            });
            // Will cancel but after the 1ms sleep completes
            co_return kCancel;
        };
        CHECK(el.Now() == 1ms);
    });
}

} // namespace

// NOLINTEND(*-avoid-do-while)
// NOLINTEND(*-avoid-capturing-lambda-coroutines)
// NOLINTEND(*-avoid-reference-coroutine-parameters)
