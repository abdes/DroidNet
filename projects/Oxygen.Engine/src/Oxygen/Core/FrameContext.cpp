//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <atomic>
#include <cstring>
#include <memory>
#include <ranges>
#include <stdexcept>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/FrameContext.h>

using oxygen::engine::FrameContext;
using PhaseId = oxygen::core::PhaseId;
using oxygen::core::meta::PhaseCanMutateGameState;

namespace {
template <typename Dst, typename Src>
auto ReconstructFromMove(Dst& dst, Src& src) noexcept -> void
{
  std::destroy_at(std::addressof(dst));
  std::construct_at(std::addressof(dst), std::move(src));
}

} // namespace

FrameContext::FrameContext()
{
  // Unified snapshot slots initialize their gameSnapshot by default constructor
  visible_snapshot_index_ = 0u;
  std::atomic_store(&atomic_input_snapshot_, InputBlobPtr {});
}

FrameContext::FrameContext(const Immutable& imm)
  : immutable_(imm)
{
  // Unified snapshot slots initialize their gameSnapshot by default constructor
  visible_snapshot_index_ = 0u;
  std::atomic_store(&atomic_input_snapshot_, InputBlobPtr {});
}

auto FrameContext::SetScene(observer_ptr<scene::Scene> s) noexcept -> void
{
  // Can be mutated until PhaseId::KSceneMutation (not included).
  CHECK_F(engine_state_.current_phase < PhaseId::kSceneMutation);
  scene_ = s;
}

auto FrameContext::PublishSnapshots(EngineTag) noexcept -> UnifiedSnapshot&
{
  // Only during PhaseId::kSnapshot
  CHECK_F(engine_state_.current_phase == PhaseId::kSnapshot);

  // Decide next version and target buffer while holding the locks.
  const uint64_t version = snapshot_version_ + 1u;
  const uint32_t next = (visible_snapshot_index_ + 1u) & 1u;
  auto& unified = snapshot_buffers_[static_cast<size_t>(next)];

  // CreateUnifiedSnapshot expects the caller to hold the relevant mutexes
  // to snapshot coordinator-owned state (staged module data, surfaces, and
  // views). Acquire them here to guarantee consistent copies.
  // Lock order must match the declaration order to avoid potential deadlocks.
  std::unique_lock stage_lock(staged_module_mutex_);
  std::unique_lock surfaces_lock(surfaces_mutex_);
  std::unique_lock views_lock(views_mutex_);

  CreateUnifiedSnapshot(unified, version);

  // Publish: update visible index and version (engine-only writers)
  visible_snapshot_index_ = next;
  snapshot_version_ = version;

  return unified;
}

auto FrameContext::SetInputSnapshot(InputBlobPtr inp, EngineTag) noexcept
  -> void
{
  CHECK_F(engine_state_.current_phase == PhaseId::kInput);
  // Coordinator-only: publish the input snapshot atomically for readers.
  atomic_input_snapshot_.store(std::move(inp), std::memory_order_release);
}

auto FrameContext::RegisterView(ViewContext view) noexcept -> ViewId
{
  // Views may only be registered before the Snapshot phase (exclusive).
  CHECK_F(engine_state_.current_phase < PhaseId::kSnapshot);

  std::unique_lock lock(views_mutex_);

  // Allocate a new unique ViewId
  static std::atomic<uint64_t> next_view_id { 1 };
  const ViewId id { next_view_id.fetch_add(1, std::memory_order_relaxed) };

  // Set the id in the ViewContext before storing
  view.id = id;
  views_.emplace(id, std::move(view));
  return id;
}

auto FrameContext::UpdateView(ViewId id, ViewContext view) noexcept -> void
{
  // Views may only be updated before the Snapshot phase (exclusive).
  CHECK_F(engine_state_.current_phase < PhaseId::kSnapshot);

  std::unique_lock lock(views_mutex_);
  auto it = views_.find(id);
  if (it != views_.end()) {
    // Preserve the ViewId when updating
    view.id = id;
    it->second = std::move(view);
  } else {
    LOG_F(WARNING, "UpdateView: ViewId {} not found", id.get());
  }
}

auto FrameContext::RemoveView(ViewId id) noexcept -> void
{
  // Views may only be removed before the Snapshot phase (exclusive).
  CHECK_F(engine_state_.current_phase < PhaseId::kSnapshot);

  std::unique_lock lock(views_mutex_);
  const auto erased = views_.erase(id);
  if (erased == 0) {
    LOG_F(WARNING, "RemoveView: ViewId {} not found", id.get());
  }
}

