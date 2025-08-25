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
#include "FrameContext.h"
#include "GraphicsLayer.h"
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

  //! Graphics layer access
  [[nodiscard]] std::weak_ptr<GraphicsLayerIntegration> GetGraphics() noexcept
  {
    return full_graphics_;
  }
  [[nodiscard]] std::weak_ptr<GraphicsLayerIntegration>
  GetGraphics() const noexcept
  {
    return full_graphics_;
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
  void PhaseFrameStart(FrameContext& context);
  auto PhaseInput(FrameContext& context) -> co::Co<>; // async (simulated work)
  auto PhaseNetworkReconciliation(FrameContext& context)
    -> co::Co<>; // network packet application & reconciliation
  void PhaseRandomSeedManagement(); // synchronous random seed management for
                                    // determinism
  auto PhaseFixedSim(FrameContext& context)
    -> co::Co<>; // cooperative parallelism within deterministic phase
  auto PhaseGameplay(FrameContext& context)
    -> co::Co<>; // async (simulated work)
  auto PhaseSceneMutation(FrameContext& context)
    -> co::Co<>; // async (simulated work) - B2 barrier
  auto PhaseTransforms(FrameContext& context)
    -> co::Co<>; // async (simulated work)

  void PhaseSnapshot(FrameContext& context);

  auto ParallelTasks(FrameContext& context) -> co::Co<>;
  auto PhasePostParallel(FrameContext& context)
    -> co::Co<>; // async (simulated work)

  auto PhaseFrameGraph(FrameContext& context)
    -> co::Co<>; // async (simulated work)

  auto PhaseCommandRecord(FrameContext& context)

    -> co::Co<>; // async (simulated work)
  void PhasePresent(FrameContext& context); // synchronous presentation

  // Poll futures from async jobs (Assets, PSOs, BLAS, LightMaps)
  void PhaseAsyncPoll(FrameContext& context);

  void PhaseBudgetAdapt();

  void PhaseFrameEnd(FrameContext& context);

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

  void SetRenderGraphBuilder(FrameContext& context);
  void ClearRenderGraphBuilder(FrameContext& context);
  std::unique_ptr<RenderGraphBuilder> render_graph_builder_;

  std::vector<SyntheticTaskSpec> parallel_specs_ {};
  std::vector<ParallelResult> parallel_results_ {};
  std::mutex parallel_results_mutex_;
  std::vector<AsyncJobState> async_jobs_ {};

  oxygen::co::ThreadPool& pool_;
  EngineProps props_ {};
  co::Nursery* nursery_ { nullptr };
  uint64_t frame_index_ { 0 };

  // Timing helpers
  std::chrono::steady_clock::time_point frame_start_ts_ {};
  std::chrono::microseconds phase_accum_ { 0 };
  FrameSnapshot snapshot_ {};

  // HACK: temporary until full graphics integration
  GraphicsLayer graphics_;
  std::shared_ptr<GraphicsLayerIntegration> full_graphics_;

  // Module management system
  ModuleManager module_manager_;

  // Signals completion when FrameLoop exits.
  oxygen::co::Event completed_ {};
};

} // namespace oxygen::examples::asyncsim
