//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Engine/FrameContext.h>

using oxygen::engine::FrameContext;

auto FrameContext::PublishSnapshots(EngineTag) noexcept -> uint64_t
{
  // RATIONALE: Snapshot publishing must be serialized and only performed
  // by the engine coordinator to ensure consistent state visibility across
  // parallel workers. Adding EngineTag requirement prevents accidental
  // snapshot publishing from application modules.

  // Build unified snapshot under the snapshot lock
  std::unique_lock lock(snapshotLock_);

  // Create the unified snapshot (game data + frame coordination context)
  uint64_t newVersion = CreateUnifiedSnapshot();

  return newVersion;
}

void FrameContext::PopulateFrameSnapshotViews(FrameSnapshot& frame_snapshot,
  const GameStateSnapshot& game_snapshot) const noexcept
{
  // // Let each data structure handle its own snapshot creation
  // game_snapshot.gameData.scene.PopulateSnapshot(
  //   frame_snapshot.transforms, frame_snapshot.spatial);
  // game_snapshot.gameData.animation.PopulateSnapshot(frame_snapshot.skeletons);
  // game_snapshot.gameData.particles.PopulateSnapshot(
  //   frame_snapshot.particle_emitters);
  // game_snapshot.gameData.materials.PopulateSnapshot(frame_snapshot.materials);
}

auto FrameContext::CreateUnifiedSnapshot() noexcept -> uint64_t
{
  // This method assumes snapshotLock_ is already held by the caller

  // Select the next buffer to write to
  uint32_t next
    = (visibleSnapshotIndex_.load(std::memory_order_relaxed) + 1u) & 1u;
  auto& unified = snapshotBuffers_[static_cast<size_t>(next)];

  // Create or reuse GameStateSnapshot
  if (!unified.gameSnapshot) {
    unified.gameSnapshot = std::make_shared<GameStateSnapshot>();
  }
  auto s = unified.gameSnapshot;

  // Copy basic game state
  s->views = game_state_.views;
  // s->lights = game_state_.lights;
  // s->drawBatches = game_state_.drawBatches;
  s->surfaces = engine_state_.surfaces;
  s->presentable_flags = engine_state_.presentable_flags;
  s->userContext.ptr = game_state_.userContext.ptr;

  // Copy cross-module game data using templated CopyFrom method
  s->gameData.CopyFrom(game_state_.gameData);

  // Copy the atomic input snapshot into the snapshot
  s->input = std::atomic_load(&atomicInputSnapshot_);

  // Assign a new monotonic version
#ifdef NDEBUG
  // Release build: increment the global version and publish.
  const uint64_t newVersion = ++snapshotVersion_;
#else
  // Debug build: prefer fetch_add to make the action explicit.
  const uint64_t newVersion = snapshotVersion_.fetch_add(1u) + 1u;
#endif
  s->version = newVersion;

  // Populate the FrameSnapshot with coordination context and views into
  // GameStateSnapshot
  PopulateFrameSnapshot(unified.frameSnapshot, *s);

  // Atomically publish both snapshots together
  visibleSnapshotIndex_.store(next, std::memory_order_release);
  return newVersion;
}

// Populate FrameSnapshot within a GameStateSnapshot with coordination context
// and views
void FrameContext::PopulateFrameSnapshot(FrameSnapshot& frame_snapshot,
  const GameStateSnapshot& game_snapshot) const noexcept
{
  // This method assumes snapshotLock_ is already held by the caller

  // Basic frame identification and timing
  frame_snapshot.frame_index = frameIndex_;
  frame_snapshot.epoch = engine_state_.epoch;
  frame_snapshot.frame_start_time = GetFrameStartTime();
  frame_snapshot.frame_budget
    = std::chrono::duration_cast<std::chrono::microseconds>(
      metrics_.budget.cpuBudget);

  // Budget context for adaptive scheduling
  frame_snapshot.budget.cpu_budget = metrics_.budget.cpuBudget;
  frame_snapshot.budget.gpu_budget = metrics_.budget.gpuBudget;
  frame_snapshot.budget.is_over_budget
    = (metrics_.timing.cpuTime > frame_snapshot.frame_budget);
  frame_snapshot.budget.should_degrade_quality
    = frame_snapshot.budget.is_over_budget;

  // Populate data views from the game snapshot (zero-copy views)
  PopulateFrameSnapshotViews(frame_snapshot, game_snapshot);

  // Validation context
  frame_snapshot.validation.snapshot_version = game_snapshot.version;
  frame_snapshot.validation.resource_generation
    = engine_state_.epoch; // Use epoch as resource generation
}
