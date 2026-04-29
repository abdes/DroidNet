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
#include <string_view>
#include <thread>

#include <asio/signal_set.hpp>
#include <glm/vec3.hpp>

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
#include <Oxygen/SceneSync/SceneObserverSyncModule.h>
#include <Oxygen/Scripting/Module/ScriptingModule.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/RendererCapability.h>

#include "Common/DemoCli.h"
#include "Common/FrameCaptureCliOptions.h"
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
      .directional_shadow_policy = app.directional_shadow_policy,
      .enable_imgui = !app.headless,
    };
    constexpr auto kRenderSceneVortexCapabilities
      = oxygen::vortex::RendererCapabilityFamily::kScenePreparation
      | oxygen::vortex::RendererCapabilityFamily::kGpuUploadAndAssetBinding
      | oxygen::vortex::RendererCapabilityFamily::kLightingData
      | oxygen::vortex::RendererCapabilityFamily::kShadowing
      | oxygen::vortex::RendererCapabilityFamily::kEnvironmentLighting
      | oxygen::vortex::RendererCapabilityFamily::kFinalOutputComposition
      | oxygen::vortex::RendererCapabilityFamily::kDiagnosticsAndProfiling
      | oxygen::vortex::RendererCapabilityFamily::kDeferredShading;
    register_module(std::make_unique<oxygen::physics::PhysicsModule>(
      oxygen::engine::kPhysicsModulePriority));
    register_module(
      std::make_unique<oxygen::scenesync::SceneObserverSyncModule>(
        engine::kSceneObserverSyncModulePriority));
    register_module(std::make_unique<oxygen::scripting::ScriptingModule>(
      engine::kScriptingModulePriority));
    register_module(
      std::make_unique<oxygen::examples::render_scene::MainModule>(app));

    register_module(std::make_unique<oxygen::vortex::Renderer>(
      app.gfx_weak, renderer_config, kRenderSceneVortexCapabilities));
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

