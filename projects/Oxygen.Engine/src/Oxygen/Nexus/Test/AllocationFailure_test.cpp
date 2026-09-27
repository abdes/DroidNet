//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at <https://opensource.org/licenses/BSD-3-Clause>.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstdint>
#include <cstdlib>
#include <new>
#include <optional>
#include <utility>
#include <variant>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Graphics/Common/Detail/DeferredReclaimer.h>
#include <Oxygen/Nexus/FrameDrivenIndexReuse.h>
#include <Oxygen/Nexus/IndexReuse.h>
#include <Oxygen/Nexus/RetirementState.h>
#include <Oxygen/Testing/GTest.h>

namespace {
// This executable alone intercepts allocations made by the instantiated Nexus
// templates. Graphics DLL allocation is not intercepted by executable new.
struct AllocationControl {
  int remaining { -1 };
  int rejected { 0 };
  static auto Current() noexcept -> AllocationControl&
  {
    thread_local AllocationControl state;
    return state;
  }
};
constexpr uint64_t kGrowthProbeIndex = 4096U;
class FailAllocations {
public:
  explicit FailAllocations(int after = 0)
  {
    AllocationControl::Current().remaining = after;
  }
  OXYGEN_MAKE_NON_COPYABLE(FailAllocations)
  OXYGEN_MAKE_NON_MOVABLE(FailAllocations)
  ~FailAllocations() { AllocationControl::Current().remaining = -1; }
};
} // namespace

auto operator new(std::size_t size) -> void*
{
  if (AllocationControl::Current().remaining == 0) {
    ++AllocationControl::Current().rejected;
    throw std::bad_alloc();
  }
  if (AllocationControl::Current().remaining > 0) {
    --AllocationControl::Current().remaining;
  }
  // The replacement new/delete boundary owns raw memory; calling new recurses.
  // NOLINTNEXTLINE(cppcoreguidelines-no-malloc,cppcoreguidelines-owning-memory)
  if (auto* memory = std::malloc(size == 0 ? 1 : size)) {
    return memory;
  }
  throw std::bad_alloc();
}

auto operator delete(void* block) noexcept -> void
{
  // Paired with the replacement new above; delete would recurse here.
  // NOLINTNEXTLINE(cppcoreguidelines-no-malloc,cppcoreguidelines-owning-memory)
  std::free(block);
}
auto operator delete(void* block, std::size_t /*size*/) noexcept -> void
{
  // Sized delete releases the same allocator-owned raw storage.
  // NOLINTNEXTLINE(cppcoreguidelines-no-malloc,cppcoreguidelines-owning-memory)
  std::free(block);
}

namespace {
using oxygen::nexus::FinalizeResult;
using oxygen::nexus::FrameDrivenIndexReuse;
using oxygen::nexus::IndexReuse;

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
        (void)core.ActivateSlot(kGrowthProbeIndex);
      } catch (const std::bad_alloc&) {
        failed = true;
      }
    }
    EXPECT_TRUE(failed);
    EXPECT_TRUE(core.IsHandleCurrent(live));
    EXPECT_EQ(core.GetTelemetrySnapshot().allocate_calls, 1U);
    EXPECT_EQ(core.ActivateSlot(kGrowthProbeIndex).generation.get(), 1U);
  }
}

NOLINT_TEST(
  IndexReuseAllocationFailure, RetirementFinalizationAndAbandonDoNotAllocate)
{
  IndexReuse<uint64_t> core;
  const auto first = core.ActivateSlot(0);
  const auto second = core.ActivateSlot(1);
  const auto rejected_before = AllocationControl::Current().rejected;
  FinalizeResult<uint64_t> result;
  {
    FailAllocations fail;
    auto a = core.TryRetire(first);
    auto b = core.TryRetire(second);
    result = a->Finalize();
    core.Close();
  }
  EXPECT_EQ(AllocationControl::Current().rejected, rejected_before);
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
    FrameDrivenIndexReuse<uint64_t> adapter(reclaimer,
      [&](uint64_t, std::monostate) noexcept -> void { ++returned; });
    adapter.Release(adapter.ActivateSlot(0));
    adapter.Release(adapter.ActivateSlot(1));
    bool failed = false;
    {
      FailAllocations fail(fail_after);
      try {
        (void)adapter.ActivateSlot(kGrowthProbeIndex);
      } catch (const std::bad_alloc&) {
        failed = true;
      }
    }
    if (failed) {
      ++exercised_failures;
      EXPECT_EQ(adapter.GetTelemetrySnapshot().allocate_calls, 2U);
      EXPECT_EQ(adapter.ActivateSlot(kGrowthProbeIndex).generation.get(), 1U);
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
    reclaimer, [&](uint64_t, std::monostate) noexcept -> void { ++returned; });
  const auto first = adapter.ActivateSlot(0);
  const auto second = adapter.ActivateSlot(1);
  const auto rejected_before = AllocationControl::Current().rejected;
  {
    FailAllocations fail;
    adapter.Release(first);
    adapter.Release(second);
    reclaimer.OnBeginFrame(oxygen::frame::Slot { 0 });
  }
  EXPECT_EQ(returned, 2U);
  EXPECT_EQ(AllocationControl::Current().rejected, rejected_before);
}
} // namespace

NOLINT_TEST(RetirementStateAllocationFailure, OwnerUseAndPinDrainDoNotAllocate)
{
  oxygen::nexus::IndexReuse<uint64_t> core;
  const auto handle = core.ActivateSlot(0U);
  oxygen::nexus::RetirementState<uint64_t> state;
  ASSERT_TRUE(state.Activate(1U));
  ASSERT_TRUE(state.AcquireUse());
  ASSERT_TRUE(state.AcquireRetainedPin());
  const auto rejected_before = AllocationControl::Current().rejected;
  bool transitions_ok = true;
  std::optional<oxygen::nexus::FinalizeResult<uint64_t>> result;
  {
    FailAllocations fail;
    transitions_ok = state.ReleaseOwner() && state.BeginRetirement();
    auto ticket = core.TryRetire(handle);
    if (ticket) {
      transitions_ok &= state.SetRetirement(std::move(*ticket));
    } else {
      transitions_ok = false;
    }
    transitions_ok &= state.ReleaseUse();
    transitions_ok &= state.TakeOrdinaryDrained();
    transitions_ok &= !state.Finalize().has_value();
    transitions_ok &= state.ReleaseRetainedPin();
    result = state.Finalize();
  }
  EXPECT_TRUE(transitions_ok);
  EXPECT_EQ(AllocationControl::Current().rejected, rejected_before);
  if (!result) {
    FAIL() << "Retirement did not finalize";
    return;
  }
  EXPECT_EQ(result->index, 0U);
}
