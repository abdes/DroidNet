//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <chrono>

#include <Oxygen/Testing/GTest.h>

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

class AllOfTest : public OxCoTestFixture { };

// ReSharper disable CppClangTidyCppcoreguidelinesAvoidCapturingLambdaCoroutines

NOLINT_TEST_F(AllOfTest, Smoke)
{
    oxygen::co::Run(*el_, [this]() -> Co<> {
        co_await AllOf(
            el_->Sleep(2ms),
            el_->Sleep(3ms),
            [&]() -> Co<> {
                co_await el_->Sleep(5ms);
            });
        EXPECT_EQ(el_->Now(), 5ms);
    });
}

NOLINT_TEST_F(AllOfTest, Empty)
{
    oxygen::co::Run(*el_, []() -> Co<> {
        [[maybe_unused]] auto r = co_await AllOf();
        static_assert(std::tuple_size_v<decltype(r)> == 0);
    });
}

NOLINT_TEST_F(AllOfTest, ImmediateFront)
{
    oxygen::co::Run(*el_, [this]() -> Co<> {
        co_await AllOf(
            [&]() -> Co<> {
                DLOG_F(INFO, "Immediate co_return");
                co_return;
            },
            [&]() -> Co<> { co_await el_->Sleep(1ms); });
        EXPECT_EQ(el_->Now(), 1ms);
    });
}

NOLINT_TEST_F(AllOfTest, ImmediateBack)
{
    oxygen::co::Run(*el_, [this]() -> Co<> {
        co_await AllOf(
            [&]() -> Co<> { co_await el_->Sleep(1ms); },
            [&]() -> Co<> {
                DLOG_F(INFO, "Immediate co_return");
                co_return;
            });
        EXPECT_EQ(el_->Now(), 1ms);
    });
}

NOLINT_TEST_F(AllOfTest, RetVal)
{
    oxygen::co::Run(*el_, []() -> Co<> {
        auto [a, b] = co_await AllOf(
            []() -> Co<int> { co_return 42; },
            []() -> Co<int> { co_return 43; });
        EXPECT_EQ(a, 42);
        EXPECT_EQ(b, 43);
    });
}

NOLINT_TEST_F(AllOfTest, Exception)
{
    oxygen::co::Run(*el_, [this]() -> Co<> {
        bool cancelled = false;
        NOLINT_EXPECT_THROW(
            co_await AllOf(
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
