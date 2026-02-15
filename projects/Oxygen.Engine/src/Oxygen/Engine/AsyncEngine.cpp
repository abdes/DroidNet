//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <ranges>
#include <thread>
#include <utility>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Config/PathFinder.h>
#include <Oxygen/Console/Console.h>
#include <Oxygen/Content/AssetLoader.h>
#include <Oxygen/Core/EngineTag.h>
#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Core/Time/PhysicalClock.h>
#include <Oxygen/Engine/AsyncEngine.h>
#include <Oxygen/Engine/TimeManager.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Input/InputSystem.h>
#include <Oxygen/OxCo/ThreadPool.h>
#include <Oxygen/Platform/Platform.h>
#include <Oxygen/content/EngineTag.h>

// Implementation of EngineTagFactory. Provides access to EngineTag capability
// tokens, only from the engine core. When building tests, allow tests to
// override by defining OXYGEN_ENGINE_TESTING.
#if !defined(OXYGEN_ENGINE_TESTING)
namespace oxygen::engine::internal {
auto EngineTagFactory::Get() noexcept -> EngineTag { return EngineTag {}; }
} // namespace oxygen::engine::internal
#endif

#if !defined(OXYGEN_ENGINE_TESTING)
namespace oxygen::content::internal {
auto EngineTagFactory::Get() noexcept -> EngineTag { return EngineTag {}; }
} // namespace oxygen::content::internal
#endif

using namespace std::chrono;
using namespace std::chrono_literals;
namespace t = oxygen::time;

namespace oxygen {

using core::PhaseId;
using namespace oxygen::engine;

// For convenience in timing configuration access
using TimingConfig = TimingConfig;

namespace {
  using oxygen::console::CommandContext;
  using oxygen::console::CommandDefinition;
  using oxygen::console::CommandFlags;
  using oxygen::console::CommandSource;
  using oxygen::console::CVarDefinition;
  using oxygen::console::CVarFlags;
  using oxygen::console::ExecutionResult;
  using oxygen::console::ExecutionStatus;

  constexpr std::string_view kCVarEngineTargetFps = "ngin.target_fps";

  auto MakeConfigFileContext() -> CommandContext
  {
    return CommandContext {
      .source = CommandSource::kConfigFile,
      .shipping_build = false,
    };
  }

  struct PhaseTimer {
    PhaseTimer(FrameContext& context, core::PhaseId phase,
      const time::PhysicalClock& clock)
      : context_(context)
      , phase_(phase)
      , clock_(clock)
      , start_(clock.Now())
    {
    }

    ~PhaseTimer()
    {
      const auto duration
        = duration_cast<microseconds>(clock_.Since(start_).get());
      context_.SetPhaseDuration(
        phase_, duration, engine::internal::EngineTagFactory::Get());
    }

    FrameContext& context_;
    core::PhaseId phase_;
    const time::PhysicalClock& clock_;
    time::PhysicalTime start_;
  };
} // namespace

AsyncEngine::AsyncEngine(std::shared_ptr<Platform> platform,
  std::weak_ptr<Graphics> graphics, EngineConfig config) noexcept
  : config_(std::move(config))
  , platform_(std::move(platform))
  , gfx_weak_(std::move(graphics))
  , path_finder_config_(
      std::make_shared<const PathFinderConfig>(config_.path_finder_config))
  , path_finder_(path_finder_config_, std::filesystem::current_path())
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
  InitializeConsoleRuntime();

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
    // Ensure AssetLoader is activated (its own nursery opened) before the
    // frame loop starts so other subsystems can rely on it.
    if (asset_loader_) {
      co_await nursery_->Start(
        &oxygen::content::AssetLoader::ActivateAsync, asset_loader_.get());
    }

    co_await FrameLoop();
    co_await Shutdown();

    // Signal completion once the frame loop has finished executing.
    completed_.Trigger();
    LOG_F(INFO, "Engine completed after {} frames", frame_number_);
  });
}

auto AsyncEngine::Shutdown() -> co::Co<>
{
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
  // Move first so any re-entrant or late calls to AsyncEngine::GetModule()
  // observe a null module_manager_ during teardown.
  auto module_manager = std::move(module_manager_);
  module_manager.reset();

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
  SavePersistedConsoleCVars();
  SavePersistedConsoleHistory();
  co_await platform_->Shutdown();
}