auto FrameContext::SetViewOutput(
  ViewId id, observer_ptr<graphics::Framebuffer> output) noexcept -> void
{
  // Output setting is allowed during rendering phases (FrameGraph,
  // CommandRecord) and Compositing. CHECK_F(engine_state_.current_phase >=
  // PhaseId::kFrameGraph &&
  //         engine_state_.current_phase <= PhaseId::kCompositing);

  std::unique_lock lock(views_mutex_);
  if (auto it = views_.find(id); it != views_.end()) {
    it->second.output = std::move(output);
  } else {
    LOG_F(WARNING, "SetViewOutput: ViewId {} not found", id.get());
  }
}

auto FrameContext::GetViewContext(ViewId id) const -> const ViewContext&
{
  std::shared_lock lock(views_mutex_);
  auto it = views_.find(id);
  if (it == views_.end()) {
    throw std::out_of_range("ViewId not found");
  }
  return it->second;
}

auto FrameContext::ClearViews(EngineTag) noexcept -> void
{
  // Clearing views is only allowed before Snapshot phase (exclusive).
  CHECK_F(engine_state_.current_phase < PhaseId::kSnapshot);

  std::unique_lock lock(views_mutex_);
  views_.clear();
}

auto FrameContext::SetCurrentPhase(core::PhaseId p, EngineTag) noexcept -> void
{
  engine_state_.current_phase = p;
}

auto FrameContext::SetFrameTiming(const FrameTiming& t, EngineTag) noexcept
  -> void
{
  metrics_.timing = t;
}

auto FrameContext::SetPhaseDuration(core::PhaseId phase,
  std::chrono::microseconds duration, EngineTag) noexcept -> void
{
  metrics_.timing.stage_timings[phase] = duration;
}

auto FrameContext::SetFrameStartTime(
  std::chrono::steady_clock::time_point t, EngineTag) noexcept -> void
{
  frame_start_time_ = t;
}

//=== Professional Timing System Implementation ===-------------------------//

auto FrameContext::SetModuleTimingData(
  const ModuleTimingData& timing, EngineTag) noexcept -> void
{
  module_timing_ = timing;
}

auto FrameContext::GetModuleTimingData() const noexcept
  -> const ModuleTimingData&
{
  return module_timing_;
}

auto FrameContext::GetGameDeltaTime() const noexcept -> time::CanonicalDuration
{
  return module_timing_.game_delta_time;
}

auto FrameContext::GetFixedDeltaTime() const noexcept -> time::CanonicalDuration
{
  return module_timing_.fixed_delta_time;
}

auto FrameContext::GetInterpolationAlpha() const noexcept -> float
{
  return module_timing_.interpolation_alpha;
}

auto FrameContext::GetTimeScale() const noexcept -> float
{
  return module_timing_.time_scale;
}

auto FrameContext::IsGamePaused() const noexcept -> bool
{
  return module_timing_.is_paused;
}

auto FrameContext::GetCurrentFPS() const noexcept -> float
{
  return module_timing_.current_fps;
}

auto FrameContext::SetBudgetStats(const BudgetStats& stats, EngineTag) noexcept
  -> void
{
  metrics_.budget = stats;
}

auto FrameContext::SetMetrics(const Metrics& metrics, EngineTag) noexcept
  -> void
{
  metrics_ = metrics;
}

// Added surface lifetime must be guaranteed for the frame duration by the
// caller.
auto FrameContext::AddSurface(observer_ptr<graphics::Surface> s) noexcept
  -> void
{
  // Surfaces are part of authoritative GameState and may be mutated until the
  // Snapshot phase (not included). Enforce with an explicit phase-order check
  // to match the header documentation.
  CHECK_F(engine_state_.current_phase < PhaseId::kSnapshot);

  std::unique_lock lock(surfaces_mutex_);
  surfaces_.push_back(s);
  // Keep presentable flags in sync - new surfaces start as not presentable
  presentable_flags_.push_back(0);
  DCHECK_F(presentable_flags_.size() == surfaces_.size());
}

auto FrameContext::RemoveSurfaceAt(size_t index) noexcept -> bool
{
  // Surface removal is a structural mutation of GameState; only allowed before
  // the Snapshot phase (exclusive).
  CHECK_F(engine_state_.current_phase < PhaseId::kSnapshot);

  std::unique_lock lock(surfaces_mutex_);

  if (index >= surfaces_.size()) {
    return false; // Index out of bounds
  }
  // FIXME: Capture pointer of surface being removed for view cleanup
  auto removed_surface_ptr = surfaces_[index].get();
  surfaces_.erase(surfaces_.begin() + static_cast<std::ptrdiff_t>(index));
  // Keep presentable flags in sync
  if (index < presentable_flags_.size()) {
    presentable_flags_.erase(
      presentable_flags_.begin() + static_cast<std::ptrdiff_t>(index));
  }
  DCHECK_F(presentable_flags_.size() == surfaces_.size());
  return true;
}

