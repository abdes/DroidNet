//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at <https://opensource.org/licenses/BSD-3-Clause>.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <barrier>
#include <limits>
#include <thread>

#include <Oxygen/Nexus/IndexReuse.h>
#include <Oxygen/Testing/GTest.h>

namespace oxygen::nexus {
struct IndexReuseTestAccess {
  static auto SeedGeneration(
    IndexReuse<uint32_t>& core, uint32_t index, uint32_t generation) -> void
  {
    std::lock_guard lock(core.state_->mutex);
    core.state_->generations.table_[index].value.store(generation);
  }
};
} // namespace oxygen::nexus

namespace {
using namespace oxygen::nexus;

NOLINT_TEST(
  IndexReuseTest, RetirementInvalidatesBeforeCompletionAndAdvancesOnce)
{
  IndexReuse<uint32_t> core;
  const auto first = core.ActivateSlot(3);
  auto ticket = core.TryRetire(first);
  ASSERT_TRUE(ticket);
  EXPECT_FALSE(core.IsHandleCurrent(first));
  EXPECT_THROW(core.ActivateSlot(3), std::logic_error);
  EXPECT_EQ(core.TryRetire(first).error(), RetireError::kAlreadyRetiring);
  const auto result = ticket->Finalize();
  ASSERT_EQ(result.disposition, FinalizeDisposition::kReusable);
  EXPECT_EQ(result.index, 3U);
  EXPECT_EQ(
    ticket->Finalize().disposition, FinalizeDisposition::kAlreadyResolved);
  const auto second = core.ActivateSlot(*result.index);
  EXPECT_EQ(second.generation.get(), first.generation.get() + 1);
  EXPECT_EQ(core.TryRetire(first).error(), RetireError::kStale);
  EXPECT_TRUE(core.IsHandleCurrent(second));
}

NOLINT_TEST(IndexReuseTest, StaleReleaseCannotClaimReactivatedSlot)
{
  IndexReuse<uint32_t> core;
  const auto old = core.ActivateSlot(0);
  std::barrier rendezvous(2);
  auto error = RetireError::kUnavailable;
  std::jthread worker([&] {
    // The caller has observed the old handle before another thread recycles it.
    const bool was_current = core.IsHandleCurrent(old);
    rendezvous.arrive_and_wait();
    rendezvous.arrive_and_wait();
    if (was_current) {
      error = core.TryRetire(old).error();
    }
  });
  rendezvous.arrive_and_wait();
  auto retired = core.TryRetire(old);
  ASSERT_TRUE(retired);
  const auto result = retired->Finalize();
  const auto current = core.ActivateSlot(*result.index);
  rendezvous.arrive_and_wait();
  worker.join();
  EXPECT_EQ(error, RetireError::kStale);
  EXPECT_TRUE(core.IsHandleCurrent(current));
}

NOLINT_TEST(IndexReuseTest, GrowthPreservesConcurrentExistingTransitions)
{
  IndexReuse<uint32_t> core;
  const auto existing = core.ActivateSlot(0);
  std::barrier rendezvous(2);
  std::jthread worker([&] {
    rendezvous.arrive_and_wait();
    auto ticket = core.TryRetire(existing);
    EXPECT_TRUE(ticket);
    if (ticket) {
      EXPECT_EQ(ticket->Finalize().disposition, FinalizeDisposition::kReusable);
    }
  });
  rendezvous.arrive_and_wait();
  const auto grown = core.ActivateSlot(4096);
  worker.join();
  EXPECT_TRUE(core.IsHandleCurrent(grown));
  EXPECT_EQ(core.ActivateSlot(0).generation.get(), 2U);
}

NOLINT_TEST(IndexReuseTest, GenerationExhaustionNeverWrapsOrReturnsIndex)
{
  IndexReuse<uint32_t> core;
  auto handle = core.ActivateSlot(0);
  constexpr auto max_generation = (std::numeric_limits<uint32_t>::max)();
  IndexReuseTestAccess::SeedGeneration(core, 0, max_generation - 1);
  handle.generation = oxygen::bindless::Generation { max_generation - 1 };
  auto before_last = core.TryRetire(handle);
  ASSERT_TRUE(before_last);
  EXPECT_EQ(before_last->Finalize().index, 0U);
  const auto last = core.ActivateSlot(0);
  EXPECT_EQ(last.generation.get(), max_generation);
  auto ticket = core.TryRetire(last);
  ASSERT_TRUE(ticket);
  EXPECT_FALSE(core.IsHandleCurrent(last));
  const auto result = ticket->Finalize();
  EXPECT_EQ(result.disposition, FinalizeDisposition::kExhausted);
  EXPECT_FALSE(result.index);
  EXPECT_THROW(core.ActivateSlot(0), std::logic_error);
  EXPECT_EQ(core.GetTelemetrySnapshot().exhausted_slots, 1U);
}

NOLINT_TEST(
  IndexReuseTest, AbandonAndMoveAssignmentQuarantineOnlyUnresolvedSlots)
{
  IndexReuse<uint32_t> core;
  const auto first = core.ActivateSlot(0);
  const auto second = core.ActivateSlot(1);
  {
    auto a = core.TryRetire(first);
    auto b = core.TryRetire(second);
    ASSERT_TRUE(a);
    ASSERT_TRUE(b);
    *a = std::move(*b);
    EXPECT_EQ(b->Finalize().disposition, FinalizeDisposition::kAlreadyResolved);
    EXPECT_EQ(core.GetTelemetrySnapshot().abandoned_retirements, 1U);
  }
  EXPECT_EQ(core.GetTelemetrySnapshot().abandoned_retirements, 2U);
  EXPECT_EQ(core.GetTelemetrySnapshot().pending_count, 0U);
  EXPECT_THROW(core.ActivateSlot(0), std::logic_error);
  EXPECT_THROW(core.ActivateSlot(1), std::logic_error);
}

NOLINT_TEST(IndexReuseTest, CloseAllowsExistingRetirementButNeverReuse)
{
  IndexReuse<uint32_t> core;
  const auto handle = core.ActivateSlot(0);
  core.Close();
  core.Close();
  EXPECT_FALSE(core.IsHandleCurrent(handle));
  EXPECT_THROW(core.ActivateSlot(1), std::logic_error);
  auto ticket = core.TryRetire(handle);
  ASSERT_TRUE(ticket);
  EXPECT_EQ(ticket->Finalize().disposition, FinalizeDisposition::kClosed);
  EXPECT_EQ(core.GetTelemetrySnapshot().pending_count, 0U);
}

NOLINT_TEST(IndexReuseTest, CloseTakesPrecedenceOverExhaustion)
{
  IndexReuse<uint32_t> core;
  auto handle = core.ActivateSlot(0);
  constexpr auto max_generation = (std::numeric_limits<uint32_t>::max)();
  IndexReuseTestAccess::SeedGeneration(core, 0, max_generation);
  handle.generation = oxygen::bindless::Generation { max_generation };
  auto ticket = core.TryRetire(handle);
  ASSERT_TRUE(ticket);
  core.Close();
  EXPECT_EQ(ticket->Finalize().disposition, FinalizeDisposition::kClosed);
}

NOLINT_TEST(IndexReuseTest, TicketSurvivesFacadeAndMoveClosesDestination)
{
  IndexReuse<uint32_t> source;
  const auto source_handle = source.ActivateSlot(1);
  IndexReuse<uint32_t> destination;
  auto ticket = destination.TryRetire(destination.ActivateSlot(0));
  destination = std::move(source);
  EXPECT_EQ(ticket->Finalize().disposition, FinalizeDisposition::kClosed);
  EXPECT_TRUE(destination.IsHandleCurrent(source_handle));
  auto late = [&] {
    IndexReuse<uint32_t> transient;
    return transient.TryRetire(transient.ActivateSlot(0));
  }();
  EXPECT_EQ(late->Finalize().disposition, FinalizeDisposition::kClosed);
}

NOLINT_TEST(
  IndexReuseTest, InvalidIndicesAndDuplicateActivationPreserveLiveHandle)
{
  IndexReuse<int64_t> core;
  const auto current = core.ActivateSlot(1);
  EXPECT_THROW(core.ActivateSlot(-1), std::out_of_range);
  EXPECT_THROW(core.ActivateSlot(0xffffffffLL), std::out_of_range);
  EXPECT_THROW(core.ActivateSlot(1), std::logic_error);
  EXPECT_TRUE(core.IsHandleCurrent(current));
  EXPECT_FALSE(
    core.IsHandleCurrent({ 63, oxygen::bindless::Generation { 1 } }));
}
} // namespace
