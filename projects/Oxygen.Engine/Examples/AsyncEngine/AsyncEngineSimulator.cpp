//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "AsyncEngineSimulator.h"

#include <algorithm>
#include <ranges>
#include <thread>

#include "ModuleContext.h"

using namespace std::chrono_literals;

namespace oxygen::examples::asyncsim {

AsyncEngineSimulator::AsyncEngineSimulator(
  oxygen::co::ThreadPool& pool, EngineProps props) noexcept
  : pool_(pool)
  , props_(props)
{
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

  // Default single surface setup if no surfaces are explicitly added
  surfaces_.push_back({ "MainSurface", 800us, 200us, 300us, false, false });

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

void AsyncEngineSimulator::AddSurface(const RenderSurface& surface)
{
  // If this is the first surface being added and we only have the default,
  // replace it
  if (surfaces_.size() == 1 && surfaces_[0].name == "MainSurface") {
    surfaces_.clear();
  }
  surfaces_.push_back(surface);
}

void AsyncEngineSimulator::ClearSurfaces() { surfaces_.clear(); }

auto AsyncEngineSimulator::Run(uint32_t frame_count) -> void
{
  CHECK_F(nursery_ != nullptr,
    "Nursery must be opened via StartAsync before Run (call StartAsync first)");
  nursery_->Start(
    [this, frame_count]() -> co::Co<> { co_await FrameLoop(frame_count); });
}

auto AsyncEngineSimulator::InitializeModules() -> co::Co<>
{
  ModuleContext context(0, pool_, graphics_, props_);
  co_await module_manager_.InitializeModules(context);
}

auto AsyncEngineSimulator::ShutdownModules() -> co::Co<>
{
  ModuleContext context(frame_index_, pool_, graphics_, props_);
  co_await module_manager_.ShutdownModules(context);
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
    ModuleContext context(frame_index_, pool_, graphics_, props_);

    // Fence polling, epoch advance, deferred destruction retirement
    PhaseFrameStart();

    // B0: Input snapshot
    co_await PhaseInput(context);
    // B1: Fixed simulation deterministic state
    co_await PhaseFixedSim(context);
    // Variable gameplay logic
    co_await PhaseGameplay(context);
    // Network packet application & reconciliation
    co_await PhaseNetworkReconciliation(context);
    // Random seed management for determinism
    co_await PhaseRandomSeedManagement();
    // B2: Structural mutations
    co_await PhaseSceneMutation(context);
    // Transform propagation
    co_await PhaseTransforms(context);
    // Immutable snapshot build (B3)
    co_await PhaseSnapshot(context);

    // Build immutable snapshot for Category B tasks (B3 complete after this).
    snapshot_.frame_index = frame_index_;
    context.SetFrameSnapshot(&snapshot_);
    LOG_F(1, "[F{}][B3 built] Immutable snapshot ready", frame_index_);

    // Launch and join Category B barriered parallel tasks (B4 upon completion).
    co_await ParallelTasks(context);

    // Serial post-parallel integration (Category A resumes after B4)
    co_await PhasePostParallel(context);
    // Frame graph/render pass dependency planning
    co_await PhaseFrameGraph(context);
    // Global descriptor/bindless table publication
    co_await PhaseDescriptorTablePublication(context);
    // Resource state transitions planning
    co_await PhaseResourceStateTransitions(context);
    // Multi-surface command recording and submission
    co_await PhaseCommandRecord(context);
    LOG_F(1, "[F{}][B5 submitted] All command lists submitted via pipeline",
      frame_index_);
    // Synchronous sequential presentation
    PhasePresent(context);
    // Frame pacing immediately after Present
    if (props_.target_fps > 0) {
      const auto desired = std::chrono::nanoseconds(
        1'000'000'000ull / static_cast<uint64_t>(props_.target_fps));
      auto now = std::chrono::steady_clock::now();
      auto frame_elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
        now - frame_start_ts_);
      if (frame_elapsed < desired) {
        auto sleep_for = desired - frame_elapsed;
        LOG_F(INFO,
          "[F{}] Frame pacing: elapsed={}us target={}us sleeping={}us",
          frame_index_,
          std::chrono::duration_cast<std::chrono::microseconds>(frame_elapsed)
            .count(),
          std::chrono::duration_cast<std::chrono::microseconds>(desired)
            .count(),
          std::chrono::duration_cast<std::chrono::microseconds>(sleep_for)
            .count());
        std::this_thread::sleep_for(sleep_for);
      } else {
        LOG_F(INFO,
          "[F{}] Frame pacing: elapsed={}us exceeded target ({}us) no sleep",
          frame_index_,
          std::chrono::duration_cast<std::chrono::microseconds>(frame_elapsed)
            .count(),
          std::chrono::duration_cast<std::chrono::microseconds>(desired)
            .count());
      }
    }
    // Poll async pipeline readiness and integrate ready resources
    PhaseAsyncPoll(context);
    LOG_F(1, "[F{}][B6 async polled] Async resources integrated (if any)",
      frame_index_);
    // Adaptive budget management for next frame
    PhaseBudgetAdapt();
    // Frame end timing and metrics
    PhaseFrameEnd();

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

void AsyncEngineSimulator::PhaseFrameStart()
{
  frame_start_ts_ = std::chrono::steady_clock::now();
  phase_accum_ = 0us;

  // Initialize graphics layer for this frame
  graphics_.BeginFrame(frame_index_);

  // TODO: Implement epoch advance for resource lifetime management
  // Advance frame epoch counter for generation-based validation

  LOG_F(1, "Frame {} start (epoch advance)", frame_index_);
}
auto AsyncEngineSimulator::PhaseInput(ModuleContext& context) -> co::Co<>
{
  LOG_F(1, "[F{}][A] PhaseInput", frame_index_);

  // Execute module input processing first
  co_await module_manager_.ExecuteInput(context);

  // Then execute engine's own input processing
  co_await SimulateWorkOrdered(500us);
}
auto AsyncEngineSimulator::PhaseFixedSim(ModuleContext& context) -> co::Co<>
{
  LOG_F(1, "[F{}][A] PhaseFixedSim", frame_index_);

  // Execute module fixed simulation first
  co_await module_manager_.ExecuteFixedSimulation(context);

  // Then execute engine's own fixed simulation
  co_await SimulateWorkOrdered(1000us);
}
auto AsyncEngineSimulator::PhaseGameplay(ModuleContext& context) -> co::Co<>
{
  LOG_F(1, "[F{}][A] PhaseGameplay", frame_index_);

  // Execute module gameplay logic first
  co_await module_manager_.ExecuteGameplay(context);

  // Then execute engine's own gameplay logic
  co_await SimulateWorkOrdered(1500us);
}
auto AsyncEngineSimulator::PhaseNetworkReconciliation(ModuleContext& context)
  -> co::Co<>
{
  LOG_F(1, "[F{}][A] PhaseNetworkReconciliation", frame_index_);

  // Execute module network reconciliation first
  co_await module_manager_.ExecuteNetworkReconciliation(context);

  // TODO: Implement network packet application & authoritative reconciliation
  // Apply received network packets to authoritative game state
  // Reconcile client predictions with server authority
  co_await SimulateWorkOrdered(300us);
}
auto AsyncEngineSimulator::PhaseRandomSeedManagement() -> co::Co<>
{
  LOG_F(1, "[F{}][A] PhaseRandomSeedManagement", frame_index_);
  // TODO: Implement deterministic random seed management
  // Update random seeds for deterministic simulation across frames
  // Ensure reproducible random number generation for gameplay systems
  co_await SimulateWorkOrdered(100us);
}
auto AsyncEngineSimulator::PhaseSceneMutation(ModuleContext& context)
  -> co::Co<>
{
  LOG_F(1, "[F{}][A] PhaseSceneMutation (B2: structural integrity barrier)",
    frame_index_);

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
auto AsyncEngineSimulator::PhaseTransforms(ModuleContext& context) -> co::Co<>
{
  LOG_F(1, "[F{}][A] PhaseTransforms", frame_index_);

  // Execute module transform propagation first
  co_await module_manager_.ExecuteTransformPropagation(context);

  // Then execute engine's own transform work
  co_await SimulateWorkOrdered(400us);
}
auto AsyncEngineSimulator::PhaseSnapshot(ModuleContext& context) -> co::Co<>
{
  LOG_F(1, "[F{}][A] PhaseSnapshot (build immutable snapshot)", frame_index_);

  // Execute module snapshot building first
  co_await module_manager_.ExecuteSnapshotBuild(context);

  // Then execute engine's own snapshot work
  co_await SimulateWorkOrdered(300us);
}
auto AsyncEngineSimulator::PhasePostParallel(ModuleContext& context) -> co::Co<>
{
  LOG_F(1, "[F{}][A] PhasePostParallel (integrate Category B outputs)",
    frame_index_);

  // Execute module post-parallel integration first
  co_await module_manager_.ExecutePostParallel(context);

  // Then execute engine's own post-parallel work
  co_await SimulateWorkOrdered(600us);
}
auto AsyncEngineSimulator::PhaseFrameGraph(ModuleContext& context) -> co::Co<>
{
  LOG_F(1, "[F{}][A] PhaseFrameGraph", frame_index_);

  // Execute module frame graph work first
  co_await module_manager_.ExecuteFrameGraph(context);

  // Frame graph creates and manages render targets for the current frame
  auto& registry = graphics_.GetResourceRegistry();
  auto& allocator = graphics_.GetDescriptorAllocator();

  // Create frame-specific render targets based on frame graph analysis
  // Main color buffer for this frame
  // 1. Allocate descriptor slot first
  auto color_descriptor = allocator.AllocateDescriptor();
  // 2. Create GPU resource and register handle
  auto color_buffer_handle = registry.RegisterResource(
    "ColorBuffer_Frame" + std::to_string(frame_index_));

  // Depth buffer for this frame
  // 1. Allocate descriptor slot first
  auto depth_descriptor = allocator.AllocateDescriptor();
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
auto AsyncEngineSimulator::PhaseDescriptorTablePublication(
  ModuleContext& context) -> co::Co<>
{
  LOG_F(1, "[F{}][A] PhaseDescriptorTablePublication", frame_index_);

  // Execute module descriptor publication work first
  co_await module_manager_.ExecuteDescriptorPublication(context);

  // Use Graphics layer descriptor allocator for global descriptor management
  auto& allocator = graphics_.GetDescriptorAllocator();

  // At this point, all resources for the frame should be ready and have
  // descriptors allocated In a real engine, this would batch-update the GPU
  // descriptor heap with all frame resources

  // Simulate allocating descriptors for frame-specific resources (uniform
  // buffers, etc.)
  auto frame_descriptor_count
    = 3; // Per-frame uniforms, camera data, lighting data
  for (uint32_t i = 0; i < frame_descriptor_count; ++i) {
    auto descriptor_id = allocator.AllocateDescriptor();
    LOG_F(2, "[F{}] Allocated frame descriptor {} for uniform buffer",
      frame_index_, descriptor_id);
  }

  // Publish the complete descriptor table to GPU - this makes all resources
  // visible to shaders In a real engine, this would issue GPU commands to
  // update descriptor heaps
  allocator.PublishDescriptorTable(frame_index_);

  LOG_F(1,
    "[F{}] Published global descriptor table (all resources now "
    "bindless-accessible)",
    frame_index_);

  co_await SimulateWorkOrdered(200us);
}
auto AsyncEngineSimulator::PhaseResourceStateTransitions(ModuleContext& context)
  -> co::Co<>
{
  LOG_F(1, "[F{}][A] PhaseResourceStateTransitions", frame_index_);

  // Execute module resource state transitions first
  co_await module_manager_.ExecuteResourceTransitions(context);

  // TODO: Implement resource state transition planning
  // Plan GPU resource state transitions for optimal barrier placement
  // Coordinate with frame graph for proper resource lifecycle management
  co_await SimulateWorkOrdered(300us);
}
auto AsyncEngineSimulator::PhaseCommandRecord(ModuleContext& context)
  -> co::Co<>
{
  LOG_F(1, "[F{}][A] PhaseCommandRecord - {} surfaces (record+submit pipeline)",
    frame_index_, surfaces_.size());

  // Execute module command recording first
  co_await module_manager_.ExecuteCommandRecord(context);

  // Reset surface states for new frame
  for (auto& surface : surfaces_) {
    surface.commands_recorded = false;
    surface.commands_submitted = false;
  }

  // Record and submit commands in parallel for each surface using immediate
  // lambda invocation pattern Each surface does: Record -> Submit immediately
  // (pipeline style)
  std::vector<co::Co<>> pipeline_tasks;
  pipeline_tasks.reserve(surfaces_.size());

  for (size_t i = 0; i < surfaces_.size(); ++i) {
    pipeline_tasks.push_back([](const RenderSurface& surface, size_t index,
                               AsyncEngineSimulator* sim) -> co::Co<> {
      // Execute both record and submit on the same thread pool worker
      co_await sim->pool_.Run([surface, index, sim](
                                co::ThreadPool::CancelToken) -> void {
        loguru::set_thread_name((std::string("pool-") + surface.name).c_str());
        // Record commands for this surface
        sim->RecordSurfaceCommands(surface, index);
        // Immediately submit commands on the same thread
        sim->SubmitSurfaceCommands(surface, index);
      });
      co_return;
    }(surfaces_[i], i, this));
  }

  // Wait for all surfaces to complete their record+submit pipeline
  co_await co::AllOf(std::move(pipeline_tasks));

  LOG_F(1,
    "[F{}][A] PhaseCommandRecord complete - all {} surfaces recorded+submitted",
    frame_index_, surfaces_.size());
}

void AsyncEngineSimulator::PhasePresent(ModuleContext& context)
{
  LOG_F(1, "[F{}][A] PhasePresent - {} surfaces synchronously", frame_index_,
    surfaces_.size());

  // Execute module present work first (fire and forget for now)
  auto present_work = module_manager_.ExecutePresent(context);

  // Present all surfaces synchronously (sequential presentation)
  for (size_t i = 0; i < surfaces_.size(); ++i) {
    PresentSurface(surfaces_[i], i);
  }

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
    LOG_F(1, "[F{}] Scheduled shadow map for cleanup (handle={})", frame_index_,
      shadow_handle);
  }

  LOG_F(1, "[F{}][A] PhasePresent complete - all {} surfaces presented",
    frame_index_, surfaces_.size());
}
void AsyncEngineSimulator::PhaseAsyncPoll(ModuleContext& context)
{
  // Execute module async work (fire and forget style for now)
  auto async_work = module_manager_.ExecuteAsyncWork(context);

  // Execute engine's async job polling
  TickAsyncJobs();
}
void AsyncEngineSimulator::PhaseBudgetAdapt()
{
  // TODO: Implement adaptive budget management
  // Monitor CPU frame time, GPU idle %, and queue depths
  // Degrade/defer tasks when over budget (IK refinement, particle collisions,
  // GI updates) Upgrade tasks when under budget (extra probe updates, higher
  // LOD, prefetch assets) Provide hysteresis to avoid oscillation (time-window
  // averaging)
}
void AsyncEngineSimulator::PhaseFrameEnd()
{
  // Finalize graphics layer for this frame
  graphics_.EndFrame();

  auto frame_end = std::chrono::steady_clock::now();
  auto total = std::chrono::duration_cast<std::chrono::microseconds>(
    frame_end - frame_start_ts_);
  LOG_F(1, "Frame {} end | total={}us parallel_jobs={} parallel_span={}us",
    frame_index_, total.count(), parallel_results_.size(),
    0); // parallel span placeholder
}

void AsyncEngineSimulator::LaunchParallelTasks()
{
  parallel_results_.clear();
  parallel_results_.reserve(parallel_specs_.size());
  for (const auto& spec : parallel_specs_) {
    auto start = std::chrono::steady_clock::now();
    std::this_thread::sleep_for(spec.cost);
    auto end = std::chrono::steady_clock::now();
    parallel_results_.push_back({ spec.name,
      std::chrono::duration_cast<std::chrono::microseconds>(end - start) });
  }
}

void AsyncEngineSimulator::JoinParallelTasks() { /* already joined inline */ }

auto AsyncEngineSimulator::ParallelTasks(ModuleContext& context) -> co::Co<>
{
  parallel_results_.clear();
  parallel_results_.reserve(parallel_specs_.size());

  LOG_F(1, "[F{}][B] Dispatching {} parallel tasks + module parallel work",
    frame_index_, parallel_specs_.size());

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

void AsyncEngineSimulator::TickAsyncJobs()
{
  size_t ready_count = 0;
  size_t pending_count = 0;
  for (auto& job : async_jobs_) {
    if (!job.ready) {
      if (job.submit_frame == 0)
        job.submit_frame = frame_index_;
      if (job.remaining > 0ms) {
        auto slice = 5ms; // increased slice for quicker readiness
        if (job.remaining <= slice) {
          job.remaining = 0ms;
          job.ready = true;
          LOG_F(1, "[F{}][C] Async job {} READY (submitted frame {})",
            frame_index_, job.name, job.submit_frame);

          // TODO: Implement proper async resource publishing with generation
          // checks Each async job should publish results with atomic swap and
          // generation validation Handle specific job types:
          // - AssetLoadA: I/O → decompress → transcode → GPU upload → publish
          // swap
          // - ShaderCompileA: Compile & reflection (fallback variant until
          // ready)
          // - PSOBuild: Pipeline State Object build & cache insertion
          // - BLASBuild/TLASRefit: Acceleration structure builds/refits
          // - LightmapBake/ProbeBake: Progressive GI baking & denoise
          // - NavMeshGen: Navigation mesh generation or updates
          // - ProceduralGeometry: Terrain tiles, impostors regeneration
          // - GPUReadback: Timings, screenshots, async compute results

          LOG_F(1, "[F{}][C] PUBLISH {} resource to main frame state",
            frame_index_, job.name);
        } else {
          job.remaining -= slice;
        }
      }
    }
    if (job.ready)
      ++ready_count;
    else
      ++pending_count;
  }
  LOG_F(1, "[F{}][C] AsyncPoll summary: ready={} pending={}", frame_index_,
    ready_count, pending_count);
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

void AsyncEngineSimulator::RecordSurfaceCommands(
  const RenderSurface& surface, size_t surface_index)
{
  LOG_F(1, "[F{}][B][{}] Recording commands for surface '{}' ({}us)",
    frame_index_, surface_index, surface.name, surface.record_cost.count());

  std::this_thread::sleep_for(surface.record_cost);

  // Mark surface as having commands recorded
  const_cast<RenderSurface&>(surface).commands_recorded = true;

  LOG_F(1, "[F{}][B][{}][DONE] Surface '{}' commands recorded", frame_index_,
    surface_index, surface.name);
}

void AsyncEngineSimulator::SubmitSurfaceCommands(
  const RenderSurface& surface, size_t surface_index)
{
  LOG_F(1, "[F{}][B][{}] Submitting commands for surface '{}' (same thread)",
    frame_index_, surface_index, surface.name);

  std::this_thread::sleep_for(surface.submit_cost);

  const_cast<RenderSurface&>(surface).commands_submitted = true;

  LOG_F(1,
    "[F{}][B][{}][DONE] Surface '{}' commands submitted ({}us same thread)",
    frame_index_, surface_index, surface.name, surface.submit_cost.count());
}

void AsyncEngineSimulator::PresentSurface(
  const RenderSurface& surface, size_t surface_index)
{
  LOG_F(1, "[F{}][A][{}] Presenting surface '{}'", frame_index_, surface_index,
    surface.name);

  // Simulate presentation work (synchronous per surface)
  std::this_thread::sleep_for(surface.present_cost);

  LOG_F(1, "[F{}][A][{}][DONE] Surface '{}' presented ({}us)", frame_index_,
    surface_index, surface.name, surface.present_cost.count());
}

void AsyncEngineSimulator::InitializeDetachedServices()
{
  LOG_F(1, "Initializing detached services (Category D)");

  // TODO: Initialize crash dump detection service
  // Set up crash dump monitoring and symbolication service
  // This service runs detached from frame loop and handles crash reporting
  LOG_F(1, "[D] Crash dump detection service initialized");
}

} // namespace oxygen::examples::asyncsim