auto AsyncEngine::Stop() -> void { shutdown_requested_ = true; }

// Register a module (takes ownership). Modules are sorted by priority.
// ReSharper disable once CppMemberFunctionMayBeConst
auto AsyncEngine::RegisterModule(std::unique_ptr<EngineModule> module) noexcept
  -> bool
{
  if (!module_manager_) {
    return false;
  }
  return module_manager_->RegisterModule(std::move(module));
}

// Optional: unregister by name. Returns true if removed.
// ReSharper disable once CppMemberFunctionMayBeConst
auto AsyncEngine::UnregisterModule(std::string_view name) noexcept -> void
{
  if (!module_manager_) {
    return;
  }
  module_manager_->UnregisterModule(name);
}

auto AsyncEngine::SubscribeModuleAttached(
  ModuleAttachedCallback cb, const bool replay_existing) -> ModuleSubscription
{
  if (!module_manager_) {
    return {};
  }
  return module_manager_->SubscribeModuleAttached(
    std::move(cb), replay_existing);
}

auto AsyncEngine::GetEngineConfig() const noexcept -> const EngineConfig&
{
  return config_;
}

auto AsyncEngine::GetAssetLoader() const noexcept
  -> observer_ptr<content::IAssetLoader>
{
  return observer_ptr { asset_loader_.get() };
}

auto AsyncEngine::SetTargetFps(uint32_t fps) noexcept -> void
{
  if (fps > EngineConfig::kMaxTargetFps) {
    fps = EngineConfig::kMaxTargetFps;
  }
  if (config_.target_fps == fps) {
    return;
  }
  config_.target_fps = fps;
  LOG_F(INFO, "AsyncEngine target_fps set to {}", config_.target_fps);
}

auto AsyncEngine::GetConsole() noexcept -> console::Console&
{
  return console_;
}

