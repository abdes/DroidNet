//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//
// Covers: Content/Internal/PayloadBudget.h

#include <chrono>
#include <functional>
#include <utility>
#include <vector>

#include <Oxygen/Content/Internal/PayloadBudget.h>
#include <Oxygen/OxCo/Algorithms.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Nursery.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/OxCo/test/Utils/TestEventLoop.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using namespace std::chrono_literals;
using oxygen::co::Co;
using oxygen::co::kJoin;
using oxygen::co::testing::TestEventLoop;
using oxygen::content::internal::PayloadBudget;
using oxygen::serio::ByteCount;

constexpr auto kCapacity = ByteCount { 100 };

//! The leases of a test's loads, in the order their loads were granted.
struct Grants {
  std::vector<int> order;
  std::vector<PayloadBudget::Lease> leases;
};

//! A load that leases `bytes`, records `id` when granted, and keeps its lease
//! in `grants` until the test returns it.
auto Load(PayloadBudget& budget, const ByteCount bytes, const int id,
  Grants& grants) -> Co<>
{
  auto lease = co_await budget.Acquire(bytes);
  grants.order.push_back(id);
  grants.leases.push_back(std::move(lease));
}

//! A load that fits is granted at once.
NOLINT_TEST(PayloadBudgetTest, GrantsALoadThatFits)
{
  TestEventLoop loop;
  auto budget = PayloadBudget(kCapacity);

  oxygen::co::Run(loop, [&]() -> Co<> {
    const auto lease = co_await budget.Acquire(ByteCount { 60 });

    EXPECT_EQ(lease.Bytes(), ByteCount { 60 });
    EXPECT_EQ(budget.InUse(), ByteCount { 60 });
  });

  EXPECT_EQ(budget.InUse(), ByteCount { 0 });
}

//! Loads that do not fit wait, and are granted in arrival order as leases
//! return.
NOLINT_TEST(PayloadBudgetTest, GrantsWaitingLoadsInArrivalOrder)
{
  TestEventLoop loop;
  auto budget = PayloadBudget(kCapacity);
  auto grants = Grants {};

  oxygen::co::Run(loop, [&]() -> Co<> {
    OXCO_WITH_NURSERY(nursery)
    {
      nursery.Start(
        Load, std::ref(budget), ByteCount { 80 }, 1, std::ref(grants));
      nursery.Start(
        Load, std::ref(budget), ByteCount { 50 }, 2, std::ref(grants));
      nursery.Start(
        Load, std::ref(budget), ByteCount { 40 }, 3, std::ref(grants));
      co_await loop.Sleep(1ms);
      EXPECT_EQ(grants.order, (std::vector { 1 }));
      EXPECT_EQ(budget.Waiting(), 2U);

      grants.leases.clear();
      co_await loop.Sleep(1ms);
      EXPECT_EQ(grants.order, (std::vector { 1, 2, 3 }));
      EXPECT_EQ(budget.InUse(), ByteCount { 90 });
      grants.leases.clear();
      co_return kJoin;
    };
  });

  EXPECT_EQ(budget.InUse(), ByteCount { 0 });
}

//! A load that would fit waits behind an earlier one that does not.
NOLINT_TEST(PayloadBudgetTest, ALaterLoadDoesNotBargeAhead)
{
  TestEventLoop loop;
  auto budget = PayloadBudget(kCapacity);
  auto grants = Grants {};

  oxygen::co::Run(loop, [&]() -> Co<> {
    OXCO_WITH_NURSERY(nursery)
    {
      nursery.Start(
        Load, std::ref(budget), ByteCount { 70 }, 1, std::ref(grants));
      nursery.Start(
        Load, std::ref(budget), ByteCount { 60 }, 2, std::ref(grants));
      nursery.Start(
        Load, std::ref(budget), ByteCount { 10 }, 3, std::ref(grants));
      co_await loop.Sleep(1ms);

      // 10 bytes would fit beside the first load, but the second waits first.
      EXPECT_EQ(grants.order, (std::vector { 1 }));
      EXPECT_EQ(budget.Waiting(), 2U);
      grants.leases.clear();
      co_await loop.Sleep(1ms);
      EXPECT_EQ(grants.order, (std::vector { 1, 2, 3 }));
      grants.leases.clear();
      co_return kJoin;
    };
  });
}

