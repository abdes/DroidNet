//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "AsyncEngineSimulator.h"

#include <algorithm>
#include <ranges>
#include <thread>

#include "./Modules/RenderGraphModule.h"
#include "Renderer/Graph/RenderGraphBuilder.h"
#include <Oxygen/Engine/FrameContext.h>
#include <Oxygen/Engine/Modules/ModuleManager.h>

using namespace std::chrono_literals;

// Engine core implementation of EngineTagFactory
// This provides access to EngineTag capability tokens for engine-internal
// operations
namespace oxygen::engine::internal {
EngineTag EngineTagFactory::Get() noexcept { return EngineTag {}; }
}

namespace oxygen::engine::asyncsim {

AsyncEngineSimulator::AsyncEngineSimulator(
  oxygen::co::ThreadPool& pool, EngineConfig props) noexcept
  : pool_(pool)
  , props_(props)
{
  // HACK: Create a full graphics layer integration instance
  full_graphics_ = std::make_shared<GraphicsLayerIntegration>(graphics_);

  // Example synthetic parallel tasks (Category B)
  parallel_specs_.push_back(
    { "Animation", TaskCategory::ParallelFrame, 2000us });
  parallel_specs_.push_back({ "IK", TaskCategory::ParallelFrame, 1800us });
  parallel_specs_.push_back(
    { "BlendShapes", TaskCategory::ParallelFrame, 1200us });
  parallel_specs_.push_back(
    { "Particles", TaskCategory::ParallelFrame, 1500us });
  parallel_specs_.push_back({ "Culling", TaskCategory::ParallelFrame, 1800us });
  parallel_specs_.push_back({ "LOD", TaskCategory::ParallelFrame, 1200us });
  parallel_specs_.push_back({ "AIBatch", TaskCategory::ParallelFrame, 2200us });
  parallel_specs_.push_back(
    { "LightClustering", TaskCategory::ParallelFrame, 1600us });
  parallel_specs_.push_back(
    { "MaterialBaking", TaskCategory::ParallelFrame, 1400us });
  parallel_specs_.push_back(
    { "GPUUploadStaging", TaskCategory::ParallelFrame, 800us });
  parallel_specs_.push_back(
    { "OcclusionQuery", TaskCategory::ParallelFrame, 900us });

  // Example async jobs (multi-frame Category C)
  async_jobs_.push_back({ "AssetLoadA", 10ms, 0, false });
  async_jobs_.push_back({ "ShaderCompileA", 15ms, 0, false });
  async_jobs_.push_back({ "PSOBuild", 12ms, 0, false });
  async_jobs_.push_back({ "BLASBuild", 25ms, 0, false });
  async_jobs_.push_back({ "TLASRefit", 8ms, 0, false });
  async_jobs_.push_back({ "LightmapBake", 45ms, 0, false });
  async_jobs_.push_back({ "ProbeBake", 30ms, 0, false });
  async_jobs_.push_back({ "NavMeshGen", 35ms, 0, false });
  async_jobs_.push_back({ "ProceduralGeometry", 20ms, 0, false });
  async_jobs_.push_back({ "GPUReadback", 5ms, 0, false });

  // Initialize detached services (Category D)
  InitializeDetachedServices();

  // Register some initial persistent resources with the Graphics layer
  auto& registry = graphics_.GetResourceRegistry();
  registry.RegisterResource("FrameUniformBuffer");
  registry.RegisterResource("GlobalVertexBuffer");
  registry.RegisterResource("GlobalIndexBuffer");
  registry.RegisterResource("ShadowMapAtlas");
  registry.RegisterResource("EnvironmentMap");
}

AsyncEngineSimulator::~AsyncEngineSimulator() = default;

auto AsyncEngineSimulator::Run(uint32_t frame_count) -> void
{
  module_manager_ = std::make_unique<ModuleManager>(this);

  CHECK_F(nursery_ != nullptr,
    "Nursery must be opened via StartAsync before Run (call StartAsync first)");
  nursery_->Start(
    [this, frame_count]() -> co::Co<> { co_await FrameLoop(frame_count); });
}

auto AsyncEngineSimulator::InitializeModules() -> co::Co<>
{
  // Modules get engine reference, not FrameContext for init/shutdown
  co_await module_manager_.InitializeModules(*this);
}

auto AsyncEngineSimulator::ShutdownModules() -> co::Co<>
{
  // Modules get engine reference, not FrameContext for init/shutdown
  co_await module_manager_.ShutdownModules(*this);
}

auto AsyncEngineSimulator::FrameLoop(uint32_t frame_count) -> co::Co<>
{
  LOG_F(INFO, "Starting frame loop for {} frames (target_fps={})", frame_count,
    props_.target_fps);

  // Initialize modules before frame loop
  co_await InitializeModules();

  for (uint32_t i = 0; i < frame_count; ++i) {
    LOG_SCOPE_F(INFO, fmt::format("Frame {}", i).c_str());
    frame_index_ = i;

    // Create module context for this frame
    FrameContext context;
    auto engineTag = internal::EngineTagFactory::Get();

    // Configure engine state with current frame
    context.SetThreadPool(&pool_, engineTag);
    context.SetGraphicsBackend(std::weak_ptr<Graphics> {},
      engineTag); // TODO: Set proper graphics reference

    // Advance frame index to current frame
    for (uint64_t j = 0; j < frame_index_; ++j) {
      context.AdvanceFrameIndex(engineTag);
    }

    // TODO: SetSurfacesPtr equivalent needs to be implemented in new API
    // context.SetSurfacesPtr(&surfaces_);

    // Fence polling, epoch advance, deferred destruction retirement
    PhaseFrameStart(context);

    // B0: Input snapshot
    co_await PhaseInput(context);
    // Network packet application & reconciliation
    co_await PhaseNetworkReconciliation(context);
    // Random seed management for determinism (BEFORE any systems use
    // randomness)
    PhaseRandomSeedManagement();
    // B1: Fixed simulation deterministic state
    co_await PhaseFixedSim(context);
    // Variable gameplay logic
    co_await PhaseGameplay(context);
    // B2: Structural mutations
    co_await PhaseSceneMutation(context);
    // Transform propagation
    co_await PhaseTransforms(context);

    // Immutable snapshot build (B3)
    PhaseSnapshot(context);

    // Launch and join Category B barriered parallel tasks (B4 upon completion).
    co_await ParallelTasks(context);
    // Serial post-parallel integration (Category A resumes after B4)
    co_await PhasePostParallel(context);

    {
      SetRenderGraphBuilder(context);

      // Frame graph/render pass dependency planning, resource transitions,
      // optimization, bindless indices collection for the frame
      co_await PhaseFrameGraph(context);

      // Unified command recording and submission phase (parallel recording with
      // ordered submission)
      co_await PhaseCommandRecord(context);

      ClearRenderGraphBuilder(context);
    }

    // Synchronous sequential presentation
    PhasePresent(context);

    // Poll async pipeline readiness and integrate ready resources
    PhaseAsyncPoll(context);

    // Adaptive budget management for next frame
    PhaseBudgetAdapt();

    // Frame end timing and metrics
    PhaseFrameEnd(context);

    // Yield control to thread pool
    co_await pool_.Run([](co::ThreadPool::CancelToken) { });
  }

  // Shutdown modules after frame loop
  co_await ShutdownModules();

  // Signal completion once the frame loop has finished executing.
  LOG_F(INFO,
    "Simulation complete after {} frames. Triggering completion event.",
    frame_count);
  completed_.Trigger();
  co_return;
}

void AsyncEngineSimulator::PhaseFrameStart(FrameContext& context)
{
  frame_start_ts_ = std::chrono::steady_clock::now();
  phase_accum_ = 0us;

  // Reset presentable flags for new frame - surfaces start as not presentable
  context.ClearPresentableFlags(internal::EngineTagFactory::Get());
  context.SetCurrentPhase(
    FrameContext::FramePhase::FrameStart, internal::EngineTagFactory::Get());
  context.SetFrameStartTime(frame_start_ts_, internal::EngineTagFactory::Get());
  context.SetGraphicsBackend(full_graphics_, internal::EngineTagFactory::Get());
  // TODO: setup all the properties of context that need to be set at start of
  // frame

  // Initialize graphics layer for this frame
  graphics_.BeginFrame(frame_index_);

  // Execute module frame start work
  module_manager_.ExecuteFrameStart(context);

  // TODO: Implement epoch advance for resource lifetime management
  // Advance frame epoch counter for generation-based validation

  LOG_F(1, "Frame {} start (epoch advance)", frame_index_);
}
auto AsyncEngineSimulator::PhaseInput(FrameContext& context) -> co::Co<>
{
  LOG_F(1, "[F{}][A] PhaseInput", frame_index_);

  // Engine core sets the current phase
  auto engine_tag = internal::EngineTagFactory::Get();
  context.SetCurrentPhase(FrameContext::FramePhase::Input, engine_tag);

  // Execute module input processing first
  co_await module_manager_.ExecuteInput(context);

  // Then execute engine's own input processing
  co_await SimulateWorkOrdered(500us);
}
auto AsyncEngineSimulator::PhaseFixedSim(FrameContext& context) -> co::Co<>
{
  LOG_F(1, "[F{}][A] PhaseFixedSim", frame_index_);
  // NOTE: This phase uses coroutines for cooperative parallelism within the
  // phase. Multiple physics modules can cooperate efficiently (rigid body,
  // particles, fluids, cloth, etc.) but the phase runs to completion before
  // engine continues. This maintains deterministic timing while enabling
  // modular efficiency.

  // Engine core sets the current phase
  auto engine_tag = internal::EngineTagFactory::Get();
  context.SetCurrentPhase(
    FrameContext::FramePhase::FixedSimulation, engine_tag);

  // Execute module fixed simulation cooperatively
  co_await module_manager_.ExecuteFixedSimulation(context);

  // Engine's own fixed simulation work
  // Real implementation: physics integration, collision detection, constraint
  // solving
  co_await SimulateWorkOrdered(1000us);
}
auto AsyncEngineSimulator::PhaseGameplay(FrameContext& context) -> co::Co<>
{
  LOG_F(1, "[F{}][A] PhaseGameplay", frame_index_);

  // Engine core sets the current phase
  auto engine_tag = internal::EngineTagFactory::Get();
  context.SetCurrentPhase(FrameContext::FramePhase::Gameplay, engine_tag);

  // Execute module gameplay logic first
  co_await module_manager_.ExecuteGameplay(context);

  // Then execute engine's own gameplay logic
  co_await SimulateWorkOrdered(1500us);
}
auto AsyncEngineSimulator::PhaseNetworkReconciliation(FrameContext& context)
  -> co::Co<>
{
  LOG_F(1, "[F{}][A] PhaseNetworkReconciliation", frame_index_);

  // Engine core sets the current phase
  auto engine_tag = internal::EngineTagFactory::Get();
  context.SetCurrentPhase(
    FrameContext::FramePhase::NetworkReconciliation, engine_tag);

  // Execute module network reconciliation first
  co_await module_manager_.ExecuteNetworkReconciliation(context);

  // TODO: Implement network packet application & authoritative reconciliation
  // Apply received network packets to authoritative game state
  // Reconcile client predictions with server authority
  co_await SimulateWorkOrdered(300us);
}
void AsyncEngineSimulator::PhaseRandomSeedManagement()
{
  LOG_F(1, "[F{}][A] PhaseRandomSeedManagement", frame_index_);
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
auto AsyncEngineSimulator::PhaseSceneMutation(FrameContext& context) -> co::Co<>
{
  LOG_F(1, "[F{}][A] PhaseSceneMutation (B2: structural integrity barrier)",
    frame_index_);

  // Engine core sets the current phase
  auto engine_tag = internal::EngineTagFactory::Get();
  context.SetCurrentPhase(FrameContext::FramePhase::SceneMutation, engine_tag);

  // Execute module scene mutations first
  co_await module_manager_.ExecuteSceneMutation(context);

  // Simulate dynamic resource creation during scene mutations
  auto& registry = graphics_.GetResourceRegistry();
  auto& allocator = graphics_.GetDescriptorAllocator();

  // Simulate creating new dynamic resources (streaming assets, UI textures,
  // etc.)
  if (frame_index_ % 4 == 0) {
    // Every 4th frame, simulate loading new streaming textures
    // 1. Allocate descriptor slot first (reserve space in global heap)
    auto descriptor_id = allocator.AllocateDescriptor();
    // 2. Create GPU resource and register handle (maps to descriptor slot)
    auto texture_handle = registry.RegisterResource(
      "StreamingTexture_" + std::to_string(frame_index_));
    LOG_F(2, "[F{}] Created streaming texture {} with descriptor {}",
      frame_index_, texture_handle, descriptor_id);
  }

  if (frame_index_ % 7 == 0) {
    // Every 7th frame, simulate creating temporary render targets
    // 1. Allocate descriptor slot first
    auto descriptor_id = allocator.AllocateDescriptor();
    // 2. Create GPU resource and register handle
    auto rt_handle = registry.RegisterResource(
      "TempRenderTarget_" + std::to_string(frame_index_));
    LOG_F(2, "[F{}] Created temp render target {} with descriptor {}",
      frame_index_, rt_handle, descriptor_id);
  }

  // TODO: Implement scene graph structural mutations
  // Apply spawn/despawn, reparent operations, handle allocations
  // Ensure structural integrity before transform propagation
  co_await SimulateWorkOrdered(300us);
}
auto AsyncEngineSimulator::PhaseTransforms(FrameContext& context) -> co::Co<>
{
  LOG_F(1, "[F{}][A] PhaseTransforms", frame_index_);

  // Engine core sets the current phase
  auto engine_tag = internal::EngineTagFactory::Get();
  context.SetCurrentPhase(
    FrameContext::FramePhase::TransformPropagation, engine_tag);

  // Execute module transform propagation first
  co_await module_manager_.ExecuteTransformPropagation(context);

  // Then execute engine's own transform work
  co_await SimulateWorkOrdered(400us);
}

void AsyncEngineSimulator::PhaseSnapshot(FrameContext& context)
{
  LOG_F(1, "[F{}][A] PhaseSnapshot (build immutable snapshot)", frame_index_);
  context.PublishSnapshots(internal::EngineTagFactory::Get());
}

auto AsyncEngineSimulator::PhasePostParallel(FrameContext& context) -> co::Co<>
{
  LOG_F(1, "[F{}][A] PhasePostParallel (integrate Category B outputs)",
    frame_index_);

  // Execute module post-parallel integration first
  co_await module_manager_.ExecutePostParallel(context);

  // Then execute engine's own post-parallel work
  co_await SimulateWorkOrdered(600us);
}

void AsyncEngineSimulator::SetRenderGraphBuilder(FrameContext& context)
{
  render_graph_builder_ = std::make_unique<RenderGraphBuilder>();
  // Initialize the builder with the current frame context
  render_graph_builder_->BeginGraph(context);
}

void AsyncEngineSimulator::ClearRenderGraphBuilder(FrameContext& /*context*/)
{
  render_graph_builder_.reset();
}

auto AsyncEngineSimulator::PhaseFrameGraph(FrameContext& context) -> co::Co<>
{
  LOG_F(1, "[F{}][A] PhaseFrameGraph", frame_index_);

  // Engine core sets the current phase
  auto engine_tag = internal::EngineTagFactory::Get();
  context.SetCurrentPhase(FrameContext::FramePhase::FrameGraph, engine_tag);

  // Execute module frame graph work - modules will use
  // context.GetRenderGraphBuilder()
  co_await module_manager_.ExecuteFrameGraph(context);

  // Frame graph creates and manages render targets for the current frame
  auto& registry = graphics_.GetResourceRegistry();
  auto& allocator = graphics_.GetDescriptorAllocator();

  // Create frame-specific render targets based on frame graph analysis
  // Main color buffer for this frame
  // 1. Allocate descriptor slot first
  [[maybe_unused]] auto color_descriptor = allocator.AllocateDescriptor();
  // 2. Create GPU resource and register handle
  auto color_buffer_handle = registry.RegisterResource(
    "ColorBuffer_Frame" + std::to_string(frame_index_));

  // Depth buffer for this frame
  // 1. Allocate descriptor slot first
  [[maybe_unused]] auto depth_descriptor = allocator.AllocateDescriptor();
  // 2. Create GPU resource and register handle
  auto depth_buffer_handle = registry.RegisterResource(
    "DepthBuffer_Frame" + std::to_string(frame_index_));

  // Shadow map if needed (every few frames)
  if (frame_index_ % 3 == 0) {
    // 1. Allocate descriptor slot first
    auto shadow_descriptor = allocator.AllocateDescriptor();
    // 2. Create GPU resource and register handle
    auto shadow_map_handle = registry.RegisterResource(
      "ShadowMap_Frame" + std::to_string(frame_index_));
    LOG_F(2, "[F{}] Created shadow map {} with descriptor {}", frame_index_,
      shadow_map_handle, shadow_descriptor);
  }

  LOG_F(2, "[F{}] Created frame render targets: color={}, depth={}",
    frame_index_, color_buffer_handle, depth_buffer_handle);

  // TODO: Implement frame graph/render pass dependency planning
  // Resolve pass dependencies and resource transition planning
  co_await SimulateWorkOrdered(500us);
}

auto AsyncEngineSimulator::PhaseCommandRecord(FrameContext& context) -> co::Co<>
{
  LOG_F(1,
    "[F{}][A] PhaseCommandRecord - {} surfaces (unified record+submit phase)",
    context.GetFrameIndex(), context.GetSurfaces().size());

  // Execute module command recording first
  co_await module_manager_.ExecuteCommandRecord(context);

  // In the unified design, the render graph / renderer modules own command
  // recording and submission as a single phase. Module implementations should
  // perform per-view recording and submission, then mark views/surfaces as
  // Modules may stage FrameGraph or CommandRecord phases to mark work as
  // ready (for example by calling context.SetSurfacePresentable()). The
  // simulator no longer performs per-surface record/submit work itself.
  LOG_F(1, "[F{}][A] PhaseCommandRecord complete - modules recorded commands",
    frame_index_);
}

void AsyncEngineSimulator::PhasePresent(FrameContext& context)
{
  // If modules marked surfaces as presentable during rendering, use those
  // flags to determine which surfaces to present. This allows modules to
  // mark surfaces as ready asynchronously and the engine to present them.
  auto presentable_surfaces = context.GetPresentableSurfaces();

  LOG_F(1, "[F{}][A] PhasePresent - {} surfaces synchronously",
    context.GetFrameIndex(), presentable_surfaces.size());

  bool presented_via_flags = !presentable_surfaces.empty();
  if (presented_via_flags) {
    // Use the filtered presentable surfaces directly
    graphics_.PresentSurfaces(presentable_surfaces);
  }

  // TODO:does not belong here - just for dummy testing
  {
    // After presentation, schedule frame-specific resources for cleanup
    // These resources are safe to destroy after this frame completes
    auto& reclaimer = graphics_.GetDeferredReclaimer();

    // Schedule cleanup of this frame's render targets (they're done being used)
    // Use simulated handles that correspond to resources created this frame
    auto color_handle = 100000 + frame_index_; // Simulated color buffer handle
    auto depth_handle = 200000 + frame_index_; // Simulated depth buffer handle

    reclaimer.ScheduleReclaim(color_handle, frame_index_,
      "ColorBuffer_Frame" + std::to_string(frame_index_));
    reclaimer.ScheduleReclaim(depth_handle, frame_index_,
      "DepthBuffer_Frame" + std::to_string(frame_index_));

    LOG_F(1,
      "[F{}] Scheduled 2 render targets for deferred cleanup (color={}, "
      "depth={})",
      frame_index_, color_handle, depth_handle);

    // Every few frames, schedule cleanup of temporary resources
    if (frame_index_ % 3 == 0 && frame_index_ > 0) {
      auto shadow_handle = 300000 + frame_index_; // Simulated shadow map handle
      reclaimer.ScheduleReclaim(shadow_handle, frame_index_,
        "ShadowMap_Frame" + std::to_string(frame_index_));
      LOG_F(1, "[F{}] Scheduled shadow map for cleanup (handle={})",
        frame_index_, shadow_handle);
    }
  }

  LOG_F(1, "[F{}][A] PhasePresent complete - all {} surfaces presented",
    context.GetFrameIndex(), presentable_surfaces.size());
}
void AsyncEngineSimulator::PhaseAsyncPoll(FrameContext& context)
{
  // Engine core sets the current phase for async work
  auto engine_tag = internal::EngineTagFactory::Get();
  context.SetCurrentPhase(FrameContext::FramePhase::AsyncPoll, engine_tag);

  // Execute module async work (fire and forget style for now)
  auto async_work = module_manager_.ExecuteAsyncWork(context);
}

void AsyncEngineSimulator::PhaseBudgetAdapt()
{
  // TODO: Implement adaptive budget management
  // Monitor CPU frame time, GPU idle %, and queue depths
  // Degrade/defer tasks when over budget (IK refinement, particle collisions,
  // GI updates) Upgrade tasks when under budget (extra probe updates, higher
  // LOD, prefetch assets) Provide hysteresis to avoid oscillation (time-window
  // averaging)

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
        frame_index_,
        std::chrono::duration_cast<std::chrono::microseconds>(frame_elapsed)
          .count(),
        std::chrono::duration_cast<std::chrono::microseconds>(desired).count(),
        std::chrono::duration_cast<std::chrono::microseconds>(sleep_for)
          .count());
      std::this_thread::sleep_for(sleep_for);
    } else {
      LOG_F(INFO,
        "[F{}] Frame pacing: elapsed={}us exceeded target ({}us) no sleep",
        frame_index_,
        std::chrono::duration_cast<std::chrono::microseconds>(frame_elapsed)
          .count(),
        std::chrono::duration_cast<std::chrono::microseconds>(desired).count());
    }
  }
}
void AsyncEngineSimulator::PhaseFrameEnd(FrameContext& context)
{
  // Execute module frame end work first
  module_manager_.ExecuteFrameEnd(context);

  // Finalize graphics layer for this frame
  graphics_.EndFrame();

  auto frame_end = std::chrono::steady_clock::now();
  auto total = std::chrono::duration_cast<std::chrono::microseconds>(
    frame_end - frame_start_ts_);
  LOG_F(1, "Frame {} end | total={}us parallel_jobs={} parallel_span={}us",
    frame_index_, total.count(), parallel_results_.size(),
    0); // parallel span placeholder
}