auto AsyncEngine::GetConsole() const noexcept -> const console::Console&
{
  return console_;
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
    auto context = observer_ptr { &frame_context_ };
    const auto tag = internal::EngineTagFactory::Get();

    // Reset stage timings for the new frame to ensure zero-duration phases are
    // correctly reported.
    auto timing = frame_context_.GetFrameTiming();
    for (const auto phase : enum_as_index<core::PhaseId>) {
      timing.stage_timings[phase] = std::chrono::microseconds(0);
    }
    context->SetFrameTiming(timing, tag);

    // Fence polling, epoch advance, deferred destruction retirement
    {
      PhaseTimer timer(
        frame_context_, core::PhaseId::kFrameStart, GetPhysicalClock());
      co_await PhaseFrameStart(context);
    }

    // B0: Input snapshot
    {
      PhaseTimer timer(
        frame_context_, core::PhaseId::kInput, GetPhysicalClock());
      co_await PhaseInput(context);
    }
    // Network packet application & reconciliation
    {
      PhaseTimer timer(frame_context_, core::PhaseId::kNetworkReconciliation,
        GetPhysicalClock());
      co_await PhaseNetworkReconciliation(context);
    }
    // Random seed management for determinism (BEFORE any systems use
    // randomness)
    {
      PhaseTimer timer(frame_context_, core::PhaseId::kRandomSeedManagement,
        GetPhysicalClock());
      PhaseRandomSeedManagement(context);
    }
    // B1: Fixed simulation deterministic state
    {
      PhaseTimer timer(
        frame_context_, core::PhaseId::kFixedSimulation, GetPhysicalClock());
      co_await PhaseFixedSim(context);
    }
    // Variable gameplay logic
    {
      PhaseTimer timer(
        frame_context_, core::PhaseId::kGameplay, GetPhysicalClock());
      co_await PhaseGameplay(context);
    }
    // B2: Structural mutations
    {
      PhaseTimer timer(
        frame_context_, core::PhaseId::kSceneMutation, GetPhysicalClock());
      co_await PhaseSceneMutation(context);
    }
    // Transform propagation
    {
      PhaseTimer timer(frame_context_, core::PhaseId::kTransformPropagation,
        GetPhysicalClock());
      co_await PhaseTransforms(context);
    }
    // Publish view registrations after transforms and before snapshot.
    {
      PhaseTimer timer(
        frame_context_, core::PhaseId::kPublishViews, GetPhysicalClock());
      co_await PhasePublishViews(context);
    }

    // Immutable snapshot build (B4)
    const UnifiedSnapshot* snapshot_ptr = nullptr;
    {
      PhaseTimer timer(
        frame_context_, core::PhaseId::kSnapshot, GetPhysicalClock());
      snapshot_ptr = &co_await PhaseSnapshot(context);
    }
    const auto& snapshot = *snapshot_ptr;

    // Launch and join Category B barriered parallel tasks (B4 upon completion).
    {
      PhaseTimer timer(
        frame_context_, core::PhaseId::kParallelTasks, GetPhysicalClock());
      co_await ParallelTasks(context, snapshot);
    }
    // Serial post-parallel integration (Category A resumes after B4)
    {
      PhaseTimer timer(
        frame_context_, core::PhaseId::kPostParallel, GetPhysicalClock());
      co_await PhasePostParallel(context);
    }

    // UI update phase: process UI systems, generate rendering artifacts
    {
      PhaseTimer timer(
        frame_context_, core::PhaseId::kGuiUpdate, GetPhysicalClock());
      co_await PhaseGuiUpdate(context);
    }

    // Frame multi-view rendering
    {
      PhaseTimer timer_pre(
        frame_context_, core::PhaseId::kPreRender, GetPhysicalClock());
      co_await PhasePreRender(context);
      PhaseTimer timer_render(
        frame_context_, core::PhaseId::kRender, GetPhysicalClock());
      co_await PhaseRender(context);
      PhaseTimer timer_comp(
        frame_context_, core::PhaseId::kCompositing, GetPhysicalClock());
      co_await PhaseCompositing(context);
    }

    // Synchronous sequential presentation
    {
      PhaseTimer timer(
        frame_context_, core::PhaseId::kPresent, GetPhysicalClock());
      PhasePresent(context);
    }

    // Poll async pipeline readiness and integrate ready resources
    {
      PhaseTimer timer(
        frame_context_, core::PhaseId::kAsyncPoll, GetPhysicalClock());
      PhaseAsyncPoll(context);
    }

    // Adaptive budget management for next frame
    {
      PhaseTimer timer(
        frame_context_, core::PhaseId::kBudgetAdapt, GetPhysicalClock());
      PhaseBudgetAdapt(context);
    }

    // Frame end timing and metrics
    {
      PhaseTimer timer(
        frame_context_, core::PhaseId::kFrameEnd, GetPhysicalClock());
      co_await PhaseFrameEnd(context);
    }

    // Yield control to thread pool before pacing so any residual
    // work doesn't skew the next frame start timestamp.
    co_await platform_->Threads().Run([](co::ThreadPool::CancelToken) { });

    // Measure pacing separately
    const auto pacing_start = GetPhysicalClock().Now();

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
    const auto pacing_end = GetPhysicalClock().Now();
    const auto pacing_duration
      = duration_cast<microseconds>(pacing_end.get() - pacing_start.get());

    // Update the metrics for the frame just completed (will be seen by next
    // frame's UI)
    {
      auto final_timing = context->GetFrameTiming();
      final_timing.pacing_duration = pacing_duration;
      context->SetFrameTiming(final_timing, tag);
    }
  }
  co_return;
}

