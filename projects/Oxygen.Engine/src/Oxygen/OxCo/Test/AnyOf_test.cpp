//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <chrono>

#include <Oxygen/Testing/GTest.h>

#include "Utils/OxCoTestFixture.h"
#include "Utils/TestEventLoop.h"
#include <Oxygen/Base/Unreachable.h>
#include <Oxygen/OxCo/Algorithms.h>
#include <Oxygen/OxCo/Awaitables.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Run.h>

using namespace std::chrono_literals;
using namespace oxygen::co::detail;
using namespace oxygen::co;
using namespace oxygen::co::testing;

namespace {

// ReSharper disable CppMemberFunctionMayBeStatic
struct Ready {
  // NOLINTNEXTLINE(*-convert-member-functions-to-static)
  [[maybe_unused]] auto await_early_cancel() noexcept -> bool { return false; }
  // NOLINTNEXTLINE(*-convert-member-functions-to-static)
  [[nodiscard]] auto await_ready() const noexcept { return true; }
  // NOLINTNEXTLINE(*-convert-member-functions-to-static)
  [[noreturn]] void await_suspend(Handle) noexcept { oxygen::Unreachable(); }
  void await_resume() noexcept { }
  // NOLINTNEXTLINE(*-convert-member-functions-to-static)
  [[maybe_unused]] auto await_cancel(Handle /*unused*/) noexcept -> bool
  {
    return false;
  }
  // NOLINTNEXTLINE(*-convert-member-functions-to-static)
  [[nodiscard]] [[maybe_unused]] auto await_must_resume() const noexcept -> bool
  {
    return true;
  }
};

struct ReadyCancellable {
  // NOLINTNEXTLINE(*-convert-member-functions-to-static)
  [[nodiscard]] auto await_ready() const noexcept { return true; }
  // NOLINTNEXTLINE(*-convert-member-functions-to-static)
  [[noreturn]] void await_suspend(Handle) noexcept { oxygen::Unreachable(); }
  void await_resume() noexcept { }
  // NOLINTNEXTLINE(*-convert-member-functions-to-static)
  [[maybe_unused]] auto await_cancel(Handle) noexcept
  {
    return std::true_type {};
  }
};
// ReSharper restore CppMemberFunctionMayBeStatic

class AnyOfTest : public OxCoTestFixture { };

// ReSharper disable CppClangTidyCppcoreguidelinesAvoidCapturingLambdaCoroutines

NOLINT_TEST_F(AnyOfTest, Smoke)
{
  oxygen::co::Run(*el_, [this]() -> Co<> {
    auto [a, b, c] = co_await AnyOf(el_->Sleep(2ms), el_->Sleep(3ms),
      [&]() -> Co<> { co_await el_->Sleep(5ms); });
    EXPECT_TRUE(a);
    EXPECT_FALSE(b);
    EXPECT_FALSE(c);
    EXPECT_EQ(el_->Now(), 2ms);
  });
}

NOLINT_TEST_F(AnyOfTest, Empty)
{
  oxygen::co::Run(*el_, []() -> Co<> {
    [[maybe_unused]] auto r = co_await AnyOf();
    static_assert(std::tuple_size_v<decltype(r)> == 0);
  });
}

NOLINT_TEST_F(AnyOfTest, ImmediateFront)
{
  oxygen::co::Run(*el_, [this]() -> Co<> {
    co_await AnyOf(
      [&]() -> Co<> {
        DLOG_F(INFO, "Immediate co_return");
        co_return;
      },
      [&]() -> Co<> { co_await el_->Sleep(1ms); });
    EXPECT_EQ(el_->Now(), 0ms);
  });
}

NOLINT_TEST_F(AnyOfTest, ImmediateBack)
{
  oxygen::co::Run(*el_, [this]() -> Co<> {
    co_await AnyOf([&]() -> Co<> { co_await el_->Sleep(1ms); },
      [&]() -> Co<> {
        DLOG_F(INFO, "Immediate co_return");
        co_return;
      });
    EXPECT_EQ(el_->Now(), 0ms);
  });
}

NOLINT_TEST_F(AnyOfTest, ImmediateBoth)
{
  oxygen::co::Run(*el_, [this]() -> Co<> {
    auto [a, b] = co_await AnyOf(Ready {}, Ready {});
    EXPECT_EQ(el_->Now(), 0ms);
    EXPECT_TRUE(a);
    EXPECT_TRUE(b);

    std::tie(a, b) = co_await AnyOf(ReadyCancellable {}, ReadyCancellable {});
    EXPECT_EQ(el_->Now(), 0ms);
    EXPECT_TRUE(a);
    EXPECT_FALSE(b);
  });
}

NOLINT_TEST_F(AnyOfTest, NonCancellable)
{
  oxygen::co::Run(*el_, [this]() -> Co<> {
    auto [a, b, c] = co_await AnyOf(
      el_->Sleep(2ms), el_->Sleep(3ms, kNonCancellable), el_->Sleep(5ms));
    EXPECT_EQ(el_->Now(), 3ms);
    EXPECT_TRUE(a);
    EXPECT_TRUE(b);
    EXPECT_FALSE(c);
  });
}

NOLINT_TEST_F(AnyOfTest, ReturnRef)
{
  oxygen::co::Run(*el_, [this]() -> Co<> {
    int x = 42;
    auto [lx, s1]
      = co_await AnyOf([&]() -> Co<int&> { co_return x; }, el_->Sleep(2ms));
    EXPECT_EQ(&*lx, &x);
    EXPECT_FALSE(s1);

    auto [rx, s2] = co_await AnyOf(
      [&]() -> Co<int&&> { co_return std::move(x); }, el_->Sleep(2ms));
    EXPECT_EQ(&*rx, &x);
    EXPECT_FALSE(s2);
  });
}

NOLINT_TEST_F(AnyOfTest, ImmediateLambda)
{
  oxygen::co::Run(*el_, [this]() -> Co<> {
    co_await AnyOf(Ready {}, [&]() -> Co<> { co_await el_->Sleep(1ms); });
    EXPECT_EQ(el_->Now(), 0ms);
  });
}

NOLINT_TEST_F(AnyOfTest, Exception)
{
  oxygen::co::Run(*el_, [this]() -> Co<> {
    bool cancelled = false;
    NOLINT_EXPECT_THROW(co_await AnyOf(
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
