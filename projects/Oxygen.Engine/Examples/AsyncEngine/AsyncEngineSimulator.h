//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//
#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/Resource.h>
#include <Oxygen/Base/Unreachable.h>
#include <Oxygen/OxCo/Algorithms.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Event.h>
#include <Oxygen/OxCo/Nursery.h>
#include <Oxygen/OxCo/ThreadPool.h>

#include "EngineTypes.h"
#include "GraphicsLayer.h"
#include "ModuleContext.h"
#include "ModuleManager.h"

namespace oxygen::examples::asyncsim {

//! Async engine simulator orchestrating frame phases.
class AsyncEngineSimulator final {
public:
  explicit AsyncEngineSimulator(
    oxygen::co::ThreadPool& pool, EngineProps props = {}) noexcept;
  ~AsyncEngineSimulator();

  OXYGEN_MAKE_NON_COPYABLE(AsyncEngineSimulator)
  OXYGEN_MAKE_NON_MOVABLE(AsyncEngineSimulator)

  auto StartAsync(co::TaskStarted<> started = {}) -> co::Co<>
  {
    return OpenNursery(nursery_, std::move(started));
  }

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

  //! Starts internal coroutine frame loop (returns immediately).
  auto Run(uint32_t frame_count) -> void; //!< Fire-and-forget cooperative loop

  //! Initialize all registered modules
  auto InitializeModules() -> co::Co<>;

  //! Shutdown all registered modules
  auto ShutdownModules() -> co::Co<>;

  //! Configure rendering surfaces for multi-surface rendering
  void AddSurface(const RenderSurface& surface);
  void ClearSurfaces();
  [[nodiscard]] const std::vector<RenderSurface>& GetSurfaces() const noexcept
  {
    return surfaces_;
  }

  //! Graphics layer access
  [[nodiscard]] GraphicsLayer& GetGraphics() noexcept { return graphics_; }
  [[nodiscard]] const GraphicsLayer& GetGraphics() const noexcept
  {
    return graphics_;
  }

  //! Module management
  [[nodiscard]] ModuleManager& GetModuleManager() noexcept
  {
    return module_manager_;
  }
  [[nodiscard]] const ModuleManager& GetModuleManager() const noexcept
  {
    return module_manager_;
  }

private:
  // Ordered phases (Category A) - now with module integration
  void PhaseFrameStart();
  auto PhaseInput(ModuleContext& context) -> co::Co<>; // async (simulated work)
  auto PhaseFixedSim(ModuleContext& context)
    -> co::Co<>; // async (simulated work)
  auto PhaseGameplay(ModuleContext& context)
    -> co::Co<>; // async (simulated work)
  auto PhaseNetworkReconciliation(ModuleContext& context)
    -> co::Co<>; // network packet application & reconciliation
  auto PhaseRandomSeedManagement()
    -> co::Co<>; // random seed management for determinism
  auto PhaseSceneMutation(ModuleContext& context)
    -> co::Co<>; // async (simulated work) - B2 barrier
  auto PhaseTransforms(ModuleContext& context)
    -> co::Co<>; // async (simulated work)
  auto PhaseSnapshot(ModuleContext& context)
    -> co::Co<>; // async (simulated work)
  auto PhasePostParallel(ModuleContext& context)
    -> co::Co<>; // async (simulated work)
  auto PhaseFrameGraph(ModuleContext& context)
    -> co::Co<>; // async (simulated work)
  auto PhaseDescriptorTablePublication(ModuleContext& context)
    -> co::Co<>; // global descriptor/bindless table publication
  auto PhaseResourceStateTransitions(ModuleContext& context)
    -> co::Co<>; // resource state transitions planning
  auto PhaseCommandRecord(ModuleContext& context)
    -> co::Co<>; // async (simulated work)
  void PhasePresent(ModuleContext& context); // synchronous presentation

  // Multi-surface rendering helpers
  void RecordSurfaceCommands(
    const RenderSurface& surface, size_t surface_index);
  void SubmitSurfaceCommands(
    const RenderSurface& surface, size_t surface_index);
  void PresentSurface(const RenderSurface& surface, size_t surface_index);
  void PhaseAsyncPoll(ModuleContext& context);
  void PhaseBudgetAdapt();
  void PhaseFrameEnd();

  // Launch structured parallel tasks
  void LaunchParallelTasks(); // legacy sync version (unused in async path)
  void JoinParallelTasks(); // legacy sync version (unused in async path)
  void ResetParallelSync();
  auto ParallelTasks(ModuleContext& context)
    -> co::Co<>; // coroutine version with module integration

  // Async job ticking
  void TickAsyncJobs();

  // Detached services (Category D)
  void InitializeDetachedServices();

  auto SimulateWork(std::chrono::microseconds cost) const
    -> co::Co<>; // yields via thread pool
  auto SimulateWork(std::string name, std::chrono::microseconds cost) const
    -> co::Co<>; // named task (thread pool)
  auto SimulateWorkOrdered(std::chrono::microseconds cost) const
    -> co::Co<>; // runs inline (Category A)

  //! Internal coroutine performing the per-frame sequence and yielding.
  auto FrameLoop(uint32_t frame_count) -> co::Co<>;

  std::vector<SyntheticTaskSpec> parallel_specs_ {};
  std::vector<ParallelResult> parallel_results_ {};
  std::mutex parallel_results_mutex_;
  std::vector<AsyncJobState> async_jobs_ {};
  std::vector<RenderSurface> surfaces_ {};

  oxygen::co::ThreadPool& pool_;
  EngineProps props_ {};
  co::Nursery* nursery_ { nullptr };
  uint64_t frame_index_ { 0 };

  // Timing helpers
  std::chrono::steady_clock::time_point frame_start_ts_ {};
  std::chrono::microseconds phase_accum_ { 0 };
  FrameSnapshot snapshot_ {};

  // Graphics layer owning global systems
  GraphicsLayer graphics_;

  // Module management system
  ModuleManager module_manager_;

  // Signals completion when FrameLoop exits.
  oxygen::co::Event completed_ {};
};

} // namespace oxygen::examples::asyncsim
