//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <ranges>
#include <thread>
#include <utility>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Engine/AsyncEngine.h>
#include <Oxygen/Engine/EngineTag.h>
#include <Oxygen/Engine/FrameContext.h>
#include <Oxygen/Engine/Modules/ModuleManager.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Input/InputSystem.h>
#include <Oxygen/OxCo/Algorithms.h>
#include <Oxygen/OxCo/ThreadPool.h>
#include <Oxygen/Platform/Platform.h>

// Implementation of EngineTagFactory. Provides access to EngineTag capability
// tokens, only from the engine core. When building tests, allow tests to
// override by defining OXYGEN_ENGINE_TESTING.
#if !defined(OXYGEN_ENGINE_TESTING)
namespace oxygen::engine::internal {
auto EngineTagFactory::Get() noexcept -> EngineTag { return EngineTag {}; }
} // namespace oxygen::engine::internal
#endif

using namespace std::chrono_literals;

namespace oxygen {

using core::PhaseId;
using namespace oxygen::engine;

// For convenience in timing configuration access
using TimingConfig = oxygen::TimingConfig;

AsyncEngine::AsyncEngine(std::shared_ptr<Platform> platform,
  std::weak_ptr<Graphics> graphics, oxygen::EngineConfig config) noexcept
  : config_(std::move(config))
  , platform_(std::move(platform))
  , gfx_weak_(std::move(graphics))
  , module_manager_(std::make_unique<ModuleManager>(observer_ptr { this }))
{
  CHECK_F(platform_ != nullptr);
  CHECK_F(!gfx_weak_.expired());
  CHECK_F(
    platform_->HasThreads(), "Platform must be configured with a thread pool");

  // Initialize timing system
  last_frame_time_ = std::chrono::steady_clock::now();
  timing_history_.fill(
    std::chrono::microseconds(16667)); // Initialize with 60Hz

  // Initialize detached services (Category D)
  InitializeDetachedServices();
}

AsyncEngine::~AsyncEngine() = default;

auto AsyncEngine::Run() -> void
{
  if (!module_manager_) {
    throw std::logic_error(
      "FrameLoop ended already. Engine needs to be re-created.");
  }
  // TODO: Register engine own modules

  CHECK_NOTNULL_F(
    nursery_, "Nursery must be opened via ActivateAsync before Run");

  nursery_->Start([this]() -> co::Co<> {
    co_await FrameLoop();
    // Signal completion once the frame loop has finished executing.
    LOG_F(INFO, "Engine completed after {} frames", frame_number_);
    co_await Shutdown();
    completed_.Trigger();
  });
}

auto AsyncEngine::Shutdown() -> co::Co<>
{
  // Ensure work is done and deferred reclaims are processed
  if (!gfx_weak_.expired()) {
    gfx_weak_.lock()->Flush();
  }
  // Shutdown the platform event pump
  co_await platform_->Shutdown();

  if (nursery_) {
    nursery_->Cancel();
  }
  DLOG_F(INFO, "AsyncEngine Live Object stopped");

  // This will shut down all modules
  module_manager_.reset();
}

auto AsyncEngine::Stop() -> void { shutdown_requested_ = true; }

// Register a module (takes ownership). Modules are sorted by priority.
auto AsyncEngine::RegisterModule(std::unique_ptr<EngineModule> module) noexcept
  -> bool
{
  return module_manager_->RegisterModule(std::move(module));
}

// Optional: unregister by name. Returns true if removed.
auto AsyncEngine::UnregisterModule(std::string_view name) noexcept -> void
{
  module_manager_->UnregisterModule(name);
}

auto AsyncEngine::GetEngineConfig() const noexcept
  -> const oxygen::EngineConfig&
{
  return config_;
}

auto AsyncEngine::NextFrame() -> bool
{
  ++frame_number_;
  frame_slot_ = frame::Slot { static_cast<frame::Slot::UnderlyingType>(
    (frame_number_.get() - 1ULL) % frame::kFramesInFlight.get()) };

  if (config_.frame_count > 0 && frame_number_.get() > config_.frame_count) {
    return false; // Completed requested number of frames
  }
  return true;
}

auto AsyncEngine::FrameLoop() -> co::Co<>
{
  LOG_F(INFO, "Starting frame loop for {} frames (target_fps={})",
    config_.frame_count, config_.target_fps);

  frame_number_ = frame::SequenceNumber { 0 };
  frame_slot_ = frame::Slot { 0 };
  // Initialize pacing deadline to now to start immediately
  next_frame_deadline_ = std::chrono::steady_clock::now();

  while (true) {
    if (shutdown_requested_) {
      LOG_F(INFO, "Shutdown requested, stopping frame loop...");
      co_await Shutdown();
      break;
    }
    // Check for termination requets
    if (platform_->Async().OnTerminate().Triggered()
      || platform_->Windows().LastWindowClosed().Triggered()) {
      LOG_F(INFO, "Termination requested, stopping frame loop...");
      break;
    }

    if (!NextFrame()) {
      // Cancel that last increment
      frame_number_ = frame::SequenceNumber { frame_number_.get() - 1ULL };
      break;
    }

    LOG_SCOPE_F(1, fmt::format("Frame {}", frame_number_.get()).c_str());

    // Create module context for this frame
    FrameContext context;

    // Fence polling, epoch advance, deferred destruction retirement
    co_await PhaseFrameStart(context);

    // B0: Input snapshot
    co_await PhaseInput(context);
    // Network packet application & reconciliation
    co_await PhaseNetworkReconciliation(context);
    // Random seed management for determinism (BEFORE any systems use
    // randomness)
    PhaseRandomSeedManagement(context);
    // B1: Fixed simulation deterministic state
    co_await PhaseFixedSim(context);
    // Variable gameplay logic
    co_await PhaseGameplay(context);
    // B2: Structural mutations
    co_await PhaseSceneMutation(context);
    // Transform propagation
    co_await PhaseTransforms(context);

    // Immutable snapshot build (B3)
    const auto& snapshot = co_await PhaseSnapshot(context);

    // Launch and join Category B barriered parallel tasks (B4 upon completion).
    co_await ParallelTasks(context, snapshot);
    // Serial post-parallel integration (Category A resumes after B4)
    co_await PhasePostParallel(context);

    {
      // SetRenderGraphBuilder(context);

      // Frame graph/render pass dependency planning, resource transitions,
      // optimization, bindless indices collection for the frame
      co_await PhaseFrameGraph(context);

      // Unified command recording and submission phase (parallel recording with
      // ordered submission)
      co_await PhaseCommandRecord(context);

      // ClearRenderGraphBuilder(context);
    }

    // TODO: Ensure PhaseRecord comes out with all pending command lists flushed

    // Synchronous sequential presentation
    PhasePresent(context);

    // Poll async pipeline readiness and integrate ready resources
    PhaseAsyncPoll(context);

    // Adaptive budget management for next frame
    PhaseBudgetAdapt();

    // Frame end timing and metrics
    co_await PhaseFrameEnd(context);

    // Yield control to thread pool before pacing so any residual
    // work doesn't skew the next frame start timestamp.
    co_await platform_->Threads().Run([](co::ThreadPool::CancelToken) { });

    // Deadline-based frame pacing for improved accuracy
    if (config_.target_fps > 0) {
      const auto period_ns = std::chrono::nanoseconds(
        1'000'000'000ull / static_cast<uint64_t>(config_.target_fps));

      // Establish or advance the next deadline monotonically from the frame
      // start time to target exact start-to-start periods.
      if (next_frame_deadline_.time_since_epoch().count() == 0) {
        next_frame_deadline_ = frame_start_ts_ + period_ns;
      } else {
        next_frame_deadline_ += period_ns;
      }
      const auto now = std::chrono::steady_clock::now();
      // If we fell significantly behind, re-synchronize to avoid accumulating
      // lag (late by more than one period).
      if (now > next_frame_deadline_ + period_ns) {
        next_frame_deadline_ = now + period_ns;
      }

      // Sleep until a little before the deadline to mitigate OS sleep
      // overshoot, then yield/spin-finish for precision.
      const auto safety_margin = config_.timing.pacing_safety_margin;
      if (next_frame_deadline_ > now) {
        const auto sleep_until_ts = next_frame_deadline_ - safety_margin;
        if (sleep_until_ts > now) {
          co_await platform_->Async().SleepFor(
            std::chrono::duration_cast<std::chrono::microseconds>(
              sleep_until_ts - now));
        }
        // Finish: cooperative tiny pauses until the deadline.
        for (;;) {
          const auto n2 = std::chrono::steady_clock::now();
          if (n2 >= next_frame_deadline_)
            break;
          // cooperative tiny pause (platform abstraction could provide this)
          std::this_thread::yield();
        }
      }

      LOG_F(2,
        "[F{}] Pacing to deadline: target={}us ({}ns), next deadline in {}us",
        frame_number_,
        std::chrono::duration_cast<std::chrono::microseconds>(period_ns)
          .count(),
        period_ns.count(),
        std::chrono::duration_cast<std::chrono::microseconds>(
          next_frame_deadline_ - now)
          .count());
    }
  }
  co_return;
}

auto AsyncEngine::PhaseFrameStart(FrameContext& context) -> co::Co<>
{
  const auto tag = internal::EngineTagFactory::Get();
  context.SetCurrentPhase(PhaseId::kFrameStart, tag);

  frame_start_ts_ = std::chrono::steady_clock::now();
  phase_accum_ = 0us;

  // TODO: setup all the properties of context that need to be set

  context.ClearViews(tag); // FIXME: for now we clear views every frame
  context.ClearPresentableFlags(tag);
  context.SetFrameSequenceNumber(frame_number_, tag);
  context.SetFrameSlot(frame_slot_, tag);
  context.SetFrameStartTime(frame_start_ts_, tag);
  context.SetThreadPool(&platform_->Threads(), tag);
  context.SetGraphicsBackend(gfx_weak_, tag);

  // Update timing data for this frame - must be called after SetFrameStartTime
  // but before modules execute so they have access to current timing data
  UpdateFrameTiming(context);

  // Initialize graphics layer for this frame
  auto gfx = gfx_weak_.lock();
  if (!gfx) {
    // TODO: Handle graphics backend invalidation
    throw std::logic_error("Graphics backend no longer valid.");
  }
  gfx->BeginFrame(frame_number_, frame_slot_);

  // Execute module frame start work
  co_await module_manager_->ExecutePhase(PhaseId::kFrameStart, context);

  // TODO: Implement epoch advance for resource lifetime management
  // Advance frame epoch counter for generation-based validation

  LOG_F(2, "Frame {} start (epoch advance)", frame_number_);
}

auto AsyncEngine::PhaseInput(FrameContext& context) -> co::Co<>
{
  const auto tag = internal::EngineTagFactory::Get();
  context.SetCurrentPhase(PhaseId::kInput, tag);

  LOG_F(2, "[F{}][A] PhaseInput", frame_number_);

  // Engine core sets the current phase
  context.SetCurrentPhase(PhaseId::kInput, tag);

  // Execute module input processing first
  co_await module_manager_->ExecutePhase(PhaseId::kInput, context);

  // Publish the input snapshot built by the InputSystem so that it becomes
  // available early in the frame to subsequent phases. The FrameContext
  // contract requires SetInputSnapshot to be called during kInput.
  if (module_manager_) {
    // Find the InputSystem module (by type id) and publish its snapshot if any
    for (auto& mod_ref : module_manager_->GetModules()) {
      auto* mod_ptr = &mod_ref;
      if (mod_ptr
        && mod_ptr->GetTypeId() == engine::InputSystem::ClassTypeId()) {
        auto* input_sys = static_cast<engine::InputSystem*>(mod_ptr);
        auto snap = input_sys->GetCurrentSnapshot();
        if (snap) {
          // Publish type-erased input snapshot directly as blob
          context.SetInputSnapshot(
            std::static_pointer_cast<const void>(std::move(snap)), tag);
        }
        break;
      }
    }
  }
}

auto AsyncEngine::PhaseFixedSim(FrameContext& context) -> co::Co<>
{
  const auto tag = internal::EngineTagFactory::Get();
  context.SetCurrentPhase(PhaseId::kFixedSimulation, tag);

  LOG_F(2, "[F{}][A] PhaseFixedSim", frame_number_);
  // NOTE: This phase uses coroutines for cooperative parallelism within the
  // phase. Multiple physics modules can cooperate efficiently (rigid body,
  // particles, fluids, cloth, etc.) but the phase runs to completion before
  // engine continues. This maintains deterministic timing while enabling
  // modular efficiency.

  // Enhanced fixed timestep implementation for deterministic simulation
  // Use timing configuration from EngineConfig
  const auto& timing_config = config_.timing;

  const auto& fixed_delta = timing_config.fixed_delta;
  const auto& max_accumulator = timing_config.max_accumulator;
  const uint32_t max_substeps = timing_config.max_substeps;

  // Prevent "spiral of death" by clamping accumulator
  accumulated_fixed_time_ = std::min(accumulated_fixed_time_, max_accumulator);

  uint32_t steps = 0;

  // Run fixed timestep simulation in substeps for deterministic timing
  while (accumulated_fixed_time_ >= fixed_delta && steps < max_substeps) {

    // Update module timing for this substep
    auto module_timing = context.GetModuleTimingData();
    module_timing.fixed_delta_time = fixed_delta;
    module_timing.fixed_steps_this_frame = steps + 1;
    context.SetModuleTimingData(module_timing, tag);

    // Engine core sets the current phase for this substep
    context.SetCurrentPhase(PhaseId::kFixedSimulation, tag);

    LOG_F(2, "[F{}][A] PhaseFixedSim substep {} (accumulated: {}us)",
      frame_number_, steps + 1, accumulated_fixed_time_.count());

    // Execute module fixed simulation cooperatively
    co_await module_manager_->ExecutePhase(PhaseId::kFixedSimulation, context);

    // TODO: Engine's own fixed simulation work
    // Real implementation: physics integration, collision detection, constraint
    // solving

    accumulated_fixed_time_ -= fixed_delta;
    ++steps;
  }

  if (steps == 0) {
    // If no substeps ran, still execute modules once for consistency
    LOG_F(2, "[F{}][A] PhaseFixedSim no substeps needed (accumulated: {}us)",
      frame_number_, accumulated_fixed_time_.count());
    co_await module_manager_->ExecutePhase(PhaseId::kFixedSimulation, context);
  }

  // Update final module timing with completed substep count and interpolation
  // alpha
  auto module_timing = context.GetModuleTimingData();
  module_timing.fixed_steps_this_frame = steps;
  if (fixed_delta.count() > 0) {
    module_timing.interpolation_alpha
      = static_cast<float>(accumulated_fixed_time_.count())
      / static_cast<float>(fixed_delta.count());
    module_timing.interpolation_alpha
      = std::clamp(module_timing.interpolation_alpha, 0.0f, 1.0f);
  }
  context.SetModuleTimingData(module_timing, tag);

  LOG_F(2, "[F{}][A] PhaseFixedSim completed {} substeps, alpha={:.3f}",
    frame_number_, steps, module_timing.interpolation_alpha);
}

auto AsyncEngine::PhaseGameplay(FrameContext& context) -> co::Co<>
{
  const auto tag = internal::EngineTagFactory::Get();
  context.SetCurrentPhase(PhaseId::kGameplay, tag);

  LOG_F(2, "[F{}][A] PhaseGameplay", frame_number_);

  // Engine core sets the current phase
  context.SetCurrentPhase(PhaseId::kGameplay, tag);

  // Execute module gameplay logic first
  co_await module_manager_->ExecutePhase(PhaseId::kGameplay, context);
}

auto AsyncEngine::PhaseNetworkReconciliation(FrameContext& context) -> co::Co<>
{
  const auto tag = internal::EngineTagFactory::Get();
  context.SetCurrentPhase(PhaseId::kNetworkReconciliation, tag);

  LOG_F(2, "[F{}][A] PhaseNetworkReconciliation", frame_number_);

  // Engine core sets the current phase
  context.SetCurrentPhase(PhaseId::kNetworkReconciliation, tag);

  // TODO: Implement network packet application & authoritative reconciliation
  // Apply received network packets to authoritative game state
  // Reconcile client predictions with server authority
  co_return;
}

auto AsyncEngine::PhaseRandomSeedManagement(FrameContext& context) -> void
{
  const auto tag = internal::EngineTagFactory::Get();
  context.SetCurrentPhase(PhaseId::kRandomSeedManagement, tag);

  LOG_F(2, "[F{}][A] PhaseRandomSeedManagement", frame_number_);
  // CRITICAL: This phase must execute BEFORE any systems that consume
  // randomness to ensure deterministic behavior across runs and network
  // clients.
  //
  // Systems that depend on deterministic randomness include:
  // - Physics simulation (particle dynamics, soft body physics)
  // - AI decision making and pathfinding
  // - Gameplay mechanics (weapon spread, critical hits, loot generation)
  // - Procedural content generation
  // - Animation noise and procedural animation
  // - Particle systems
  // - Audio variation systems
  //
  // Random seed advancement strategy:
  // 1. Advance global seed based on frame index for temporal consistency
  // 2. Branch seeds for different subsystems to avoid cross-contamination
  // 3. Ensure seeds are synchronized across network clients after
  // reconciliation

  // TODO: Implement deterministic random seed management
  // Real implementation would be:
  // - globalSeed = hashFunction(frame_index_, networkSeed_);
  // - physicsSeed = branchSeed(globalSeed, PHYSICS_STREAM);
  // - aiSeed = branchSeed(globalSeed, AI_STREAM);
  // - gameplaySeed = branchSeed(globalSeed, GAMEPLAY_STREAM);
  // This is pure computation - no I/O, no waiting, deterministic timing
}

auto AsyncEngine::PhaseSceneMutation(FrameContext& context) -> co::Co<>
{
  const auto tag = internal::EngineTagFactory::Get();
  context.SetCurrentPhase(PhaseId::kSceneMutation, tag);

  LOG_F(2, "[F{}][A] PhaseSceneMutation (B2: structural integrity barrier)",
    frame_number_);

  // Engine core sets the current phase
  context.SetCurrentPhase(PhaseId::kSceneMutation, tag);

  // Execute module scene mutations first
  co_await module_manager_->ExecutePhase(PhaseId::kSceneMutation, context);
}

auto AsyncEngine::PhaseTransforms(FrameContext& context) -> co::Co<>
{
  const auto tag = internal::EngineTagFactory::Get();
  context.SetCurrentPhase(PhaseId::kTransformPropagation, tag);

  LOG_F(2, "[F{}][A] PhaseTransforms", frame_number_);

  // Engine core sets the current phase
  context.SetCurrentPhase(PhaseId::kTransformPropagation, tag);

  // Execute module transform propagation first
  co_await module_manager_->ExecutePhase(
    PhaseId::kTransformPropagation, context);
}

auto AsyncEngine::PhaseSnapshot(FrameContext& context)
  -> co::Co<const UnifiedSnapshot&>
{
  const auto tag = internal::EngineTagFactory::Get();
  context.SetCurrentPhase(PhaseId::kSnapshot, tag);

  LOG_F(2, "[F{}][A] PhaseSnapshot (build immutable snapshot)", frame_number_);
  // Execute module snapshot handlers synchronously (main thread)
  co_await module_manager_->ExecutePhase(PhaseId::kSnapshot, context);

  // Engine consolidates contributions and publishes snapshots last
  const auto& snapshot
    = context.PublishSnapshots(internal::EngineTagFactory::Get());
  LOG_F(2, "[F{}][A] Published snapshots v{}", frame_number_,
    snapshot.frameSnapshot.validation.snapshot_version);

  co_return snapshot;
}

// void AsyncEngine::SetRenderGraphBuilder(FrameContext& context)
// {
//   render_graph_builder_ = std::make_unique<RenderGraphBuilder>();
//   auto tag = internal::EngineTagFactory::Get();

//   // Initialize the builder with the current frame context
//   render_graph_builder_->BeginGraph(context);

//   // Set the builder in the frame context so modules can access it
//   context.SetRenderGraphBuilder(
//     observer_ptr { render_graph_builder_.get() }, tag);
// }

// void AsyncEngine::ClearRenderGraphBuilder(FrameContext& context)
// {
//   auto tag = internal::EngineTagFactory::Get();
//   context.SetRenderGraphBuilder(
//     observer_ptr<RenderGraphBuilder> {}, tag);
//   render_graph_builder_.reset();
// }

auto AsyncEngine::PhaseFrameGraph(FrameContext& context) -> co::Co<>
{
  const auto tag = internal::EngineTagFactory::Get();
  context.SetCurrentPhase(PhaseId::kFrameGraph, tag);

  LOG_F(2, "[F{}][A] PhaseFrameGraph", frame_number_);

  // Engine core sets the current phase
  context.SetCurrentPhase(PhaseId::kFrameGraph, tag);

  // Execute module frame graph work - modules will use
  // context.GetRenderGraphBuilder()
  co_await module_manager_->ExecutePhase(PhaseId::kFrameGraph, context);
}

auto AsyncEngine::PhaseCommandRecord(FrameContext& context) -> co::Co<>
{
  const auto tag = internal::EngineTagFactory::Get();
  context.SetCurrentPhase(PhaseId::kCommandRecord, tag);

  LOG_F(2,
    "[F{}][A] PhaseCommandRecord - {} surfaces (unified record+submit phase)",
    frame_number_, context.GetSurfaces().size());

  // Execute module command recording first
  co_await module_manager_->ExecutePhase(PhaseId::kCommandRecord, context);

  LOG_F(2, "[F{}][A] PhaseCommandRecord complete - modules recorded commands",
    frame_number_);
}

auto AsyncEngine::PhasePresent(FrameContext& context) -> void
{
  const auto tag = internal::EngineTagFactory::Get();
  context.SetCurrentPhase(PhaseId::kPresent, tag);

  // If modules marked surfaces as presentable during rendering, use those
  // flags to determine which surfaces to present. This allows modules to
  // mark surfaces as ready asynchronously and the engine to present them.
  const auto presentable_surfaces = context.GetPresentableSurfaces();

  LOG_F(2, "[F{}][A] PhasePresent - {} surfaces", frame_number_,
    presentable_surfaces.size());

  const auto gfx = gfx_weak_.lock();
  if (!gfx) {
    // TODO: Handle graphics backend invalidation
    throw std::logic_error("Graphics backend no longer valid.");
  }

  if (!presentable_surfaces.empty()) {
    gfx->PresentSurfaces(presentable_surfaces);
  }

  LOG_F(2, "[F{}][A] PhasePresent complete - all {} surfaces presented",
    frame_number_, presentable_surfaces.size());
}

auto AsyncEngine::PhaseAsyncPoll(FrameContext& context) -> void
{
  // Engine core sets the current phase for async work
  const auto tag = internal::EngineTagFactory::Get();
  context.SetCurrentPhase(PhaseId::kAsyncPoll, tag);

  // Execute module async work (fire and forget style for now)
  auto async_work = module_manager_->ExecutePhase(PhaseId::kAsyncPoll, context);
}

auto AsyncEngine::PhaseBudgetAdapt() -> void
{
  // TODO: Implement adaptive budget management
  // Monitor CPU frame time, GPU idle %, and queue depths
  // Degrade/defer tasks when over budget (IK refinement, particle collisions,
  // GI updates) Upgrade tasks when under budget (extra probe updates, higher
  // LOD, prefetch assets) Provide hysteresis to avoid oscillation
  // (time-window averaging)
}

auto AsyncEngine::PhaseFrameEnd(FrameContext& context) -> co::Co<>
{
  const auto tag = internal::EngineTagFactory::Get();
  context.SetCurrentPhase(PhaseId::kFrameEnd, tag);

  // Execute module frame end work first
  co_await module_manager_->ExecutePhase(PhaseId::kFrameEnd, context);

  // Finalize graphics layer for this frame
  auto gfx = gfx_weak_.lock();
  if (!gfx) {
    // TODO: Handle graphics backend invalidation
    throw std::logic_error("Graphics backend no longer valid.");
  }
  gfx->EndFrame(frame_number_, frame_slot_);

  auto frame_end = std::chrono::steady_clock::now();
  auto total = std::chrono::duration_cast<std::chrono::microseconds>(
    frame_end - frame_start_ts_);
  LOG_F(2, "Frame {} end | total={}us", frame_number_, total.count());
}

auto AsyncEngine::ParallelTasks(
  FrameContext& context, const UnifiedSnapshot& snapshot) -> co::Co<>
{
  const auto tag = internal::EngineTagFactory::Get();
  context.SetCurrentPhase(PhaseId::kParallelTasks, tag);

  // parallel_results_.clear();
  // parallel_results_.reserve(parallel_specs_.size());

  // LOG_F(1, "[F{}][B] Dispatching {} parallel tasks + module parallel work",
  //   frame_index_, parallel_specs_.size());

  // // Engine core sets the current phase for parallel work
  // auto tag = internal::EngineTagFactory::Get();
  // context.SetCurrentPhase(PhaseId::kParallelWork, tag);

  // // Create vector of coroutines following the test pattern
  // std::vector<co::Co<>> jobs;
  // jobs.reserve(parallel_specs_.size() + 1); // +1 for module parallel work

  // // Add module parallel work as first job
  // jobs.emplace_back(module_manager_.ExecuteParallelWork(context));

  // // Create each coroutine directly like in the MuxRange tests
  // for (const auto& spec : parallel_specs_) {
  //   // Create coroutine with proper parameter capture using immediate
  //   lambda
  //   // invocation. Parallel tasks operate on immutable snapshot (Category
  //   B):
  //   // - Animation: pose evaluation on immutable skeleton data
  //   // - IK: Inverse Kinematics solving separate from animation
  //   // - BlendShapes: morph target weights calculation
  //   // - Particles: per-system simulation producing private buffers
  //   // - Culling: frustum, portal, BVH, occlusion on immutable spatial
  //   indices
  //   // - LOD: selection & impostor decisions
  //   // - AIBatch: batch evaluation & pathfinding queries (read-only world)
  //   // - LightClustering: tiled/clustered light culling (CPU portion)
  //   // - MaterialBaking: dynamic parameter baking / uniform block packing
  //   // - GPUUploadStaging: population (writes into reserved
  //   sub-allocations)
  //   // - OcclusionQuery: reduction from prior frame
  //   jobs.push_back([this](std::string task_name,
  //                    std::chrono::microseconds task_cost) -> co::Co<> {
  //     LOG_F(1, "[F{}][B][START] {} (cost {}us)", frame_index_, task_name,
  //       task_cost.count());

  //     auto start = std::chrono::steady_clock::now();
  //     co_await SimulateWork(task_name, task_cost);
  //     auto end = std::chrono::steady_clock::now();

  //     ParallelResult r { task_name,
  //       std::chrono::duration_cast<std::chrono::microseconds>(end - start)
  //       };
  //     {
  //       std::scoped_lock lk(parallel_results_mutex_);
  //       parallel_results_.push_back(r);
  //     }
  //     LOG_F(1, "[F{}][B][DONE] {} ({}us)", frame_index_, r.name,
  //       r.duration.count());
  //     co_return;
  //   }(spec.name, spec.cost)); // Immediately invoke with the current spec
  //   values
  // }

  // LOG_F(1, "[F{}][B] Awaiting parallel barrier ({} tasks)", frame_index_,
  //   jobs.size());
  // co_await co::AllOf(std::move(jobs));
  // LOG_F(1, "[F{}][B4 complete] Barrier complete", frame_index_);
  co_return;
}

auto AsyncEngine::PhasePostParallel(FrameContext& context) -> co::Co<>
{
  const auto tag = internal::EngineTagFactory::Get();
  context.SetCurrentPhase(PhaseId::kPostParallel, tag);

  LOG_F(2, "[F{}][A] PhasePostParallel (integrate Category B outputs)",
    frame_number_);

  // Execute module post-parallel integration first
  co_await module_manager_->ExecutePhase(PhaseId::kPostParallel, context);

  // TODO: execute engine's own post-parallel work
}

auto AsyncEngine::InitializeDetachedServices() -> void
{
  LOG_F(1, "Initializing detached services (Category D)");

  // TODO: Initialize crash dump detection service
  // Set up crash dump monitoring and symbolication service
  // This service runs detached from frame loop and handles crash reporting
  LOG_F(1, "[D] Crash dump detection service initialized");
}

auto AsyncEngine::UpdateFrameTiming(FrameContext& context) -> void
{
  const auto current_time = std::chrono::steady_clock::now();
  const auto raw_delta = current_time - last_frame_time_;

  // Clamp delta to prevent "spiral of death" - convert to microseconds first
  const auto raw_delta_us
    = std::chrono::duration_cast<std::chrono::microseconds>(raw_delta);
  const auto clamped_delta = (std::min)(raw_delta_us,
    std::chrono::microseconds(50000)); // Max 50ms

  // Update timing history for smoothing
  timing_history_[timing_index_] = clamped_delta;
  timing_index_ = (timing_index_ + 1) % kTimingSamples;

  // Calculate smoothed delta for engine use
  auto total = std::chrono::microseconds(0);
  for (const auto& sample : timing_history_) {
    total += sample;
  }
  const auto smoothed_delta = total / kTimingSamples;

  // Build clean module timing data (no engine internals)
  engine::ModuleTimingData module_timing;

  // Game time with scaling and pause support
  if (!module_timing.is_paused) {
    // Scale the delta time - convert scale to duration multiplier
    const auto scale_factor = module_timing.time_scale;
    const auto scaled_delta_ns
      = std::chrono::duration_cast<std::chrono::nanoseconds>(clamped_delta)
          .count()
      * scale_factor;
    const auto scaled_delta
      = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::nanoseconds(
          static_cast<std::chrono::nanoseconds::rep>(scaled_delta_ns)));
    module_timing.game_delta_time = scaled_delta;

    // Accumulate time for PhaseFixedSim (engine internal)
    accumulated_fixed_time_ += scaled_delta;
  } else {
    module_timing.game_delta_time = std::chrono::microseconds(0);
  }

  // Get timing configuration from EngineConfig
  const auto& timing_config = config_.timing;

  // Fixed timestep data (from engine configuration)
  module_timing.fixed_delta_time = timing_config.fixed_delta;

  // Calculate interpolation alpha for smooth rendering
  if (timing_config.fixed_delta.count() > 0) {
    module_timing.interpolation_alpha
      = static_cast<float>(accumulated_fixed_time_.count())
      / static_cast<float>(timing_config.fixed_delta.count());
    module_timing.interpolation_alpha
      = std::clamp(module_timing.interpolation_alpha, 0.0f, 1.0f);
  }

  // Performance metrics
  module_timing.current_fps = clamped_delta.count() > 0
    ? 1000000.0f / static_cast<float>(clamped_delta.count())
    : 0.0f;

  // Set clean timing data in context using EngineTag
  const auto tag = internal::EngineTagFactory::Get();
  context.SetModuleTimingData(module_timing, tag);

  DLOG_F(2,
    "[F{}] Frame timing: raw={}us, clamped={}us, smoothed={}us, "
    "accumulated={}us, fps={:.1f}",
    frame_number_, raw_delta.count(), clamped_delta.count(),
    smoothed_delta.count(), accumulated_fixed_time_.count(),
    module_timing.current_fps);

#if !defined(NDEBUG)
  // Engine health summary every second
  static auto last_health_log = std::chrono::steady_clock::now();
  static frame::SequenceNumber last_frame_count = frame_number_;

  if (current_time - last_health_log >= 1s) {
    const auto frames_this_second
      = frame_number_.get() - last_frame_count.get();
    const auto fps_efficiency = frames_this_second > 0
      ? (frames_this_second / static_cast<float>(config_.target_fps)) * 100.0f
      : 100.0f;
    const auto fixed_time_health = timing_config.fixed_delta.count() > 0
      ? (accumulated_fixed_time_.count() * 100.0f)
        / static_cast<float>(timing_config.fixed_delta.count())
      : 0.0f;

    LOG_SCOPE_F(INFO,
      fmt::format("Engine Health Summary ({})", frame_number_.get()).c_str());
    LOG_F(INFO, "Instantaneous FPS : {:.1f}", module_timing.current_fps);
    LOG_F(INFO, "Frames this sec   : {}/{} ({:.1f}%)", frames_this_second,
      config_.target_fps, fps_efficiency);
    LOG_F(INFO, "Fixed sim health  : {:.1f}%", fixed_time_health);

    last_health_log = current_time;
    last_frame_count = frame_number_;
  }
#endif // !NDEBUG

  last_frame_time_ = current_time;
}

} // namespace oxygen::engine::asyncsim
