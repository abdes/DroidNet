//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>

#include "DemoShell/Runtime/DemoMain.h"
#include "DemoShell/Services/SettingsService.h"
#include "DemoShell/UI/EnvironmentVm.h"
#include "RenderScene/MainModule.h"
#include "RenderScene/RuntimePaths.h"

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Clap/CommandLineContext.h>
#include <Oxygen/Clap/Fluent/DSL.h>
#include <Oxygen/Clap/Fluent/OptionValueBuilder.h>
#include <Oxygen/Clap/Option.h>
#include <Oxygen/Config/PathFinderConfig.h>
#include <Oxygen/Config/RendererConfig.h>
#include <Oxygen/Core/EngineModule.h>
#include <Oxygen/Engine/AsyncEngine.h>
#include <Oxygen/Host/HostConfig.h>
#include <Oxygen/Host/RunHost.h>
#include <Oxygen/Host/RuntimeCli.h>
#include <Oxygen/PhysicsModule/PhysicsModule.h>
#include <Oxygen/SceneSync/SceneObserverSyncModule.h>
#include <Oxygen/Scripting/Module/ScriptingModule.h>

#ifdef OXYGEN_BUILD_UI_TESTS
#  include "DemoShell/test/UiTestSession.h"
#endif

namespace examples = oxygen::examples;
namespace host = oxygen::host;
namespace render_scene = oxygen::examples::render_scene;

namespace {

auto ParseDirectionalShadowPolicy(std::string value)
  -> oxygen::DirectionalShadowImplementationPolicy
{
  std::ranges::transform(
    value, value.begin(), [](const unsigned char c) -> char {
      return static_cast<char>(std::tolower(c));
    });
  if (value == "conventional" || value == "conventional-only") {
    return oxygen::DirectionalShadowImplementationPolicy::kConventionalOnly;
  }
  if (value == "vsm" || value == "virtual-shadow-map") {
    return oxygen::DirectionalShadowImplementationPolicy::kVirtualShadowMap;
  }
  throw std::invalid_argument("Invalid value for --directional-shadows. "
                              "Expected one of: conventional, vsm");
}

//! Developer options; listed by `help-advanced`.
struct DeveloperOptions {
  bool verify_hashes { false };
  bool hot_reload { true };
  std::string cvars_archive;
  std::string directional_shadows { "conventional" };
};

auto MakeEnvironmentOptions(render_scene::StartupOptions& startup,
  std::string& environment_profile) -> oxygen::clap::Options::Ptr
{
  using oxygen::clap::Option;
  auto options = std::make_shared<oxygen::clap::Options>("Environment");
  options->Add(Option::WithKey("preview-sun")
      .About("Opt in to preview sunlight: reuse a directional light or "
             "create one when no scene sun exists")
      .Long("preview-sun")
      .WithValue<bool>()
      .DefaultValue(startup.preview_sun_enabled)
      .UserFriendlyName("enabled")
      .StoreTo(&startup.preview_sun_enabled)
      .Build());
  options->Add(Option::WithKey("environment-profile")
      .About("Startup profile: scene, custom, outdoor-sunny, outdoor-cloudy, "
             "foggy-daylight, outdoor-dawn, outdoor-dusk")
      .Long("environment-profile")
      .WithValue<std::string>()
      .StoreTo(&environment_profile)
      .Build());
  return options;
}

auto MakeDeveloperOptions(render_scene::StartupOptions& startup,
  DeveloperOptions& developer) -> oxygen::clap::Options::Ptr
{
  using oxygen::clap::Option;
  auto options = std::make_shared<oxygen::clap::Options>("Developer options");
  options->Add(Option::WithKey("verify-hashes")
      .About("Enable content hash verification for mounted sources")
      .Long("verify-hashes")
      .WithValue<bool>()
      .DefaultValue(false)
      .UserFriendlyName("verify-hashes")
      .StoreTo(&developer.verify_hashes)
      .Build());
  options->Add(Option::WithKey("hot-reload")
      .About("Enable hot-reloading of script files from disk")
      .Long("hot-reload")
      .WithValue<bool>()
      .DefaultValue(true)
      .UserFriendlyName("hot-reload")
      .StoreTo(&developer.hot_reload)
      .Build());
  options->Add(Option::WithKey("cvars-archive")
      .About("Override the persisted CVar archive path for this run")
      .Long("cvars-archive")
      .WithValue<std::string>()
      .UserFriendlyName("path")
      .StoreTo(&developer.cvars_archive)
      .Build());
  options->Add(Option::WithKey("directional-shadows")
      .About("Directional shadow backend policy")
      .Long("directional-shadows")
      .WithValue<std::string>()
      .DefaultValue(std::string("conventional"))
      .UserFriendlyName("policy")
      .StoreTo(&developer.directional_shadows)
      .Build());
  options->Add(Option::WithKey("scene")
      .About(
        "Load a cooked scene by name, virtual path, stem, or key at startup")
      .Long("scene")
      .WithValue<std::string>()
      .UserFriendlyName("scene")
      .StoreTo(&startup.scene_name)
      .Build());
  options->Add(Option::WithKey("startup-skybox")
      .About("Load and equip a cubemap skybox on the startup scene")
      .Long("startup-skybox")
      .WithValue<std::string>()
      .UserFriendlyName("path")
      .StoreTo(&startup.skybox_path)
      .Build());
  return options;
}

auto ReadStartupOptions(const examples::SettingsService& settings)
  -> render_scene::StartupOptions
{
  const auto frame = [&settings](const char* key) -> std::uint32_t {
    return static_cast<std::uint32_t>(
      std::max(settings.GetFloat(key).value_or(0.0F), 0.0F));
  };
  return render_scene::StartupOptions {
    .preview_sun_enabled
    = settings.GetBool("render_scene.preview_sun.enabled").value_or(false),
    .sky_light_lifecycle_proof_enabled
    = settings.GetBool("env.sky_light.lifecycle_proof.enabled").value_or(false),
    .sky_light_lifecycle_disable_frame
    = frame("env.sky_light.lifecycle_proof.disable_frame"),
    .sky_light_lifecycle_enable_frame
    = frame("env.sky_light.lifecycle_proof.enable_frame"),
  };
}

} // namespace

