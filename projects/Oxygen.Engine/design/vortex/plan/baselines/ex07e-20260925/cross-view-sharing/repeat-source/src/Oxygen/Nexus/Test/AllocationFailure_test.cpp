//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at <https://opensource.org/licenses/BSD-3-Clause>.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstdlib>
#include <new>

#include <Oxygen/Nexus/FrameDrivenIndexReuse.h>
#include <Oxygen/Testing/GTest.h>

namespace {
// This executable alone intercepts allocations made by the instantiated Nexus
// templates. Graphics DLL allocation is not intercepted by executable new.
thread_local int allocations_before_failure = -1;
thread_local int rejected_allocations = 0;
class FailAllocations {
public:
  explicit FailAllocations(int after = 0)
  {
    allocations_before_failure = after;
  }
  ~FailAllocations() { allocations_before_failure = -1; }
};
} // namespace

auto operator new(std::size_t size) -> void*
{
  if (allocations_before_failure == 0) {
    ++rejected_allocations;
    throw std::bad_alloc();
  }
  if (allocations_before_failure > 0) {
    --allocations_before_failure;
  }
  if (auto* memory = std::malloc(size == 0 ? 1 : size)) {
    return memory;
  }
  throw std::bad_alloc();
}
auto operator delete(void* memory) noexcept -> void { std::free(memory); }
auto operator delete(void* memory, std::size_t) noexcept -> void
{
  std::free(memory);
}

namespace {
using namespace oxygen::nexus;

NOLINT_TEST(
  IndexReuseAllocationFailure, FailedGrowthPreservesExistingGeneration)
{
  // Phase reserve and GenerationTracker growth are separate allocations.
  for (int fail_after = 0; fail_after < 2; ++fail_after) {
    IndexReuse<uint64_t> core;
    const auto live = core.ActivateSlot(0);
    bool failed = false;
    {
      FailAllocations fail(fail_after);
      try {
        (void)core.ActivateSlot(4096);
      } catch (const std::bad_alloc&) {
        failed = true;
      }
    }
    EXPECT_TRUE(failed);
    EXPECT_TRUE(core.IsHandleCurrent(live));
    EXPECT_EQ(core.GetTelemetrySnapshot().allocate_calls, 1U);
    EXPECT_EQ(core.ActivateSlot(4096).generation.get(), 1U);
  }
}

NOLINT_TEST(
  IndexReuseAllocationFailure, RetirementFinalizationAndAbandonDoNotAllocate)
{
  IndexReuse<uint64_t> core;
  const auto first = core.ActivateSlot(0);
  const auto second = core.ActivateSlot(1);
  const auto rejected_before = rejected_allocations;
  FinalizeResult<uint64_t> result;
  {
    FailAllocations fail;
    auto a = core.TryRetire(first);
    auto b = core.TryRetire(second);
    result = a->Finalize();
    core.Close();
  }
  EXPECT_EQ(rejected_allocations, rejected_before);
  EXPECT_EQ(result.index, 0U);
  EXPECT_EQ(core.GetTelemetrySnapshot().abandoned_retirements, 1U);
  EXPECT_EQ(core.GetTelemetrySnapshot().pending_count, 0U);
}

NOLINT_TEST(
  IndexReuseAllocationFailure, FailedActivationPreservesPendingActions)
{
  int exercised_failures = 0;
  // Exercise every allocation in the executable's adapter/core activation.
  for (int fail_after = 0; fail_after < 8; ++fail_after) {
    oxygen::graphics::detail::DeferredReclaimer reclaimer;
    int returned = 0;
    FrameDrivenIndexReuse<uint64_t> adapter(
      reclaimer, [&](uint64_t, std::monostate) noexcept { ++returned; });
    adapter.Release(adapter.ActivateSlot(0));
    adapter.Release(adapter.ActivateSlot(1));
    bool failed = false;
    {
      FailAllocations fail(fail_after);
      try {
        (void)adapter.ActivateSlot(4096);
      } catch (const std::bad_alloc&) {
        failed = true;
      }
    }
    if (failed) {
      ++exercised_failures;
      EXPECT_EQ(adapter.GetTelemetrySnapshot().allocate_calls, 2U);
      EXPECT_EQ(adapter.ActivateSlot(4096).generation.get(), 1U);
    }
    reclaimer.OnBeginFrame(oxygen::frame::Slot { 0 });
    EXPECT_EQ(returned, 2);
    EXPECT_EQ(adapter.GetTelemetrySnapshot().pending_count, 0U);
  }
  EXPECT_GE(exercised_failures, 4);
}

NOLINT_TEST(
  IndexReuseAllocationFailure, AdapterReleaseAndCompletionDoNotAllocateInNexus)
{
  oxygen::graphics::detail::DeferredReclaimer reclaimer;
  uint64_t returned = 0;
  FrameDrivenIndexReuse<uint64_t> adapter(
    reclaimer, [&](uint64_t, std::monostate) noexcept { ++returned; });
  const auto first = adapter.ActivateSlot(0);
  const auto second = adapter.ActivateSlot(1);
  const auto rejected_before = rejected_allocations;
  {
    FailAllocations fail;
    adapter.Release(first);
    adapter.Release(second);
    reclaimer.OnBeginFrame(oxygen::frame::Slot { 0 });
  }
  EXPECT_EQ(returned, 2U);
  EXPECT_EQ(rejected_allocations, rejected_before);
}
} // namespace
