//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include "Utils/OxCoTestFixture.h"
#include "Utils/TestEventLoop.h"
#include <Oxygen/OxCo/Algorithms.h>
#include <Oxygen/OxCo/Awaitables.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Detail/ScopeGuard.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/OxCo/Shared.h>

using namespace std::chrono_literals;
using oxygen::co::Co;
using oxygen::co::Shared;
using oxygen::co::detail::ReadyAwaiter;
using oxygen::co::detail::ScopeGuard;
using oxygen::co::testing::kNonCancellable;
using oxygen::co::testing::OxCoTestFixture;

namespace {

class SharedTest : public OxCoTestFixture {
protected:
  using UseFunction = std::function<Co<int>(milliseconds)>;
  using SharedProducer = std::function<Co<int>()>;

  Shared<SharedProducer> shared_ {};
  std::unique_ptr<UseFunction> use_ {};

  void SetUp() override
  {
    OxCoTestFixture::SetUp();

    shared_ = Shared<SharedProducer>(std::in_place, [&]() -> Co<int> {
      co_await el_->Sleep(5ms);
      co_return 42;
    });

    use_ = std::make_unique<UseFunction>(
      [&](const milliseconds delay = 0ms) -> Co<int> {
        if (delay != 0ms) {
          co_await el_->Sleep(delay);
        }
        int ret = co_await shared_;
        EXPECT_EQ(el_->Now(), 5ms);
        EXPECT_EQ(ret, 42);
        co_return ret;
      });
  }

  [[nodiscard]] auto Use(const milliseconds delay = 0ms) const -> Co<int>
  {
    return (*use_)(delay);
  }
};

// ReSharper disable CppClangTidyCppcoreguidelinesAvoidCapturingLambdaCoroutines

NOLINT_TEST_F(SharedTest, Smoke)
{
  oxygen::co::Run(*el_, [this]() -> Co<> {
    auto [x, y] = co_await AllOf(Use(), Use(1ms));
    EXPECT_EQ(x, 42);
    EXPECT_EQ(y, 42);
    EXPECT_EQ(el_->Now(), 5ms);
  });
}

NOLINT_TEST_F(SharedTest, Cancellation)
{
  oxygen::co::Run(*el_, [this]() -> Co<> {
    co_await AnyOf(AllOf(Use(), Use(1ms)), el_->Sleep(3ms));
    EXPECT_EQ(el_->Now(), 3ms);
  });
}

class SharedNoCancelTest : public OxCoTestFixture { };

NOLINT_TEST_F(SharedNoCancelTest, Completes)
{

  oxygen::co::Run(*el_, [&]() -> Co<> {
    auto shared = Shared([&]() -> Co<> {
      auto check = ScopeGuard([&]() noexcept { EXPECT_EQ(el_->Now(), 5ms); });
      co_await el_->Sleep(10ms);
    });

    auto first = [&]() -> Co<> {
      co_await AnyOf(shared, el_->Sleep(2ms));
      EXPECT_EQ(el_->Now(), 2ms);
    };

    auto second = [&]() -> Co<> {
      co_await el_->Sleep(1ms);
      co_await AnyOf(shared, el_->Sleep(4ms));
      EXPECT_EQ(el_->Now(), 5ms);
    };

    co_await AllOf(first(), second());
  });
}

struct MyAwaitable {
  int value;
  MyAwaitable(int v)
    : value(v)
  {
  }

  ReadyAwaiter<int> operator co_await() noexcept
  {
    return ReadyAwaiter<int>(std::forward<int>(value));
  }
};

NOLINT_TEST_F(SharedTest, InPlaceConstructor)
{
  oxygen::co::Run(*el_, [&]() -> Co<> {
    Shared<MyAwaitable> shared(std::in_place, 123);
    int result = co_await shared;
    EXPECT_EQ(result, 123);
  });
}

class SharedCancelWaitTest : public OxCoTestFixture { };

NOLINT_TEST_F(SharedCancelWaitTest, IntResult)
{
  oxygen::co::Run(*el_, [&]() -> Co<> {
    auto shared = Shared([&]() -> Co<int> {
      co_await el_->Sleep(5ms, kNonCancellable);
      co_await el_->Sleep(5ms);
      co_return 42;
    });

    co_await AllOf(AnyOf(shared, el_->Sleep(3ms)), [&]() -> Co<> {
      co_await el_->Sleep(4ms);
      auto res = co_await shared.AsOptional();
      EXPECT_FALSE(res);
    });
  });
}

NOLINT_TEST_F(SharedCancelWaitTest, VoidResult)
{
  oxygen::co::Run(*el_, [&]() -> Co<> {
    auto shared = Shared([&]() -> Co<> {
      co_await el_->Sleep(5ms, kNonCancellable);
      co_await el_->Sleep(5ms);
    });

    co_await AllOf(AnyOf(shared, el_->Sleep(3ms)), [&]() -> Co<> {
      co_await el_->Sleep(4ms);
      auto res = co_await shared.AsOptional();
      EXPECT_FALSE(res);
    });
  });
}

} // namespace
