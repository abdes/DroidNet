//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstdint>
#include <iterator>
#include <memory>
#include <ranges>
#include <string>
#include <unordered_set>
#include <vector>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Composition/TypedObject.h>
#include <Oxygen/Core/PhaseRegistry.h>
#include <Oxygen/Engine/EngineTag.h>
#include <Oxygen/Engine/FrameContext.h>

namespace oxygen::engine::internal {
auto EngineTagFactory::Get() noexcept -> EngineTag { return EngineTag {}; }
} // namespace oxygen::engine::internal

namespace {

using oxygen::core::PhaseId;
using oxygen::engine::EngineTag;
using oxygen::engine::FrameContext;
using oxygen::engine::internal::EngineTagFactory;

// Simple typed payload for testing ModuleData staging
struct TestPayload : oxygen::Object {
  OXYGEN_TYPED(TestPayload)
public:
  explicit TestPayload(const int _value)
    : value(_value)
  {
  }

  int value { 0 };

  auto operator==(const TestPayload& other) const -> bool
  {
    return value == other.value;
  }
};

// Minimal concrete Surface implementation for tests
struct DummySurface final : oxygen::graphics::Surface {
  DummySurface()
    : Surface("DummySurface")
  {
  }

  OXYGEN_DEFAULT_COPYABLE(DummySurface)
  OXYGEN_DEFAULT_MOVABLE(DummySurface)

  ~DummySurface() override = default;

  auto Resize() -> void override { }
  auto GetCurrentBackBufferIndex() const -> uint32_t override { return 0u; }
  auto GetCurrentBackBuffer() const
    -> std::shared_ptr<oxygen::graphics::Texture> override
  {
    return nullptr;
  }
  auto GetBackBuffer(uint32_t) const
    -> std::shared_ptr<oxygen::graphics::Texture> override
  {
    return nullptr;
  }
  auto Present() const -> void override { }
  auto Width() const -> uint32_t override { return 0u; }
  auto Height() const -> uint32_t override { return 0u; }
};

// Minimal concrete RenderableView implementation for tests
struct DummyRenderableView final : oxygen::engine::RenderableView {
  DummyRenderableView() = default;

  // Return a static dummy surface reference for tests wrapped in expected
  auto GetSurface() const noexcept
    -> std::expected<std::reference_wrapper<oxygen::graphics::Surface>,
      std::string> override
  {
    static DummySurface s;
    return std::expected<std::reference_wrapper<oxygen::graphics::Surface>,
      std::string>(std::ref(s));
  }

  // Return a simple View for resolution; tests don't inspect it deeply
  auto Resolve() const noexcept -> oxygen::View override
  {
    oxygen::View::Params p;
    return oxygen::View(p);
  }

  // Implement Named interface
  auto GetName() const noexcept -> std::string_view override
  {
    return "DummyRenderableView";
  }

