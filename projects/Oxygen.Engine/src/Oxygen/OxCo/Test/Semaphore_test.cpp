//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/OxCo/Semaphore.h>

#include <catch2/catch_test_macros.hpp>
#include <chrono>

#include "Utils/TestEventLoop.h"
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Nursery.h>
#include <Oxygen/OxCo/Run.h>

using namespace std::chrono_literals;
using oxygen::co::Co;
using oxygen::co::kJoin;
using oxygen::co::Run;
using oxygen::co::Semaphore;
using oxygen::co::testing::TestEventLoop;

// NOLINTBEGIN(*-avoid-do-while)
// NOLINTBEGIN(*-avoid-capturing-lambda-coroutines)

namespace {

TEST_CASE("Semaphore")
{
  TestEventLoop el;
  Run(el, [&]() -> Co<> {
    SECTION("basic operation")
    {
      Semaphore sem(5);
      int concurrency = 0;
      auto worker = [&]() -> Co<> {
        auto lk = co_await sem.Lock();
        ++concurrency;
        CHECK(concurrency <= 5);
        co_await el.Sleep(1ms);
        --concurrency;
      };
      OXCO_WITH_NURSERY(nursery)
      {
        for (int i = 0; i != 20; ++i) {
          nursery.Start(worker);
        }
        co_return kJoin;
      };
      CHECK(el.Now() == 4ms);
    }

    SECTION("initialization")
    {
      Semaphore sem1(1);
      CHECK(sem1.Value() == 1);

      Semaphore sem2(10);
      CHECK(sem2.Value() == 10);

      Semaphore sem3(0);
      CHECK(sem3.Value() == 0);
    }

    SECTION("acquire and release")
    {
      Semaphore sem(1);
      CHECK(sem.Value() == 1);

      co_await sem.Acquire();
      CHECK(sem.Value() == 0);

      sem.Release();
      CHECK(sem.Value() == 1);
    }

    SECTION("lock guard move semantics")
    {
      Semaphore sem(1);
      auto lk1 = co_await sem.Lock();
      CHECK(sem.Value() == 0);

      auto lk2 = std::move(lk1);
      CHECK(sem.Value() == 0);
    }

    SECTION("zero initial value")
    {
      Semaphore sem(0);
      bool acquired = false;

      auto worker = [&]() -> Co<> {
        co_await sem.Acquire();
        acquired = true;
      };

      OXCO_WITH_NURSERY(nursery)
      {
        nursery.Start(worker);
        co_await el.Sleep(1ms);
        CHECK(!acquired);

        sem.Release();
        co_await el.Sleep(1ms);
        CHECK(acquired);

        co_return kJoin;
      };
    }

    SECTION("multiple releases")
    {
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

        co_await el.Sleep(1ms);
        CHECK(acquired_count == 0);

        sem.Release();
        co_await el.Sleep(1ms);
        CHECK(acquired_count == 1);

        sem.Release();
        sem.Release();
        co_await el.Sleep(1ms);
        CHECK(acquired_count == 3);

        co_return kJoin;
      };
    }

    SECTION("immediate cancellation")
    {
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
        co_await el.Sleep(1ms);
        CHECK(cancelled);

        co_return kJoin;
      };
    }
  });
}

} // namespace

// NOLINTEND(*-avoid-capturing-lambda-coroutines)
// NOLINTEND(*-avoid-do-while)