//! A payload larger than the budget loads alone: when nothing else is in
//! flight, and nothing else while it is.
NOLINT_TEST(PayloadBudgetTest, ServesAnOversizedLoadAlone)
{
  TestEventLoop loop;
  auto budget = PayloadBudget(kCapacity);
  auto grants = Grants {};

  oxygen::co::Run(loop, [&]() -> Co<> {
    OXCO_WITH_NURSERY(nursery)
    {
      nursery.Start(
        Load, std::ref(budget), ByteCount { 30 }, 1, std::ref(grants));
      nursery.Start(
        Load, std::ref(budget), ByteCount { 250 }, 2, std::ref(grants));
      nursery.Start(
        Load, std::ref(budget), ByteCount { 10 }, 3, std::ref(grants));
      co_await loop.Sleep(1ms);
      EXPECT_EQ(grants.order, (std::vector { 1 }));

      grants.leases.clear();
      co_await loop.Sleep(1ms);
      EXPECT_EQ(grants.order, (std::vector { 1, 2 }));
      EXPECT_EQ(budget.InUse(), ByteCount { 250 });

      grants.leases.clear();
      co_await loop.Sleep(1ms);
      EXPECT_EQ(grants.order, (std::vector { 1, 2, 3 }));
      grants.leases.clear();
      co_return kJoin;
    };
  });
}

//! A cancelled waiter leaves the queue; the loads behind it are served as if
//! it had never come.
NOLINT_TEST(PayloadBudgetTest, ACancelledWaiterLeavesTheQueue)
{
  TestEventLoop loop;
  auto budget = PayloadBudget(kCapacity);
  auto grants = Grants {};

  oxygen::co::Run(loop, [&]() -> Co<> {
    OXCO_WITH_NURSERY(nursery)
    {
      nursery.Start(
        Load, std::ref(budget), ByteCount { 90 }, 1, std::ref(grants));
      // Waits for 100 bytes until its timeout cancels it.
      nursery.Start([&]() -> Co<> {
        co_await oxygen::co::AnyOf(
          budget.Acquire(ByteCount { 100 }), loop.Sleep(1ms));
      });
      nursery.Start(
        Load, std::ref(budget), ByteCount { 10 }, 3, std::ref(grants));
      co_await loop.Sleep(2ms);

      // The cancelled waiter no longer blocks the 10 bytes behind it.
      EXPECT_EQ(budget.Waiting(), 0U);
      EXPECT_EQ(grants.order, (std::vector { 1, 3 }));
      grants.leases.clear();
      co_return kJoin;
    };
  });

  EXPECT_EQ(budget.InUse(), ByteCount { 0 });
}

//! A moved lease returns its bytes once: the moved-from lease holds none, and
//! a lease assigned over returns its own bytes first.
NOLINT_TEST(PayloadBudgetTest, AMovedLeaseReturnsItsBytesOnce)
{
  TestEventLoop loop;
  auto budget = PayloadBudget(kCapacity);

  oxygen::co::Run(loop, [&]() -> Co<> {
    auto first = co_await budget.Acquire(ByteCount { 30 });
    auto moved = PayloadBudget::Lease(std::move(first));
    EXPECT_EQ(moved.Bytes(), ByteCount { 30 });
    EXPECT_EQ(budget.InUse(), ByteCount { 30 });

    auto second = co_await budget.Acquire(ByteCount { 20 });
    EXPECT_EQ(budget.InUse(), ByteCount { 50 });
    moved = std::move(second);
    // The 30 bytes `moved` held are back; the 20 bytes moved into it stay.
    EXPECT_EQ(moved.Bytes(), ByteCount { 20 });
    EXPECT_EQ(budget.InUse(), ByteCount { 20 });
  });

  EXPECT_EQ(budget.InUse(), ByteCount { 0 });
}

} // namespace
