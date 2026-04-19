//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <source_location>
#include <span>
#include <string>
#include <thread>

#include <asio/signal_set.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Clap/Cli.h>
#include <Oxygen/Clap/Command.h>
#include <Oxygen/Clap/CommandLineContext.h>
#include <Oxygen/Clap/Fluent/DSL.h>
#include <Oxygen/Clap/Fluent/OptionValueBuilder.h>
#include <Oxygen/Clap/Option.h>
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
#include <Oxygen/SceneSync/RuntimeMotionProducerModule.h>
#include <Oxygen/Vortex/ShaderDebugMode.h>
#include <Oxygen/Vortex/RendererCapability.h>
#include <Oxygen/Vortex/Renderer.h>

#include "Common/DemoCli.h"
#include "Common/FrameCaptureCliOptions.h"
#include "DemoShell/Runtime/DemoAppContext.h"
#include "VortexBasic/MainModule.h"

using namespace oxygen;
using namespace oxygen::engine;
using namespace oxygen::graphics;
using namespace std::chrono_literals;

namespace {

auto ParseVortexShaderDebugMode(std::string_view text)
  -> oxygen::vortex::ShaderDebugMode
{
  using oxygen::vortex::ShaderDebugMode;

  if (text.empty() || text == "disabled") {
    return ShaderDebugMode::kDisabled;
  }
  if (text == "base-color") {
    return ShaderDebugMode::kBaseColor;
  }
  if (text == "world-normals") {
    return ShaderDebugMode::kWorldNormals;
  }
  if (text == "roughness") {
    return ShaderDebugMode::kRoughness;
  }
  if (text == "metalness") {
    return ShaderDebugMode::kMetalness;
  }
  if (text == "scene-depth-raw") {
    return ShaderDebugMode::kSceneDepthRaw;
  }
  if (text == "scene-depth-linear") {
    return ShaderDebugMode::kSceneDepthLinear;
  }

  throw std::runtime_error(
    "Unsupported Vortex shader debug mode: " + std::string(text));
}

auto EventLoopRun(const oxygen::examples::DemoAppContext& app) -> void
{
  while (app.running.load(std::memory_order_relaxed)) {

    app.platform->Async().PollOne();
    if (!app.headless) {
      app.platform->Events().PollOne();
    }

    if (!app.running.load(std::memory_order_relaxed)) {
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

auto RegisterEngineModules(oxygen::examples::DemoAppContext& app,
  const oxygen::vortex::ShaderDebugMode shader_debug_mode) -> void
{
  LOG_F(INFO, "Registering engine modules...");

  auto register_module = [&](std::unique_ptr<engine::EngineModule> module) {
    const bool registered = app.engine->RegisterModule(std::move(module));
    if (!registered) {
      LOG_F(ERROR, "Failed to register module");
      throw std::runtime_error("Module registration failed");
    }
  };

  {
    auto input_sys = std::make_unique<oxygen::engine::InputSystem>(
      app.platform->Input().ForRead());
    app.input_system = observer_ptr { input_sys.get() };
    register_module(std::move(input_sys));

    oxygen::RendererConfig renderer_config {
      .upload_queue_key = app.queue_strategy.KeyFor(QueueRole::kTransfer).get(),
    };

    // Register MainModule before the renderer so it runs first in each phase.
    register_module(std::make_unique<oxygen::examples::vortex_basic::MainModule>(
      app, shader_debug_mode));
    register_module(
      std::make_unique<oxygen::scenesync::RuntimeMotionProducerModule>());

    switch (app.engine->GetEngineConfig().renderer.implementation) {
      using enum oxygen::RendererImplementation;
    case kVortex: {
      constexpr auto kVortexValidationCapabilities
        = oxygen::vortex::RendererCapabilityFamily::kScenePreparation
        | oxygen::vortex::RendererCapabilityFamily::kGpuUploadAndAssetBinding
        | oxygen::vortex::RendererCapabilityFamily::kLightingData
        | oxygen::vortex::RendererCapabilityFamily::kDeferredShading
        | oxygen::vortex::RendererCapabilityFamily::kEnvironmentLighting
        | oxygen::vortex::RendererCapabilityFamily::kFinalOutputComposition
        | oxygen::vortex::RendererCapabilityFamily::kDiagnosticsAndProfiling;
      register_module(std::make_unique<oxygen::vortex::Renderer>(
        app.gfx_weak, renderer_config, kVortexValidationCapabilities));
      break;
    }

    case kLegacy:
      throw std::runtime_error(
        "VortexBasic requires EngineConfig.renderer.implementation = "
        "RendererImplementation::kVortex");
    }
  }
}

auto AsyncMain(oxygen::examples::DemoAppContext& app, uint32_t frames,
  const oxygen::vortex::ShaderDebugMode shader_debug_mode)
  -> co::Co<int>
{
  OXCO_WITH_NURSERY(n)
  {
    app.running.store(true, std::memory_order_relaxed);

    co_await n.Start(&Platform::ActivateAsync, std::ref(*app.platform));
    app.platform->Run();

    DCHECK_F(!app.gfx_weak.expired());
    auto gfx = app.gfx_weak.lock();
    co_await n.Start(&Graphics::ActivateAsync, std::ref(*gfx));
    gfx->Run();

    co_await n.Start(&AsyncEngine::ActivateAsync, std::ref(*app.engine));
    app.engine->Run();

    RegisterEngineModules(app, shader_debug_mode);

    n.Start([&app, &n]() -> co::Co<> {
      co_await app.platform->Windows().LastWindowClosed();
      LOG_F(INFO, "VortexBasic: last window closed -> shutting down engine");
      app.engine->Stop();
      co_return;
    });

    co_await app.engine->Completed();

    co_return co::kCancel;
  };

  co_return EXIT_SUCCESS;
}
} // namespace

extern "C" auto MainImpl(std::span<const char*> args) -> int
{
  using namespace oxygen::clap; // NOLINT

  uint32_t frames = 0U;
  uint32_t target_fps = 100U;
  bool headless = false;
  bool enable_vsync = true;
  std::string shader_debug_mode_name;
  oxygen::examples::cli::GraphicsToolingCliState graphics_tooling_cli {};
  oxygen::examples::cli::FrameCaptureCliState capture_cli {};
  oxygen::examples::DemoAppContext app {};

  try {
    auto vortex_options = std::make_shared<clap::Options>("Vortex options");
    vortex_options->Add(clap::Option::WithKey("shader-debug-mode")
        .About("Deferred debug visualization mode: disabled, base-color, "
               "world-normals, roughness, metalness, scene-depth-raw, or "
               "scene-depth-linear")
        .Long("shader-debug-mode")
        .WithValue<std::string>()
        .DefaultValue(std::string("disabled"))
        .UserFriendlyName("mode")
        .StoreTo(&shader_debug_mode_name)
        .Build());
    vortex_options->Add(clap::Option::WithKey("with-atmosphere")
        .About("Enable the atmosphere/aerial-perspective view layer")
        .Long("with-atmosphere")
        .WithValue<bool>()
        .DefaultValue(false)
        .UserFriendlyName("enabled")
        .StoreTo(&app.with_atmosphere)
        .Build());
    vortex_options->Add(clap::Option::WithKey("with-height-fog")
        .About("Enable the height-fog view layer")
        .Long("with-height-fog")
        .WithValue<bool>()
        .DefaultValue(false)
        .UserFriendlyName("enabled")
        .StoreTo(&app.with_height_fog)
        .Build());
    vortex_options->Add(clap::Option::WithKey("with-local-fog")
        .About("Enable local fog volume authoring and rendering")
        .Long("with-local-fog")
        .WithValue<bool>()
        .DefaultValue(false)
        .UserFriendlyName("enabled")
        .StoreTo(&app.with_local_fog)
        .Build());

    const Command::Ptr default_command
      = CommandBuilder(Command::DEFAULT)
          .WithOptions(oxygen::examples::cli::MakeRuntimeOptions({
            .frames = &frames,
            .target_fps = &target_fps,
            .headless = &headless,
            .fullscreen = &app.fullscreen,
            .vsync = &enable_vsync,
          }))
          .WithOptions(vortex_options)
          .WithOptions(oxygen::examples::cli::MakeGraphicsToolingOptions(
            graphics_tooling_cli))
          .WithOptions(oxygen::examples::cli::MakeCaptureOptions(capture_cli))
          .WithOptions(
            oxygen::examples::cli::MakeAdvancedCaptureOptions(capture_cli),
            true);

    auto cli = oxygen::examples::cli::BuildCli("vortex-basic",
      "Minimal Vortex deferred renderer exercise", default_command);

    const int argc = static_cast<int>(args.size());
    const char** argv = args.data();
    auto context = cli->Parse(argc, argv);
    if (oxygen::examples::cli::HandleMetaCommand(context, default_command)) {
      return EXIT_SUCCESS;
    }

    oxygen::examples::cli::ValidateGraphicsToolingOptions(graphics_tooling_cli);
    LOG_F(INFO, "Parsed frames option = {}", frames);
    LOG_F(INFO, "Parsed fps option = {}", target_fps);
    LOG_F(INFO, "Parsed fullscreen option = {}", app.fullscreen);
    LOG_F(INFO, "Parsed vsync option = {}", enable_vsync);
    LOG_F(INFO, "Parsed with-atmosphere option = {}", app.with_atmosphere);
    LOG_F(INFO, "Parsed with-height-fog option = {}", app.with_height_fog);
    LOG_F(INFO, "Parsed with-local-fog option = {}", app.with_local_fog);
    const auto shader_debug_mode
      = ParseVortexShaderDebugMode(shader_debug_mode_name);
    LOG_F(INFO, "Parsed Vortex shader debug mode = {}",
      oxygen::vortex::to_string(shader_debug_mode));
    oxygen::examples::cli::LogGraphicsToolingOptions(graphics_tooling_cli);
    oxygen::examples::cli::LogCaptureOptions(capture_cli);
    LOG_F(INFO, "Starting vortex-basic for {} frames (target {} fps)", frames,
      target_fps);

    app.headless = headless;

    app.platform = std::make_shared<Platform>(PlatformConfig {
      .headless = headless,
      .thread_pool_size = (std::min)(4u, std::thread::hardware_concurrency()),
    });

    const auto workspace_root
      = std::filesystem::path(std::source_location::current().file_name())
          .parent_path()
          .parent_path()
          .parent_path();

    const auto path_finder_config
      = PathFinderConfig::Create().WithWorkspaceRoot(workspace_root).Build();
    const auto frame_capture_config
      = oxygen::examples::cli::BuildFrameCaptureConfig(capture_cli, headless);
    const GraphicsConfig gfx_config {
      .enable_debug_layer = graphics_tooling_cli.enable_debug_layer,
      .enable_validation = false,
      .enable_aftermath = graphics_tooling_cli.enable_aftermath,
      .preferred_card_name = std::nullopt,
      .headless = headless,
      .enable_vsync = enable_vsync,
      .frame_capture = frame_capture_config,
      .extra = {},
    };
    const auto& loader = GraphicsBackendLoader::GetInstance();
    app.gfx_weak = loader.LoadBackend(
      headless ? BackendType::kHeadless : BackendType::kDirect3D12, gfx_config,
      path_finder_config);
    CHECK_F(!app.gfx_weak.expired());
    app.gfx_weak.lock()->CreateCommandQueues(app.queue_strategy);

    app.engine = std::make_shared<AsyncEngine>(
      app.platform,
      app.gfx_weak,
      EngineConfig {
        .renderer = {
          .implementation = RendererImplementation::kVortex,
        },
        .application = { .name = "Vortex Basic Example", .version = 1u, },
        .target_fps = target_fps,
        .frame_count = frames,
        .enable_asset_loader = true,
        .path_finder_config = path_finder_config,
        .timing = {
          .pacing_safety_margin = 250us,
        }
      }
    );

    if (context.ovm.HasOption("vsync")) {
      (void)app.engine->GetConsole().SetCVarFromText({
        .name = "gfx.vsync",
        .text = enable_vsync ? "true" : "false",
      });
      if (const auto gfx = app.gfx_weak.lock()) {
        gfx->SetVSyncEnabled(enable_vsync);
      }
    }

    const auto rc = co::Run(app, AsyncMain(app, frames, shader_debug_mode));

    app.platform->Stop();
    app.engine.reset();
    if (!app.gfx_weak.expired()) {
      auto gfx = app.gfx_weak.lock();
      gfx->Stop();
      gfx.reset();
    }
    loader.UnloadBackend();
    app.platform.reset();

    LOG_F(INFO, "exit code: {}", rc);
    loguru::flush();
    loguru::shutdown();
    return rc;
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