auto AsyncEngine::PhaseFrameStart(observer_ptr<FrameContext> context)
  -> co::Co<>
{
  const auto tag = internal::EngineTagFactory::Get();
  context->SetCurrentPhase(PhaseId::kFrameStart, tag);
  frame_start_ts_ = GetPhysicalClock().Now();

  // TODO: setup all the properties of context that need to be set

  // NOTE: Views are now persistent with stable IDs across frames
  // Modules register views once (RegisterView) and update them (UpdateView)
  context->ClearPresentableFlags(tag);
  context->SetFrameSequenceNumber(frame_number_, tag);
  context->SetFrameSlot(frame_slot_, tag);
  context->SetFrameStartTime(frame_start_ts_.get(), tag);
  context->SetThreadPool(&platform_->Threads(), tag);
  context->SetGraphicsBackend(gfx_weak_, tag);

  // Update timing data for this frame via TimeManager
  if (time_manager_) {
    time_manager_->BeginFrame(frame_start_ts_);
    // Populate ModuleTimingData for modules
    const auto& td = time_manager_->GetFrameTimingData();
    ModuleTimingData module_timing {};
    module_timing.game_delta_time = td.simulation_delta;
    module_timing.fixed_delta_time
      = time_manager_->GetSimulationClock().GetFixedTimestep();
    module_timing.interpolation_alpha
      = static_cast<float>(td.interpolation_alpha);
    module_timing.current_fps = static_cast<float>(td.current_fps);
    context->SetModuleTimingData(module_timing, tag);
  }

  // Apply runtime console-driven settings at a deterministic frame boundary.
  ApplyConsoleStateAtFrameStart(context);

  // Initialize graphics layer for this frame.
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

auto AsyncEngine::InitializeConsoleRuntime() -> void
{
  RegisterEngineConsoleBindings();
  RegisterServiceConsoleBindings();
  LoadPersistedConsoleCVars();
  LoadPersistedConsoleHistory();
  ApplyAllConsoleCVars();
}

auto AsyncEngine::RegisterEngineConsoleBindings() -> void
{
  (void)console_.RegisterCVar(CVarDefinition {
    .name = std::string(kCVarEngineTargetFps),
    .help = "Target frames per second (0 = uncapped)",
    .default_value = int64_t { static_cast<int64_t>(config_.target_fps) },
    .flags = CVarFlags::kArchive,
    .min_value = 0.0,
    .max_value = static_cast<double>(EngineConfig::kMaxTargetFps),
  });

  (void)console_.RegisterCommand(CommandDefinition {
    .name = "ngin.cvars.save",
    .help = "Save archived CVars to the configured cvars archive path",
    .flags = CommandFlags::kNone,
    .handler = [this](const std::vector<std::string>&, const CommandContext&)
      -> ExecutionResult { return console_.SaveArchiveCVars(path_finder_); },
  });

  (void)console_.RegisterCommand(CommandDefinition {
    .name = "ngin.cvars.load",
    .help = "Load archived CVars from the configured cvars archive path",
    .flags = CommandFlags::kNone,
    .handler = [this](const std::vector<std::string>&,
                 const CommandContext&) -> ExecutionResult {
      const auto result
        = console_.LoadArchiveCVars(path_finder_, MakeConfigFileContext());
      if (result.status == ExecutionStatus::kOk) {
        ApplyAllConsoleCVars();
      }
      return result;
    },
  });

  (void)console_.RegisterCommand(CommandDefinition {
    .name = "ngin.console.history.save",
    .help = "Save console command history",
    .flags = CommandFlags::kNone,
    .handler = [this](const std::vector<std::string>&, const CommandContext&)
      -> ExecutionResult { return console_.SaveHistory(path_finder_); },
  });

  (void)console_.RegisterCommand(CommandDefinition {
    .name = "ngin.console.history.load",
    .help = "Load console command history",
    .flags = CommandFlags::kNone,
    .handler = [this](const std::vector<std::string>&, const CommandContext&)
      -> ExecutionResult { return console_.LoadHistory(path_finder_); },
  });
}

auto AsyncEngine::RegisterServiceConsoleBindings() -> void
{
  if (auto gfx = gfx_weak_.lock()) {
    gfx->RegisterConsoleBindings(observer_ptr { &console_ });
  }
  if (asset_loader_) {
    asset_loader_->RegisterConsoleBindings(observer_ptr { &console_ });
  }
}

auto AsyncEngine::LoadPersistedConsoleCVars() -> void
{
  const auto result
    = console_.LoadArchiveCVars(path_finder_, MakeConfigFileContext());
  if (result.status == ExecutionStatus::kOk) {
    LOG_F(INFO, "{}", result.output);
  } else if (result.status != ExecutionStatus::kNotFound) {
    LOG_F(WARNING, "{}", result.error);
  }
}

auto AsyncEngine::SavePersistedConsoleCVars() const -> void
{
  const auto result = console_.SaveArchiveCVars(path_finder_);
  if (result.status == ExecutionStatus::kOk) {
    LOG_F(INFO, "{}", result.output);
  } else {
    LOG_F(WARNING, "{}", result.error);
  }
}

auto AsyncEngine::LoadPersistedConsoleHistory() -> void
{
  const auto result = console_.LoadHistory(path_finder_);
  if (result.status == ExecutionStatus::kOk) {
    LOG_F(INFO, "{}", result.output);
  } else if (result.status != ExecutionStatus::kNotFound) {
    LOG_F(WARNING, "{}", result.error);
  }
}

auto AsyncEngine::SavePersistedConsoleHistory() const -> void
{
  const auto result = console_.SaveHistory(path_finder_);
  if (result.status == ExecutionStatus::kOk) {
    LOG_F(INFO, "{}", result.output);
  } else {
    LOG_F(WARNING, "{}", result.error);
  }
}

auto AsyncEngine::ApplyEngineOwnedConsoleCVars() -> void
{
  int64_t target_fps = 0;
  if (console_.TryGetCVarValue<int64_t>(kCVarEngineTargetFps, target_fps)) {
    const auto clamped = std::clamp<int64_t>(
      target_fps, 0, static_cast<int64_t>(EngineConfig::kMaxTargetFps));
    const auto new_target_fps = static_cast<uint32_t>(clamped);
    if (config_.target_fps != new_target_fps) {
      SetTargetFps(new_target_fps);
    }
  }
}

auto AsyncEngine::ApplyAllConsoleCVars() -> void
{
  ApplyEngineOwnedConsoleCVars();
  if (auto gfx = gfx_weak_.lock()) {
    gfx->ApplyConsoleCVars(console_);
  }
  if (asset_loader_) {
    asset_loader_->ApplyConsoleCVars(console_);
  }
  if (module_manager_) {
    module_manager_->ApplyConsoleCVars(observer_ptr { &console_ });
  }
}

auto AsyncEngine::ApplyConsoleStateAtFrameStart(
  [[maybe_unused]] observer_ptr<FrameContext> context) -> void
{
  const auto applied = console_.ApplyLatchedCVars();
  if (applied > 0U) {
    DLOG_F(2, "Applied {} latched CVars at frame start", applied);
  }
  ApplyAllConsoleCVars();
}

// ReSharper disable once CppMemberFunctionMayBeConst
auto AsyncEngine::PhaseInput(observer_ptr<FrameContext> context) -> co::Co<>
{
  const auto tag = internal::EngineTagFactory::Get();
  context->SetCurrentPhase(PhaseId::kInput, tag);

  LOG_F(2, "[F{}][A] PhaseInput", frame_number_);

  // Engine core sets the current phase
  context->SetCurrentPhase(PhaseId::kInput, tag);

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
    context->SetInputSnapshot(
      std::static_pointer_cast<const void>(std::move(snap)), tag);
  }
}

