//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <atomic>
#include <cstdint>
#include <latch>
#include <limits>
#include <thread>
#include <utility>
#include <vector>

#include <Oxygen/Graphics/Common/AllocationBudget.h>
#include <Oxygen/Graphics/Common/AllocationBudgetTag.h>
#include <Oxygen/Testing/GTest.h>

namespace oxygen::graphics {
namespace {
  NOLINT_TEST(
    AllocationBudgetTest, SubdomainSharesTotalAndMoveReleasesExactlyOnce)
  {
    constexpr std::uint64_t kTotal = 10U;
    constexpr std::uint64_t kGeneral = 7U;
    auto budget = AllocationBudget({
      .total = SizeBytes { kTotal },
      .compact_indices = SizeBytes { 4U },
      .driver_headroom = SizeBytes { 0U },
    });
    auto general = budget.TryReserve(SizeBytes { kGeneral });
    ASSERT_TRUE(general);
    EXPECT_FALSE(
      budget.TryReserve(SizeBytes { 4U }, AllocationCategory::kCompactIndices));
    auto compact = budget.TryReserve(
      SizeBytes { 3U }, AllocationCategory::kCompactIndices);
    ASSERT_TRUE(compact);
    EXPECT_EQ(budget.Snapshot().allocated.get(), 10U);
    *general = std::move(*compact);
    EXPECT_EQ(budget.Snapshot().allocated.get(), 3U);
    EXPECT_EQ(budget.Snapshot().compact_indices.get(), 3U);
    general->ReduceTo(SizeBytes { 2U });
    EXPECT_EQ(budget.Snapshot().allocated.get(), 2U);
    compact.reset();
    general.reset();
    EXPECT_EQ(budget.Snapshot().allocated.get(), 0U);
    EXPECT_EQ(budget.Snapshot().compact_indices.get(), 0U);
    EXPECT_EQ(budget.Snapshot().rejected_requests, 1U);
  }

  NOLINT_TEST(AllocationBudgetTest, FullUint64EnvelopeCannotWrap)
  {
    constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();
    auto budget = AllocationBudget({
      .total = SizeBytes { maximum },
      .compact_indices = SizeBytes { maximum },
      .driver_headroom = SizeBytes { 0U },
    });
    auto first = budget.TryReserve(SizeBytes { maximum - 1U });
    ASSERT_TRUE(first);
    EXPECT_FALSE(budget.TryReserve(SizeBytes { 2U }));
    EXPECT_EQ(budget.Snapshot().last_available.get(), 1U);
    auto last = budget.TryReserve(SizeBytes { 1U });
    ASSERT_TRUE(last);
    EXPECT_EQ(budget.Snapshot().allocated.get(), maximum);
    first.reset();
    last.reset();
    EXPECT_EQ(budget.Snapshot().allocated.get(), 0U);
  }

  NOLINT_TEST(AllocationBudgetTest, ConcurrentRequestsCannotOversubscribe)
  {
    auto budget = AllocationBudget({
      .total = SizeBytes { 64U },
      .compact_indices = SizeBytes { 64U },
      .driver_headroom = SizeBytes { 0U },
    });
    auto ready = std::latch { 16 };
    auto release = std::latch { 1 };
    auto admitted = std::atomic<unsigned> { 0U };
    auto threads = std::vector<std::jthread> {};
    for (unsigned index = 0U; index < 16U; ++index) {
      threads.emplace_back([&] -> void {
        auto charge = budget.TryReserve(SizeBytes { 8U });
        if (charge) {
          ++admitted;
        }
        ready.count_down();
        release.wait();
      });
    }
    ready.wait();
    EXPECT_EQ(admitted.load(), 8U);
    EXPECT_EQ(budget.Snapshot().allocated.get(), 64U);
    release.count_down();
    threads.clear();
    EXPECT_EQ(budget.Snapshot().allocated.get(), 0U);
  }
} // namespace
} // namespace oxygen::graphics
