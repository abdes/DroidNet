//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <bitset>
#include <cstddef>
#include <set>
#include <type_traits>
#include <unordered_set>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Core/PhaseRegistry.h>

namespace {

using namespace oxygen::core;
using oxygen::EnumAsIndex;

// -----------------------------------------------------------------------------
// PhaseRegistry compile-time invariants
// -----------------------------------------------------------------------------

static_assert(PhaseIndex::end().get() == kPhaseRegistry.size(),
  "PhaseId::kCount must match kPhaseRegistry.size()");

static_assert(BarrierIndex::end().get() == kBarrierRegistry.size(),
  "BarrierId::kCount must match kBarrierRegistry.size()");

// MakePhaseMask correctness
constexpr auto inputIndex = PhaseIndex { PhaseId::kInput }.get();
static_assert(
  MakePhaseMask(PhaseId::kInput) == (static_cast<PhaseMask>(1u) << inputIndex),
  "MakePhaseMask must set the correct bit for a PhaseId");

// Verify mutation flags for a couple of representative phases
static_assert(meta::PhaseCanMutateGameState(PhaseId::kGameplay),
  "Gameplay phase must be allowed to mutate GameState");
static_assert(!meta::PhaseCanMutateGameState(PhaseId::kParallelTasks),
  "ParallelTasks must not be allowed to mutate GameState");

//! Verify compile-time invariants are present and make the test runner
//! discoverable. Compile-time asserts above validate layout and masks.
NOLINT_TEST(PhaseRegistryCompileAndRuntime, CompileTimeAssertions)
{
  SUCCEED();
}

// -----------------------------------------------------------------------------
// Compile-time specialization example: per-phase scheduler traits
// -----------------------------------------------------------------------------
template <PhaseId P> struct PhaseScheduler {
  static constexpr int kOptimizedValue
    = static_cast<int>(P) * 10 + (meta::PhaseCanMutateGameState(P) ? 1 : 0);
};

//! Demonstrates compile-time routing/specialization by using PhaseScheduler
//! traits instantiated per PhaseId.
//! TestCase_CompileTimeSpecialization: verifies PhaseScheduler yields
//! per-phase optimized constants computed at compile time.
NOLINT_TEST(PhaseRegistryExtras, CompileTimeSpecialization)
{
  // Arrange & Act: obtain the compile-time optimized values.
  constexpr int v_game = PhaseScheduler<PhaseId::kGameplay>::kOptimizedValue;
  constexpr int v_input = PhaseScheduler<PhaseId::kInput>::kOptimizedValue;

  // Assert: compare to expected compile-time formula.
  EXPECT_EQ(v_game, static_cast<int>(PhaseId::kGameplay) * 10 + 1);
  EXPECT_EQ(v_input,
    static_cast<int>(PhaseId::kInput) * 10
      + (meta::PhaseCanMutateGameState(PhaseId::kInput) ? 1 : 0));
}

// -----------------------------------------------------------------------------
// Compile-time Zero-cost dispatch (consteval-built table of function pointers)
// -----------------------------------------------------------------------------

consteval std::array<int, PhaseIndex::end().get()> BuildPhaseIndexMap()
{
  std::array<int, PhaseIndex::end().get()> m {};
  m.fill(-1);
  for (PhaseIndex p { PhaseId::kFrameStart }; p < PhaseIndex::end(); ++p) {
    const PhaseIndex id_idx { kPhaseRegistry[p].id };
    m[id_idx.get()] = static_cast<int>(p.get());
  }
  return m;
}
constexpr auto kPhaseIndex = BuildPhaseIndexMap();

using Handler = int (*)();
static int DefaultHandler() { return -1; }

template <PhaseId P> static int RunPhaseImpl()
{
  // Different phases yield different compile-time constants; include a
  // predicate value so the compiler can specialize code per-phase.
  return static_cast<int>(P) + (meta::PhaseCanMutateGameState(P) ? 100 : 0);
}

consteval std::array<Handler, PhaseIndex::end().get()> BuildHandlerTable()
{
  std::array<Handler, PhaseIndex::end().get()> tbl {};
  tbl.fill(&DefaultHandler);

  // Install specialized handlers for a few representative phases.
  tbl[static_cast<std::size_t>(
    kPhaseIndex[PhaseIndex { PhaseId::kInput }.get()])]
    = &RunPhaseImpl<PhaseId::kInput>;
  tbl[static_cast<std::size_t>(
    kPhaseIndex[PhaseIndex { PhaseId::kGameplay }.get()])]
    = &RunPhaseImpl<PhaseId::kGameplay>;
  tbl[static_cast<std::size_t>(
    kPhaseIndex[PhaseIndex { PhaseId::kParallelTasks }.get()])]
    = &RunPhaseImpl<PhaseId::kParallelTasks>;
  tbl[static_cast<std::size_t>(
    kPhaseIndex[PhaseIndex { PhaseId::kAsyncPoll }.get()])]
    = &RunPhaseImpl<PhaseId::kAsyncPoll>;

  return tbl;
}

constexpr auto kHandlerTable = BuildHandlerTable();

// Compile-time check: specialization reflects meta predicates
static_assert(PhaseScheduler<PhaseId::kGameplay>::kOptimizedValue
    == static_cast<int>(PhaseId::kGameplay) * 10 + 1,
  "Gameplay scheduler specialization must reflect game-state mutability");

//! Demonstrates a zero-cost dispatch table built at compile time.
//! TestCase_ZeroCostDispatchTable: validates kHandlerTable entries are
//! callable and return the phase-specific compile-time constants.
NOLINT_TEST(PhaseRegistryExtras, ZeroCostDispatchTable)
{
  // Arrange: pick a few phase ids and resolve their index using the
  // consteval-built kPhaseIndex.
  const auto idx_input = kPhaseIndex[PhaseIndex { PhaseId::kInput }.get()];
  const auto idx_game = kPhaseIndex[PhaseIndex { PhaseId::kGameplay }.get()];
  const auto idx_parallel
    = kPhaseIndex[PhaseIndex { PhaseId::kParallelTasks }.get()];

  // Act: call handlers from the compile-time table.
  const int r_input = kHandlerTable[static_cast<std::size_t>(idx_input)]();
  const int r_game = kHandlerTable[static_cast<std::size_t>(idx_game)]();
  const int r_parallel
    = kHandlerTable[static_cast<std::size_t>(idx_parallel)]();

  // Assert: results match the compile-time specialization formula.
  EXPECT_EQ(r_input,
    static_cast<int>(PhaseId::kInput)
      + (meta::PhaseCanMutateGameState(PhaseId::kInput) ? 100 : 0));
  EXPECT_EQ(r_game,
    static_cast<int>(PhaseId::kGameplay)
      + (meta::PhaseCanMutateGameState(PhaseId::kGameplay) ? 100 : 0));
  EXPECT_EQ(r_parallel,
    static_cast<int>(PhaseId::kParallelTasks)
      + (meta::PhaseCanMutateGameState(PhaseId::kParallelTasks) ? 100 : 0));
}

// -----------------------------------------------------------------------------
// PhaseRegistry runtime smoke tests
// -----------------------------------------------------------------------------

//! Ensure the canonical registry ordering matches PhaseId numeric values.
//! TestCase_RegistryOrdering: each entry index must equal its PhaseId
//! numeric value to preserve canonical ordering.
NOLINT_TEST(PhaseRegistryRuntime, RegistryOrdering)
{
  // Arrange: kPhaseRegistry is the canonical registry to validate.

  // Act & Assert: iterate and verify index == id for each entry.
  for (PhaseIndex p { PhaseId::kFrameStart }; p < PhaseIndex::end(); ++p) {
    EXPECT_EQ(PhaseIndex { kPhaseRegistry[p].id }.get(), p.get());
  }
}

//! Validate the allowed_mutations in the registry agree with meta predicates.
//! TestCase_MutabilityPolicy: check AllowMutation bits match meta:: helpers
//! for game/frame/engine mutation permissions.
NOLINT_TEST(PhaseRegistryRuntime, MutabilityPolicy)
{
  // Arrange: none — using constexpr registry and meta helpers.

  // Act & Assert: for each entry, the bitmask should match meta:: helpers.
  for (const auto& d : kPhaseRegistry) {
    // Prefer testing flag presence directly using the enum flag operators.
    EXPECT_EQ((d.allowed_mutations & AllowMutation::kGameState)
        == AllowMutation::kGameState,
      meta::PhaseCanMutateGameState(d.id));

    EXPECT_EQ((d.allowed_mutations & AllowMutation::kFrameState)
        == AllowMutation::kFrameState,
      meta::PhaseCanMutateFrameState(d.id));

    EXPECT_EQ((d.allowed_mutations & AllowMutation::kEngineState)
        == AllowMutation::kEngineState,
      meta::PhaseCanMutateEngineState(d.id));
  }
}

//! Sanity check that each barrier references a valid phase and ids are unique.
//! TestCase_BarrierMappingSanity: barriers must reference valid phases and
//! barrier ids must be unique.
NOLINT_TEST(PhaseRegistryRuntime, BarrierMappingSanity)
{
  // Arrange: build PhaseId -> index map for fast lookup.
  std::array<int, PhaseIndex::end().get()> idx_map {};
  idx_map.fill(-1);
  for (PhaseIndex p { PhaseId::kFrameStart }; p < PhaseIndex::end(); ++p) {
    idx_map[PhaseIndex { kPhaseRegistry[p].id }.get()]
      = static_cast<int>(p.get());
  }

  // Act & Assert: check barrier mappings and uniqueness.
  std::set<std::uint32_t> barrier_ids;
  for (const auto& b : kBarrierRegistry) {
    EXPECT_GE(static_cast<int>(b.after_phase), 0);
    EXPECT_LT(PhaseIndex { b.after_phase }.get(), kPhaseRegistry.size());
    EXPECT_NE(idx_map[PhaseIndex { b.after_phase }.get()], -1);
    barrier_ids.insert(static_cast<std::uint32_t>(b.id));
  }
  EXPECT_EQ(barrier_ids.size(), kBarrierRegistry.size());
}

//! Ensure each phase and barrier has a non-empty name and description.
//! TestCase_DocstringCoverage: ensure Phase/Barrier name and description
//! accessors return non-empty strings for each registry entry.
NOLINT_TEST(PhaseRegistryRuntime, DocstringCoverage)
{
  // Arrange: none — using detail accessors.

  // Act & Assert: check phase docstrings.
  for (PhaseIndex p { PhaseId::kFrameStart }; p < PhaseIndex::end(); ++p) {
    const auto id = static_cast<PhaseId>(p.get());
    const char* n = detail::PhaseName(id);
    const char* d = detail::PhaseDescription(id);
    EXPECT_NE(n, nullptr);
    EXPECT_NE(d, nullptr);
    EXPECT_NE(n[0], '\0');
    EXPECT_NE(d[0], '\0');
  }

  // Act & Assert: check barrier docstrings.
  for (std::size_t i = 0; i < static_cast<std::size_t>(BarrierId::kCount);
    ++i) {
    const auto id = static_cast<BarrierId>(i);
    const char* n = detail::BarrierName(id);
    const char* d = detail::BarrierDescription(id);
    EXPECT_NE(n, nullptr);
    EXPECT_NE(d, nullptr);
    EXPECT_NE(n[0], '\0');
    EXPECT_NE(d[0], '\0');
  }
}

//! Verify mask utilities produce expected single-bit masks and combinations.
//! TestCase_MaskCompatibility: MakePhaseMask and combination operations
//! produce the expected single-bit masks and counts.
NOLINT_TEST(PhaseRegistryRuntime, MaskCompatibility)
{
  // Arrange: build a combined mask.
  const PhaseMask mask
    = MakePhaseMask(PhaseId::kInput) | MakePhaseMask(PhaseId::kGameplay);

  // Act & Assert: check contained bits and popcount.
  EXPECT_NE((mask & MakePhaseMask(PhaseId::kInput)), 0u);
  EXPECT_NE((mask & MakePhaseMask(PhaseId::kGameplay)), 0u);
  EXPECT_EQ(std::bitset<32>(mask).count(), 2u);
}

//! Verify UsesCoroutines reports true for barriered concurrency phases.
//! TestCase_UsesCoroutinesCorrectness: ensures ExecutionModel mapping
//! is reflected by the PhaseDesc::UsesCoroutines method.
NOLINT_TEST(PhaseRegistryExtras, UsesCoroutinesCorrectness)
{
  // Arrange: none - using constexpr registry and per-entry metadata.

  // Act & Assert: each phase should advertise coroutine usage when it uses
  // the barriered concurrency execution model.
  for (const auto& d : kPhaseRegistry) {
    const bool expects = (d.category == ExecutionModel::kBarrieredConcurrency);
    EXPECT_EQ(d.UsesCoroutines(), expects);
  }
}

//! Verify phases with known thread-safe semantics are marked thread_safe.
//! TestCase_ThreadSafetyExpectations: asserts registry thread_safe flags
//! match the expected per-phase mapping.
NOLINT_TEST(PhaseRegistryExtras, ThreadSafetyExpectations)
{
  // Arrange: helper mapping of expected thread-safety for named phases.
  auto expect_thread_safe = [](PhaseId id) {
    switch (id) {
    case PhaseId::kParallelTasks:
    case PhaseId::kRender:
    case PhaseId::kCompositing:
    case PhaseId::kAsyncPoll:
    case PhaseId::kDetachedServices:
      return true;
    default:
      return false;
    }
  };

  // Act & Assert: verify registry flags match the expectation helper.
  for (const auto& d : kPhaseRegistry) {
    EXPECT_EQ(d.thread_safe, expect_thread_safe(d.id));
  }
}

//! Ensure there are no duplicate Phase ids in the registry.
//! TestCase_NoDuplicatePhases: verifies phase id uniqueness.
NOLINT_TEST(PhaseRegistryExtras, NoDuplicatePhases)
{
  // Arrange: collect phase ids.
  std::unordered_set<PhaseIndex> phase_ids;
  for (const auto& d : kPhaseRegistry) {
    phase_ids.insert(PhaseIndex { d.id });
  }

  // Act & Assert: ensure no duplicate phase ids.
  EXPECT_EQ(phase_ids.size(), kPhaseRegistry.size());
}

//! Ensure there are no duplicate Barrier ids in the registry.
//! TestCase_NoDuplicateBarriers: verifies barrier id uniqueness.
NOLINT_TEST(PhaseRegistryExtras, NoDuplicateBarriers)
{
  // Arrange: collect barrier ids.
  std::unordered_set<BarrierIndex> barrier_ids;
  for (const auto& b : kBarrierRegistry) {
    barrier_ids.insert(BarrierIndex { b.id });
  }

  // Act & Assert: ensure no duplicate barrier ids.
  EXPECT_EQ(barrier_ids.size(), kBarrierRegistry.size());
}

//! Ensure barriers reference phases in non-decreasing order.
//! TestCase_BarrierMonotonicity: optional invariant to keep barrier
//! ordering monotonic with phase ordering.
NOLINT_TEST(PhaseRegistryExtras, BarrierMonotonicity)
{
  // Arrange: create PhaseId -> index map for quick phase ordering lookup.
  std::array<int, static_cast<std::size_t>(PhaseId::kCount)> idx_map {};
  idx_map.fill(-1);
  for (PhaseIndex p { PhaseId::kFrameStart }; p < PhaseIndex::end(); ++p) {
    idx_map[static_cast<std::size_t>(kPhaseRegistry[p].id)]
      = static_cast<int>(p.get());
  }

  // Act & Assert: barriers should reference phases in non-decreasing order.
  int last_idx = -1;
  for (const auto& b : kBarrierRegistry) {
    const int idx = idx_map[static_cast<std::size_t>(b.after_phase)];
    EXPECT_GE(idx, 0);
    EXPECT_GE(idx, last_idx);
    last_idx = idx;
  }
}

// Compile-time ABI/trait checks
static_assert(std::is_trivially_copyable_v<PhaseDesc>,
  "PhaseDesc must be trivially copyable");
static_assert(
  std::is_standard_layout_v<PhaseDesc>, "PhaseDesc must have standard layout");

} // namespace
