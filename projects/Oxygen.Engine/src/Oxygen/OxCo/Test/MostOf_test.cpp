//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "Oxygen/OxCo/Algorithms.h"
#include "Oxygen/OxCo/Awaitables.h"
#include "Oxygen/OxCo/Co.h"
#include "Oxygen/OxCo/Run.h"
#include "Utils/OxCoTestFixture.h"
#include "Utils/TestEventLoop.h"

using namespace std::chrono_literals;
using namespace oxygen::co::detail;
using namespace oxygen::co;
using namespace oxygen::co::testing;

namespace {

constexpr NonCancellableTag kNonCancellable;

class MostOfTest : public OxCoTestFixture { };

// ReSharper disable CppClangTidyCppcoreguidelinesAvoidCapturingLambdaCoroutines

TEST_F(MostOfTest, Smoke)
{
    oxygen::co::Run(*el_, [this]() -> Co<> {
        co_await MostOf(
            el_->Sleep(2ms),
            el_->Sleep(3ms),
            [&]() -> Co<> { co_await el_->Sleep(5ms); });
        EXPECT_EQ(el_->Now(), 5ms);
    });
}

TEST_F(MostOfTest, Empty)
{
    oxygen::co::Run(*el_, []() -> Co<> {
        [[maybe_unused]] auto r = co_await MostOf();
        static_assert(std::tuple_size_v<decltype(r)> == 0);
    });
}

TEST_F(MostOfTest, RetVal)
{
    oxygen::co::Run(*el_, []() -> Co<> {
        auto [a, b] = co_await MostOf(
            []() -> Co<int> { co_return 42; },
            []() -> Co<int> { co_return 43; });
        EXPECT_EQ(*a, 42);
        EXPECT_EQ(*b, 43);
    });
}

TEST_F(MostOfTest, NonCancellable)
{
    oxygen::co::Run(*el_, [this]() -> Co<> {
        bool resumed = false;
        auto sub = [&]() -> Co<> {
            auto [a, b, c] = co_await MostOf(
                [&]() -> Co<int> { co_return 42; },
                el_->Sleep(3ms, kNonCancellable),
                el_->Sleep(5ms));
            EXPECT_TRUE(a);
            EXPECT_EQ(*a, 42);
            EXPECT_TRUE(b);
            EXPECT_FALSE(c);
            resumed = true;
        };
        co_await AnyOf(sub(), el_->Sleep(1ms));
        EXPECT_EQ(el_->Now(), 3ms);
        EXPECT_TRUE(resumed);
    });
}

TEST_F(MostOfTest, Exception)
{
    oxygen::co::Run(*el_, [this]() -> Co<> {
        bool cancelled = false;
        EXPECT_THROW(
            co_await MostOf(
                [&]() -> Co<> {
                    ScopeGuard guard([&]() noexcept { cancelled = true; });
                    co_await SuspendForever {};
                },
                [&]() -> Co<> {
                    co_await el_->Sleep(1ms);
                    throw std::runtime_error("boo!");
                });
            , std::runtime_error);
        EXPECT_TRUE(cancelled);
    });
}

} // namespace