// ReSharper disable once CppMemberFunctionMayBeConst
auto AsyncEngine::PhaseFixedSim(observer_ptr<FrameContext> context) -> co::Co<>
{
  const auto tag = internal::EngineTagFactory::Get();
  context->SetCurrentPhase(PhaseId::kFixedSimulation, tag);

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
      ModuleTimingData module_timing = context->GetModuleTimingData();
      module_timing.fixed_delta_time = sim.GetFixedTimestep();
      module_timing.fixed_steps_this_frame = s + 1;
      context->SetModuleTimingData(module_timing, tag);

      context->SetCurrentPhase(PhaseId::kFixedSimulation, tag);
      co_await module_manager_->ExecutePhase(
        PhaseId::kFixedSimulation, context);
    }

    ModuleTimingData module_timing = context->GetModuleTimingData();
    module_timing.fixed_steps_this_frame = steps;
    module_timing.interpolation_alpha
      = static_cast<float>(result.interpolation_alpha);
    context->SetModuleTimingData(module_timing, tag);

    LOG_F(2, "[F{}][A] PhaseFixedSim completed {} substeps, alpha={:.3f}",
      frame_number_, steps, module_timing.interpolation_alpha);
  } else {
    // Fallback: execute once to keep modules functional
    co_await module_manager_->ExecutePhase(PhaseId::kFixedSimulation, context);
  }
}

// ReSharper disable once CppMemberFunctionMayBeConst
auto AsyncEngine::PhaseGameplay(observer_ptr<FrameContext> context) -> co::Co<>
{
  const auto tag = internal::EngineTagFactory::Get();
  context->SetCurrentPhase(PhaseId::kGameplay, tag);

  LOG_F(2, "[F{}][A] PhaseGameplay", frame_number_);

  // Engine core sets the current phase
  context->SetCurrentPhase(PhaseId::kGameplay, tag);

  // Execute module gameplay logic first
  co_await module_manager_->ExecutePhase(PhaseId::kGameplay, context);
}

