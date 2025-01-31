//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <chrono>

#include <gtest/gtest.h>

#include "Oxygen/OxCo/Algorithms.h"
#include "Oxygen/OxCo/Co.h"
#include "Oxygen/OxCo/Coroutine.h"
#include "Oxygen/OxCo/Run.h"
#include "Utils/OxCoTestFixture.h"
#include "Utils/TestEventLoop.h"

using namespace oxygen::co;
using namespace oxygen::co::testing;
using namespace std::chrono_literals;

namespace {

struct ThrowingAwaitable {
    // ReSharper disable CppMemberFunctionMayBeStatic
    // NOLINTNEXTLINE(*-convert-member-functions-to-static)
    [[nodiscard]] auto await_ready() const noexcept -> bool { return false; }
    // NOLINTNEXTLINE(*-convert-member-functions-to-static)
    [[noreturn]] void await_suspend(detail::Handle /*unused*/) { throw std::runtime_error("test"); }
    void await_resume() noexcept { }
    // ReSharper restore CppMemberFunctionMayBeStatic
};

// ReSharper disable CppClangTidyCppcoreguidelinesAvoidCapturingLambdaCoroutines

class ThrowingAwaitableTest : public OxCoTestFixture { };

// NOLINTNEXTLINE
TEST_F(ThrowingAwaitableTest, Immediate)
{
    ::Run(*el_, []() -> Co<> {
        // NOLINTNEXTLINE
        EXPECT_THROW(co_await ThrowingAwaitable {}, std::runtime_error);
    });
}

// NOLINTNEXTLINE
TEST_F(ThrowingAwaitableTest, FirstInsideMux)
{
    ::Run(*el_, [&]() -> Co<> {
        // NOLINTNEXTLINE
        EXPECT_THROW(
            co_await AnyOf(
                ThrowingAwaitable(),
                el_->Sleep(5ms)),
            std::runtime_error);
        EXPECT_EQ(el_->Now(), 0ms);
    });
}

// NOLINTNEXTLINE
TEST_F(ThrowingAwaitableTest, LastInsideMux)
{
    ::Run(*el_, [&]() -> Co<> {
        // NOLINTNEXTLINE
        EXPECT_THROW(
            co_await AnyOf(
                el_->Sleep(5ms),
                ThrowingAwaitable()),
            std::runtime_error);
        EXPECT_EQ(el_->Now(), 0ms);
    });
}

// NOLINTNEXTLINE
TEST_F(ThrowingAwaitableTest, InsideNonCancellableMux)
{
    ::Run(*el_, [&]() -> Co<> {
        // NOLINTNEXTLINE
        EXPECT_THROW(
            co_await AnyOf(
                el_->Sleep(5ms, kNonCancellable),
                ThrowingAwaitable()),
            std::runtime_error);
        EXPECT_EQ(el_->Now(), 5ms);
    });
}

} // namespace