auto FrameContext::ClearSurfaces(EngineTag) noexcept -> void
{
  // Clearing all surfaces mutates GameState topology; enforce Snapshot bound.
  CHECK_F(engine_state_.current_phase < PhaseId::kSnapshot);

  std::unique_lock lock(surfaces_mutex_);
  views_.clear(); // Clear views since they reference the surfaces
  surfaces_.clear();
  // Keep presentable flags in sync
  presentable_flags_.clear();
  DCHECK_F(presentable_flags_.size() == surfaces_.size());
}

auto FrameContext::SetSurfacePresentable(
  size_t index, bool presentable) noexcept -> void
{
  // Presentable flags are frame-state and per the header can be mutated up to
  // (but not including) the Present phase. Use an explicit PhaseId ordering
  // check to match the documentation.
  CHECK_F(engine_state_.current_phase < PhaseId::kPresent);

  std::shared_lock lock(surfaces_mutex_);
  // Allow flag setting during later phases when rendering work completes
  if (index >= surfaces_.size() || index >= presentable_flags_.size()) {
    return; // Index out of bounds - silently ignore
  }

  // Use atomic store for thread-safe access during parallel phases
  std::atomic_ref<uint8_t> flag_ref(presentable_flags_[index]);
  flag_ref.store(presentable ? 1u : 0u, std::memory_order_release);
}

auto FrameContext::IsSurfacePresentable(size_t index) const noexcept -> bool
{
  std::shared_lock lock(surfaces_mutex_);
  if (index >= presentable_flags_.size()) {
    return false; // Index out of bounds
  }

  // Use atomic load for thread-safe access
  std::atomic_ref<const uint8_t> flag_ref(presentable_flags_[index]);
  return flag_ref.load(std::memory_order_acquire) != 0;
}

auto FrameContext::GetPresentableSurfaces() const noexcept
  -> std::vector<observer_ptr<graphics::Surface>>
{
  std::shared_lock lock(surfaces_mutex_);
  std::vector<observer_ptr<graphics::Surface>> presentable_surfaces;

  const size_t surface_count
    = std::min(surfaces_.size(), presentable_flags_.size());

  presentable_surfaces.reserve(surface_count); // Reserve max possible size

  for (size_t i = 0; i < surface_count; ++i) {
    // Use atomic load for thread-safe access
    std::atomic_ref<const uint8_t> flag_ref(presentable_flags_[i]);
    if (flag_ref.load(std::memory_order_acquire) != 0) {
      presentable_surfaces.push_back(surfaces_[i]);
    }
  }

  return presentable_surfaces;
}

auto FrameContext::ClearPresentableFlags(EngineTag) noexcept -> void
{
  // Presentable flags are frame-state and per the header can be mutated up to
  // (but not including) the Present phase. Use an explicit PhaseId ordering
  // check to match the documentation.
  CHECK_F(engine_state_.current_phase < PhaseId::kPresent);
  // By invariant, this is called single-threaded by the engine before kPresent
  // and cannot race with SetSurfacePresentable. No additional locking needed.
  std::ranges::fill(presentable_flags_, 0);
}

auto FrameContext::ReportError(TypeId source_type_id, std::string message,
  std::optional<std::string> source_key) noexcept -> void
{
  std::unique_lock lock { error_mutex_ };
  frame_errors_.emplace_back(FrameError {
    .source_type_id = source_type_id,
    .message = std::move(message),
    .source_key = std::move(source_key),
  });
}

auto FrameContext::HasErrors() const noexcept -> bool
{
  std::shared_lock lock { error_mutex_ };
  return !frame_errors_.empty();
}

auto FrameContext::GetErrors() const noexcept -> std::vector<FrameError>
{
  std::shared_lock lock { error_mutex_ };
  return frame_errors_;
}

auto FrameContext::ClearErrorsFromSource(TypeId source_type_id) noexcept -> void
{
  std::unique_lock lock { error_mutex_ };
  std::erase_if(frame_errors_, [source_type_id](const FrameError& error) {
    return error.source_type_id == source_type_id;
  });
}

