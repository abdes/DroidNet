//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <chrono>

#include <Oxygen/Testing/GTest.h>

#include "Utils/OxCoTestFixture.h"
#include "Utils/TestEventLoop.h"
#include <Oxygen/OxCo/Algorithms.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Coroutine.h>
#include <Oxygen/OxCo/Run.h>

using oxygen::co::AnyOf;
using oxygen::co::Co;
using oxygen::co::Run;
using oxygen::co::testing::OxCoTestFixture;
using namespace std::chrono_literals;

namespace {

struct ThrowingAwaitable {
    // ReSharper disable CppMemberFunctionMayBeStatic
    // NOLINTNEXTLINE(*-convert-member-functions-to-static)
    [[nodiscard]] auto await_ready() const noexcept -> bool { return false; }
    // NOLINTNEXTLINE(*-convert-member-functions-to-static)
    [[noreturn]] void await_suspend(oxygen::co::detail::Handle /*unused*/) { throw std::runtime_error("test"); }
    void await_resume() noexcept { }
    // ReSharper restore CppMemberFunctionMayBeStatic
};

// ReSharper disable CppClangTidyCppcoreguidelinesAvoidCapturingLambdaCoroutines

class ThrowingAwaitableTest : public OxCoTestFixture { };

NOLINT_TEST_F(ThrowingAwaitableTest, Immediate)
{
    ::Run(*el_, []() -> Co<> {
        // NOLINTNEXTLINE
        EXPECT_THROW(co_await ThrowingAwaitable {}, std::runtime_error);
    });
}

NOLINT_TEST_F(ThrowingAwaitableTest, FirstInsideMux)
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

NOLINT_TEST_F(ThrowingAwaitableTest, LastInsideMux)
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

NOLINT_TEST_F(ThrowingAwaitableTest, InsideNonCancellableMux)
{
    ::Run(*el_, [&]() -> Co<> {
        // NOLINTNEXTLINE
        EXPECT_THROW(
            co_await AnyOf(
                el_->Sleep(5ms, oxygen::co::testing::kNonCancellable),
                ThrowingAwaitable()),
            std::runtime_error);
        EXPECT_EQ(el_->Now(), 5ms);
    });
}

} // namespace