  void SetName(std::string_view) noexcept override { /* no-op for tests */ }
};

//! Stage and read back module data using new facade API
NOLINT_TEST(FrameContext_basic_test, StageModuleData)
{
  // Arrange
  FrameContext ctx;
  auto tag = EngineTagFactory::Get();
  ctx.SetCurrentPhase(
    PhaseId::kSceneMutation, tag); // Use mutation phase instead of kSnapshot

  // Act - Use new facade API for staging data
  ctx.StageModuleData<TestPayload>(TestPayload { 42 });
  ctx.SetCurrentPhase(PhaseId::kSnapshot, tag);
  auto& snap = ctx.PublishSnapshots(tag);
  const auto version = snap.gameSnapshot.version;

  // Assert
  EXPECT_GT(version, 0u);

  // Check module data using new facade
  const auto& module_data = snap.moduleData;
  EXPECT_TRUE(module_data.Has<TestPayload>());

  auto payload_opt = module_data.Get<TestPayload>();
  ASSERT_TRUE(payload_opt.has_value());
  EXPECT_EQ(payload_opt->get().value, 42);
}

//! Test Has() and Keys() methods of ModuleData facade
NOLINT_TEST(FrameContext_basic_test, ModuleDataQueries)
{
  // Arrange
  FrameContext ctx;
  auto tag = EngineTagFactory::Get();
  ctx.SetCurrentPhase(PhaseId::kSceneMutation, tag);

  struct AnotherPayload {
    static constexpr auto ClassTypeId() -> oxygen::TypeId
    {
      return 0xABCDEF02ull;
    }

    std::string name;
    auto operator==(const AnotherPayload&) const -> bool = default;
  };

  // Act - Stage multiple different types
  ctx.StageModuleData<TestPayload>(TestPayload { 7 });
  ctx.StageModuleData<AnotherPayload>(AnotherPayload { "test" });
  ctx.SetCurrentPhase(PhaseId::kSnapshot, tag);
  auto& snap = ctx.PublishSnapshots(tag);
  const auto version = snap.gameSnapshot.version;

  // Assert
  EXPECT_GT(version, 0u);

  const auto& module_data = snap.moduleData;

  // Check Has() method
  EXPECT_TRUE(module_data.Has<TestPayload>());
  EXPECT_TRUE(module_data.Has<AnotherPayload>());

  // Check Keys() method
  auto keys = module_data.Keys();
  EXPECT_EQ(keys.size(), 2u);

  // Verify we can retrieve both payloads
  auto payload1 = module_data.Get<TestPayload>();
  auto payload2 = module_data.Get<AnotherPayload>();
  ASSERT_TRUE(payload1.has_value());
  ASSERT_TRUE(payload2.has_value());
  EXPECT_EQ(payload1->get().value, 7);
  EXPECT_EQ(payload2->get().name, "test");
}

//! Staging during non-mutation phases is rejected
NOLINT_TEST(FrameContext_basic_test, StageOutsideMutationPhasesRejected)
{
  // Arrange
  FrameContext ctx;
  auto tag = EngineTagFactory::Get();
  ctx.SetCurrentPhase(PhaseId::kParallelTasks, tag); // Non-mutation phase

  // Act
  NOLINT_ASSERT_DEATH(
    ctx.StageModuleData<TestPayload>(TestPayload { 1 }), ".*");
  ctx.SetCurrentPhase(PhaseId::kSnapshot, tag);
  auto& snap = ctx.PublishSnapshots(tag);
  const auto v1 = snap.gameSnapshot.version;

  // Assert
  EXPECT_GT(v1, 0u);

  const auto& module_data = snap.moduleData;
  EXPECT_FALSE(module_data.Has<TestPayload>());
  auto payload = module_data.Get<TestPayload>();
  EXPECT_FALSE(payload.has_value());
}

//! Staging is per-frame and cleared after publish
NOLINT_TEST(FrameContext_basic_test, StagingClearedAfterPublish)
{
  // Arrange
  FrameContext ctx;
  auto tag = EngineTagFactory::Get();
  ctx.SetCurrentPhase(PhaseId::kSceneMutation, tag);

  // Act
  ctx.StageModuleData<TestPayload>(TestPayload { 9 });
  ctx.SetCurrentPhase(PhaseId::kSnapshot, tag);
  (void)ctx.PublishSnapshots(tag); // First publish

  // After publish, the next publish without restaging should not carry over
  ctx.SetCurrentPhase(PhaseId::kSnapshot, tag);
  auto& snap = ctx.PublishSnapshots(tag); // Second publish

  // Assert
  const auto& module_data = snap.moduleData;
  EXPECT_FALSE(module_data.Has<TestPayload>());
  auto payload = module_data.Get<TestPayload>();
  EXPECT_FALSE(payload.has_value());
}

//! Test duplicate staging detection
NOLINT_TEST(FrameContext_basic_test, DuplicateStagingRejected)
{
  // Arrange
  FrameContext ctx;
  auto tag = EngineTagFactory::Get();
  ctx.SetCurrentPhase(PhaseId::kSceneMutation, tag);

  // Act
  ctx.StageModuleData<TestPayload>(TestPayload { 1 });
  // Second staging with same TypeId should throw
  EXPECT_THROW(
    ctx.StageModuleData<TestPayload>(TestPayload { 2 }), std::invalid_argument);
}

//! Test Views mutations blocked in non-GameState phases
NOLINT_TEST(FrameContext_basic_test, ViewsBlockedInNonGameStateMutationPhases)
{
  // Arrange
  FrameContext ctx;
  auto tag = EngineTagFactory::Get();

  // Initially in kSceneMutation phase - should allow GameState mutations
  ctx.SetCurrentPhase(PhaseId::kSceneMutation, tag);

  // Add a view (should succeed in GameState mutation phase)
  auto view = std::make_shared<DummyRenderableView>();
  ctx.AddView(view);
  EXPECT_EQ(std::ranges::distance(ctx.GetViews()), 1u);

  // Move to snapshot phase (allows FrameState but not GameState mutations)
  ctx.SetCurrentPhase(PhaseId::kSnapshot, tag);
  (void)ctx.PublishSnapshots(tag); // Publish snapshots

  // Now try to mutate Views - should trigger CHECK_F (death) due to phase
  // restrictions. Use ASSERT_DEATH to verify behavior in tests.
  auto view2 = std::make_shared<DummyRenderableView>();
  NOLINT_ASSERT_DEATH(ctx.AddView(view2), ".*");
  // The visible views should remain unchanged after the failed mutation.
  EXPECT_EQ(std::ranges::distance(ctx.GetViews()), 1u);
}

//! Ensure surface, presentable flag and view mutations die when attempted
//! outside their allowed phases.
// Views: adding/clearing in Snapshot phase should die
NOLINT_TEST(FrameContext_basic_test, ViewsMutatorsDieInSnapshot)
{
  FrameContext ctx;
  auto tag = EngineTagFactory::Get();

  ctx.SetCurrentPhase(PhaseId::kSnapshot, tag);
  (void)ctx.PublishSnapshots(tag);

  auto bad_view = std::make_shared<DummyRenderableView>();

  NOLINT_ASSERT_DEATH(ctx.AddView(bad_view), ".*");
  NOLINT_ASSERT_DEATH(ctx.ClearViews(tag), ".*");
}

// Surfaces: structural mutations must die in Snapshot phase
NOLINT_TEST(FrameContext_basic_test, SurfaceMutatorsDieInSnapshot)
{
  FrameContext ctx;
  auto tag = EngineTagFactory::Get();

  // Insert a surface while in a mutation phase so we have a valid starting
  // state for removal/presentable tests
  auto dummy_surface = std::make_shared<DummySurface>();
  ctx.SetCurrentPhase(PhaseId::kSceneMutation, tag);
  ctx.AddSurface(dummy_surface);

  // Move to Snapshot where structural mutations must be rejected
  ctx.SetCurrentPhase(PhaseId::kSnapshot, tag);
  (void)ctx.PublishSnapshots(tag);

  NOLINT_ASSERT_DEATH(ctx.AddSurface(dummy_surface), ".*");
  NOLINT_ASSERT_DEATH(ctx.RemoveSurfaceAt(0u), ".*");
  NOLINT_ASSERT_DEATH(ctx.ClearSurfaces(tag), ".*");
  NOLINT_ASSERT_DEATH(
    ctx.SetSurfaces(
      std::vector<std::shared_ptr<oxygen::graphics::Surface>> {}, tag),
    ".*");
}

// Presentable flags: must not be mutated at or after Present
NOLINT_TEST(FrameContext_basic_test, PresentableFlagsDieAtOrAfterPresent)
{
  FrameContext ctx;
  auto tag = EngineTagFactory::Get();

  // Add a surface in a mutation phase so index 0 exists
  auto dummy_surface = std::make_shared<DummySurface>();
  ctx.SetCurrentPhase(PhaseId::kSceneMutation, tag);
  ctx.AddSurface(dummy_surface);

  // Move to Present phase and verify presentable flag mutations die
  ctx.SetCurrentPhase(PhaseId::kPresent, tag);
  NOLINT_ASSERT_DEATH(ctx.SetSurfacePresentable(0u, true), ".*");
  NOLINT_ASSERT_DEATH(ctx.ClearPresentableFlags(tag), ".*");
}

// Exhaustive per-phase checks for the various PhaseId-guarded operations.
// These iterate all PhaseId values and assert allowed vs. disallowed behavior
// according to the predicates and ordered-phase comparisons used in
// FrameContext.cpp. This avoids random single-phase sampling and ensures
// all phases are covered.

NOLINT_TEST(FrameContext_basic_test, ViewsPhaseMatrix)
{
  auto tag = EngineTagFactory::Get();
  auto view = std::make_shared<DummyRenderableView>();

  for (uint32_t ui = 0u; ui < static_cast<uint32_t>(PhaseId::kCount); ++ui) {
    const auto phase = static_cast<PhaseId>(ui);
    FrameContext ctx;
    ctx.SetCurrentPhase(phase, tag);

    if (ui < static_cast<uint32_t>(PhaseId::kSnapshot)) {
      // Allowed: adding/clearing views before Snapshot
      ctx.AddView(view);
      EXPECT_EQ(std::ranges::distance(ctx.GetViews()), 1u);
      ctx.ClearViews(tag);
      EXPECT_EQ(std::ranges::distance(ctx.GetViews()), 0u);
    } else {
      // Disallowed: should be fatal
      NOLINT_ASSERT_DEATH(ctx.AddView(view), ".*");
      NOLINT_ASSERT_DEATH(ctx.ClearViews(tag), ".*");
    }
  }
}

NOLINT_TEST(FrameContext_basic_test, SurfacesPhaseMatrix)
{
  auto tag = EngineTagFactory::Get();

  for (uint32_t ui = 0u; ui < static_cast<uint32_t>(PhaseId::kCount); ++ui) {
    const auto phase = static_cast<PhaseId>(ui);
    FrameContext ctx;
    ctx.SetCurrentPhase(phase, tag);

    auto dummy = std::make_shared<DummySurface>();

    if (ui < static_cast<uint32_t>(PhaseId::kSnapshot)) {
      // Structural mutations allowed before Snapshot
      ctx.AddSurface(dummy);
      EXPECT_EQ(ctx.GetPresentableSurfaces().size(), 0u);

      // Removing the surface should succeed
      EXPECT_TRUE(ctx.RemoveSurfaceAt(0u));

      // Clear and replace operations should succeed
      ctx.AddSurface(dummy);
      ctx.ClearSurfaces(tag);
      ctx.SetSurfaces(
        std::vector<std::shared_ptr<oxygen::graphics::Surface>> {}, tag);
    } else {
      // Disallowed structural mutations
      NOLINT_ASSERT_DEATH(ctx.AddSurface(dummy), ".*");
      NOLINT_ASSERT_DEATH(ctx.RemoveSurfaceAt(0u), ".*");
      NOLINT_ASSERT_DEATH(ctx.ClearSurfaces(tag), ".*");
      NOLINT_ASSERT_DEATH(
        ctx.SetSurfaces(
          std::vector<std::shared_ptr<oxygen::graphics::Surface>> {}, tag),
        ".*");
    }
  }
}

NOLINT_TEST(FrameContext_basic_test, PresentableFlagsPhaseMatrix)
{
  auto tag = EngineTagFactory::Get();

  for (uint32_t ui = 0u; ui < static_cast<uint32_t>(PhaseId::kCount); ++ui) {
    const auto phase = static_cast<PhaseId>(ui);
    FrameContext ctx;

    // Ensure a surface exists by inserting in a mutation phase first
    ctx.SetCurrentPhase(PhaseId::kSceneMutation, tag);
    auto dummy = std::make_shared<DummySurface>();
    ctx.AddSurface(dummy);

    // Move to the phase under test
    ctx.SetCurrentPhase(phase, tag);

    if (ui < static_cast<uint32_t>(PhaseId::kPresent)) {
      // Allowed to set/clear presentable flags before Present
      ctx.SetSurfacePresentable(0u, true);
      EXPECT_TRUE(ctx.IsSurfacePresentable(0u));
      ctx.ClearPresentableFlags(tag);
      EXPECT_FALSE(ctx.IsSurfacePresentable(0u));
    } else {
      // Disallowed at or after Present
      NOLINT_ASSERT_DEATH(ctx.SetSurfacePresentable(0u, true), ".*");
      NOLINT_ASSERT_DEATH(ctx.ClearPresentableFlags(tag), ".*");
    }
  }
}

NOLINT_TEST(FrameContext_basic_test, SetScenePhaseMatrix)
{
  auto tag = EngineTagFactory::Get();

  for (uint32_t ui = 0u; ui < static_cast<uint32_t>(PhaseId::kCount); ++ui) {
    const auto phase = static_cast<PhaseId>(ui);
    FrameContext ctx;
    ctx.SetCurrentPhase(phase, tag);

    if (ui < static_cast<uint32_t>(PhaseId::kSceneMutation)) {
      // Allowed before SceneMutation
      ctx.SetScene(nullptr);
    } else {
      NOLINT_ASSERT_DEATH(ctx.SetScene(nullptr), ".*");
    }
  }
}

// Bounds and invalid-index behavior for surface operations
NOLINT_TEST(FrameContext_basic_test, RemoveSurfaceAtOutOfRangeAllowedPhase)
{
  auto tag = EngineTagFactory::Get();

  // In allowed phase (before Snapshot), removal of a non-existent index
  // should return false rather than fatal.
  FrameContext ctx;
  ctx.SetCurrentPhase(PhaseId::kSceneMutation, tag);
  EXPECT_FALSE(ctx.RemoveSurfaceAt(42u)); // out-of-range
}

NOLINT_TEST(FrameContext_basic_test, RemoveSurfaceAtOutOfRangeDiesInSnapshot)
{
  auto tag = EngineTagFactory::Get();

  // In Snapshot phase structural mutations are disallowed; the guard should
  // trigger before the bounds check and cause a fatal assertion.
  FrameContext ctx;
  ctx.SetCurrentPhase(PhaseId::kSnapshot, tag);
  (void)ctx.PublishSnapshots(tag);

  NOLINT_ASSERT_DEATH(ctx.RemoveSurfaceAt(42u), ".*");
}

NOLINT_TEST(FrameContext_basic_test, SetSurfacePresentableOutOfRangeNoOp)
{
  auto tag = EngineTagFactory::Get();

  // Setting presentable on an out-of-range index should be ignored and not
  // cause a fatal assertion when allowed by phase guards. Verify that
  // out-of-range accesses are silently ignored in mutation phases, but the
  // phase guard is checked first in later phases (e.g. Present) and will
  // trigger before any bounds access.
  FrameContext ctx;
  ctx.SetCurrentPhase(PhaseId::kSceneMutation, tag);
  ctx.SetSurfacePresentable(999u, true); // should be ignored
  EXPECT_FALSE(ctx.IsSurfacePresentable(999u));

  // In Present phase the phase-guard runs first and will cause a fatal
  // assertion for any attempt to mutate presentable flags. Ensure this
  // happens even for out-of-range indices.
  ctx.SetCurrentPhase(PhaseId::kPresent, tag);
  NOLINT_ASSERT_DEATH(ctx.SetSurfacePresentable(999u, true), ".*");
}

NOLINT_TEST(FrameContext_basic_test, PublishSnapshotsPhaseMatrix)
{
  auto tag = EngineTagFactory::Get();

  for (uint32_t ui = 0u; ui < static_cast<uint32_t>(PhaseId::kCount); ++ui) {
    const auto phase = static_cast<PhaseId>(ui);
    FrameContext ctx;
    ctx.SetCurrentPhase(phase, tag);

    if (phase == PhaseId::kSnapshot) {
      // Allowed only during Snapshot
      (void)ctx.PublishSnapshots(tag);
    } else {
      NOLINT_ASSERT_DEATH((void)ctx.PublishSnapshots(tag), ".*");
    }
  }
}

NOLINT_TEST(FrameContext_basic_test, SetInputSnapshotPhaseMatrix)
{
  auto tag = EngineTagFactory::Get();

  for (uint32_t ui = 0u; ui < static_cast<uint32_t>(PhaseId::kCount); ++ui) {
    const auto phase = static_cast<PhaseId>(ui);
    FrameContext ctx;
    ctx.SetCurrentPhase(phase, tag);

    auto inp = std::make_shared<oxygen::engine::InputSnapshot>();
    if (phase == PhaseId::kInput) {
      ctx.SetInputSnapshot(inp, tag);
    } else {
      NOLINT_ASSERT_DEATH(ctx.SetInputSnapshot(inp, tag), ".*");
    }
  }
}

NOLINT_TEST(FrameContext_basic_test, GetStagingModuleDataPhaseMatrix)
{
  auto tag = EngineTagFactory::Get();

  for (uint32_t ui = 0u; ui < static_cast<uint32_t>(PhaseId::kCount); ++ui) {
    const auto phase = static_cast<PhaseId>(ui);
    FrameContext ctx;
    ctx.SetCurrentPhase(phase, tag);

    const bool allowed = oxygen::core::meta::PhaseCanMutateGameState(phase)
      || phase == PhaseId::kSnapshot;

    if (allowed) {
      (void)ctx.GetStagingModuleData();
    } else {
      NOLINT_ASSERT_DEATH((void)ctx.GetStagingModuleData(), ".*");
    }
  }
}

//! Stage directly during Snapshot phase and verify retrieval from snapshot
NOLINT_TEST(FrameContext_basic_test, StageDuringSnapshotAllowed)
{
  // Arrange
  FrameContext ctx;
  auto tag = EngineTagFactory::Get();
  ctx.SetCurrentPhase(PhaseId::kSnapshot, tag);

  // Act
  ctx.StageModuleData<TestPayload>(TestPayload { 111 });
  auto& snap = ctx.PublishSnapshots(tag);

  // Assert
  const auto& md = snap.moduleData;
  EXPECT_TRUE(md.Has<TestPayload>());
  auto v = md.Get<TestPayload>();
  ASSERT_TRUE(v.has_value());
  EXPECT_EQ(v->get().value, 111);
}

//! Mutate staged payload through mutable facade before publish; verify snapshot
NOLINT_TEST(FrameContext_basic_test, MutableViewMutationReflectedInSnapshot)
{
  // Arrange
  FrameContext ctx;
  auto tag = EngineTagFactory::Get();
  ctx.SetCurrentPhase(PhaseId::kSceneMutation, tag);

  // Act
  ctx.StageModuleData<TestPayload>(TestPayload { 5 });

  // Access the staged storage via mutable facade and mutate the value
  auto& staging = ctx.GetStagingModuleData();
  auto v = staging.Get<TestPayload>();
  ASSERT_TRUE(v.has_value());
  v->get().value = 9;

  // Publish and read back immutable view
  ctx.SetCurrentPhase(PhaseId::kSnapshot, tag);
  auto& snap = ctx.PublishSnapshots(tag);

  // Assert
  const auto& md = snap.moduleData;
  auto got = md.Get<TestPayload>();
  ASSERT_TRUE(got.has_value());
  EXPECT_EQ(got->get().value, 9);
}

//! Verify Keys() empty state and membership semantics
NOLINT_TEST(FrameContext_basic_test, ModuleDataKeysAndMembership)
{
  // Arrange
  FrameContext ctx;
  auto tag = EngineTagFactory::Get();
  ctx.SetCurrentPhase(PhaseId::kSceneMutation, tag);

  // No staged data yet
  ctx.SetCurrentPhase(PhaseId::kSnapshot, tag);
  auto& snap0 = ctx.PublishSnapshots(tag);
  const auto& md0 = snap0.moduleData;
  auto keys0 = md0.Keys();
  EXPECT_TRUE(keys0.empty());
  EXPECT_FALSE(md0.Has<TestPayload>());
  EXPECT_FALSE(md0.Get<TestPayload>().has_value());

  // Act - stage two types
  ctx.SetCurrentPhase(PhaseId::kSceneMutation, tag);
  ctx.StageModuleData<TestPayload>(TestPayload { 1 });
  struct P2 {
    static constexpr auto ClassTypeId() -> oxygen::TypeId
    {
      return 0xABCDEF10ull;
    }
    int x { 0 };
  };
  ctx.StageModuleData<P2>(P2 { 2 });

  // Publish
  ctx.SetCurrentPhase(PhaseId::kSnapshot, tag);
  auto& snap1 = ctx.PublishSnapshots(tag);
  const auto& md1 = snap1.moduleData;

  // Assert - order agnostic
  auto keys1 = md1.Keys();
  EXPECT_EQ(keys1.size(), 2u);
  EXPECT_TRUE(md1.Has<TestPayload>());
  EXPECT_TRUE(md1.Has<P2>());
}

//! Stage const payload (decays internally) and read back with const and
//! non-const T
NOLINT_TEST(FrameContext_basic_test, ImmutableConstAndDecayRetrieval)
{
  // Arrange
  FrameContext ctx;
  auto tag = EngineTagFactory::Get();
  ctx.SetCurrentPhase(PhaseId::kSceneMutation, tag);

  const TestPayload kPayload { 77 };

  // Act
  ctx.StageModuleData<const TestPayload>(kPayload);
  ctx.SetCurrentPhase(PhaseId::kSnapshot, tag);
  auto& snap = ctx.PublishSnapshots(tag);
  const auto& md = snap.moduleData;

  // Assert - both retrieval spellings should succeed
  auto v1 = md.Get<TestPayload>();
  auto v2 = md.Get<const TestPayload>();
  ASSERT_TRUE(v1.has_value());
  ASSERT_TRUE(v2.has_value());
  EXPECT_EQ(v1->get().value, 77);
  EXPECT_EQ(v2->get().value, 77);
}

//! Stage and retrieve a move-only payload to ensure movable constraint works
NOLINT_TEST(FrameContext_basic_test, MoveOnlyPayloadStagingAndAccess)
{
  // Arrange
  FrameContext ctx;
  auto tag = EngineTagFactory::Get();
  ctx.SetCurrentPhase(PhaseId::kSceneMutation, tag);

  struct MoveOnlyPayload {
    static constexpr auto ClassTypeId() -> oxygen::TypeId
    {
      return 0xABCDEF20ull;
    }
    MoveOnlyPayload() = default;
    explicit MoveOnlyPayload(int v)
      : value(v)
    {
    }
    MoveOnlyPayload(const MoveOnlyPayload&) = delete;
    MoveOnlyPayload& operator=(const MoveOnlyPayload&) = delete;
    MoveOnlyPayload(MoveOnlyPayload&&) noexcept = default;
    MoveOnlyPayload& operator=(MoveOnlyPayload&&) noexcept = default;
    int value { 0 };
  };

  // Act - stage move-only type
  ctx.StageModuleData<MoveOnlyPayload>(MoveOnlyPayload { 321 });

  // Mutate via mutable view
  auto& staging = ctx.GetStagingModuleData();
  auto v = staging.Get<MoveOnlyPayload>();
  ASSERT_TRUE(v.has_value());
  v->get().value = 654;

  // Publish and read back
  ctx.SetCurrentPhase(PhaseId::kSnapshot, tag);
  auto& snap = ctx.PublishSnapshots(tag);
  const auto& md = snap.moduleData;
  auto got = md.Get<MoveOnlyPayload>();
  ASSERT_TRUE(got.has_value());
  EXPECT_EQ(got->get().value, 654);
}

//! Keys() returns the exact set of staged TypeIds (order independent)
NOLINT_TEST(FrameContext_basic_test, ModuleDataExactKeysSet)
{
  // Arrange
  FrameContext ctx;
  auto tag = EngineTagFactory::Get();
  ctx.SetCurrentPhase(PhaseId::kSceneMutation, tag);

  struct P3 {
    static constexpr auto ClassTypeId() -> oxygen::TypeId
    {
      return 0xABCDEF30ull;
    }
    int a { 0 };
  };

  // Act - stage two known types
  ctx.StageModuleData<TestPayload>(TestPayload { 1 });
  ctx.StageModuleData<P3>(P3 { 2 });

  // Publish and inspect keys
  ctx.SetCurrentPhase(PhaseId::kSnapshot, tag);
  auto& snap = ctx.PublishSnapshots(tag);
  const auto& md = snap.moduleData;
  const auto keys = md.Keys();

  // Assert: convert to unordered_set for order-agnostic equality
  std::unordered_set<oxygen::TypeId> keyset(keys.begin(), keys.end());
  std::unordered_set<oxygen::TypeId> expected {
    TestPayload::ClassTypeId(),
    P3::ClassTypeId(),
  };
  EXPECT_EQ(keyset, expected);
}

} // namespace
