//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include "Oxygen/OxCo/Awaitables.h"
#include "Oxygen/OxCo/Co.h"
#include "Oxygen/OxCo/Run.h"
#include "Utils/OxCoTestFixture.h"

using namespace oxygen::co;
using namespace oxygen::co::testing;

namespace {

class OxCoBasicTest : public OxCoTestFixture { };

NOLINT_TEST_F(OxCoBasicTest, SmokeTest)
{
    ::Run(*el_, []() -> Co<> {
        auto one = [&]() -> Co<int> { co_return 1; };
        auto two = [&]() -> Co<int> { co_return 2; };
        auto three = [&]() -> Co<int> { co_return 3; };

        auto six = [&]() -> Co<int> { // NOLINT(cppcoreguidelines-avoid-capturing-lambda-coroutines)
            const int x = co_await one;
            const int y = co_await two;
            const int z = co_await three;
            co_return x + y + z;
        };

        const int ret = co_await six;
        EXPECT_EQ(ret, 6);
    });
}

NOLINT_TEST_F(OxCoBasicTest, AwaitableTask)
{
    ::Run(*el_, []() -> Co<> {
        auto task_lambda = []() -> Co<int> { co_return 42; };
        Co<int> task = task_lambda();
        const int x = co_await task;
        EXPECT_EQ(x, 42);
    });
}

NOLINT_TEST_F(OxCoBasicTest, AwaitableCallable)
{
    ::Run(*el_, []() -> Co<> {
        const int x = co_await []() -> Co<int> { co_return 43; };
        EXPECT_EQ(x, 43);
    });
}

NOLINT_TEST_F(OxCoBasicTest, ReturnTypes)
{
    ::Run(*el_, []() -> Co<> {
        int x = 42;
        // ReSharper disable once CppVariableCanBeMadeConstexpr
        const int cx = 43; // no constexpr for test

        auto ref_task = [&]() -> Co<int&> { co_return x; };
        int& ref = co_await ref_task();
        EXPECT_EQ(&ref, &x);

        auto cref_task = [&]() -> Co<const int&> { co_return cx; };
        const int& cref = co_await cref_task();
        EXPECT_EQ(&cref, &cx);

        auto rv_task = [&]() -> Co<int&&> { co_return std::move(x); };
        int&& rv = co_await rv_task();
        EXPECT_EQ(&rv, &x);
    });
}

NOLINT_TEST_F(OxCoBasicTest, Frames)
{
    ::Run(*el_, []() -> Co<> {
        using detail::CoroutineFrame;
        using detail::FrameCast;
        using detail::ProxyFrame;
        using detail::TaskFrame;

        CoroutineFrame f1;
        EXPECT_EQ(FrameCast<ProxyFrame>(&f1), nullptr);
        EXPECT_EQ(FrameCast<TaskFrame>(&f1), nullptr);

        ProxyFrame f2;
        CoroutineFrame* f3 = &f2;
        EXPECT_EQ(FrameCast<ProxyFrame>(f3), &f2);
        EXPECT_EQ(FrameCast<TaskFrame>(f3), nullptr);
        EXPECT_EQ(f2.FollowLink(), nullptr);

        f2.LinkTo(f1.ToHandle());
        EXPECT_EQ(f2.FollowLink(), f1.ToHandle());

        TaskFrame f4;
        CoroutineFrame* f5 = &f4;
        ProxyFrame* f6 = &f4;
        EXPECT_EQ(FrameCast<ProxyFrame>(f5), &f4);
        EXPECT_EQ(FrameCast<TaskFrame>(f5), &f4);
        EXPECT_EQ(FrameCast<TaskFrame>(f6), &f4);
        EXPECT_EQ(f4.FollowLink(), nullptr);

        f4.LinkTo(f2.ToHandle());
        EXPECT_EQ(f4.FollowLink(), f2.ToHandle());

        co_return;
    });
}

NOLINT_TEST_F(OxCoBasicTest, Exceptions)
{
    ::Run(*el_, []() -> Co<> {
        auto bad = [&]() -> Co<> {
            co_await std::suspend_never();
            throw std::runtime_error("boo!");
        };
        NOLINT_EXPECT_THROW(co_await bad, std::runtime_error);
    });
}

NOLINT_TEST_F(OxCoBasicTest, Just)
{
    ::Run(*el_, []() -> Co<> {
        auto mk_async = [](int n) { return [n]() -> Co<int> { return Just(n); }; };
        const auto x = co_await mk_async(42)();
        EXPECT_EQ(x, 42);

        int i;
        int& ri = co_await Just<int&>(i);
        EXPECT_EQ(&ri, &i);

        auto p = std::make_unique<int>(42);
        auto q = co_await Just(std::move(p));
        EXPECT_EQ(p, nullptr);
        EXPECT_EQ(*q, 42);

        const auto& rq = co_await Just<std::unique_ptr<int>&>(q);
        EXPECT_EQ(*q, 42);
        EXPECT_EQ(*rq, 42);
    });
}

NOLINT_TEST_F(OxCoBasicTest, NoOp)
{
    ::Run(*el_, []() -> Co<> {
        auto mk_noop = []() -> Co<> { return NoOp(); };
        co_await NoOp();
        co_await mk_noop();
    });
}
// ReSharper disable CppMemberFunctionMayBeStatic
// ReSharper disable CppClangTidyCppcoreguidelinesAvoidCapturingLambdaCoroutines
// NOLINTBEGIN(*-convert-member-functions-to-static, *-use-nodiscard, *-special-member-functions)

struct NonMoveableAwaiter {
    NonMoveableAwaiter() = default;
    OXYGEN_MAKE_NON_MOVEABLE(NonMoveableAwaiter)

    auto await_ready() const noexcept -> bool { return true; }
    auto await_suspend(detail::Handle) -> bool { return false; }
    auto await_resume() -> int { return 42; }
};

struct NonMoveableAwaitable {
    NonMoveableAwaitable() = default;
    OXYGEN_MAKE_NON_MOVEABLE(NonMoveableAwaitable)

    auto operator co_await() const -> NonMoveableAwaiter { return {}; }
};

// NOLINTEND(*-convert-member-functions-to-static, *-use-nodiscard, *-special-member-functions)
// ReSharper enable CppClangTidyCppcoreguidelinesAvoidCapturingLambdaCoroutines
// ReSharper enable CppMemberFunctionMayBeStatic

NOLINT_TEST_F(OxCoBasicTest, NonMoveable)
{
    // Silence clang-tidy warnings about un-needed default constructors
    [[maybe_unused]] NonMoveableAwaiter _1;
    [[maybe_unused]] NonMoveableAwaitable _2;

    ::Run(*el_, []() -> Co<> {
        int v = co_await NonMoveableAwaiter {};
        EXPECT_EQ(v, 42);

        v = co_await NonMoveableAwaitable {};
        EXPECT_EQ(v, 42);
    });
}

} // namespace
