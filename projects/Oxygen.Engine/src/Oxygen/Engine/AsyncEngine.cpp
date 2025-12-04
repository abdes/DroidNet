//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <chrono>
#include <ranges>
#include <thread>
#include <utility>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/EngineTag.h>
#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Core/Time/PhysicalClock.h>
#include <Oxygen/Engine/AsyncEngine.h>
#include <Oxygen/Engine/TimeManager.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Input/InputSystem.h>
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

using namespace std::chrono;
using namespace std::chrono_literals;
namespace t = oxygen::time;

namespace oxygen {

using core::PhaseId;
using namespace oxygen::engine;

// For convenience in timing configuration access
using TimingConfig = TimingConfig;

AsyncEngine::AsyncEngine(std::shared_ptr<Platform> platform,
  std::weak_ptr<Graphics> graphics, EngineConfig config) noexcept
  : config_(std::move(config))
  , platform_(std::move(platform))
  , gfx_weak_(std::move(graphics))
  , module_manager_(std::make_unique<ModuleManager>(observer_ptr { this }))
{
  CHECK_F(platform_ != nullptr);
  CHECK_F(!gfx_weak_.expired());
  CHECK_F(
    platform_->HasThreads(), "Platform must be configured with a thread pool");

  // Initialize time manager as a component of this Composition
  {
    TimeManager::Config tm_cfg {
      .fixed_timestep = config_.timing.fixed_delta,
      .default_time_scale = 1.0,
      .start_paused = false,
      .animation_scale = 1.0,
      .network_smoothing_factor = 0.1,
    };
    time_manager_ = &AddComponent<TimeManager>(GetPhysicalClock(), tm_cfg);
  }

  // Initialize detached services (Category D)
  InitializeDetachedServices();

  LOG_F(INFO, "AsyncEngine created");
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
    co_await Shutdown();

    // Signal completion once the frame loop has finished executing.
    completed_.Trigger();
    LOG_F(INFO, "Engine completed after {} frames", frame_number_);
  });
}

auto AsyncEngine::Shutdown() -> co::Co<>
{
  // No need to flush the Graphics backend here, as it should have been done
  // already when the engine frame loop was terminating.
  // Shutdown order: first stop modules (so they can safely access platform
  // and graphics resources during their OnShutdown), then shutdown the
  // platform to tear down native windows and event pumps.

  // Drain outstanding GPU work and process any pending deferred releases
  // registered during normal frame processing before we start shutting down
  // modules. This ensures modules' destructors won't final-release resources
  // while the GPU still has in-flight references.
  if (!gfx_weak_.expired()) {
    if (auto gfx = gfx_weak_.lock()) {
      try {
        LOG_F(INFO,
          "AsyncEngine::Shutdown - pre-shutdown flush: draining GPU and "
          "processing pending deferred releases");
        gfx->Flush();
      } catch (const std::exception& e) {
        LOG_F(WARNING,
          "AsyncEngine::Shutdown - pre-shutdown Graphics::Flush() threw: {}",
          e.what());
      } catch (...) {
        LOG_F(WARNING,
          "AsyncEngine::Shutdown - pre-shutdown Graphics::Flush() threw "
          "unknown exception");
      }
    }
  }

  // This will shut down all modules synchronously (reverse order).
  module_manager_.reset();

  // After modules have had an opportunity to perform shutdown work (which may
  // include queue submissions or deferred-release registrations), ensure the
  // Graphics backend is flushed so all GPU work is completed and the
  // DeferredReclaimer has a chance to process its pending actions. Failing to
  // flush here can lead to final-release of device objects that are still in
  // use by the GPU which causes validation errors or crashes.
  // After modules' OnShutdown() has run and destructors may have scheduled
  // additional deferred releases, flush again to ensure the DeferredReclaimer
  // processes those actions before we shutdown the platform.
  if (!gfx_weak_.expired()) {
    if (auto gfx = gfx_weak_.lock()) {
      try {
        LOG_F(INFO,
          "AsyncEngine::Shutdown - post-shutdown flush: processing deferred "
          "releases registered during module shutdown");
        gfx->Flush();
      } catch (const std::exception& e) {
        LOG_F(WARNING,
          "AsyncEngine::Shutdown - post-shutdown Graphics::Flush() threw: {}",
          e.what());
      } catch (...) {
        LOG_F(WARNING,
          "AsyncEngine::Shutdown - post-shutdown Graphics::Flush() threw "
          "unknown exception");
      }
    }
  }

  // Now shutdown the platform event pump so modules are able to perform
  // any required cleanup while platform objects are still alive.
  co_await platform_->Shutdown();
}

