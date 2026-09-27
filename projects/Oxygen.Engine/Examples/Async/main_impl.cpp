//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <source_location>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

#include "Async/MainModule.h"
#include "Common/DemoCli.h"
#include "Common/FrameCaptureCli.h"
#include "Common/FrameCaptureCliOptions.h"
#include "DemoShell/Runtime/DemoAppContext.h"
#include "DemoShell/Services/SettingsService.h"

#include <Oxygen/Config/EngineConfig.h>
#include <Oxygen/Config/PathFinderConfig.h>
#include <Oxygen/Config/PlatformConfig.h>
#include <Oxygen/Config/RendererConfig.h>
#include <Oxygen/Console/StartupPlan.h>
#include <Oxygen/Graphics/Common/Types/QueueRole.h>
#ifdef OXYGEN_BUILD_UI_TESTS
#  include "DemoShell/Test/UiTestSession.h"
#endif

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Clap/Cli.h>
#include <Oxygen/Clap/Command.h>
#include <Oxygen/Core/EngineModule.h>
#include <Oxygen/Engine/AsyncEngine.h>
#include <Oxygen/Graphics/Common/BackendModule.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Input/InputSystem.h>
#include <Oxygen/Loader/GraphicsBackendLoader.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/EventLoop.h>
#include <Oxygen/OxCo/Nursery.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/Platform/Platform.h>
#include <Oxygen/SceneSync/SceneObserverSyncModule.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/RendererCapability.h>

namespace co = oxygen::co;
namespace engine = oxygen::engine;
using oxygen::AsyncEngine;
using oxygen::EngineConfig;
using oxygen::Graphics;
using oxygen::GraphicsBackendLoader;
using oxygen::GraphicsConfig;
using oxygen::observer_ptr;
using oxygen::PathFinderConfig;
using oxygen::Platform;
using oxygen::PlatformConfig;
using oxygen::RendererImplementation;
using oxygen::examples::SettingsService;
using oxygen::graphics::BackendType;
using oxygen::graphics::QueueRole;
using namespace std::chrono_literals;

namespace {

//! Pump platform work while AsyncEngine schedules and paces its frame phases.
auto EventLoopRun(const oxygen::examples::DemoAppContext& app) -> void
{
  while (app.running.load(std::memory_order_relaxed)) {
    const bool async_work = app.platform->Async().PollOne() != 0U;
    const bool input_work = !app.headless && app.platform->Events().PollOne();
    if (!async_work && !input_work) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }
}
} // namespace

template <> struct co::EventLoopTraits<oxygen::examples::DemoAppContext> {
  static auto Run(oxygen::examples::DemoAppContext& app) -> void
  {
    EventLoopRun(app);
  }
  static auto Stop(oxygen::examples::DemoAppContext& app) -> void
  {
    app.running.store(false, std::memory_order_relaxed);
  }
  static auto IsRunning(const oxygen::examples::DemoAppContext& app) -> bool
  {
    return app.running.load(std::memory_order_relaxed);
  }
  static auto EventLoopId(oxygen::examples::DemoAppContext& app) -> EventLoopID
  {
    return EventLoopID(&app);
  }
};

