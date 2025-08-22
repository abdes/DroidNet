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

#include "GraphicsLayer.h"

namespace oxygen::examples::asyncsim {

//! Basic synthetic task categories.
enum class TaskCategory { Ordered, ParallelFrame, AsyncPipeline, Detached };

struct SyntheticTaskSpec {
  std::string name;
  TaskCategory category { TaskCategory::ParallelFrame };
  std::chrono::microseconds cost { 1000 }; // simulated CPU time
};

struct ParallelResult {
  std::string name;
  std::chrono::microseconds duration { 0 };
};

struct AsyncJobState {
  std::string name;
  std::chrono::microseconds remaining { 0 };
  uint64_t submit_frame { 0 };
  bool ready { false };
};

struct FrameMetrics {
  uint64_t frame_index { 0 };
  std::chrono::microseconds frame_cpu_time { 0 };
  std::chrono::microseconds parallel_span { 0 };
  size_t parallel_jobs { 0 };
  size_t async_ready { 0 };
};

//! Engine configuration properties.
struct EngineProps {
  uint32_t target_fps { 0 }; //!< 0 = uncapped
};

//! Immutable per-frame snapshot passed to Category B parallel tasks
//! (placeholder).
struct FrameSnapshot {
  uint64_t frame_index { 0 };
};

//! Represents a rendering surface with command recording state
struct RenderSurface {
  std::string name;
  std::chrono::microseconds record_cost {
    800
  }; // simulated command recording time
  std::chrono::microseconds submit_cost { 200 }; // simulated submission time
  std::chrono::microseconds present_cost { 300 }; // simulated presentation time
  bool commands_recorded { false };
  bool commands_submitted { false };
};

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

private:
  // Ordered phases (Category A)
  void PhaseFrameStart();
  auto PhaseInput() -> co::Co<>; // async (simulated work)
  auto PhaseFixedSim() -> co::Co<>; // async (simulated work)
  auto PhaseGameplay() -> co::Co<>; // async (simulated work)
  auto PhaseNetworkReconciliation()
    -> co::Co<>; // network packet application & reconciliation
  auto PhaseRandomSeedManagement()
    -> co::Co<>; // random seed management for determinism
  auto PhaseSceneMutation() -> co::Co<>; // async (simulated work) - B2 barrier
  auto PhaseTransforms() -> co::Co<>; // async (simulated work)
  auto PhaseSnapshot() -> co::Co<>; // async (simulated work)
  auto PhasePostParallel() -> co::Co<>; // async (simulated work)
  auto PhaseFrameGraph() -> co::Co<>; // async (simulated work)
  auto PhaseDescriptorTablePublication()
    -> co::Co<>; // global descriptor/bindless table publication
  auto PhaseResourceStateTransitions()
    -> co::Co<>; // resource state transitions planning
  auto PhaseCommandRecord() -> co::Co<>; // async (simulated work)
  void PhasePresent(); // synchronous presentation

  // Multi-surface rendering helpers
  void RecordSurfaceCommands(
    const RenderSurface& surface, size_t surface_index);
  void SubmitSurfaceCommands(
    const RenderSurface& surface, size_t surface_index);
  void PresentSurface(const RenderSurface& surface, size_t surface_index);
  void PhaseAsyncPoll();
  void PhaseBudgetAdapt();
  void PhaseFrameEnd();

  // Launch structured parallel tasks
  void LaunchParallelTasks(); // legacy sync version (unused in async path)
  void JoinParallelTasks(); // legacy sync version (unused in async path)
  void ResetParallelSync();
  auto ParallelTasks() -> co::Co<>; // coroutine version (future integration)

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

  // Signals completion when FrameLoop exits.
  oxygen::co::Event completed_ {};
};

} // namespace oxygen::examples::asyncsim