auto AsyncEngineSimulator::ParallelTasks(FrameContext& context) -> co::Co<>
{
  parallel_results_.clear();
  parallel_results_.reserve(parallel_specs_.size());

  LOG_F(1, "[F{}][B] Dispatching {} parallel tasks + module parallel work",
    frame_index_, parallel_specs_.size());

  // Engine core sets the current phase for parallel work
  auto engine_tag = internal::EngineTagFactory::Get();
  context.SetCurrentPhase(FrameContext::FramePhase::ParallelWork, engine_tag);

  // Create vector of coroutines following the test pattern
  std::vector<co::Co<>> jobs;
  jobs.reserve(parallel_specs_.size() + 1); // +1 for module parallel work

  // Add module parallel work as first job
  jobs.emplace_back(module_manager_.ExecuteParallelWork(context));

  // Create each coroutine directly like in the MuxRange tests
  for (const auto& spec : parallel_specs_) {
    // Create coroutine with proper parameter capture using immediate lambda
    // invocation. Parallel tasks operate on immutable snapshot (Category B):
    // - Animation: pose evaluation on immutable skeleton data
    // - IK: Inverse Kinematics solving separate from animation
    // - BlendShapes: morph target weights calculation
    // - Particles: per-system simulation producing private buffers
    // - Culling: frustum, portal, BVH, occlusion on immutable spatial indices
    // - LOD: selection & impostor decisions
    // - AIBatch: batch evaluation & pathfinding queries (read-only world)
    // - LightClustering: tiled/clustered light culling (CPU portion)
    // - MaterialBaking: dynamic parameter baking / uniform block packing
    // - GPUUploadStaging: population (writes into reserved sub-allocations)
    // - OcclusionQuery: reduction from prior frame
    jobs.push_back([this](std::string task_name,
                     std::chrono::microseconds task_cost) -> co::Co<> {
      LOG_F(1, "[F{}][B][START] {} (cost {}us)", frame_index_, task_name,
        task_cost.count());

      auto start = std::chrono::steady_clock::now();
      co_await SimulateWork(task_name, task_cost);
      auto end = std::chrono::steady_clock::now();

      ParallelResult r { task_name,
        std::chrono::duration_cast<std::chrono::microseconds>(end - start) };
      {
        std::scoped_lock lk(parallel_results_mutex_);
        parallel_results_.push_back(r);
      }
      LOG_F(1, "[F{}][B][DONE] {} ({}us)", frame_index_, r.name,
        r.duration.count());
      co_return;
    }(spec.name, spec.cost)); // Immediately invoke with the current spec values
  }

  LOG_F(1, "[F{}][B] Awaiting parallel barrier ({} tasks)", frame_index_,
    jobs.size());
  co_await co::AllOf(std::move(jobs));
  LOG_F(1, "[F{}][B4 complete] Barrier complete", frame_index_);
  co_return;
}