extern "C" auto MainImpl(std::span<const char*> args) -> int
{
  const auto runtime_paths = render_scene::ResolveRuntimePaths();
#ifdef OXYGEN_BUILD_UI_TESTS
  const bool isolated_settings
    = examples::testing::UiTestSession::InitializeSettings();
#else
  constexpr bool isolated_settings = false;
#endif
  if (runtime_paths.installed && !isolated_settings) {
    examples::SettingsService::InitializeForDemoApp(
      runtime_paths.showcase / "demo_settings.json", runtime_paths.root);
  }
  const auto settings = examples::SettingsService::ForDemoApp();

  auto startup = ReadStartupOptions(*settings);
  std::string environment_profile;
  auto developer = DeveloperOptions {};
  auto renderer_config = oxygen::RendererConfig {};

  return examples::RunDemo(args,
    {
      .program = "render-scene",
      .about = "Render a cooked scene from mounted content",
      .application = "RenderScene Example",
      .runtime_options = { .headless = false },
      .options = {
        { MakeEnvironmentOptions(startup, environment_profile), false },
        { MakeDeveloperOptions(startup, developer), true },
      },
      .configure =
        [&](const oxygen::clap::CommandLineContext& context,
          host::HostConfig& config) -> void {
        // The installed showcase opens Lantern under its sky unless asked
        // otherwise.
        if (runtime_paths.installed && startup.scene_name.empty()
          && settings->GetString("content.active_scene.key")
               .value_or("")
               .empty()) {
          startup.scene_name = "Lantern";
        }
        if (runtime_paths.installed && startup.scene_name == "Lantern"
          && startup.skybox_path.empty() && environment_profile.empty()) {
          startup.skybox_path
            = (runtime_paths.content / "images/showcase/Sky.hdr").string();
          startup.preview_sun_enabled = true;
        }
        if (!environment_profile.empty()) {
          const auto index
            = examples::ui::EnvironmentVm::FindPresetIndex(environment_profile);
          if (!index) {
            throw std::invalid_argument("Unknown environment profile '"
              + environment_profile
              + "'. Use scene, custom, outdoor-sunny, outdoor-cloudy, "
                "foggy-daylight, outdoor-dawn, or outdoor-dusk.");
          }
          startup.environment_profile = *index - 2;
        }
        if (!startup.skybox_path.empty()) {
          if (startup.environment_profile
            && *startup.environment_profile != -1) {
            throw std::invalid_argument(
              "--startup-skybox requires --environment-profile custom.");
          }
          startup.environment_profile = -1;
        }
        renderer_config.directional_shadow_policy
          = ParseDirectionalShadowPolicy(developer.directional_shadows);

        auto paths
          = oxygen::PathFinderConfig::Create()
              .WithWorkspaceRoot(runtime_paths.root)
              .WithScriptSourceRoots({ runtime_paths.content })
              .WithCVarsArchivePath("bin/Oxygen/render-scene/cvars.json");
        if (runtime_paths.installed) {
          paths = std::move(paths)
                    .WithShaderLibraryPath("share/oxygen/shaders/shaders.bin")
                    .WithEngineContentPakPath("share/oxygen/engine.pak")
                    .WithCVarsArchivePath(runtime_paths.showcase / "cvars.json")
                    .WithScriptBytecodeCachePath(
                      runtime_paths.showcase / "scripts.bin");
        }
        if (!developer.cvars_archive.empty()) {
          paths = std::move(paths).WithCVarsArchivePath(
            std::filesystem::path(developer.cvars_archive));
        }
        config.engine.path_finder_config = std::move(paths).Build();
        config.engine.asset_loader.verify_content_hashes
          = developer.verify_hashes;
        config.engine.scripting.enable_hot_reload = developer.hot_reload;

        auto& cvars = config.startup_cvars;
        if (host::WasPassed(context, "hot-reload")) {
          cvars.Set("ngin.scripting.hot_reload", developer.hot_reload);
        }
        if (host::WasPassed(context, "verify-hashes")) {
          cvars.Set("cntt.verify_content_hashes", developer.verify_hashes);
        }
        if (!startup.scene_name.empty()) {
          LOG_F(INFO, "RenderScene: startup scene '{}'", startup.scene_name);
        }
      },
      .register_modules
      = [&](oxygen::AsyncEngine& engine, host::HostContext& context) -> void {
        host::RegisterRequiredModule(engine,
          std::make_unique<oxygen::physics::PhysicsModule>(
            oxygen::engine::kPhysicsModulePriority));
        host::RegisterRequiredModule(engine,
          std::make_unique<oxygen::scenesync::SceneObserverSyncModule>(
            oxygen::engine::kSceneObserverSyncModulePriority));
        host::RegisterRequiredModule(engine,
          std::make_unique<oxygen::scripting::ScriptingModule>(
            oxygen::engine::kScriptingModulePriority));
        host::RegisterRequiredModule(engine,
          std::make_unique<render_scene::MainModule>(context, startup));
        examples::RegisterDemoRenderer(engine, context, renderer_config);
      },
    });
}