auto AsyncEngine::Stop() -> void { shutdown_requested_ = true; }

// Register a module (takes ownership). Modules are sorted by priority.
// ReSharper disable once CppMemberFunctionMayBeConst
auto AsyncEngine::RegisterModule(std::unique_ptr<EngineModule> module) noexcept
  -> bool
{
  return module_manager_->RegisterModule(std::move(module));
}

// Optional: unregister by name. Returns true if removed.
// ReSharper disable once CppMemberFunctionMayBeConst
auto AsyncEngine::UnregisterModule(std::string_view name) noexcept -> void
{
  module_manager_->UnregisterModule(name);
}

auto AsyncEngine::GetEngineConfig() const noexcept -> const EngineConfig&
{
  return config_;
}

auto AsyncEngine::SetTargetFps(uint32_t fps) noexcept -> void
{
  if (fps > EngineConfig::kMaxTargetFps) {
    fps = EngineConfig::kMaxTargetFps;
  }
  config_.target_fps = fps;
  LOG_F(INFO, "AsyncEngine target_fps set to {}", config_.target_fps);
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
  // Initialize pacing deadline to now() to start immediately
  next_frame_deadline_ = GetPhysicalClock().Now();

  while (true) {
    if (shutdown_requested_) {
      LOG_F(INFO, "Shutdown requested, stopping frame loop...");
      co_await Shutdown();
      break;
    }
    // Check for termination requests
    // Engine termination should be driven explicitly by the platform's
    // OnTerminate() signal (e.g. Ctrl-C or higher-level termination). Do
    // NOT automatically stop the engine on LastWindowClosed: top-level
    // application code (main_impl) is responsible for reacting to window
    // lifecycle events and initiating an orderly shutdown via Stop(). This
    // avoids duplicate/overlapping shutdown paths.
    if (platform_->Async().OnTerminate().Triggered()) {
      LOG_F(INFO, "Termination requested, stopping frame loop...");
      break;
    }

    if (!NextFrame()) {
      // Cancel that last increment
      frame_number_ = frame::SequenceNumber { frame_number_.get() - 1ULL };
      break;
    }

    LOG_SCOPE_F(1, fmt::format("Frame {}", frame_number_.get()).c_str());

    // Use persistent frame context (views persist across frames with stable
    // IDs)
    auto& context = frame_context_;

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

    // UI update phase: process UI systems, generate rendering artifacts
    co_await PhaseGuiUpdate(context);

    // Frame multi-view rendering
    {
      co_await PhasePreRender(context);
      co_await PhaseRender(context);
      co_await PhaseCompositing(context);
    }

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
      const auto period_ns = nanoseconds(
        1'000'000'000ull / static_cast<uint64_t>(config_.target_fps));

      // Establish or advance the next deadline monotonically from the frame
      // start time to target exact start-to-start periods.
      if (next_frame_deadline_.get().time_since_epoch().count() == 0) {
        next_frame_deadline_
          = t::PhysicalTime { frame_start_ts_.get() + period_ns };
      } else {
        next_frame_deadline_
          = t::PhysicalTime { next_frame_deadline_.get() + period_ns };
      }
      const auto now = GetPhysicalClock().Now();
      // If we fell significantly behind, re-synchronize to avoid accumulating
      // lag (late by more than one period).
      if (now.get() > next_frame_deadline_.get() + period_ns) {
        next_frame_deadline_ = t::PhysicalTime { now.get() + period_ns };
      }

      // Sleep until a little before the deadline to mitigate OS sleep
      // overshoot, then yield/spin-finish for precision.
      const auto safety_margin = config_.timing.pacing_safety_margin;
      if (next_frame_deadline_.get() > now.get()) {
        const auto sleep_until_ts = next_frame_deadline_.get() - safety_margin;
        if (sleep_until_ts > now.get()) {
          co_await platform_->Async().SleepFor(
            duration_cast<microseconds>(sleep_until_ts - now.get()));
        }
        // Finish: cooperative tiny pauses until the deadline.
        for (;;) {
          const auto n2 = GetPhysicalClock().Now();
          if (n2.get() >= next_frame_deadline_.get()) {
            break;
          }
          // cooperative tiny pause (platform abstraction could provide this)
          std::this_thread::yield();
        }
      }

      LOG_F(2,
        "[F{}] Pacing to deadline: target={}us ({}ns), next deadline in {}us",
        frame_number_, duration_cast<microseconds>(period_ns).count(),
        period_ns.count(),
        duration_cast<microseconds>(next_frame_deadline_.get() - now.get())
          .count());
    }
  }
  co_return;
}

