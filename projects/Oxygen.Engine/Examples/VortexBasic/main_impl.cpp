//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <chrono>
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

#include "VortexBasic/MainModule.h"

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
#include <Oxygen/Console/CVar.h>
#include <Oxygen/Engine/AsyncEngine.h>
#include <Oxygen/Graphics/Common/Types/QueueRole.h>
#include <Oxygen/Host/HostConfig.h>
#include <Oxygen/Host/RunHost.h>
#include <Oxygen/Host/RuntimeCli.h>
#include <Oxygen/SceneSync/RuntimeMotionProducerModule.h>
#include <Oxygen/Vortex/Diagnostics/ShaderDebugModeRegistry.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/RendererCapability.h>
#include <Oxygen/Vortex/SceneRenderer/ShadingMode.h>
#include <Oxygen/Vortex/ShaderDebugMode.h>

namespace host = oxygen::host;
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
using oxygen::console::CVarValue;
using oxygen::examples::vortex_basic::MainModule;
using oxygen::examples::vortex_basic::SceneOptions;
using oxygen::examples::vortex_basic::ValidationOptions;
using oxygen::graphics::QueueRole;
using namespace std::chrono_literals;

namespace {

auto ParseVortexShaderDebugMode(const std::string_view text)
  -> vortex::ShaderDebugMode
{
  if (text.empty() || text == "disabled" || text == "normal") {
    return vortex::ShaderDebugMode::kDisabled;
  }
  if (const auto* info = vortex::FindShaderDebugModeInfo(text);
    info != nullptr && info->supported) {
    return info->mode;
  }
  throw std::runtime_error(
    "Unsupported Vortex shader debug mode: " + std::string(text));
}

//! Applies the validation scene and shading path names to `validation`.
auto ResolveValidation(const std::string& scene_name,
  const std::string& shading_path, ValidationOptions& validation,
  SceneOptions& scene) -> void
{
  using ExposureFixture = ValidationOptions::ExposureFixture;
  if (scene_name != "default" && scene_name != "sidedness"
    && scene_name != "exposure-fixed" && scene_name != "exposure-locked"
    && scene_name != "exposure-curve-cancellation") {
    throw std::runtime_error("Unknown validation scene: " + scene_name);
  }
  if (shading_path != "deferred" && shading_path != "forward") {
    throw std::runtime_error("Unknown shading path: " + shading_path);
  }
  validation.sidedness_scene = scene_name == "sidedness";
  if (scene_name == "exposure-fixed") {
    validation.exposure_fixture = ExposureFixture::kFixed;
  } else if (scene_name == "exposure-locked") {
    validation.exposure_fixture = ExposureFixture::kLockedAuto;
  } else if (scene_name == "exposure-curve-cancellation") {
    validation.exposure_fixture = ExposureFixture::kCurveCancellation;
  }
  constexpr float kLockedEvLimit = 160.0F;
  constexpr float kEvLimit = 32.0F;
  const float ev_limit
    = validation.exposure_fixture == ExposureFixture::kLockedAuto
    ? kLockedEvLimit
    : kEvLimit;
  if (validation.fixed_exposure_ev < -ev_limit
    || validation.fixed_exposure_ev > ev_limit) {
    throw std::runtime_error(
      "--validation-exposure-ev is outside the fixture range");
  }
  if (validation.IsExposureFixture() && shading_path != "deferred") {
    throw std::runtime_error(
      "exposure fixtures use the deferred emissive input");
  }
  if (validation.normal_map && !validation.sidedness_scene) {
    throw std::runtime_error(
      "--validation-normal-map requires --validation-scene sidedness");
  }
  validation.shading_mode = shading_path == "forward"
    ? vortex::ShadingMode::kForward
    : vortex::ShadingMode::kDeferred;
  if (validation.sidedness_scene || validation.IsExposureFixture()) {
    scene.with_atmosphere = false;
    scene.with_height_fog = false;
    scene.with_local_fog = false;
    scene.with_volumetric_fog = false;
  }
}

auto SeedSceneCVars(const SceneOptions& scene, host::HostConfig& config) -> void
{
  auto& cvars = config.startup_cvars;
  cvars.Set("vtx.volumetric_fog.directional_shadows",
    CVarValue { scene.volumetric_directional_shadows });
  cvars.Set("vtx.volumetric_fog.temporal_reprojection",
    CVarValue { scene.volumetric_temporal_reprojection });
  if (scene.with_occlusion) {
    cvars.Set("vtx.occlusion.enable", CVarValue { true });
  }
  if (scene.with_local_fog) {
    cvars.Set("vtx.local_fog.enable", CVarValue { true });
    cvars.Set("vtx.local_fog.render_into_volumetric_fog",
      CVarValue { scene.local_fog_into_volumetric });
    cvars.Set("vtx.local_fog.max_density_into_volumetric_fog",
      CVarValue {
        static_cast<double>(scene.local_fog_volumetric_max_density) });
  }
}

auto RegisterModules(AsyncEngine& engine, host::HostContext& context,
  const vortex::ShaderDebugMode shader_debug_mode,
  const ValidationOptions& validation, const SceneOptions& scene) -> void
{
  // The main module runs before the renderer in every phase.
  host::RegisterRequiredModule(engine,
    std::make_unique<MainModule>(
      context, shader_debug_mode, validation, scene));
  host::RegisterRequiredModule(
    engine, std::make_unique<oxygen::scenesync::RuntimeMotionProducerModule>());

  constexpr auto kCapabilities
    = vortex::RendererCapabilityFamily::kScenePreparation
    | vortex::RendererCapabilityFamily::kGpuUploadAndAssetBinding
    | vortex::RendererCapabilityFamily::kLightingData
    | vortex::RendererCapabilityFamily::kDeferredShading
    | vortex::RendererCapabilityFamily::kEnvironmentLighting
    | vortex::RendererCapabilityFamily::kFinalOutputComposition
    | vortex::RendererCapabilityFamily::kDiagnosticsAndProfiling;
  const auto capabilities = validation.sidedness_scene
    ? kCapabilities | vortex::RendererCapabilityFamily::kShadowing
    : kCapabilities;
  const RendererConfig renderer_config {
    .upload_queue_key
    = context.queue_strategy.KeyFor(QueueRole::kTransfer).get(),
  };
  host::RegisterRequiredModule(engine,
    std::make_unique<vortex::Renderer>(
      context.gfx_weak, renderer_config, capabilities));
}

} // namespace

