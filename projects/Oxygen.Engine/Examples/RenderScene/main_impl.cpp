//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <atomic>
#include <cctype>
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
#include <Oxygen/Console/Console.h>
#include <Oxygen/Core/EngineModule.h>
#include <Oxygen/Engine/AsyncEngine.h>
#include <Oxygen/Graphics/Common/BackendModule.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Direct3D12/ImGui/ImGuiBackend.h>
#include <Oxygen/Input/InputSystem.h>
#include <Oxygen/Loader/GraphicsBackendLoader.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/EventLoop.h>
#include <Oxygen/OxCo/Nursery.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/PhysicsModule/PhysicsModule.h>
#include <Oxygen/Platform/Platform.h>
#include <Oxygen/Renderer/ImGui/ImGuiModule.h>
#include <Oxygen/Renderer/Renderer.h>
#include <Oxygen/SceneSync/SceneObserverSyncModule.h>
#include <Oxygen/Scripting/Module/ScriptingModule.h>

#include "DemoShell/Runtime/DemoAppContext.h"
#include "DemoShell/Services/SettingsService.h"
#include "RenderScene/MainModule.h"

using namespace oxygen;
using namespace oxygen::engine;
using namespace oxygen::graphics;
using oxygen::examples::SettingsService;
using namespace std::chrono_literals;

namespace {

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

auto RegisterEngineModules(oxygen::examples::DemoAppContext& app) -> void
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
      .shadow_quality_tier = oxygen::ShadowQualityTier::kUltra,
      .directional_shadow_policy = app.directional_shadow_policy,
    };
    auto renderer_unique
      = std::make_unique<engine::Renderer>(app.gfx_weak, renderer_config);

    app.renderer = observer_ptr { renderer_unique.get() };
    register_module(std::make_unique<oxygen::physics::PhysicsModule>(
      oxygen::engine::kPhysicsModulePriority));
    register_module(
      std::make_unique<oxygen::scenesync::SceneObserverSyncModule>(
        engine::kSceneObserverSyncModulePriority));
    register_module(std::make_unique<oxygen::scripting::ScriptingModule>(
      engine::kScriptingModulePriority));
    register_module(
      std::make_unique<oxygen::examples::render_scene::MainModule>(app));

    register_module(std::move(renderer_unique));

    if (!app.headless) {
      auto imgui_backend = std::make_unique<
        oxygen::graphics::d3d12::D3D12ImGuiGraphicsBackend>();
      auto imgui_module = std::make_unique<oxygen::engine::imgui::ImGuiModule>(
        app.platform, std::move(imgui_backend));
      register_module(std::move(imgui_module));
    }
  }
}

auto AsyncMain(oxygen::examples::DemoAppContext& app, uint32_t frames)
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

    RegisterEngineModules(app);

    n.Start([&app, &n]() -> co::Co<> {
      co_await app.platform->Windows().LastWindowClosed();
      LOG_F(INFO,
        "RenderScene example: last window closed -> shutting down engine");

      app.engine->Stop();
      co_return;
    });

    co_await app.engine->Completed();

    co_return co::kCancel;
  };

  co_return EXIT_SUCCESS;
}

} // namespace