auto AsyncEngine::PhaseFrameStart(FrameContext& context) -> co::Co<>
{
  const auto tag = internal::EngineTagFactory::Get();
  context.SetCurrentPhase(PhaseId::kFrameStart, tag);
  frame_start_ts_ = GetPhysicalClock().Now();

  // TODO: setup all the properties of context that need to be set

  // NOTE: Views are now persistent with stable IDs across frames
  // Modules register views once (RegisterView) and update them (UpdateView)
  context.ClearPresentableFlags(tag);
  context.SetFrameSequenceNumber(frame_number_, tag);
  context.SetFrameSlot(frame_slot_, tag);
  context.SetFrameStartTime(frame_start_ts_.get(), tag);
  context.SetThreadPool(&platform_->Threads(), tag);
  context.SetGraphicsBackend(gfx_weak_, tag);

  // Update timing data for this frame via TimeManager
  if (time_manager_) {
    time_manager_->BeginFrame();
    // Populate ModuleTimingData for modules
    const auto& td = time_manager_->GetFrameTimingData();
    ModuleTimingData module_timing {};
    module_timing.game_delta_time = td.simulation_delta;
    module_timing.fixed_delta_time
      = time_manager_->GetSimulationClock().GetFixedTimestep();
    module_timing.interpolation_alpha
      = static_cast<float>(td.interpolation_alpha);
    module_timing.current_fps = static_cast<float>(td.current_fps);
    context.SetModuleTimingData(module_timing, tag);
  }

  // Initialize graphics layer for this frame
  auto gfx = gfx_weak_.lock();
  if (!gfx) {
    // TODO: Handle graphics backend invalidation
    throw std::logic_error("Graphics backend no longer valid.");
  }
  gfx->BeginFrame(frame_number_, frame_slot_);

  // Process platform frame start operations (deferred window closes, etc.)
  platform_->OnFrameStart();

  // Execute module frame start work
  co_await module_manager_->ExecutePhase(PhaseId::kFrameStart, context);

  // TODO: Implement epoch advance for resource lifetime management
  // Advance frame epoch counter for generation-based validation

  LOG_F(2, "Frame {} start (epoch advance)", frame_number_);
}

// ReSharper disable once CppMemberFunctionMayBeConst
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
  const auto input_sys_opt = module_manager_->GetModule<InputSystem>();
  DCHECK_F(input_sys_opt.has_value());
  const auto& input_sys = input_sys_opt.value().get();
  if (auto snap = input_sys.GetCurrentSnapshot()) {
    // Publish type-erased input snapshot directly as blob
    context.SetInputSnapshot(
      std::static_pointer_cast<const void>(std::move(snap)), tag);
  }
}