extern "C" auto MainImpl(std::span<const char*> args) -> int
{
  host::RuntimeCliState runtime { .target_fps = 100U };
  std::string shader_debug_mode_name;
  std::string validation_scene_name;
  std::string shading_path_name;
  ValidationOptions validation {};
  SceneOptions scene {};

  try {
    auto vortex_options = std::make_shared<Options>("Vortex options");
    vortex_options->Add(Option::WithKey("validation-scene")
        .About("Procedural scene: default, sidedness, exposure-fixed, "
               "exposure-locked, or exposure-curve-cancellation")
        .Long("validation-scene")
        .WithValue<std::string>()
        .DefaultValue(std::string("default"))
        .StoreTo(&validation_scene_name)
        .Build());
    vortex_options->Add(Option::WithKey("validation-exposure-ev")
        .About("Manual EV100 for the constant-4096 exposure-fixed fixture")
        .Long("validation-exposure-ev")
        .WithValue<float>()
        .DefaultValue(14.0F)
        .StoreTo(&validation.fixed_exposure_ev)
        .Build());
    vortex_options->Add(Option::WithKey("validation-exposure-key")
        .Long("validation-exposure-key")
        .WithValue<float>()
        .DefaultValue(12.5F)
        .StoreTo(&validation.exposure_key)
        .Build());
    vortex_options->Add(Option::WithKey("validation-exposure-compensation")
        .Long("validation-exposure-compensation")
        .WithValue<float>()
        .DefaultValue(0.0F)
        .StoreTo(&validation.exposure_compensation)
        .Build());
    vortex_options->Add(Option::WithKey("validation-exposure-enabled")
        .Long("validation-exposure-enabled")
        .WithValue<bool>()
        .DefaultValue(true)
        .StoreTo(&validation.exposure_enabled)
        .Build());
    vortex_options->Add(Option::WithKey("validation-camera-exposure")
        .Long("validation-camera-exposure")
        .WithValue<bool>()
        .DefaultValue(false)
        .StoreTo(&validation.camera_exposure)
        .Build());
    vortex_options->Add(Option::WithKey("validation-invalid-exposure")
        .About(
          "Inject an invalid key at frame 8; retain the last valid exposure")
        .Long("validation-invalid-exposure")
        .WithValue<bool>()
        .DefaultValue(false)
        .StoreTo(&validation.inject_invalid_exposure)
        .Build());
    vortex_options->Add(Option::WithKey("shading-path")
        .About("Native Vortex shading path: deferred or forward")
        .Long("shading-path")
        .WithValue<std::string>()
        .DefaultValue(std::string("deferred"))
        .StoreTo(&shading_path_name)
        .Build());
    vortex_options->Add(Option::WithKey("validation-motion")
        .About(
          "Translate the sidedness chart at a deterministic rate per frame")
        .Long("validation-motion")
        .WithValue<bool>()
        .DefaultValue(false)
        .StoreTo(&validation.animate)
        .Build());
    vortex_options->Add(Option::WithKey("validation-normal-map")
        .About("Apply a tangent +Y normal-map fixture to the sidedness chart")
        .Long("validation-normal-map")
        .WithValue<bool>()
        .DefaultValue(false)
        .StoreTo(&validation.normal_map)
        .Build());
    vortex_options->Add(Option::WithKey("shader-debug-mode")
        .About("Deferred debug visualization mode: disabled, base-color, "
               "world-normals, roughness, metalness, scene-depth-raw, "
               "scene-depth-linear, or directional-shadow-mask")
        .Long("shader-debug-mode")
        .WithValue<std::string>()
        .DefaultValue(std::string("disabled"))
        .UserFriendlyName("mode")
        .StoreTo(&shader_debug_mode_name)
        .Build());
    vortex_options->Add(Option::WithKey("with-atmosphere")
        .About("Enable the atmosphere/aerial-perspective view layer")
        .Long("with-atmosphere")
        .WithValue<bool>()
        .DefaultValue(true)
        .UserFriendlyName("enabled")
        .StoreTo(&scene.with_atmosphere)
        .Build());
    vortex_options->Add(Option::WithKey("with-height-fog")
        .About("Enable the height-fog view layer")
        .Long("with-height-fog")
        .WithValue<bool>()
        .DefaultValue(true)
        .UserFriendlyName("enabled")
        .StoreTo(&scene.with_height_fog)
        .Build());
    vortex_options->Add(Option::WithKey("with-local-fog")
        .About("Enable local fog volume authoring and rendering")
        .Long("with-local-fog")
        .WithValue<bool>()
        .DefaultValue(false)
        .UserFriendlyName("enabled")
        .StoreTo(&scene.with_local_fog)
        .Build());
    vortex_options->Add(Option::WithKey("with-volumetric-fog")
        .About("Enable volumetric fog authoring and integrated scattering")
        .Long("with-volumetric-fog")
        .WithValue<bool>()
        .DefaultValue(false)
        .UserFriendlyName("enabled")
        .StoreTo(&scene.with_volumetric_fog)
        .Build());
    vortex_options->Add(Option::WithKey("with-occlusion")
        .About("Enable Screen HZB occlusion proof geometry and runtime culling")
        .Long("with-occlusion")
        .WithValue<bool>()
        .DefaultValue(false)
        .UserFriendlyName("enabled")
        .StoreTo(&scene.with_occlusion)
        .Build());
    vortex_options->Add(Option::WithKey("with-translucency")
        .About("Enable translucent validation meshes for Stage 18 proof")
        .Long("with-translucency")
        .WithValue<bool>()
        .DefaultValue(false)
        .UserFriendlyName("enabled")
        .StoreTo(&scene.with_translucency)
        .Build());
    vortex_options->Add(Option::WithKey("volumetric-local-fog")
        .About("Inject local fog into volumetric fog when both are enabled")
        .Long("volumetric-local-fog")
        .WithValue<bool>()
        .DefaultValue(true)
        .UserFriendlyName("enabled")
        .StoreTo(&scene.local_fog_into_volumetric)
        .Build());
    vortex_options->Add(Option::WithKey("volumetric-directional-shadows")
        .About("Apply directional shadow-map visibility inside volumetric fog")
        .Long("volumetric-directional-shadows")
        .WithValue<bool>()
        .DefaultValue(true)
        .UserFriendlyName("enabled")
        .StoreTo(&scene.volumetric_directional_shadows)
        .Build());
    vortex_options->Add(Option::WithKey("volumetric-temporal-reprojection")
        .About("Enable temporal reprojection in volumetric fog")
        .Long("volumetric-temporal-reprojection")
        .WithValue<bool>()
        .DefaultValue(true)
        .UserFriendlyName("enabled")
        .StoreTo(&scene.volumetric_temporal_reprojection)
        .Build());
    vortex_options->Add(Option::WithKey("sky-light-volumetric-scattering")
        .About("SkyLight volumetric scattering intensity")
        .Long("sky-light-volumetric-scattering")
        .WithValue<float>()
        .DefaultValue(1.0F)
        .UserFriendlyName("intensity")
        .StoreTo(&scene.sky_light_volumetric_scattering_intensity)
        .Build());
    vortex_options->Add(Option::WithKey("aerial-scattering-strength")
        .About("Aerial perspective apply-time scattering strength")
        .Long("aerial-scattering-strength")
        .WithValue<float>()
        .DefaultValue(1.0F)
        .UserFriendlyName("strength")
        .StoreTo(&scene.aerial_scattering_strength)
        .Build());
    vortex_options->Add(Option::WithKey("aerial-start-depth")
        .About("Aerial perspective start depth in meters")
        .Long("aerial-start-depth")
        .WithValue<float>()
        .DefaultValue(100.0F)
        .UserFriendlyName("meters")
        .StoreTo(&scene.aerial_start_depth_m)
        .Build());
    vortex_options->Add(Option::WithKey("local-fog-volumetric-max-density")
        .About("Clamp used when injecting local fog into volumetric fog")
        .Long("local-fog-volumetric-max-density")
        .WithValue<float>()
        .DefaultValue(0.01F)
        .UserFriendlyName("density")
        .StoreTo(&scene.local_fog_volumetric_max_density)
        .Build());
    vortex_options->Add(Option::WithKey("local-fog-emissive-scale")
        .About("Scale the VortexBasic local-fog emissive proof volume")
        .Long("local-fog-emissive-scale")
        .WithValue<float>()
        .DefaultValue(1.0F)
        .UserFriendlyName("scale")
        .StoreTo(&scene.local_fog_emissive_scale)
        .Build());
    vortex_options->Add(Option::WithKey("volumetric-fog-emissive-scale")
        .About("Scale the base volumetric-fog emissive term")
        .Long("volumetric-fog-emissive-scale")
        .WithValue<float>()
        .DefaultValue(1.0F)
        .UserFriendlyName("scale")
        .StoreTo(&scene.volumetric_fog_emissive_scale)
        .Build());

    const Command::Ptr default_command
      = CommandBuilder(Command::DEFAULT)
          .WithOptions(host::MakeRuntimeOptions(runtime))
          .WithOptions(vortex_options)
          .WithOptions(host::MakeGraphicsToolingOptions(runtime))
          .WithOptions(host::MakeCaptureOptions(runtime))
          .WithOptions(host::MakeAdvancedCaptureOptions(runtime), true);
    const auto cli = host::BuildCli("vortex-basic",
      "Minimal Vortex deferred renderer exercise", default_command);
    const auto context = cli->Parse(static_cast<int>(args.size()), args.data());
    if (host::HandleMetaCommand(context, default_command)) {
      return EXIT_SUCCESS;
    }

    ResolveValidation(
      validation_scene_name, shading_path_name, validation, scene);
    const auto shader_debug_mode
      = ParseVortexShaderDebugMode(shader_debug_mode_name);
    LOG_F(INFO, "Validation scene={}, shading path={}, motion={}, debug={}",
      validation_scene_name, shading_path_name, validation.animate,
      vortex::to_string(shader_debug_mode));

    const auto workspace_root
      = std::filesystem::path(std::source_location::current().file_name())
          .parent_path()
          .parent_path()
          .parent_path();

    host::HostConfig config {};
    config.engine.renderer.implementation = RendererImplementation::kVortex;
    config.engine.application
      = { .name = "Vortex Basic Example", .version = 1U };
    config.engine.enable_asset_loader = true;
    config.engine.path_finder_config
      = PathFinderConfig::Create()
          .WithWorkspaceRoot(workspace_root)
          .WithScriptSourceRoots({ workspace_root / "Examples" / "Content" })
          .Build();
    config.engine.timing.pacing_safety_margin = 250us;
    host::ApplyRuntimeCli(context, runtime, config);
    SeedSceneCVars(scene, config);

    const auto exit_code = host::RunHost(std::move(config),
      [&](AsyncEngine& engine, host::HostContext& host_context) -> void {
        RegisterModules(
          engine, host_context, shader_debug_mode, validation, scene);
      });
    loguru::flush();
    loguru::shutdown();
    return exit_code;
  } catch (const host::RuntimeCliError& e) {
    LOG_F(ERROR, "CLI error: {}", e.what());
  } catch (const CmdLineArgumentsError& e) {
    LOG_F(ERROR, "CLI parse error: {}", e.what());
  } catch (const std::exception& e) {
    LOG_F(ERROR, "Unhandled exception: {}", e.what());
  }
  loguru::flush();
  loguru::shutdown();
  return EXIT_FAILURE;
}
