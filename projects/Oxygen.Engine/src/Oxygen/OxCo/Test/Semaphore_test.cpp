//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/OxCo/Semaphore.h>

#include <chrono>

#include <Oxygen/Testing/GTest.h>
#include <gmock/gmock.h>

#include "Utils/OxCoTestFixture.h"
#include "Utils/TestEventLoop.h"
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Nursery.h>
#include <Oxygen/OxCo/Run.h>

using namespace std::chrono_literals;
using oxygen::co::Co;
using oxygen::co::kJoin;
using oxygen::co::Run;
using oxygen::co::Semaphore;

// NOLINTBEGIN(*-avoid-do-while)
// NOLINTBEGIN(*-avoid-capturing-lambda-coroutines)

namespace {

class SemaphoreTest : public oxygen::co::testing::OxCoTestFixture { };

NOLINT_TEST_F(SemaphoreTest, BasicOperation)
{
  ::Run(*el_, [&]() -> Co<> {
    Semaphore sem(5);
    int concurrency = 0;
    auto worker = [&]() -> Co<> {
      auto lk = co_await sem.Lock();
      ++concurrency;
      EXPECT_LE(concurrency, 5);
      co_await el_->Sleep(1ms);
      --concurrency;
    };
    OXCO_WITH_NURSERY(nursery)
    {
      for (int i = 0; i != 20; ++i) {
        nursery.Start(worker);
      }
      co_return kJoin;
    };
  });
  EXPECT_EQ(el_->Now(), 4ms);
}

NOLINT_TEST_F(SemaphoreTest, Initialization)
{
  Semaphore sem1(1);
  EXPECT_EQ(sem1.Value(), 1);

  Semaphore sem2(10);
  EXPECT_EQ(sem2.Value(), 10);

  Semaphore sem3(0);
  EXPECT_EQ(sem3.Value(), 0);
}

NOLINT_TEST_F(SemaphoreTest, AcquireAndRelease)
{
  ::Run(*el_, [&]() -> Co<> {
    Semaphore sem(1);
    EXPECT_EQ(sem.Value(), 1);

    co_await sem.Acquire();
    EXPECT_EQ(sem.Value(), 0);

    sem.Release();
    EXPECT_EQ(sem.Value(), 1);
  });
}

NOLINT_TEST_F(SemaphoreTest, LockGuardMoveSemantics)
{
  ::Run(*el_, [&]() -> Co<> {
    Semaphore sem(1);
    auto lk1 = co_await sem.Lock();
    EXPECT_EQ(sem.Value(), 0);

    auto lk2 = std::move(lk1);
    EXPECT_EQ(sem.Value(), 0);
  });
}

NOLINT_TEST_F(SemaphoreTest, ZeroInitialValue)
{
  ::Run(*el_, [&]() -> Co<> {
    Semaphore sem(0);
    bool acquired = false;

    auto worker = [&]() -> Co<> {
      co_await sem.Acquire();
      acquired = true;
    };

    OXCO_WITH_NURSERY(nursery)
    {
      nursery.Start(worker);
      co_await el_->Sleep(1ms);
      EXPECT_FALSE(acquired);

      sem.Release();
      co_await el_->Sleep(1ms);
      EXPECT_TRUE(acquired);

      co_return kJoin;
    };
  });
}

NOLINT_TEST_F(SemaphoreTest, MultipleReleases)
{
  ::Run(*el_, [&]() -> Co<> {
    Semaphore sem(0);
    int acquired_count = 0;

    auto worker = [&]() -> Co<> {
      co_await sem.Acquire();
      ++acquired_count;
    };

    OXCO_WITH_NURSERY(nursery)
    {
      for (int i = 0; i != 3; ++i) {
        nursery.Start(worker);
      }

      co_await el_->Sleep(1ms);
      EXPECT_EQ(acquired_count, 0);

      sem.Release();
      co_await el_->Sleep(1ms);
      EXPECT_EQ(acquired_count, 1);

      sem.Release();
      sem.Release();
      co_await el_->Sleep(1ms);
      EXPECT_EQ(acquired_count, 3);

      co_return kJoin;
    };
  });
}

NOLINT_TEST_F(SemaphoreTest, ImmediateCancellation)
{
  ::Run(*el_, [&]() -> Co<> {
    Semaphore sem(1);
    bool cancelled = false;

    auto worker = [&]() -> Co<> {
      auto lk = co_await sem.Lock();
      cancelled = true;
    };

    OXCO_WITH_NURSERY(nursery)
    {
      nursery.Start(worker);
      nursery.Cancel();
      co_await el_->Sleep(1ms);
      EXPECT_TRUE(cancelled);

      co_return kJoin;
    };
  });
}

} // namespace

// NOLINTEND(*-avoid-capturing-lambda-coroutines)
// NOLINTEND(*-avoid-do-while)