auto FrameContext::ClearErrorsFromSource(TypeId source_type_id,
  const std::optional<std::string>& source_key) noexcept -> void
{
  std::unique_lock lock { error_mutex_ };
  std::erase_if(
    frame_errors_, [source_type_id, &source_key](const FrameError& error) {
      return error.source_type_id == source_type_id
        && error.source_key == source_key;
    });
}

auto FrameContext::ClearAllErrors() noexcept -> void
{
  std::unique_lock lock { error_mutex_ };
  frame_errors_.clear();
}

auto FrameContext::CreateUnifiedSnapshot(
  UnifiedSnapshot& out, const uint64_t version) noexcept -> void
{
  // PopulateGameStateSnapshot and PopulateFrameSnapshot expect the caller to
  // hold the necessary locks for views/surfaces/staged data.
  PopulateGameStateSnapshot(out.gameSnapshot, version);
  PopulateFrameSnapshot(out.frameSnapshot, out.gameSnapshot);
  ReconstructFromMove(out.moduleData, staged_module_data_);
}

auto FrameContext::PopulateGameStateSnapshot(
  GameStateSnapshot& out, uint64_t version) noexcept -> void
{
  // Caller must hold views_mutex_ and surfaces_mutex_ when invoking this
  // function to guarantee consistent copies of coordinator-owned state.
  // Copy shared_ptrs into the snapshot without creating duplicate default
  // entries or moving the original pointers.
  out.views.clear();
  out.views.reserve(views_.size());
  for (const auto& [id, context] : views_) {
    out.views.emplace_back(context);
  }

  out.surfaces = surfaces_;
  out.presentable_flags = presentable_flags_;

  // Cross-module game data: one-way move Mutable -> Immutable
  ReconstructFromMove(out.gameData, game_data_);

  // Input snapshot: atomically copy shared_ptr for lock-free access
  out.input = std::atomic_load(&atomic_input_snapshot_);

  // Version is decided by PublishSnapshots
  out.version = version;
}

// Populate FrameSnapshot within a GameStateSnapshot with coordination context
// and views
auto FrameContext::PopulateFrameSnapshot(FrameSnapshot& frame_snapshot,
  const GameStateSnapshot& game_snapshot) const noexcept -> void
{
  // This method assumes snapshotLock_ is already held by the caller

  // Basic frame identification and timing
  frame_snapshot.frame_index = frame_index_;
  frame_snapshot.epoch = engine_state_.epoch;
  frame_snapshot.frame_start_time = GetFrameStartTime();
  frame_snapshot.frame_budget
    = std::chrono::duration_cast<std::chrono::microseconds>(
      metrics_.budget.cpu_budget);

  // Enhanced timing data for parallel tasks
  frame_snapshot.timing = module_timing_;

  // Budget context for adaptive scheduling
  frame_snapshot.budget.cpu_budget = metrics_.budget.cpu_budget;
  frame_snapshot.budget.gpu_budget = metrics_.budget.gpu_budget;
  frame_snapshot.budget.is_over_budget
    = (metrics_.timing.frame_duration > frame_snapshot.frame_budget);
  frame_snapshot.budget.should_degrade_quality
    = frame_snapshot.budget.is_over_budget;

  // Validation context
  frame_snapshot.validation.snapshot_version = game_snapshot.version;
  frame_snapshot.validation.resource_generation
    = engine_state_.epoch; // Use epoch as resource generation
}

auto FrameContext::GetStagingModuleData() noexcept -> ModuleDataMutable&
{
  // Staging is allowed during phases that permit GameState mutation, and also
  // during the Snapshot phase where modules are allowed to contribute to the
  // snapshot. Enforce the documented policy using explicit predicates.
  CHECK_F(PhaseCanMutateGameState(engine_state_.current_phase)
    || engine_state_.current_phase == PhaseId::kSnapshot);

  return staged_module_data_;
}

auto FrameContext::StageModuleDataErased(
  TypeId type_id, std::shared_ptr<void> data) -> void
{
  // Allow staging during mutation phases, or during the Snapshot phase where
  // modules may contribute to the snapshot.
  CHECK_F(PhaseCanMutateGameState(engine_state_.current_phase)
    || engine_state_.current_phase == PhaseId::kSnapshot);

  std::unique_lock lock(staged_module_mutex_);

  // Check for duplicates
  if (staged_module_data_.data_.contains(type_id)) {
    throw std::invalid_argument("TypeId already staged");
  }

  // Store an owning pointer without RTTI; value already allocated by caller.
  staged_module_data_.data_[type_id] = std::move(data);
}