namespace {

auto RegisterEngineModules(oxygen::examples::DemoAppContext& app) -> void
{
  // Register engine modules
  LOG_F(INFO, "Registering engine modules...");

  // Helper lambda to register modules with error checking
  auto register_module
    = [&](std::unique_ptr<engine::EngineModule> module) -> void {
    const bool registered = app.engine->RegisterModule(std::move(module));
    if (!registered) {
      LOG_F(ERROR, "Failed to register module");
      throw std::runtime_error("Module registration failed");
    }
  };

  // Register built-in engine modules (one-time)
  {
    auto input_sys = std::make_unique<oxygen::engine::InputSystem>(
      app.platform->Input().ForRead());
    app.input_system = observer_ptr { input_sys.get() };
    register_module(std::move(input_sys));

    oxygen::RendererConfig renderer_config {
      .path_finder_config = app.engine->GetEngineConfig().path_finder_config,
      .upload_queue_key = app.queue_strategy.KeyFor(QueueRole::kTransfer).get(),
      .enable_imgui = !app.headless,
    };
    register_module(std::make_unique<oxygen::examples::async::MainModule>(app));
    register_module(
      std::make_unique<oxygen::scenesync::SceneObserverSyncModule>(
        engine::kSceneObserverSyncModulePriority));

    constexpr auto kAsyncVortexCapabilities
      = oxygen::vortex::RendererCapabilityFamily::kScenePreparation
      | oxygen::vortex::RendererCapabilityFamily::kGpuUploadAndAssetBinding
      | oxygen::vortex::RendererCapabilityFamily::kLightingData
      | oxygen::vortex::RendererCapabilityFamily::kShadowing
      | oxygen::vortex::RendererCapabilityFamily::kEnvironmentLighting
      | oxygen::vortex::RendererCapabilityFamily::kFinalOutputComposition
      | oxygen::vortex::RendererCapabilityFamily::kDiagnosticsAndProfiling
      | oxygen::vortex::RendererCapabilityFamily::kDeferredShading;
    register_module(std::make_unique<oxygen::vortex::Renderer>(
      app.gfx_weak, renderer_config, kAsyncVortexCapabilities));
  }
}

auto StopOnLastWindowClosed(observer_ptr<oxygen::examples::DemoAppContext> app)
  -> co::Co<>
{
  co_await app->platform->Windows().LastWindowClosed();
  app->engine->Stop();
}

auto AsyncMain(observer_ptr<oxygen::examples::DemoAppContext> app)
  -> co::Co<int>
{
  // Structured concurrency scope.
  OXCO_WITH_NURSERY(n)
  {
    app->running.store(true, std::memory_order_relaxed);

    // PLatform started and running is a prerequisite for many of the modules
    // and the other subsystems.
    co_await n.Start(&Platform::ActivateAsync, std::ref(*app->platform));
    app->platform->Run();

    DCHECK_F(!app->gfx_weak.expired());
    auto gfx = app->gfx_weak.lock();
    co_await n.Start(&Graphics::ActivateAsync, std::ref(*gfx));
    gfx->Run();

    co_await n.Start(&AsyncEngine::ActivateAsync, std::ref(*app->engine));
    app->engine->Run();

    // Everything is started, now register modules
    RegisterEngineModules(*app);

    if (!app->headless) {
      n.Start(StopOnLastWindowClosed, app);
    }

    co_await app->engine->Completed();

    co_return co::kCancel;
  };

  co_return EXIT_SUCCESS;
}
} // namespace

