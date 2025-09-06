//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <ranges>
#include <thread>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Engine/AsyncEngine.h>
#include <Oxygen/Engine/EngineTag.h>
#include <Oxygen/Engine/Modules/ModuleManager.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/OxCo/Algorithms.h>
#include <Oxygen/OxCo/ThreadPool.h>
#include <Oxygen/Platform/Platform.h>

// Implementation of EngineTagFactory. Provides access to EngineTag capability
// tokens, only from the engine core.
namespace oxygen::engine::internal {
auto EngineTagFactory::Get() noexcept -> EngineTag { return EngineTag {}; }
} // namespace oxygen::engine::internal

using namespace std::chrono_literals;

namespace oxygen {

using core::PhaseId;

using namespace oxygen::engine;

AsyncEngine::AsyncEngine(std::shared_ptr<Platform> platform,
  std::weak_ptr<Graphics> graphics, EngineProps props) noexcept
  : props_(std::move(props))
  , platform_(std::move(platform))
  , gfx_weak_(std::move(graphics))
  , module_manager_(std::make_unique<ModuleManager>(observer_ptr { this }))
{
  CHECK_F(platform_ != nullptr);
  CHECK_F(!gfx_weak_.expired());
  CHECK_F(
    platform_->HasThreads(), "Platform must be configured with a thread pool");

  // Example synthetic parallel tasks (Category B)
  // parallel_specs_.push_back(
  //   { "Animation", TaskCategory::ParallelFrame, 2000us });
  // parallel_specs_.push_back({ "IK", TaskCategory::ParallelFrame, 1800us });
  // parallel_specs_.push_back(
  //   { "BlendShapes", TaskCategory::ParallelFrame, 1200us });
  // parallel_specs_.push_back(
  //   { "Particles", TaskCategory::ParallelFrame, 1500us });
  // parallel_specs_.push_back({ "Culling", TaskCategory::ParallelFrame, 1800us
  // }); parallel_specs_.push_back({ "LOD", TaskCategory::ParallelFrame, 1200us
  // }); parallel_specs_.push_back({ "AIBatch", TaskCategory::ParallelFrame,
  // 2200us }); parallel_specs_.push_back(
  //   { "LightClustering", TaskCategory::ParallelFrame, 1600us });
  // parallel_specs_.push_back(
  //   { "MaterialBaking", TaskCategory::ParallelFrame, 1400us });
  // parallel_specs_.push_back(
  //   { "GPUUploadStaging", TaskCategory::ParallelFrame, 800us });
  // parallel_specs_.push_back(
  //   { "OcclusionQuery", TaskCategory::ParallelFrame, 900us });

  // // Example async jobs (multi-frame Category C)
  // async_jobs_.push_back({ "AssetLoadA", 10ms, 0, false });
  // async_jobs_.push_back({ "ShaderCompileA", 15ms, 0, false });
  // async_jobs_.push_back({ "PSOBuild", 12ms, 0, false });
  // async_jobs_.push_back({ "BLASBuild", 25ms, 0, false });
  // async_jobs_.push_back({ "TLASRefit", 8ms, 0, false });
  // async_jobs_.push_back({ "LightmapBake", 45ms, 0, false });
  // async_jobs_.push_back({ "ProbeBake", 30ms, 0, false });
  // async_jobs_.push_back({ "NavMeshGen", 35ms, 0, false });
  // async_jobs_.push_back({ "ProceduralGeometry", 20ms, 0, false });
  // async_jobs_.push_back({ "GPUReadback", 5ms, 0, false });

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
    gfx_weak_.lock()->Shutdown();
  }

  // Shutdown the platform event pump and processing loops
  co_await platform_->Shutdown();

  // This will shut down all modules
  module_manager_.reset();
}

auto AsyncEngine::Stop() -> void
{
  if (nursery_) {
    nursery_->Cancel();
  }
  DLOG_F(INFO, "AsyncEngine Live Object stopped");
}

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

