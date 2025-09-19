//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//
#pragma once

#include "Oxygen/OxCo/LiveObject.h"

#include <algorithm>
#include <chrono>
#include <memory>
#include <optional>
#include <string_view>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Config/EngineConfig.h>
#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Engine/api_export.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Event.h>
#include <Oxygen/OxCo/Nursery.h>

namespace oxygen::engine {
class EngineModule;
class ModuleManager;
} // namespace oxygen::engine

namespace oxygen {

class Platform;
class Graphics;

//! Async engine simulator orchestrating frame phases.
class AsyncEngine final : public co::LiveObject {
public:
  OXGN_NGIN_API AsyncEngine(std::shared_ptr<Platform> platform,
    std::weak_ptr<Graphics> graphics,
    oxygen::EngineConfig config = {}) noexcept;

  OXGN_NGIN_API ~AsyncEngine() override;

  OXYGEN_MAKE_NON_COPYABLE(AsyncEngine)
  OXYGEN_MAKE_NON_MOVABLE(AsyncEngine)

  auto ActivateAsync(co::TaskStarted<> started = {}) -> co::Co<> override
  {
    return OpenNursery(nursery_, std::move(started));
  }

  //! Starts internal coroutine frame loop (returns immediately).
  OXGN_NGIN_API auto Run() -> void override;

  [[nodiscard]] auto IsRunning() const -> bool override
  {
    return nursery_ != nullptr;
  }

  OXGN_NGIN_API auto Stop() -> void override;

  //! Completion event that becomes triggered after the simulator finishes
  //! running the requested number of frames. Can be awaited or polled.
  //! Example:
  //!   if(sim.Completed()) { /* already finished */ }
  //!   co_await sim.Completed(); // suspend until finished (if not yet)
  [[nodiscard]] auto& Completed() noexcept { return completed_; }
  [[nodiscard]] auto Completed() const noexcept
  {
    return completed_.Triggered();
  }

  //! Graphics layer access
  [[nodiscard]] auto GetGraphics() noexcept -> std::weak_ptr<Graphics>
  {
    return gfx_weak_;
  }

  [[nodiscard]] auto GetGraphics() const noexcept -> std::weak_ptr<Graphics>
  {
    return gfx_weak_;
  }

  // Register a module (takes ownership). Modules are sorted by priority.
  OXGN_NGIN_API auto RegisterModule(
    std::unique_ptr<engine::EngineModule> module) noexcept -> bool;

  // Optional: unregister by name. Returns true if removed.
  OXGN_NGIN_API auto UnregisterModule(std::string_view name) noexcept -> void;

  //! Get current engine configuration
  [[nodiscard]] auto GetEngineConfig() const noexcept
    -> const oxygen::EngineConfig&;

private:
  auto Shutdown() -> co::Co<>;

  // Ordered phases (Category A) - now with module integration
  auto PhaseFrameStart(engine::FrameContext& context) -> co::Co<>; // synchrnous
  auto PhaseInput(engine::FrameContext& context)
    -> co::Co<>; // async (simulated work)
  auto PhaseNetworkReconciliation(engine::FrameContext& context)
    -> co::Co<>; // network packet application & reconciliation
  auto PhaseRandomSeedManagement(engine::FrameContext& context)
    -> void; // synchronous random seed management for determinism
  auto PhaseFixedSim(engine::FrameContext& context)
    -> co::Co<>; // cooperative parallelism within deterministic phase
  auto PhaseGameplay(engine::FrameContext& context)
    -> co::Co<>; // async (simulated work)
  auto PhaseSceneMutation(engine::FrameContext& context)
    -> co::Co<>; // async (simulated work) - B2 barrier
  auto PhaseTransforms(engine::FrameContext& context)
    -> co::Co<>; // async (simulated work)

  // Synchronous snapshot: modules run OnSnapshot first; engine publishes last
  auto PhaseSnapshot(engine::FrameContext& context)
    -> co::Co<const engine::UnifiedSnapshot&>;

  auto ParallelTasks(engine::FrameContext& context,
    const engine::UnifiedSnapshot& snapshot) -> co::Co<>;
  auto PhasePostParallel(engine::FrameContext& context)
    -> co::Co<>; // async (simulated work)

  auto PhaseFrameGraph(engine::FrameContext& context)
    -> co::Co<>; // async (simulated work)

  auto PhaseCommandRecord(engine::FrameContext& context)

    -> co::Co<>; // async (simulated work)
  auto PhasePresent(engine::FrameContext& context)
    -> void; // synchronous presentation

  // Poll futures from async jobs (Assets, PSOs, BLAS, LightMaps)
  auto PhaseAsyncPoll(engine::FrameContext& context) -> void;

  auto PhaseBudgetAdapt() -> void;

  auto PhaseFrameEnd(engine::FrameContext& context) -> co::Co<>;

  // Detached services (Category D)
  auto InitializeDetachedServices() -> void;

  // auto SimulateWork(std::chrono::microseconds cost) const
  //   -> co::Co<>; // yields via thread pool
  // auto SimulateWork(std::string name, std::chrono::microseconds cost) const
  //   -> co::Co<>; // named task (thread pool)
  // auto SimulateWorkOrdered(std::chrono::microseconds cost) const
  //   -> co::Co<>; // runs inline (Category A)

  //! Internal coroutine performing the per-frame sequence and yielding.
  auto FrameLoop() -> co::Co<>;

  //! Advances the frame counters to the next frame, and decides if the frame
  //! loop should continue or not.
  auto NextFrame() -> bool;

  // Professional timing system integration
  auto UpdateFrameTiming(engine::FrameContext& context) -> void;

  // void SetRenderGraphBuilder(engine::FrameContext& context);
  // void ClearRenderGraphBuilder(engine::FrameContext& context);

  // std::unique_ptr<RenderGraphBuilder> render_graph_builder_;
  // std::vector<SyntheticTaskSpec> parallel_specs_ {};
  // std::vector<ParallelResult> parallel_results_ {};
  // std::mutex parallel_results_mutex_;
  // std::vector<AsyncJobState> async_jobs_ {};

  bool shutdown_requested_ { false };
  oxygen::EngineConfig config_; // Engine configuration
  co::Nursery* nursery_ { nullptr };
  frame::SequenceNumber frame_number_ { 0 };
  frame::Slot frame_slot_ { 0 };

  // Timing helpers
  std::chrono::steady_clock::time_point frame_start_ts_ {};
  std::chrono::microseconds phase_accum_ { 0 };
  engine::FrameSnapshot snapshot_ {};

  // Enhanced timing state
  std::chrono::steady_clock::time_point last_frame_time_;
  std::chrono::microseconds accumulated_fixed_time_ { 0 };
  // Targeted frame scheduler deadline (monotonic, deadline-based pacing)
  std::chrono::steady_clock::time_point next_frame_deadline_ {};

  // Frame time smoothing
  static constexpr size_t kTimingSamples = 10;
  std::array<std::chrono::microseconds, kTimingSamples> timing_history_ {};
  size_t timing_index_ { 0 };

  std::shared_ptr<Platform> platform_;
  std::weak_ptr<Graphics> gfx_weak_;

  // Module management system
  std::unique_ptr<engine::ModuleManager> module_manager_;

  // Signals completion when FrameLoop exits.
  co::Event completed_ {};
};

} // namespace oxygen