// ReSharper disable once CppMemberFunctionMayBeConst
auto AsyncEngine::PhaseNetworkReconciliation(observer_ptr<FrameContext> context)
  -> co::Co<>
{
  const auto tag = internal::EngineTagFactory::Get();
  context->SetCurrentPhase(PhaseId::kNetworkReconciliation, tag);

  LOG_F(2, "[F{}][A] PhaseNetworkReconciliation", frame_number_);

  // Engine core sets the current phase
  context->SetCurrentPhase(PhaseId::kNetworkReconciliation, tag);

  // TODO: Implement network packet application & authoritative reconciliation
  // Apply received network packets to authoritative game state
  // Reconcile client predictions with server authority
  co_return;
}

// ReSharper disable once CppMemberFunctionMayBeConst
auto AsyncEngine::PhaseRandomSeedManagement(observer_ptr<FrameContext> context)
  -> void
{
  const auto tag = internal::EngineTagFactory::Get();
  context->SetCurrentPhase(PhaseId::kRandomSeedManagement, tag);

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
auto AsyncEngine::PhaseSceneMutation(observer_ptr<FrameContext> context)
  -> co::Co<>
{
  const auto tag = internal::EngineTagFactory::Get();
  context->SetCurrentPhase(PhaseId::kSceneMutation, tag);

  LOG_F(2, "[F{}][A] PhaseSceneMutation (B2: structural integrity barrier)",
    frame_number_);

  // Engine core sets the current phase
  context->SetCurrentPhase(PhaseId::kSceneMutation, tag);

  // Execute module scene mutations first
  co_await module_manager_->ExecutePhase(PhaseId::kSceneMutation, context);
}

// ReSharper disable once CppMemberFunctionMayBeConst
auto AsyncEngine::PhaseTransforms(observer_ptr<FrameContext> context)
  -> co::Co<>
{
  const auto tag = internal::EngineTagFactory::Get();
  context->SetCurrentPhase(PhaseId::kTransformPropagation, tag);

  LOG_F(2, "[F{}][A] PhaseTransforms", frame_number_);

  // Engine core sets the current phase
  context->SetCurrentPhase(PhaseId::kTransformPropagation, tag);

  // Execute module transform propagation first
  co_await module_manager_->ExecutePhase(
    PhaseId::kTransformPropagation, context);
}

// ReSharper disable once CppMemberFunctionMayBeConst
auto AsyncEngine::PhasePublishViews(observer_ptr<FrameContext> context)
  -> co::Co<>
{
  const auto tag = internal::EngineTagFactory::Get();
  context->SetCurrentPhase(PhaseId::kPublishViews, tag);

  LOG_F(2, "[F{}][A] PhasePublishViews", frame_number_);

  co_await module_manager_->ExecutePhase(PhaseId::kPublishViews, context);
}

// ReSharper disable once CppMemberFunctionMayBeConst
auto AsyncEngine::PhaseSnapshot(observer_ptr<FrameContext> context)
  -> co::Co<const UnifiedSnapshot&>
{
  const auto tag = internal::EngineTagFactory::Get();
  context->SetCurrentPhase(PhaseId::kSnapshot, tag);

  LOG_F(2, "[F{}][A] PhaseSnapshot (build immutable snapshot)", frame_number_);
  // Execute module snapshot handlers synchronously (main thread)
  co_await module_manager_->ExecutePhase(PhaseId::kSnapshot, context);

  // Engine consolidates contributions and publishes snapshots last
  const auto& snapshot
    = context->PublishSnapshots(internal::EngineTagFactory::Get());
  LOG_F(2, "[F{}][A] Published snapshots v{}", frame_number_,
    snapshot.frameSnapshot.validation.snapshot_version);

  co_return snapshot;
}

// void AsyncEngine::SetRenderGraphBuilder(observer_ptr<FrameContext> context)
// {
//   render_graph_builder_ = std::make_unique<RenderGraphBuilder>();
//   auto tag = internal::EngineTagFactory::Get();

//   // Initialize the builder with the current frame context
//   render_graph_builder_->BeginGraph(context);

//   // Set the builder in the frame context so modules can access it
//   context->SetRenderGraphBuilder(
//     observer_ptr { render_graph_builder_.get() }, tag);
// }

// void AsyncEngine::ClearRenderGraphBuilder(observer_ptr<FrameContext> context)
// {
//   auto tag = internal::EngineTagFactory::Get();
//   context->SetRenderGraphBuilder(
//     observer_ptr<RenderGraphBuilder> {}, tag);
//   render_graph_builder_.reset();
// }

// ReSharper disable once CppMemberFunctionMayBeConst
auto AsyncEngine::PhaseGuiUpdate(observer_ptr<FrameContext> context) -> co::Co<>
{
  const auto tag = internal::EngineTagFactory::Get();
  context->SetCurrentPhase(PhaseId::kGuiUpdate, tag);

  LOG_F(2,
    "[F{}][A] PhaseGuiUpdate - UI systems and rendering artifact generation",
    frame_number_);

  // Execute module UI update work - modules generate UI rendering artifacts
  co_await module_manager_->ExecutePhase(PhaseId::kGuiUpdate, context);
}

// Pre-render phase: perform renderer and module preparation work (no
// command recording).
// ReSharper disable once CppMemberFunctionMayBeConst
auto AsyncEngine::PhasePreRender(observer_ptr<FrameContext> context) -> co::Co<>
{
  const auto tag = internal::EngineTagFactory::Get();
  context->SetCurrentPhase(PhaseId::kPreRender, tag);

  LOG_F(2, "[F{}][A] PhasePreRender - prepare rendering data", frame_number_);

  co_await module_manager_->ExecutePhase(PhaseId::kPreRender, context);
}

// Render phase: modules record commands and perform per-view rendering.
// ReSharper disable once CppMemberFunctionMayBeConst
auto AsyncEngine::PhaseRender(observer_ptr<FrameContext> context) -> co::Co<>
{
  const auto tag = internal::EngineTagFactory::Get();
  context->SetCurrentPhase(PhaseId::kRender, tag);

  LOG_F(2, "[F{}][A] PhaseRender - {} surfaces (record+submit phase)",
    frame_number_, context->GetSurfaces().size());

  co_await module_manager_->ExecutePhase(PhaseId::kRender, context);

  LOG_F(2, "[F{}][A] PhaseRender complete - modules recorded commands",
    frame_number_);
}

// ReSharper disable once CppMemberFunctionMayBeConst
auto AsyncEngine::PhaseCompositing(observer_ptr<FrameContext> context)
  -> co::Co<>
{
  const auto tag = internal::EngineTagFactory::Get();
  context->SetCurrentPhase(PhaseId::kCompositing, tag);

  LOG_F(2, "[F{}][A] PhaseCompositing", frame_number_);

  // Execute module compositing work
  co_await module_manager_->ExecutePhase(PhaseId::kCompositing, context);
}

// ReSharper disable once CppMemberFunctionMayBeConst
auto AsyncEngine::PhasePresent(observer_ptr<FrameContext> context) -> void
{
  const auto tag = internal::EngineTagFactory::Get();
  context->SetCurrentPhase(PhaseId::kPresent, tag);

  // If modules marked surfaces as presentable during rendering, use those
  // flags to determine which surfaces to present. This allows modules to
  // mark surfaces as ready asynchronously and the engine to present them.
  const auto presentable_surfaces = context->GetPresentableSurfaces();

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
auto AsyncEngine::PhaseAsyncPoll(observer_ptr<FrameContext> context) -> void
{
  // Engine core sets the current phase for async work
  const auto tag = internal::EngineTagFactory::Get();
  context->SetCurrentPhase(PhaseId::kAsyncPoll, tag);

  // Execute module async work (fire and forget style for now)
  auto async_work = module_manager_->ExecutePhase(PhaseId::kAsyncPoll, context);
}

// ReSharper disable once CppMemberFunctionMayBeStatic
auto AsyncEngine::PhaseBudgetAdapt(observer_ptr<FrameContext> context) -> void
{
  const auto tag = internal::EngineTagFactory::Get();

  // Set default budget stats based on target FPS
  FrameContext::BudgetStats budget;
  if (config_.target_fps > 0) {
    const auto budget_ms = milliseconds(1000) / config_.target_fps;
    budget.cpu_budget = budget_ms;
    budget.gpu_budget = budget_ms;
  } else {
    // Uncapped: use a default 16ms budget for metrics
    budget.cpu_budget = milliseconds(16);
    budget.gpu_budget = milliseconds(16);
  }

  // DEBUG: Force non-zero values
  if (budget.cpu_budget.count() == 0)
    budget.cpu_budget = milliseconds(16);
  if (budget.gpu_budget.count() == 0)
    budget.gpu_budget = milliseconds(16);

  context->SetBudgetStats(budget, tag);

  // TODO: Implement adaptive budget management
  // Monitor CPU frame time, GPU idle %, and queue depths
  // Degrade/defer tasks when over budget (IK refinement, particle collisions,
  // GI updates) Upgrade tasks when under budget (extra probe updates, higher
  // LOD, prefetch assets) Provide hysteresis to avoid oscillation
  // (time-window averaging)
}

auto AsyncEngine::PhaseFrameEnd(observer_ptr<FrameContext> context) -> co::Co<>
{
  const auto tag = internal::EngineTagFactory::Get();
  context->SetCurrentPhase(PhaseId::kFrameEnd, tag);

  // Execute module frame end work first
  co_await module_manager_->ExecutePhase(PhaseId::kFrameEnd, context);

  // Finalize graphics layer for this frame
  auto gfx = gfx_weak_.lock();
  if (!gfx) {
    // TODO: Handle graphics backend invalidation
    throw std::logic_error("Graphics backend no longer valid.");
  }
  gfx->EndFrame(frame_number_, frame_slot_);
  if (time_manager_) {
    time_manager_->EndFrame();
  }

  const auto frame_end = GetPhysicalClock().Now();
  const auto total
    = duration_cast<microseconds>((frame_end.get() - frame_start_ts_.get()));

  // Update frame timing metrics in context
  FrameContext::FrameTiming timing;
  timing = context->GetFrameTiming(); // preserves stage timings already set
  timing.frame_duration = total;

  // Ensure we never set exactly zero if any time elapsed
  if (timing.frame_duration.count() == 0 && total.count() > 0) {
    timing.frame_duration = microseconds(1);
  }

  context->SetFrameTiming(timing, tag);

  LOG_F(1, "Frame {} end | total={}us", frame_number_, total.count());
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
auto AsyncEngine::ParallelTasks(observer_ptr<FrameContext> context,
  const UnifiedSnapshot& snapshot) -> co::Co<>
{
  const auto tag = internal::EngineTagFactory::Get();
  context->SetCurrentPhase(PhaseId::kParallelTasks, tag);

  // parallel_results_.clear();
  // parallel_results_.reserve(parallel_specs_.size());

  // LOG_F(1, "[F{}][B] Dispatching {} parallel tasks + module parallel work",
  //   frame_index_, parallel_specs_.size());

  // // Engine core sets the current phase for parallel work
  // auto tag = internal::EngineTagFactory::Get();
  // context->SetCurrentPhase(PhaseId::kParallelWork, tag);

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
auto AsyncEngine::PhasePostParallel(observer_ptr<FrameContext> context)
  -> co::Co<>
{
  const auto tag = internal::EngineTagFactory::Get();
  context->SetCurrentPhase(PhaseId::kPostParallel, tag);

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

  if (config_.enable_asset_loader) {
    const auto tag = oxygen::content::internal::EngineTagFactory::Get();
    content::AssetLoaderConfig asset_loader_cfg {
      .thread_pool = observer_ptr<co::ThreadPool> { &platform_->Threads() },
      .verify_content_hashes = config_.asset_loader.verify_content_hashes,
    };
    asset_loader_
      = std::make_unique<content::AssetLoader>(tag, asset_loader_cfg);
    LOG_F(INFO, "[D] AssetLoader initialized");
  } else {
    LOG_F(INFO, "[D] AssetLoader disabled by config");
  }

  // TODO: Initialize crash dump detection service
  // Set up crash dump monitoring and symbolication service
  // This service runs detached from frame loop and handles crash reporting
  LOG_F(1, "[D] Crash dump detection service initialized");
}

// ReSharper disable once CppMemberFunctionMayBeConst
auto AsyncEngine::UpdateFrameTiming(observer_ptr<FrameContext> context) -> void
{
  // Now delegated to TimeManager at PhaseFrameStart
  if (!time_manager_) {
    return;
  }
  const auto& td = time_manager_->GetFrameTimingData();
  ModuleTimingData module_timing = context->GetModuleTimingData();
  module_timing.game_delta_time = td.simulation_delta;
  module_timing.fixed_delta_time
    = time_manager_->GetSimulationClock().GetFixedTimestep();
  module_timing.interpolation_alpha
    = static_cast<float>(td.interpolation_alpha);
  module_timing.current_fps = static_cast<float>(td.current_fps);
  const auto tag = internal::EngineTagFactory::Get();
  context->SetModuleTimingData(module_timing, tag);
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