namespace {

auto ParseDirectionalShadowPolicy(std::string value)
  -> oxygen::DirectionalShadowImplementationPolicy
{
  std::ranges::transform(value, value.begin(),
    [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });

  if (value == "conventional" || value == "conventional-only") {
    return oxygen::DirectionalShadowImplementationPolicy::kConventionalOnly;
  }

  throw std::runtime_error("Invalid value for --directional-shadows. "
                           "Expected one of: conventional");
}

auto NormalizeCliToken(std::string value) -> std::string
{
  std::ranges::transform(value, value.begin(),
    [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return value;
}

auto ParseFrameCaptureProvider(std::string value)
  -> oxygen::FrameCaptureProvider
{
  value = NormalizeCliToken(std::move(value));

  if (value == "off" || value == "none") {
    return oxygen::FrameCaptureProvider::kNone;
  }
  if (value == "renderdoc") {
    return oxygen::FrameCaptureProvider::kRenderDoc;
  }
  if (value == "pix") {
    return oxygen::FrameCaptureProvider::kPix;
  }

  throw std::runtime_error("Invalid value for --frame-capture-provider. "
                           "Expected one of: off, renderdoc, pix");
}

auto ParseFrameCaptureInitMode(std::string value)
  -> oxygen::FrameCaptureInitMode
{
  value = NormalizeCliToken(std::move(value));

  if (value == "disabled") {
    return oxygen::FrameCaptureInitMode::kDisabled;
  }
  if (value == "attached") {
    return oxygen::FrameCaptureInitMode::kAttachedOnly;
  }
  if (value == "search") {
    return oxygen::FrameCaptureInitMode::kSearchPath;
  }
  if (value == "path") {
    return oxygen::FrameCaptureInitMode::kExplicitPath;
  }

  throw std::runtime_error("Invalid value for --frame-capture-init. "
                           "Expected one of: disabled, attached, search, "
                           "path");
}

auto ParseFrameCaptureStartupTrigger(std::string value)
  -> oxygen::FrameCaptureStartupTrigger
{
  value = NormalizeCliToken(std::move(value));

  if (value == "none") {
    return oxygen::FrameCaptureStartupTrigger::kNone;
  }
  if (value == "next") {
    return oxygen::FrameCaptureStartupTrigger::kNextFrame;
  }

  throw std::runtime_error("Invalid value for --frame-capture-trigger. "
                           "Expected one of: none, next");
}

auto BuildFrameCaptureConfig(const std::string& provider_text,
  const std::string& init_mode_text, const std::string& trigger_text,
  const std::string& module_path, const std::string& capture_file_template)
  -> oxygen::FrameCaptureConfig
{
  const auto provider = ParseFrameCaptureProvider(provider_text);
  if (provider == oxygen::FrameCaptureProvider::kNone) {
    return {};
  }
  if (provider == oxygen::FrameCaptureProvider::kPix) {
    if (!module_path.empty() || !capture_file_template.empty()
      || NormalizeCliToken(trigger_text) != "none") {
      throw std::runtime_error("--frame-capture-module, "
                               "--frame-capture-file, and "
                               "--frame-capture-trigger are currently "
                               "supported only for RenderDoc");
    }
    return oxygen::FrameCaptureConfig { .provider = provider };
  }

  const auto init_mode = ParseFrameCaptureInitMode(init_mode_text);
  if (init_mode == oxygen::FrameCaptureInitMode::kExplicitPath
    && module_path.empty()) {
    throw std::runtime_error(
      "--frame-capture-module is required when --frame-capture-init=path");
  }

  return oxygen::FrameCaptureConfig {
    .provider = provider,
    .init_mode = init_mode,
    .startup_trigger = ParseFrameCaptureStartupTrigger(trigger_text),
    .module_path = module_path,
    .capture_file_template = capture_file_template,
  };
}

} // namespace

extern "C" auto MainImpl(std::span<const char*> args) -> void
{
  using namespace oxygen::clap; // NOLINT

  // Initialize settings service
  SettingsService::ForDemoApp();

  uint32_t frames = 0U;
  uint32_t target_fps = 100U; // desired frame pacing
  bool enable_vsync = true;
  bool verify_hashes = false;
  bool hot_reload = true;
  std::string directional_shadows = "conventional";
  std::string cvars_archive_path;
  std::string frame_capture_provider = "off";
  std::string frame_capture_init = "attached";
  std::string frame_capture_module;
  std::string frame_capture_file;
  std::string frame_capture_trigger = "none";
  oxygen::examples::DemoAppContext app {};
  app.headless = false;

  try {
    CommandBuilder default_command(Command::DEFAULT);
    default_command.WithOption(Option::WithKey("frames")
        .About("Number of frames to simulate")
        .Short("f")
        .Long("frames")
        .WithValue<uint32_t>()
        .UserFriendlyName("count")
        .StoreTo(&frames)
        .Build());
    default_command.WithOption(Option::WithKey("fps")
        .About("Target frames per second for pacing the event loop")
        .Short("r")
        .Long("fps")
        .WithValue<uint32_t>()
        .UserFriendlyName("rate")
        .StoreTo(&target_fps)
        .Build());
    default_command.WithOption(Option::WithKey("fullscreen")
        .About("Run the application in full-screen mode")
        .Short("F")
        .Long("fullscreen")
        .WithValue<bool>()
        .DefaultValue(false)
        .UserFriendlyName("fullscreen")
        .StoreTo(&app.fullscreen)
        .Build());
    default_command.WithOption(Option::WithKey("vsync")
        .About("Enable vertical synchronization (limits FPS to monitor refresh "
               "rate)")
        .Short("s")
        .Long("vsync")
        .WithValue<bool>()
        .DefaultValue(true)
        .UserFriendlyName("vsync")
        .StoreTo(&enable_vsync)
        .Build());
    default_command.WithOption(Option::WithKey("verify-hashes")
        .About("Enable hash-based content integrity verification when mounting "
               "content sources (PAK CRC32, loose cooked SHA-256)")
        .Long("verify-hashes")
        .WithValue<bool>()
        .DefaultValue(false)
        .UserFriendlyName("verify-hashes")
        .StoreTo(&verify_hashes)
        .Build());
    default_command.WithOption(Option::WithKey("hot-reload")
        .About("Enable hot-reloading of script files from disk")
        .Long("hot-reload")
        .WithValue<bool>()
        .DefaultValue(true)
        .UserFriendlyName("hot-reload")
        .StoreTo(&hot_reload)
        .Build());
    default_command.WithOption(Option::WithKey("frame-capture-provider")
        .About("Frame capture provider: off, renderdoc, pix.")
        .Long("frame-capture-provider")
        .WithValue<std::string>()
        .DefaultValue(std::string("off"))
        .UserFriendlyName("provider")
        .StoreTo(&frame_capture_provider)
        .Build());
    default_command.WithOption(Option::WithKey("frame-capture-init")
        .About("Frame capture initialization mode: disabled, attached, "
               "search, path. RenderDoc only.")
        .Long("frame-capture-init")
        .WithValue<std::string>()
        .DefaultValue(std::string("attached"))
        .UserFriendlyName("mode")
        .StoreTo(&frame_capture_init)
        .Build());
    default_command.WithOption(Option::WithKey("frame-capture-module")
        .About("Explicit frame capture module path used when "
               "--frame-capture-init=path. RenderDoc only.")
        .Long("frame-capture-module")
        .WithValue<std::string>()
        .UserFriendlyName("path")
        .StoreTo(&frame_capture_module)
        .Build());
    default_command.WithOption(Option::WithKey("frame-capture-file")
        .About("Optional RenderDoc capture file path template")
        .Long("frame-capture-file")
        .WithValue<std::string>()
        .UserFriendlyName("template")
        .StoreTo(&frame_capture_file)
        .Build());
    default_command.WithOption(Option::WithKey("frame-capture-trigger")
        .About("Frame capture startup trigger: none, next. RenderDoc only.")
        .Long("frame-capture-trigger")
        .WithValue<std::string>()
        .DefaultValue(std::string("none"))
        .UserFriendlyName("trigger")
        .StoreTo(&frame_capture_trigger)
        .Build());
    default_command.WithOption(Option::WithKey("directional-shadows")
        .About("Directional shadow backend policy: conventional.")
        .Long("directional-shadows")
        .WithValue<std::string>()
        .DefaultValue(std::string("conventional"))
        .UserFriendlyName("policy")
        .StoreTo(&directional_shadows)
        .Build());
    default_command.WithOption(Option::WithKey("cvars-archive")
        .About("Override the persisted CVar archive path for this run")
        .Long("cvars-archive")
        .WithValue<std::string>()
        .UserFriendlyName("path")
        .StoreTo(&cvars_archive_path)
        .Build());

    auto cli = CliBuilder()
                 .ProgramName("render-scene")
                 .Version("0.1")
                 .About("Render a cooked SceneAsset from a mounted .pak")
                 .WithHelpCommand()
                 .WithVersionCommand()
                 .WithCommand(default_command)
                 .Build();

    const int argc = static_cast<int>(args.size());
    const char** argv = args.data();
    auto context = cli->Parse(argc, argv);
    if (context.active_command->PathAsString() == Command::HELP
      || context.active_command->PathAsString() == Command::VERSION
      || context.ovm.HasOption(Command::HELP)) {
      return;
    }

    LOG_F(INFO, "Parsed frames option = {}", frames);
    LOG_F(INFO, "Parsed fps option = {}", target_fps);
    LOG_F(INFO, "Parsed fullscreen option = {}", app.fullscreen);
    LOG_F(INFO, "Parsed vsync option = {}", enable_vsync);
    LOG_F(INFO, "Parsed verify-hashes option = {}", verify_hashes);
    LOG_F(INFO, "Parsed frame-capture-provider option = {}",
      frame_capture_provider);
    LOG_F(INFO, "Parsed frame-capture-init option = {}", frame_capture_init);
    LOG_F(
      INFO, "Parsed frame-capture-trigger option = {}", frame_capture_trigger);
    if (!frame_capture_module.empty()) {
      LOG_F(
        INFO, "Parsed frame-capture-module option = {}", frame_capture_module);
    }
    if (!frame_capture_file.empty()) {
      LOG_F(INFO, "Parsed frame-capture-file option = {}", frame_capture_file);
    }
    LOG_F(INFO, "Parsed directional-shadows option = {}", directional_shadows);
    if (!cvars_archive_path.empty()) {
      LOG_F(INFO, "Parsed cvars-archive option = {}", cvars_archive_path);
    }
    LOG_F(INFO, "Starting async engine engine for {} frames (target {} fps)",
      frames, target_fps);
    app.directional_shadow_policy
      = ParseDirectionalShadowPolicy(directional_shadows);

    // Create the platform
    app.platform = std::make_shared<Platform>(PlatformConfig {
      .headless = app.headless,
      .thread_pool_size = (std::min)(4u, std::thread::hardware_concurrency()),
    });

    const auto workspace_root
      = std::filesystem::path(std::source_location::current().file_name())
          .parent_path()
          .parent_path()
          .parent_path();

    const auto demo_root
      = std::filesystem::path(std::source_location::current().file_name())
          .parent_path();

    // Load the graphics backend
    auto path_finder_builder
      = PathFinderConfig::Create()
          .WithWorkspaceRoot(workspace_root)
          .WithScriptSourceRoots({ demo_root.parent_path() / "Content" });
    if (!cvars_archive_path.empty()) {
      path_finder_builder
        = std::move(path_finder_builder)
            .WithCVarsArchivePath(std::filesystem::path(cvars_archive_path));
    }
    const auto path_finder_config = std::move(path_finder_builder).Build();
    const auto frame_capture_config
      = BuildFrameCaptureConfig(frame_capture_provider, frame_capture_init,
        frame_capture_trigger, frame_capture_module, frame_capture_file);
    const GraphicsConfig gfx_config {
      .enable_debug = true,
      .enable_validation = false,
      .enable_aftermath = true,
      .preferred_card_name = std::nullopt,
      .headless = app.headless,
      .enable_vsync = enable_vsync,
      .frame_capture = frame_capture_config,
      .extra = {},
    };
    const auto& loader = GraphicsBackendLoader::GetInstance();
    app.gfx_weak = loader.LoadBackend(
      app.headless ? BackendType::kHeadless : BackendType::kDirect3D12,
      gfx_config, path_finder_config);
    CHECK_F(
      !app.gfx_weak.expired()); // Expect a valid graphics backend, or abort
    app.gfx_weak.lock()->CreateCommandQueues(app.queue_strategy);

    app.engine = std::make_shared<AsyncEngine>(
      app.platform,
      app.gfx_weak,
      EngineConfig {
        .application = { .name = "RenderScene Example", .version = 1u, },
        .target_fps = target_fps,
        .frame_count = frames,
        .enable_asset_loader = true,
        .asset_loader = { .verify_content_hashes = verify_hashes, },
        .scripting = {
          .enable_hot_reload = hot_reload,
        },
        .path_finder_config = path_finder_config,
        .graphics = { .enable_vsync = enable_vsync, },
        .timing = {
          .pacing_safety_margin = 250us,
        }
      }
    );

    if (context.ovm.HasOption("fps")) {
      (void)app.engine->GetConsole().SetCVarFromText({
        .name = "ngin.target_fps",
        .text = std::to_string(target_fps),
      });
    }
    if (context.ovm.HasOption("hot-reload")) {
      (void)app.engine->GetConsole().SetCVarFromText({
        .name = "ngin.scripting.hot_reload",
        .text = hot_reload ? "true" : "false",
      });
    }
    if (context.ovm.HasOption("vsync")) {
      (void)app.engine->GetConsole().SetCVarFromText({
        .name = "gfx.vsync",
        .text = enable_vsync ? "true" : "false",
      });
      if (const auto gfx = app.gfx_weak.lock()) {
        gfx->SetVSyncEnabled(enable_vsync);
      }
    }

    const auto rc = co::Run(app, AsyncMain(app, frames));

    app.engine->Stop();
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
  } catch (const CmdLineArgumentsError& e) {
    LOG_F(ERROR, "CLI parse error: {}", e.what());
  } catch (const std::exception& e) {
    LOG_F(ERROR, "Unhandled exception: {}", e.what());
  }
}