// ReSharper disable once CppMemberFunctionMayBeConst
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

  // Fixed timestep migration: use SimulationClock from TimeManager
  if (time_manager_) {
    auto& sim = time_manager_->GetSimulationClock();
    const auto max_substeps = config_.timing.max_substeps;
    const auto result = sim.ExecuteFixedSteps(max_substeps);
    const uint32_t steps = result.steps_executed;

    for (uint32_t s = 0; s < steps; ++s) {
      auto module_timing = context.GetModuleTimingData();
      module_timing.fixed_delta_time = sim.GetFixedTimestep();
      module_timing.fixed_steps_this_frame = s + 1;
      context.SetModuleTimingData(module_timing, tag);

      context.SetCurrentPhase(PhaseId::kFixedSimulation, tag);
      co_await module_manager_->ExecutePhase(
        PhaseId::kFixedSimulation, context);
    }

    auto module_timing = context.GetModuleTimingData();
    module_timing.fixed_steps_this_frame = steps;
    module_timing.interpolation_alpha
      = static_cast<float>(result.interpolation_alpha);
    context.SetModuleTimingData(module_timing, tag);

    LOG_F(2, "[F{}][A] PhaseFixedSim completed {} substeps, alpha={:.3f}",
      frame_number_, steps, module_timing.interpolation_alpha);
  } else {
    // Fallback: execute once to keep modules functional
    co_await module_manager_->ExecutePhase(PhaseId::kFixedSimulation, context);
  }
}

// ReSharper disable once CppMemberFunctionMayBeConst
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

// ReSharper disable once CppMemberFunctionMayBeConst
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

// ReSharper disable once CppMemberFunctionMayBeConst
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
  // - AI decision-making and pathfinding
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

// ReSharper disable once CppMemberFunctionMayBeConst
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

// ReSharper disable once CppMemberFunctionMayBeConst
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

// ReSharper disable once CppMemberFunctionMayBeConst
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

// ReSharper disable once CppMemberFunctionMayBeConst
auto AsyncEngine::PhaseGuiUpdate(FrameContext& context) -> co::Co<>
{
  const auto tag = internal::EngineTagFactory::Get();
  context.SetCurrentPhase(PhaseId::kGuiUpdate, tag);

  LOG_F(2,
    "[F{}][A] PhaseGuiUpdate - UI systems and rendering artifact generation",
    frame_number_);

  // Execute module UI update work - modules generate UI rendering artifacts
  co_await module_manager_->ExecutePhase(PhaseId::kGuiUpdate, context);
}

// Pre-render phase: perform renderer and module preparation work (no
// command recording).
// ReSharper disable once CppMemberFunctionMayBeConst
auto AsyncEngine::PhasePreRender(FrameContext& context) -> co::Co<>
{
  const auto tag = internal::EngineTagFactory::Get();
  context.SetCurrentPhase(PhaseId::kPreRender, tag);

  LOG_F(2, "[F{}][A] PhasePreRender - prepare rendering data", frame_number_);

  co_await module_manager_->ExecutePhase(PhaseId::kPreRender, context);
}

// Render phase: modules record commands and perform per-view rendering.
// ReSharper disable once CppMemberFunctionMayBeConst
auto AsyncEngine::PhaseRender(FrameContext& context) -> co::Co<>
{
  const auto tag = internal::EngineTagFactory::Get();
  context.SetCurrentPhase(PhaseId::kRender, tag);

  LOG_F(2, "[F{}][A] PhaseRender - {} surfaces (record+submit phase)",
    frame_number_, context.GetSurfaces().size());

  co_await module_manager_->ExecutePhase(PhaseId::kRender, context);

  LOG_F(2, "[F{}][A] PhaseRender complete - modules recorded commands",
    frame_number_);
}

// ReSharper disable once CppMemberFunctionMayBeConst
auto AsyncEngine::PhaseCompositing(FrameContext& context) -> co::Co<>
{
  const auto tag = internal::EngineTagFactory::Get();
  context.SetCurrentPhase(PhaseId::kCompositing, tag);

  LOG_F(2, "[F{}][A] PhaseCompositing", frame_number_);

  // Execute module compositing work
  co_await module_manager_->ExecutePhase(PhaseId::kCompositing, context);
}

// ReSharper disable once CppMemberFunctionMayBeConst
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

// ReSharper disable once CppMemberFunctionMayBeConst
auto AsyncEngine::PhaseAsyncPoll(FrameContext& context) -> void
{
  // Engine core sets the current phase for async work
  const auto tag = internal::EngineTagFactory::Get();
  context.SetCurrentPhase(PhaseId::kAsyncPoll, tag);

  // Execute module async work (fire and forget style for now)
  auto async_work = module_manager_->ExecutePhase(PhaseId::kAsyncPoll, context);
}