auto NormalizeCliToken(std::string value) -> std::string
{
  std::ranges::transform(value, value.begin(),
    [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return value;
}

auto ParseDirectionalShadowPolicy(std::string value)
  -> oxygen::DirectionalShadowImplementationPolicy
{
  value = NormalizeCliToken(std::move(value));

  if (value == "conventional" || value == "conventional-only") {
    return oxygen::DirectionalShadowImplementationPolicy::kConventionalOnly;
  }
  if (value == "vsm" || value == "virtual-shadow-map") {
    return oxygen::DirectionalShadowImplementationPolicy::kVirtualShadowMap;
  }

  throw std::runtime_error("Invalid value for --directional-shadows. "
                           "Expected one of: conventional, vsm");
}

} // namespace

extern "C" auto MainImpl(std::span<const char*> args) -> int
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
  std::string startup_scene_name;
  std::string startup_skybox_path;
  std::string cvars_archive_path;
  oxygen::examples::cli::GraphicsToolingCliState graphics_tooling_cli {};
  oxygen::examples::cli::FrameCaptureCliState capture_cli {};
  oxygen::examples::DemoAppContext app {};
  app.headless = false;

  try {
    const auto developer_options
      = std::make_shared<Options>("Developer options");
    developer_options->Add(Option::WithKey("verify-hashes")
        .About("Enable content hash verification for mounted sources")
        .Long("verify-hashes")
        .WithValue<bool>()
        .DefaultValue(false)
        .UserFriendlyName("verify-hashes")
        .StoreTo(&verify_hashes)
        .Build());
    developer_options->Add(Option::WithKey("hot-reload")
        .About("Enable hot-reloading of script files from disk")
        .Long("hot-reload")
        .WithValue<bool>()
        .DefaultValue(true)
        .UserFriendlyName("hot-reload")
        .StoreTo(&hot_reload)
        .Build());
    developer_options->Add(Option::WithKey("cvars-archive")
        .About("Override the persisted CVar archive path for this run")
        .Long("cvars-archive")
        .WithValue<std::string>()
        .UserFriendlyName("path")
        .StoreTo(&cvars_archive_path)
        .Build());
    developer_options->Add(Option::WithKey("directional-shadows")
        .About("Directional shadow backend policy")
        .Long("directional-shadows")
        .WithValue<std::string>()
        .DefaultValue(std::string("conventional"))
        .UserFriendlyName("policy")
        .StoreTo(&directional_shadows)
        .Build());
    developer_options->Add(Option::WithKey("scene")
        .About("Load a cooked scene by name, virtual path, stem, or key at startup")
        .Long("scene")
        .WithValue<std::string>()
        .UserFriendlyName("scene")
        .StoreTo(&startup_scene_name)
        .Build());
    developer_options->Add(Option::WithKey("startup-skybox")
        .About("Load and equip a cubemap skybox on the startup scene")
        .Long("startup-skybox")
        .WithValue<std::string>()
        .UserFriendlyName("path")
        .StoreTo(&startup_skybox_path)
        .Build());

    const Command::Ptr default_command
      = CommandBuilder(Command::DEFAULT)
          .WithOptions(oxygen::examples::cli::MakeRuntimeOptions({
            .frames = &frames,
            .target_fps = &target_fps,
            .fullscreen = &app.fullscreen,
            .vsync = &enable_vsync,
          }))
          .WithOptions(oxygen::examples::cli::MakeGraphicsToolingOptions(
            graphics_tooling_cli))
          .WithOptions(oxygen::examples::cli::MakeCaptureOptions(capture_cli))
          .WithOptions(
            oxygen::examples::cli::MakeAdvancedCaptureOptions(capture_cli),
            true)
          .WithOptions(developer_options, true);

    auto cli = oxygen::examples::cli::BuildCli("render-scene",
      "Render a cooked scene from mounted content", default_command);

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
    oxygen::examples::cli::LogGraphicsToolingOptions(graphics_tooling_cli);
    LOG_F(INFO, "Parsed verify-hashes option = {}", verify_hashes);
    oxygen::examples::cli::LogCaptureOptions(capture_cli);
    LOG_F(INFO, "Parsed directional-shadows option = {}", directional_shadows);
    if (!startup_scene_name.empty()) {
      LOG_F(INFO, "Parsed scene option = {}", startup_scene_name);
    }
    if (!startup_skybox_path.empty()) {
      LOG_F(INFO, "Parsed startup-skybox option = {}", startup_skybox_path);
    }
    const bool explicit_startup_skybox = !startup_skybox_path.empty();
    if (!cvars_archive_path.empty()) {
      LOG_F(INFO, "Parsed cvars-archive option = {}", cvars_archive_path);
    }
    LOG_F(INFO, "Starting render-scene demo for {} frames (target {} fps)",
      frames, target_fps);
    app.directional_shadow_policy
      = ParseDirectionalShadowPolicy(directional_shadows);
    app.startup_scene_name = startup_scene_name;
    if (const auto settings = SettingsService::ForDemoApp()) {
      const auto read_int = [&](const std::string_view key,
                              const int fallback) -> int {
        return static_cast<int>(settings->GetFloat(key).value_or(
          static_cast<float>(fallback)));
      };
      const auto read_vec3
        = [&](const std::string_view prefix,
            const glm::vec3 fallback) -> glm::vec3 {
        return glm::vec3 {
          settings->GetFloat(std::string(prefix) + ".x").value_or(fallback.x),
          settings->GetFloat(std::string(prefix) + ".y").value_or(fallback.y),
          settings->GetFloat(std::string(prefix) + ".z").value_or(fallback.z),
        };
      };
      constexpr int kSkySphereSourceCubemap = 0;
      constexpr int kSkyLightSourceSpecifiedCubemap = 1;
      const bool sky_sphere_enabled
        = settings->GetBool("env.sky_sphere.enabled").value_or(false);
      const int sky_sphere_source
        = read_int("env.sky_sphere.source", kSkySphereSourceCubemap);
      const bool sky_light_enabled
        = settings->GetBool("env.sky_light.enabled").value_or(false);
      const int sky_light_source
        = read_int("env.sky_light.source", kSkyLightSourceSpecifiedCubemap);
      const bool sky_sphere_requests_cubemap = sky_sphere_enabled
        && sky_sphere_source == kSkySphereSourceCubemap;
      const bool sky_light_requests_cubemap = sky_light_enabled
        && sky_light_source == kSkyLightSourceSpecifiedCubemap;
      if (!explicit_startup_skybox) {
        app.startup_skybox_enable_sky_sphere = sky_sphere_requests_cubemap;
        app.startup_skybox_enable_sky_light = sky_light_requests_cubemap;
      }
      if (startup_skybox_path.empty()) {
        if (sky_sphere_requests_cubemap || sky_light_requests_cubemap) {
          startup_skybox_path
            = settings->GetString("env.skybox.path").value_or(std::string {});
        }
      }
      app.startup_skybox_layout = read_int("env.skybox.layout", 0);
      app.startup_skybox_output_format = read_int("env.skybox.output", 0);
      app.startup_skybox_face_size = read_int("env.skybox.face_size", 512);
      app.startup_skybox_flip_y
        = settings->GetBool("env.skybox.flip_y").value_or(false);
      app.startup_skybox_tonemap_hdr_to_ldr
        = settings->GetBool("env.skybox.tonemap_hdr_to_ldr").value_or(false);
      app.startup_skybox_hdr_exposure_ev
        = settings->GetFloat("env.skybox.hdr_exposure_ev").value_or(0.0F);
      app.startup_sky_sphere_intensity
        = settings->GetFloat("env.sky_sphere.intensity").value_or(1.0F);
      app.startup_sky_light_intensity_mul
        = settings->GetFloat("env.sky_light.intensity_mul").value_or(1.0F);
      app.startup_sky_light_diffuse
        = settings->GetFloat("env.sky_light.diffuse").value_or(1.0F);
      app.startup_sky_light_specular
        = settings->GetFloat("env.sky_light.specular").value_or(1.0F);
      app.startup_sky_light_real_time_capture_enabled
        = settings->GetBool("env.sky_light.real_time_capture_enabled")
            .value_or(false);
      app.startup_sky_light_tint
        = read_vec3("env.sky_light.tint", glm::vec3 { 1.0F });
      app.startup_sky_light_lifecycle_proof_enabled
        = settings->GetBool("env.sky_light.lifecycle_proof.enabled")
            .value_or(false);
      app.startup_sky_light_lifecycle_disable_frame
        = static_cast<std::uint32_t>(std::max(
          settings->GetFloat("env.sky_light.lifecycle_proof.disable_frame")
            .value_or(0.0F),
          0.0F));
      app.startup_sky_light_lifecycle_enable_frame
        = static_cast<std::uint32_t>(std::max(
          settings->GetFloat("env.sky_light.lifecycle_proof.enable_frame")
            .value_or(0.0F),
          0.0F));
    }
    app.startup_skybox_path = startup_skybox_path;
    if (!app.startup_skybox_path.empty()) {
      LOG_F(INFO,
        "Resolved startup skybox path='{}' layout={} output={} face_size={} "
        "flip_y={} tonemap_hdr_to_ldr={} exposure_ev={} sky_intensity={} "
        "enable_sky_sphere={} enable_sky_light={}",
        app.startup_skybox_path, app.startup_skybox_layout,
        app.startup_skybox_output_format, app.startup_skybox_face_size,
        app.startup_skybox_flip_y, app.startup_skybox_tonemap_hdr_to_ldr,
        app.startup_skybox_hdr_exposure_ev,
        app.startup_sky_sphere_intensity, app.startup_skybox_enable_sky_sphere,
        app.startup_skybox_enable_sky_light);
    }
    LOG_F(INFO, "Resolved directional shadow policy = {}",
      app.directional_shadow_policy);

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
      = oxygen::examples::cli::BuildFrameCaptureConfig(capture_cli);
    const GraphicsConfig gfx_config {
      .enable_debug_layer = graphics_tooling_cli.enable_debug_layer,
      .enable_validation = false,
      .enable_aftermath = graphics_tooling_cli.enable_aftermath,
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

    oxygen::console::ConsoleStartupPlan startup_cvars {};
    oxygen::examples::cli::SeedCommonRuntimeStartupCVars(
      context, target_fps, enable_vsync, startup_cvars);
    if (context.ovm.HasOption("hot-reload")) {
      startup_cvars.Set("ngin.scripting.hot_reload", hot_reload);
    }
    if (context.ovm.HasOption("verify-hashes")) {
      startup_cvars.Set("cntt.verify_content_hashes", verify_hashes);
    }
    if (!startup_scene_name.empty()) {
      startup_cvars.Set("vtx.local_fog.enable", true);
      startup_cvars.Set("vtx.local_fog.render_into_volumetric_fog", true);
      startup_cvars.Set("vtx.volumetric_fog.directional_shadows", true);
      startup_cvars.Set("vtx.volumetric_fog.temporal_reprojection", true);
    }

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
      },
      startup_cvars
    );

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
    return rc;
  } catch (const oxygen::examples::cli::FrameCaptureCliError& e) {
    LOG_F(ERROR, "CLI parse error: {}", e.what());
    return EXIT_FAILURE;
  } catch (const CmdLineArgumentsError& e) {
    LOG_F(ERROR, "CLI parse error: {}", e.what());
    return EXIT_FAILURE;
  } catch (const std::exception& e) {
    LOG_F(ERROR, "Unhandled exception: {}", e.what());
    return EXIT_FAILURE;
  }
}