extern "C" auto MainImpl(std::span<const char*> args) -> int
{
  using namespace oxygen::clap; // NOLINT

#ifdef OXYGEN_BUILD_UI_TESTS
  oxygen::examples::testing::UiTestSession::InitializeSettings();
#endif
  SettingsService::ForDemoApp();

  uint32_t frames = 0U;
  constexpr uint32_t kDefaultTargetFps = 100U;
  uint32_t target_fps = kDefaultTargetFps; // desired frame pacing
  bool headless = false;
  bool enable_vsync = true;
  std::string resolution;
  oxygen::examples::cli::GraphicsToolingCliState graphics_tooling_cli {};
  oxygen::examples::cli::FrameCaptureCliState capture_cli {};
  oxygen::examples::DemoAppContext app {};

  try {
    const Command::Ptr default_command
      = CommandBuilder(Command::DEFAULT)
          .WithOptions(oxygen::examples::cli::MakeRuntimeOptions({
            .frames = &frames,
            .target_fps = &target_fps,
            .headless = &headless,
            .fullscreen = &app.fullscreen,
            .vsync = &enable_vsync,
            .resolution = &resolution,
          }))
          .WithOptions(oxygen::examples::cli::MakeGraphicsToolingOptions(
            graphics_tooling_cli))
          .WithOptions(oxygen::examples::cli::MakeCaptureOptions(capture_cli))
          .WithOptions(
            oxygen::examples::cli::MakeAdvancedCaptureOptions(capture_cli),
            true);

    auto cli = oxygen::examples::cli::BuildCli(
      "async", "Async engine orchestration demo", default_command);

    const int argc = static_cast<int>(args.size());
    const char** argv = args.data();
    auto context = cli->Parse(argc, argv);
    if (oxygen::examples::cli::HandleMetaCommand(context, default_command)) {
      return EXIT_SUCCESS;
    }
    app.headless = headless;
    app.window_resolution = oxygen::examples::cli::ResolveWindowResolution(
      context, resolution, headless);

    oxygen::examples::cli::ValidateGraphicsToolingOptions(graphics_tooling_cli);
    LOG_F(INFO, "Parsed frames option = {}", frames);
    LOG_F(INFO, "Parsed fps option = {}", target_fps);
    LOG_F(INFO, "Parsed fullscreen option = {}", app.fullscreen);
    LOG_F(INFO, "Parsed vsync option = {}", enable_vsync);
    oxygen::examples::cli::LogGraphicsToolingOptions(graphics_tooling_cli);
    oxygen::examples::cli::LogCaptureOptions(capture_cli);
    LOG_F(INFO, "Starting async demo for {} frames (target {} fps)", frames,
      target_fps);

    // Create the platform
    app.platform = std::make_shared<Platform>(PlatformConfig {
      .headless = headless,
      .thread_pool_size = (std::min)(4U, std::thread::hardware_concurrency()),
    });

    const auto workspace_root
      = std::filesystem::path(std::source_location::current().file_name())
          .parent_path()
          .parent_path()
          .parent_path();

    // Load the graphics backend
    const auto path_finder_config
      = PathFinderConfig::Create()
          .WithWorkspaceRoot(workspace_root)
          .WithScriptSourceRoots({ workspace_root / "Examples" / "Content" })
          .Build();
    const auto frame_capture_config
      = oxygen::examples::cli::BuildFrameCaptureConfig(capture_cli, headless);
    const GraphicsConfig gfx_config {
      .enable_debug_layer = graphics_tooling_cli.enable_debug_layer,
      .enable_validation = false,
      .enable_aftermath = graphics_tooling_cli.enable_aftermath,
      .preferred_card_name = std::nullopt,
      .preferred_card_device_id = std::nullopt,
      .headless = headless,
      .enable_vsync = enable_vsync,
      .frame_capture = frame_capture_config,
      .extra = {},
    };
    const auto& loader = GraphicsBackendLoader::GetInstance();
    app.gfx_weak = loader.LoadBackend(
      headless ? BackendType::kHeadless : BackendType::kDirect3D12, gfx_config,
      path_finder_config);
    CHECK_F(
      !app.gfx_weak.expired()); // Expect a valid graphics backend, or abort
    app.gfx_weak.lock()->CreateCommandQueues(app.queue_strategy);

    oxygen::console::ConsoleStartupPlan startup_cvars {};
    oxygen::examples::cli::SeedCommonRuntimeStartupCVars(
      context, target_fps, enable_vsync, startup_cvars);

    app.engine = std::make_shared<AsyncEngine>(
      app.platform,
      app.gfx_weak,
      EngineConfig {
        .renderer = {
          .implementation = RendererImplementation::kVortex,
        },
        .application = { .name = "Async Example", .version = 1U, },
        .target_fps = target_fps,
        .frame_count = frames,
        .enable_asset_loader = true,
        .asset_loader = {},
        .physics = {},
        .scripting = {},
        .path_finder_config = path_finder_config,
        .graphics = gfx_config,
        .timing = {
          .pacing_safety_margin = 250us,
        },
      },
      startup_cvars
    );

    const auto rc = co::Run(app, AsyncMain(observer_ptr { &app }));

    app.platform->Stop();
    app.engine.reset();
    if (!app.gfx_weak.expired()) {
      auto gfx = app.gfx_weak.lock();
      gfx->Stop();
      gfx.reset();
    }
    // Make sure no one holds a reference to the Graphics instance at this
    // point.
    loader.UnloadBackend();
    app.platform.reset();

    LOG_F(INFO, "exit code: {}", rc);
    // Ensure any buffered log output is flushed before process exit so that
    // external log collectors (or test harnesses) receive the final messages.
    loguru::flush();
    loguru::shutdown();
#ifdef OXYGEN_BUILD_UI_TESTS
    return oxygen::examples::testing::UiTestSession::ExitCode(rc);
#else
    return rc;
#endif
  } catch (const oxygen::examples::cli::FrameCaptureCliError& e) {
    LOG_F(ERROR, "CLI parse error: {}", e.what());
    loguru::flush();
    loguru::shutdown();
    return EXIT_FAILURE;
  } catch (const CmdLineArgumentsError& e) {
    LOG_F(ERROR, "CLI parse error: {}", e.what());
    loguru::flush();
    loguru::shutdown();
    return EXIT_FAILURE;
  } catch (const std::exception& e) {
    LOG_F(ERROR, "Unhandled exception: {}", e.what());
    loguru::flush();
    loguru::shutdown();
    return EXIT_FAILURE;
  }
}
