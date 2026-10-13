//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Windows.h> // IWYU pragma: keep

#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <memory>
#include <source_location>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <libloaderapi.h>
#include <minwindef.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Clap/Cli.h>
#include <Oxygen/Clap/Command.h>
#include <Oxygen/Clap/Fluent/CommandBuilder.h>
#include <Oxygen/Clap/Fluent/DSL.h>
#include <Oxygen/Clap/Fluent/OptionValueBuilder.h>
#include <Oxygen/Clap/Option.h>
#include <Oxygen/Config/EngineConfig.h>
#include <Oxygen/Config/PathFinderConfig.h>
#include <Oxygen/Config/RendererConfig.h>
#include <Oxygen/Control/ControlServer.h>
#include <Oxygen/Core/EngineModule.h>
#include <Oxygen/Engine/AsyncEngine.h>
#include <Oxygen/Graphics/Common/Types/QueueRole.h>
#include <Oxygen/Host/HostConfig.h>
#include <Oxygen/Host/RunHost.h>
#include <Oxygen/Host/RuntimeCli.h>
#include <Oxygen/Player/LaunchFile.h>
#include <Oxygen/Player/PlayerModule.h>
#include <Oxygen/SceneSync/SceneObserverSyncModule.h>
#include <Oxygen/Scripting/Module/ScriptingModule.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/RendererCapability.h>

namespace host = oxygen::host;
namespace player = oxygen::player;
namespace vortex = oxygen::vortex;

using oxygen::AsyncEngine;
using oxygen::PathFinderConfig;
using oxygen::RendererConfig;
using oxygen::RendererImplementation;
using oxygen::clap::CmdLineArgumentsError;
using oxygen::clap::Command;
using oxygen::clap::CommandBuilder;
using oxygen::clap::Option;
using oxygen::clap::Options;
using oxygen::graphics::QueueRole;

