//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/OxCo/Value.h>

#include <catch2/catch_test_macros.hpp>
#include <chrono>

#include "Utils/TestEventLoop.h"
#include <Oxygen/OxCo/Algorithms.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Run.h>

using namespace std::chrono_literals;
using oxygen::co::AllOf;
using oxygen::co::Co;
using oxygen::co::Run;
using oxygen::co::Value;
using oxygen::co::testing::TestEventLoop;

// NOLINTBEGIN(*-avoid-do-while)
// NOLINTBEGIN(*-avoid-capturing-lambda-coroutines)

namespace {

TEST_CASE("Value")
{
    TestEventLoop el;
    SECTION("wakes tasks when its value changes")
    {
        Run(el, [&]() -> Co<> {
            Value v { 0 };

            co_await AllOf(
                [&]() -> Co<> {
                    co_await v.UntilEquals(0);
                    CHECK(el.Now() == 0ms);

                    auto [from, to] = co_await v.UntilChanged();
                    CHECK(el.Now() == 1ms);
                    CHECK(from == 0);
                    CHECK(to == 3);

                    std::tie(from, to) = co_await v.UntilChanged();
                    // Should skip the 3->3 change
                    CHECK(el.Now() == 3ms);
                    CHECK(from == 3);
                    CHECK(to == 4);

                    to = co_await v.UntilEquals(5);
                    CHECK(el.Now() == 4ms);
                    CHECK(to == 5);
                    CHECK(v == 7);
                },
                [&]() -> Co<> {
                    co_await el.Sleep(1ms);
                    v = 3;

                    co_await el.Sleep(1ms);
                    v = 3;
                    co_await el.Sleep(1ms);
                    v = 4;

                    co_await el.Sleep(1ms);
                    v = 7;
                    v = 5;
                    v = 7;
                });

            CHECK(el.Now() == 4ms);
        });
    }

    SECTION("updates value and wakes tasks when Set() is called")
    {
        Run(el, [&]() -> Co<> {
            Value v { 0 };

            co_await AllOf(
                [&]() -> Co<> {
                    co_await v.UntilEquals(1);
                    CHECK(v.Get() == 1);
                },
                [&]() -> Co<> {
                    co_await el.Sleep(1ms);
                    v.Set(1);
                });

            CHECK(el.Now() == 1ms);
        });
    }

    SECTION("updates value and wakes tasks when assigned another object")
    {
        Run(el, [&]() -> Co<> {
            Value v { 0 };

            co_await AllOf(
                [&]() -> Co<> {
                    co_await v.UntilEquals(2);
                    CHECK(v.Get() == 2);
                },
                [&]() -> Co<> {
                    co_await el.Sleep(1ms);
                    v = 2;
                });

            CHECK(el.Now() == 1ms);
        });
    }

    SECTION("`UntilMatches` suspends until predicate matches")
    {
        Run(el, [&]() -> Co<> {
            Value v { 0 };

            co_await AllOf(
                [&]() -> Co<> {
                    co_await v.UntilMatches([](const int& value) { return value > 2; });
                    CHECK(v.Get() == 3);
                },
                [&]() -> Co<> {
                    co_await el.Sleep(1ms);
                    v = 1;
                    co_await el.Sleep(1ms);
                    v = 2;
                    co_await el.Sleep(1ms);
                    v = 3;
                });

            CHECK(el.Now() == 3ms);
        });
    }

    SECTION("`UntilChanged` with custom predicate")
    {
        Run(el, [&]() -> Co<> {
            Value v { 0 };

            co_await AllOf(
                [&]() -> Co<> {
                    auto [from, to] = co_await v.UntilChanged([](const int& f, const int& t) { return f < t; });
                    CHECK(from == 0);
                    CHECK(to == 2);
                },
                [&]() -> Co<> {
                    co_await el.Sleep(1ms);
                    v = 2;
                });

            CHECK(el.Now() == 1ms);
        });
    }

    SECTION("`UntilChanged` with specific from and to values")
    {
        Run(el, [&]() -> Co<> {
            Value v { 0 };

            co_await AllOf(
                [&]() -> Co<> {
                    auto [from, to] = co_await v.UntilChanged(0, 3);
                    CHECK(from == 0);
                    CHECK(to == 3);
                },
                [&]() -> Co<> {
                    co_await el.Sleep(1ms);
                    v = 3;
                });

            CHECK(el.Now() == 1ms);
        });
    }

    SECTION("`Get` method returns current value")
    {
        Value v { 5 };
        CHECK(v.Get() == 5);
    }

    SECTION("`conversion operator` returns current value")
    {
        const Value v { 7 };
        const int& value = v;
        CHECK(value == 7);
    }
}

} // namespace

// NOLINTEND(*-avoid-capturing-lambda-coroutines)
// NOLINTEND(*-avoid-do-while)