// ReSharper disable once CppMemberFunctionMayBeStatic
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

  const auto frame_end = GetPhysicalClock().Now();
  const auto total
    = duration_cast<microseconds>((frame_end.get() - frame_start_ts_.get()));
  LOG_F(2, "Frame {} end | total={}us", frame_number_, total.count());
  // Let the platform finalize frame-level deferred operations (e.g. native
  // window destruction). Doing this after EndFrame-present ensures the
  // window and any per-frame resources are still valid during the frame and
  // are torn down only at the frame boundary.
  if (platform_) {
    try {
      LOG_F(2, "Calling Platform::OnFrameEnd at frame {}", frame_number_.get());
      platform_->OnFrameEnd();
    } catch (const std::exception& ex) {
      LOG_F(WARNING, "Platform::OnFrameEnd threw: {}", ex.what());
    }
  }
}

// ReSharper disable once CppMemberFunctionMayBeStatic
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
  //                     microseconds task_cost) -> co::Co<> {
  //     LOG_F(1, "[F{}][B][START] {} (cost {}us)", frame_index_, task_name,
  //       task_cost.count());

  //     auto start =  steady_clock::now();
  //     co_await SimulateWork(task_name, task_cost);
  //     auto end =  steady_clock::now();

  //     ParallelResult r { task_name,
  //        duration_cast< microseconds>(end - start)
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

// ReSharper disable once CppMemberFunctionMayBeConst
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

// ReSharper disable once CppMemberFunctionMayBeStatic
auto AsyncEngine::InitializeDetachedServices() -> void
{
  LOG_F(1, "Initializing detached services (Category D)");

  // TODO: Initialize crash dump detection service
  // Set up crash dump monitoring and symbolication service
  // This service runs detached from frame loop and handles crash reporting
  LOG_F(1, "[D] Crash dump detection service initialized");
}

// ReSharper disable once CppMemberFunctionMayBeConst
auto AsyncEngine::UpdateFrameTiming(FrameContext& context) -> void
{
  // Now delegated to TimeManager at PhaseFrameStart
  if (!time_manager_) {
    return;
  }
  const auto& td = time_manager_->GetFrameTimingData();
  ModuleTimingData module_timing = context.GetModuleTimingData();
  module_timing.game_delta_time = td.simulation_delta;
  module_timing.fixed_delta_time
    = time_manager_->GetSimulationClock().GetFixedTimestep();
  module_timing.interpolation_alpha
    = static_cast<float>(td.interpolation_alpha);
  module_timing.current_fps = static_cast<float>(td.current_fps);
  const auto tag = internal::EngineTagFactory::Get();
  context.SetModuleTimingData(module_timing, tag);
}

// Clock accessors
auto AsyncEngine::GetPhysicalClock() const noexcept
  -> const time::PhysicalClock&
{
  return platform_->GetPhysicalClock();
}

auto AsyncEngine::GetSimulationClock() const noexcept
  -> const time::SimulationClock&
{
  return time_manager_->GetSimulationClock();
}
auto AsyncEngine::GetSimulationClock() noexcept -> time::SimulationClock&
{
  return time_manager_->GetSimulationClock();
}
auto AsyncEngine::GetPresentationClock() const noexcept
  -> const time::PresentationClock&
{
  return time_manager_->GetPresentationClock();
}
auto AsyncEngine::GetPresentationClock() noexcept -> time::PresentationClock&
{
  return time_manager_->GetPresentationClock();
}
auto AsyncEngine::GetNetworkClock() const noexcept -> const time::NetworkClock&
{
  return time_manager_->GetNetworkClock();
}
auto AsyncEngine::GetNetworkClock() noexcept -> time::NetworkClock&
{
  return time_manager_->GetNetworkClock();
}
auto AsyncEngine::GetAuditClock() const noexcept -> const time::AuditClock&
{
  return time_manager_->GetAuditClock();
}
auto AsyncEngine::GetAuditClock() noexcept -> time::AuditClock&
{
  return time_manager_->GetAuditClock();
}

} // namespace oxygen