namespace {

//! Engine settings the command line adds to the launch file.
struct EngineCli {
  //! `NAME=VALUE` console variable assignments, applied at startup.
  std::vector<std::string> cvars;
  bool verify_hashes { false };
};

auto MakeEngineOptions(EngineCli& engine) -> std::shared_ptr<Options>
{
  auto options = std::make_shared<Options>("Engine options");
  options->Add(Option::WithKey("cvar")
      .About("Set a console variable at startup, e.g. "
             "--cvar vtx.occlusion.enable=true; repeatable")
      .Short("c")
      .Long("cvar")
      .WithValue<std::string>()
      .UserFriendlyName("name=value")
      .Repeatable()
      .CallOnEachValue([&engine](const std::string& assignment) -> void {
        engine.cvars.push_back(assignment);
      })
      .Build());
  options->Add(Option::WithKey("verify-hashes")
      .About("Verify cooked content hashes when mounting")
      .Long("verify-hashes")
      .WithValue<bool>()
      .DefaultValue(false)
      .ImplicitValue(true)
      .UserFriendlyName("enabled")
      .StoreTo(&engine.verify_hashes)
      .Build());
  return options;
}

//! Adds the `--cvar` assignments to the startup plan.
/*!
 \throws host::RuntimeCliError for an assignment without `=` or a name.
*/
auto ApplyEngineCli(const EngineCli& engine, host::HostConfig& config) -> void
{
  config.engine.asset_loader.verify_content_hashes = engine.verify_hashes;
  for (const auto& assignment : engine.cvars) {
    const auto separator = assignment.find('=');
    if (separator == std::string::npos || separator == 0U) {
      throw host::RuntimeCliError(
        "--cvar expects name=value, got '" + assignment + "'");
    }
    config.startup_cvars.Set(assignment.substr(0U, separator),
      std::string_view(assignment).substr(separator + 1U));
  }
}

auto CVarNames(const EngineCli& engine) -> std::vector<std::string>
{
  auto names = std::vector<std::string> {};
  for (const auto& assignment : engine.cvars) {
    names.push_back(assignment.substr(0U, assignment.find('=')));
  }
  return names;
}

//! Where the engine finds its shaders and keeps its script bytecode cache.
struct PlayerPaths {
  std::filesystem::path root;
  std::filesystem::path shader_library;
};

auto ExecutablePath() -> std::filesystem::path
{
  auto buffer = std::wstring(MAX_PATH, L'\0');
  for (;;) {
    const auto length = GetModuleFileNameW(
      nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0U) {
      throw std::runtime_error("cannot locate the player executable");
    }
    if (length < buffer.size()) {
      buffer.resize(length);
      return buffer;
    }
    buffer.resize(buffer.size() * 2U);
  }
}

//! An installed SDK has `bin/Oxygen.Player.exe` and the shader library under
//! `share/oxygen`; a build tree uses the engine source root.
auto ResolvePlayerPaths() -> PlayerPaths
{
  const auto install_root = ExecutablePath().parent_path().parent_path();
  const auto installed_shaders
    = install_root / "share/oxygen/shaders/shaders.bin";
  if (std::filesystem::is_regular_file(installed_shaders)) {
    return { .root = install_root, .shader_library = installed_shaders };
  }
  const auto source_root
    = std::filesystem::path(std::source_location::current().file_name())
        .parent_path()
        .parent_path()
        .parent_path()
        .parent_path();
  return {
    .root = source_root,
    .shader_library
    = PathFinderConfig::DefaultShaderLibraryPathForCurrentBuild(),
  };
}

auto MakeHostConfig(const player::PlayerLaunch& launch,
  const oxygen::clap::CommandLineContext& context,
  host::RuntimeCliState runtime, const EngineCli& engine) -> host::HostConfig
{
  // Options passed on the command line override the launch file.
  const auto fps_passed = host::WasPassed(context, "fps");
  const auto vsync_passed = host::WasPassed(context, "vsync");
  if (!vsync_passed) {
    runtime.vsync = launch.window.vsync;
  }
  if (!fps_passed) {
    runtime.target_fps = launch.window.frame_cap;
  }

  const auto paths = ResolvePlayerPaths();
  auto config = host::HostConfig {};
  config.engine.renderer.implementation = RendererImplementation::kVortex;
  config.engine.application = { .name = "Oxygen Player", .version = 1U };
  config.engine.enable_asset_loader = true;
  // The launch file is the whole input: nothing persists between launches.
  config.engine.load_console_state = false;
  config.engine.save_console_state = false;
  config.engine.scripting.enable_hot_reload
    = !launch.script_source_roots.empty();
  config.engine.path_finder_config
    = PathFinderConfig::Create()
        .WithWorkspaceRoot(paths.root)
        .WithShaderLibraryPath(paths.shader_library)
        .WithScriptSourceRoots(launch.script_source_roots)
        .Build();
  // `--control` overrides the launch file's port.
  if (launch.control.has_value()) {
    config.control = oxygen::control::ControlServerConfig {
      .port = launch.control->port,
      .application = config.engine.application.name,
      .exit_when_owner_leaves = launch.control->exit_when_owner_leaves,
    };
  }
  host::ApplyRuntimeCli(context, runtime, config);
  config.startup_cvars.Set(
    "ngin.target_fps", static_cast<std::int64_t>(runtime.target_fps));
  config.startup_cvars.Set("gfx.vsync", runtime.vsync);
  // Explicit assignments come last, so they win.
  ApplyEngineCli(engine, config);
  return config;
}

auto RegisterModules(AsyncEngine& engine, host::HostContext& context,
  player::PlayerLaunch launch, std::vector<std::string> cvar_names) -> void
{
  host::RegisterRequiredModule(engine,
    std::make_unique<oxygen::scenesync::SceneObserverSyncModule>(
      oxygen::engine::kSceneObserverSyncModulePriority));
  host::RegisterRequiredModule(engine,
    std::make_unique<oxygen::scripting::ScriptingModule>(
      oxygen::engine::kScriptingModulePriority));
  host::RegisterRequiredModule(engine,
    std::make_unique<player::PlayerModule>(
      context, std::move(launch), std::move(cvar_names)));

  constexpr auto kCapabilities
    = vortex::RendererCapabilityFamily::kScenePreparation
    | vortex::RendererCapabilityFamily::kGpuUploadAndAssetBinding
    | vortex::RendererCapabilityFamily::kLightingData
    | vortex::RendererCapabilityFamily::kShadowing
    | vortex::RendererCapabilityFamily::kEnvironmentLighting
    | vortex::RendererCapabilityFamily::kFinalOutputComposition
    | vortex::RendererCapabilityFamily::kDiagnosticsAndProfiling
    | vortex::RendererCapabilityFamily::kDeferredShading;
  const auto renderer_config = RendererConfig {
    .upload_queue_key
    = context.queue_strategy.KeyFor(QueueRole::kTransfer).get(),
  };
  host::RegisterRequiredModule(engine,
    std::make_unique<vortex::Renderer>(
      context.gfx_weak, renderer_config, kCapabilities));
}

auto Run(const oxygen::clap::CommandLineContext& context,
  const host::RuntimeCliState& runtime, const EngineCli& engine_cli,
  const std::string& launch_path) -> int
{
  auto launch = player::ReadLaunchFile(launch_path);
  if (!runtime.resolution.empty()) {
    // The command line's window size overrides the launch file's.
    const auto size = host::ParseWindowResolution(runtime.resolution);
    launch.window.width = size.width;
    launch.window.height = size.height;
  }
  auto config = MakeHostConfig(launch, context, runtime, engine_cli);
  return host::RunHost(std::move(config),
    [&](AsyncEngine& engine, host::HostContext& host_context) -> void {
      RegisterModules(
        engine, host_context, std::move(launch), CVarNames(engine_cli));
    });
}

} // namespace