auto AsyncEngine::NextFrame() -> bool
{
  ++frame_number_;
  frame_slot_ = frame::Slot { static_cast<frame::Slot::UnderlyingType>(
    (frame_number_.get() - 1ULL) % frame::kFramesInFlight.get()) };

  if (props_.frame_count > 0 && frame_number_.get() > props_.frame_count) {
    return false; // Completed requested number of frames
  }
  return true;
}

auto AsyncEngine::FrameLoop() -> co::Co<>
{
  LOG_F(INFO, "Starting frame loop for {} frames (target_fps={})",
    props_.frame_count, props_.target_fps);

  frame_number_ = frame::SequenceNumber { 0 };
  frame_slot_ = frame::Slot { 0 };

  while (true) {
    if (!NextFrame()) {
      // Cancel that last increment
      frame_number_ = frame::SequenceNumber { frame_number_.get() - 1ULL };
      break;
    }

    LOG_SCOPE_F(INFO, fmt::format("Frame {}", frame_number_.get()).c_str());

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
    co_await PhaseSnapshot(context);

    // Launch and join Category B barriered parallel tasks (B4 upon completion).
    co_await ParallelTasks(context);
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

    // Yield control to thread pool
    co_await platform_->Threads().Run([](co::ThreadPool::CancelToken) { });

    // Always give a chance to detetct platform events and termination requests.
    co_await platform_->Async().SleepFor(1ms);

    // Check for termination requets
    if (platform_->Async().OnTerminate().Triggered()
      || platform_->Windows().LastWindowClosed().Triggered()) {
      LOG_F(INFO, "Termination requested, stopping frame loop...");
      break;
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

  context.SetFrameSequenceNumber(frame_number_, tag);
  context.SetFrameSlot(frame_slot_, tag);
  context.SetFrameStartTime(frame_start_ts_, tag);
  context.SetThreadPool(&platform_->Threads(), tag);
  context.SetGraphicsBackend(gfx_weak_, tag);

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

  LOG_F(1, "Frame {} start (epoch advance)", frame_number_);
}

auto AsyncEngine::PhaseInput(FrameContext& context) -> co::Co<>
{
  const auto tag = internal::EngineTagFactory::Get();
  context.SetCurrentPhase(PhaseId::kInput, tag);

  LOG_F(1, "[F{}][A] PhaseInput", frame_number_);

  // Engine core sets the current phase
  context.SetCurrentPhase(PhaseId::kInput, tag);

  // Execute module input processing first
  co_await module_manager_->ExecutePhase(PhaseId::kInput, context);
}

auto AsyncEngine::PhaseFixedSim(FrameContext& context) -> co::Co<>
{
  const auto tag = internal::EngineTagFactory::Get();
  context.SetCurrentPhase(PhaseId::kFixedSimulation, tag);

  LOG_F(1, "[F{}][A] PhaseFixedSim", frame_number_);
  // NOTE: This phase uses coroutines for cooperative parallelism within the
  // phase. Multiple physics modules can cooperate efficiently (rigid body,
  // particles, fluids, cloth, etc.) but the phase runs to completion before
  // engine continues. This maintains deterministic timing while enabling
  // modular efficiency.

  // Engine core sets the current phase
  context.SetCurrentPhase(PhaseId::kFixedSimulation, tag);

  // Execute module fixed simulation cooperatively
  co_await module_manager_->ExecutePhase(PhaseId::kFixedSimulation, context);

  // TODO: Engine's own fixed simulation work
  // Real implementation: physics integration, collision detection, constraint
  // solving
}

auto AsyncEngine::PhaseGameplay(FrameContext& context) -> co::Co<>
{
  const auto tag = internal::EngineTagFactory::Get();
  context.SetCurrentPhase(PhaseId::kGameplay, tag);

  LOG_F(1, "[F{}][A] PhaseGameplay", frame_number_);

  // Engine core sets the current phase
  context.SetCurrentPhase(PhaseId::kGameplay, tag);

  // Execute module gameplay logic first
  co_await module_manager_->ExecutePhase(PhaseId::kGameplay, context);
}

auto AsyncEngine::PhaseNetworkReconciliation(FrameContext& context) -> co::Co<>
{
  const auto tag = internal::EngineTagFactory::Get();
  context.SetCurrentPhase(PhaseId::kNetworkReconciliation, tag);

  LOG_F(1, "[F{}][A] PhaseNetworkReconciliation", frame_number_);

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

  LOG_F(1, "[F{}][A] PhaseRandomSeedManagement", frame_number_);
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

  LOG_F(1, "[F{}][A] PhaseSceneMutation (B2: structural integrity barrier)",
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

  LOG_F(1, "[F{}][A] PhaseTransforms", frame_number_);

  // Engine core sets the current phase
  context.SetCurrentPhase(PhaseId::kTransformPropagation, tag);

  // Execute module transform propagation first
  co_await module_manager_->ExecutePhase(
    PhaseId::kTransformPropagation, context);
}

auto AsyncEngine::PhaseSnapshot(FrameContext& context) -> co::Co<>
{
  const auto tag = internal::EngineTagFactory::Get();
  context.SetCurrentPhase(PhaseId::kSnapshot, tag);

  LOG_F(1, "[F{}][A] PhaseSnapshot (build immutable snapshot)", frame_number_);
  // Execute module snapshot handlers synchronously (main thread)
  co_await module_manager_->ExecutePhase(PhaseId::kSnapshot, context);

  // Engine consolidates contributions and publishes snapshots last
  context.PublishSnapshots(internal::EngineTagFactory::Get());
  co_return;
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

  LOG_F(1, "[F{}][A] PhaseFrameGraph", frame_number_);

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

  LOG_F(1,
    "[F{}][A] PhaseCommandRecord - {} surfaces (unified record+submit phase)",
    frame_number_, context.GetSurfaces().size());

  // Execute module command recording first
  co_await module_manager_->ExecutePhase(PhaseId::kCommandRecord, context);

  LOG_F(1, "[F{}][A] PhaseCommandRecord complete - modules recorded commands",
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

  LOG_F(1, "[F{}][A] PhasePresent - {} surfaces", frame_number_,
    presentable_surfaces.size());

  const auto gfx = gfx_weak_.lock();
  if (!gfx) {
    // TODO: Handle graphics backend invalidation
    throw std::logic_error("Graphics backend no longer valid.");
  }

  if (!presentable_surfaces.empty()) {
    gfx->PresentSurfaces(presentable_surfaces);
    context.ClearPresentableFlags(tag);
  }

  LOG_F(1, "[F{}][A] PhasePresent complete - all {} surfaces presented",
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
  LOG_F(1, "Frame {} end | total={}us", frame_number_, total.count());

  // Frame pacing
  if (props_.target_fps > 0) {
    const auto desired = std::chrono::nanoseconds(
      1'000'000'000ull / static_cast<uint64_t>(props_.target_fps));
    auto now = std::chrono::steady_clock::now();
    auto frame_elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
      now - frame_start_ts_);
    if (frame_elapsed < desired) {
      auto sleep_for = desired - frame_elapsed;
      LOG_F(INFO, "[F{}] Frame pacing: elapsed={}us target={}us sleeping={}us",
        frame_number_,
        std::chrono::duration_cast<std::chrono::microseconds>(frame_elapsed)
          .count(),
        std::chrono::duration_cast<std::chrono::microseconds>(desired).count(),
        std::chrono::duration_cast<std::chrono::microseconds>(sleep_for)
          .count());
      // std::this_thread::sleep_for(sleep_for);
      co_await platform_->Async().SleepFor(
        std::chrono::duration_cast<std::chrono::microseconds>(sleep_for));
    } else {
      LOG_F(INFO,
        "[F{}] Frame pacing: elapsed={}us exceeded target ({}us) no sleep",
        frame_number_,
        std::chrono::duration_cast<std::chrono::microseconds>(frame_elapsed)
          .count(),
        std::chrono::duration_cast<std::chrono::microseconds>(desired).count());
    }
  }
}

auto AsyncEngine::ParallelTasks(FrameContext& context) -> co::Co<>
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

  LOG_F(1, "[F{}][A] PhasePostParallel (integrate Category B outputs)",
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

} // namespace oxygen::engine::asyncsim