auto AsyncEngineSimulator::SimulateWork(std::chrono::microseconds cost) const
  -> co::Co<>
{
  co_await SimulateWork(std::string { "anon" }, cost);
}

auto AsyncEngineSimulator::SimulateWork(
  std::string name, std::chrono::microseconds cost) const -> co::Co<>
{
  // Copy name into lambda to ensure lifetime extends through execution.
  co_await pool_.Run([name, cost](co::ThreadPool::CancelToken) {
    loguru::set_thread_name((std::string("pool-") + name).c_str());
    LOG_F(1, "[POOL][RUN ] {} start (cost={}us)", name, cost.count());
    std::this_thread::sleep_for(cost);
  });
  co_return;
}

auto AsyncEngineSimulator::SimulateWorkOrdered(
  std::chrono::microseconds cost) const -> co::Co<>
{
  std::this_thread::sleep_for(cost);
  co_return;
}

// Note: Per-surface helpers were removed. Presentation and unified command
// recording/submission are owned by renderer / render-graph code now.

void AsyncEngineSimulator::InitializeDetachedServices()
{
  LOG_F(1, "Initializing detached services (Category D)");

  // TODO: Initialize crash dump detection service
  // Set up crash dump monitoring and symbolication service
  // This service runs detached from frame loop and handles crash reporting
  LOG_F(1, "[D] Crash dump detection service initialized");
}

} // namespace oxygen::engine::asyncsim