extern "C" auto MainImpl(std::span<const char*> args) -> int
{
  auto runtime = host::RuntimeCliState {};
  auto engine = EngineCli {};
  auto launch_path = std::string {};
  auto exit_code = EXIT_FAILURE;
  try {
    auto player_options = std::make_shared<Options>("Player options");
    player_options->Add(Option::WithKey("launch")
        .About("Launch file (oxygen.player-launch.schema.json)")
        .Short("l")
        .Long("launch")
        .WithValue<std::string>()
        .UserFriendlyName("file")
        .StoreTo(&launch_path)
        .Build());

    const Command::Ptr default_command
      = CommandBuilder(Command::DEFAULT)
          .WithOptions(player_options)
          .WithOptions(host::MakeRuntimeOptions(runtime,
            host::RuntimeOptionSet {
              .frames = true,
              .fps = true,
              .headless = false,
              .fullscreen = true,
              .vsync = true,
              .resolution = true,
            }))
          .WithOptions(MakeEngineOptions(engine))
          .WithOptions(host::MakeGraphicsToolingOptions(runtime))
          .WithOptions(host::MakeCaptureOptions(runtime))
          .WithOptions(host::MakeAdvancedCaptureOptions(runtime), true);
    const auto cli = host::BuildCli(
      "Oxygen.Player", "Plays one cooked Oxygen scene", default_command);
    const auto context = cli->Parse(static_cast<int>(args.size()), args.data());
    if (host::HandleMetaCommand(context, default_command)) {
      exit_code = EXIT_SUCCESS;
    } else if (launch_path.empty()) {
      LOG_F(ERROR, "CLI error: --launch <file> is required");
    } else {
      exit_code = Run(context, runtime, engine, launch_path);
    }
  } catch (const player::LaunchFileError& e) {
    LOG_F(ERROR, "{}", e.what());
  } catch (const host::RuntimeCliError& e) {
    LOG_F(ERROR, "CLI error: {}", e.what());
  } catch (const CmdLineArgumentsError& e) {
    LOG_F(ERROR, "CLI parse error: {}", e.what());
  } catch (const std::exception& e) {
    LOG_F(ERROR, "Unhandled exception: {}", e.what());
  }
  loguru::flush();
  loguru::shutdown();
  return exit_code;
}
