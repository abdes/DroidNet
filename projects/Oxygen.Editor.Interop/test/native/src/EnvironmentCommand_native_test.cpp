//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstring>
#include <exception>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <Commands/AttachLightCommand.h>
#include <Commands/DirectionalLightPropertyApplier.h>
#include <Commands/PropertyApplierRegistry.h>
#include <Commands/SetPropertiesCommand.h>
#include <Commands/SetEnvironmentCommand.h>
#include <EditorModule/EditorCommand.h>
#include <Oxygen/Core/Types/Atmosphere.h>
#include <Oxygen/Core/Types/PostProcess.h>
#include <Oxygen/Scene/Environment/Fog.h>
#include <Oxygen/Scene/Environment/PostProcessVolume.h>
#include <Oxygen/Scene/Environment/SceneEnvironment.h>
#include <Oxygen/Scene/Environment/SkyAtmosphere.h>
#include <Oxygen/Scene/Environment/SkyLight.h>
#include <Oxygen/Scene/Light/DirectionalLight.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneNode.h>

#pragma unmanaged

using namespace oxygen::interop::module;

namespace {

struct NativeStatus {
  bool succeeded = true;
  const char* error_message = "";
};

struct EnvironmentSnapshot {
  NativeStatus status {};

  bool environment_exists = false;

  bool sky_exists = false;
  bool sky_enabled = false;
  bool sky_sun_disk_enabled = false;
  bool sky_render_in_main_pass = false;
  int sky_transform_mode = -1;
  float sky_planet_radius_m = 0.0F;
  float sky_atmosphere_height_m = 0.0F;
  float sky_ground_albedo_y = 0.0F;
  float sky_rayleigh_scale_height_m = 0.0F;
  float sky_mie_scale_height_m = 0.0F;
  float sky_mie_anisotropy = 0.0F;
  float sky_luminance_factor_x = 0.0F;
  float sky_aerial_luminance_factor_x = 0.0F;
  float sky_aerial_distance_scale = 0.0F;
  float sky_aerial_scattering_strength = 0.0F;
  float sky_aerial_start_depth_m = 0.0F;
  float sky_height_fog_contribution = 0.0F;
  float sky_trace_sample_count_scale = 0.0F;
  float sky_min_light_elevation_deg = 0.0F;

  bool sky_light_exists = false;
  bool sky_light_enabled = false;
  bool sky_light_realtime_capture = false;
  bool sky_light_affects_reflections = false;

  bool post_exists = false;
  int tone_mapper = -1;
  int exposure_mode = -1;
  bool exposure_enabled = false;
  float exposure_compensation_ev = 0.0F;
  float exposure_key = 0.0F;
  float manual_exposure_ev = 0.0F;
  float auto_exposure_min_ev = 0.0F;
  float auto_exposure_max_ev = 0.0F;
  float auto_exposure_speed_up = 0.0F;
  float auto_exposure_speed_down = 0.0F;
  int auto_exposure_metering_mode = -1;
  float auto_exposure_low_percentile = 0.0F;
  float auto_exposure_high_percentile = 0.0F;
  float auto_exposure_min_log_luminance = 0.0F;
  float auto_exposure_log_luminance_range = 0.0F;
  float auto_exposure_target_luminance = 0.0F;
  float auto_exposure_spot_meter_radius = 0.0F;
  float bloom_intensity = 0.0F;
  float bloom_threshold = 0.0F;
  float saturation = 0.0F;
  float contrast = 0.0F;
  float vignette_intensity = 0.0F;
  float display_gamma = 0.0F;

  bool fog_exists = false;
  bool fog_enabled = true;
};

struct ExistingAtmosphereSnapshot {
  NativeStatus status {};

  bool disabled_enabled_value = true;
  float disabled_planet_radius_m = 0.0F;
  bool reenabled_value = false;
  float reenabled_atmosphere_height_m = 0.0F;
  float reenabled_aerial_start_depth_m = 0.0F;
  int reenabled_transform_mode = -1;
  bool reenabled_render_in_main_pass = false;
};

struct DirectionalLightSnapshot {
  NativeStatus status {};

  bool has_light = false;
  int atmosphere_slot = -1;
  bool per_pixel_transmittance = false;
  float disk_luminance_scale_y = 0.0F;
};

struct DirectionalLightPropertySnapshot {
  NativeStatus status {};

  bool primary_sun_resolved_before = false;
  bool primary_sun_resolved_after = false;
  bool environment_contribution = false;
  bool is_sun_light = false;
  float intensity_lux = 0.0F;
};

thread_local char last_native_error[1024] {};

auto NativeFailure(std::string message) -> NativeStatus
{
  strcpy_s(last_native_error, message.c_str());
  return NativeStatus { .succeeded = false,
    .error_message = last_native_error };
}

template <typename TResult, typename TAction>
auto CaptureNative(TResult& result, TAction action) -> void
{
  try {
    action();
  } catch (const std::exception& exception) {
    result.status = NativeFailure(exception.what());
  } catch (...) {
    result.status = NativeFailure("unknown native exception");
  }
}

auto BuildContext(oxygen::scene::Scene& scene) -> CommandContext
{
  CommandContext context {};
  context.Scene = oxygen::observer_ptr { &scene };
  return context;
}

auto CreateTestScene(const char* name) -> std::shared_ptr<oxygen::scene::Scene>
{
  return std::make_shared<oxygen::scene::Scene>(name, 8U);
}

auto ReadEnvironmentSnapshot(oxygen::scene::Scene& scene) -> EnvironmentSnapshot
{
  namespace engine = oxygen::engine;
  namespace env = oxygen::scene::environment;

  EnvironmentSnapshot snapshot {};

  const auto environment = scene.GetEnvironment();
  snapshot.environment_exists = environment != nullptr;
  if (!environment) {
    return snapshot;
  }

  const auto sky = environment->TryGetSystem<env::SkyAtmosphere>();
  snapshot.sky_exists = sky != nullptr;
  if (sky) {
    snapshot.sky_enabled = sky->IsEnabled();
    snapshot.sky_sun_disk_enabled = sky->GetSunDiskEnabled();
    snapshot.sky_render_in_main_pass = sky->GetRenderInMainPass();
    snapshot.sky_transform_mode = static_cast<int>(sky->GetTransformMode());
    snapshot.sky_planet_radius_m = sky->GetPlanetRadiusMeters();
    snapshot.sky_atmosphere_height_m = sky->GetAtmosphereHeightMeters();
    snapshot.sky_ground_albedo_y = sky->GetGroundAlbedoRgb().y;
    snapshot.sky_rayleigh_scale_height_m
      = sky->GetRayleighScaleHeightMeters();
    snapshot.sky_mie_scale_height_m = sky->GetMieScaleHeightMeters();
    snapshot.sky_mie_anisotropy = sky->GetMieAnisotropy();
    snapshot.sky_luminance_factor_x = sky->GetSkyLuminanceFactorRgb().x;
    snapshot.sky_aerial_luminance_factor_x
      = sky->GetSkyAndAerialPerspectiveLuminanceFactorRgb().x;
    snapshot.sky_aerial_distance_scale
      = sky->GetAerialPerspectiveDistanceScale();
    snapshot.sky_aerial_scattering_strength
      = sky->GetAerialScatteringStrength();
    snapshot.sky_aerial_start_depth_m
      = sky->GetAerialPerspectiveStartDepthMeters();
    snapshot.sky_height_fog_contribution = sky->GetHeightFogContribution();
    snapshot.sky_trace_sample_count_scale = sky->GetTraceSampleCountScale();
    snapshot.sky_min_light_elevation_deg
      = sky->GetTransmittanceMinLightElevationDeg();
  }

  const auto sky_light = environment->TryGetSystem<env::SkyLight>();
  snapshot.sky_light_exists = sky_light != nullptr;
  if (sky_light) {
    snapshot.sky_light_enabled = sky_light->IsEnabled();
    snapshot.sky_light_realtime_capture
      = sky_light->GetRealTimeCaptureEnabled();
    snapshot.sky_light_affects_reflections = sky_light->GetAffectReflections();
  }

  const auto post = environment->TryGetSystem<env::PostProcessVolume>();
  snapshot.post_exists = post != nullptr;
  if (post) {
    snapshot.tone_mapper = static_cast<int>(post->GetToneMapper());
    snapshot.exposure_mode = static_cast<int>(post->GetExposureMode());
    snapshot.exposure_enabled = post->GetExposureEnabled();
    snapshot.exposure_compensation_ev = post->GetExposureCompensationEv();
    snapshot.exposure_key = post->GetExposureKey();
    snapshot.manual_exposure_ev = post->GetManualExposureEv();
    snapshot.auto_exposure_min_ev = post->GetAutoExposureMinEv();
    snapshot.auto_exposure_max_ev = post->GetAutoExposureMaxEv();
    snapshot.auto_exposure_speed_up = post->GetAutoExposureSpeedUp();
    snapshot.auto_exposure_speed_down = post->GetAutoExposureSpeedDown();
    snapshot.auto_exposure_metering_mode
      = static_cast<int>(post->GetAutoExposureMeteringMode());
    snapshot.auto_exposure_low_percentile
      = post->GetAutoExposureLowPercentile();
    snapshot.auto_exposure_high_percentile
      = post->GetAutoExposureHighPercentile();
    snapshot.auto_exposure_min_log_luminance
      = post->GetAutoExposureMinLogLuminance();
    snapshot.auto_exposure_log_luminance_range
      = post->GetAutoExposureLogLuminanceRange();
    snapshot.auto_exposure_target_luminance
      = post->GetAutoExposureTargetLuminance();
    snapshot.auto_exposure_spot_meter_radius
      = post->GetAutoExposureSpotMeterRadius();
    snapshot.bloom_intensity = post->GetBloomIntensity();
    snapshot.bloom_threshold = post->GetBloomThreshold();
    snapshot.saturation = post->GetSaturation();
    snapshot.contrast = post->GetContrast();
    snapshot.vignette_intensity = post->GetVignetteIntensity();
    snapshot.display_gamma = post->GetDisplayGamma();
  } else {
    snapshot.exposure_mode = static_cast<int>(engine::ExposureMode::kAuto);
  }

  const auto fog = environment->TryGetSystem<env::Fog>();
  snapshot.fog_exists = fog != nullptr;
  if (fog) {
    snapshot.fog_enabled = fog->IsEnabled();
  }

  return snapshot;
}

auto RunExecuteWithMissingSceneIsNoOp() -> NativeStatus
{
  NativeStatus status {};
  try {
    CommandContext context {};
    SkyAtmosphereParams atmosphere {};
    PostProcessParams post_process {};
    SetEnvironmentCommand command(atmosphere, post_process);

    command.Execute(context);
  } catch (const std::exception& exception) {
    status = NativeFailure(exception.what());
  } catch (...) {
    status = NativeFailure("unknown native exception");
  }

  return status;
}

auto RunEnabledAtmosphereCreatesRuntimeEnvironmentWithAuthoredValues(
  EnvironmentSnapshot& result) -> void
{
  CaptureNative(result, [&result] {
    auto scene = CreateTestScene("EnvironmentCommandTest");
    auto context = BuildContext(*scene);

    SkyAtmosphereParams atmosphere {};
    atmosphere.enabled = true;
    atmosphere.sun_disk_enabled = false;
    atmosphere.planet_radius_m = 6400000.0F;
    atmosphere.atmosphere_height_m = 90000.0F;
    atmosphere.ground_albedo_rgb = { 0.1F, 0.2F, 0.3F };
    atmosphere.rayleigh_scale_height_m = 7000.0F;
    atmosphere.mie_scale_height_m = 1400.0F;
    atmosphere.mie_anisotropy = 0.7F;
    atmosphere.sky_luminance_factor_rgb = { 1.1F, 1.0F, 0.9F };
    atmosphere.aerial_perspective_distance_scale = 1.2F;
    atmosphere.aerial_scattering_strength = 0.8F;
    atmosphere.aerial_perspective_start_depth_m = 40.0F;
    atmosphere.height_fog_contribution = 0.6F;

    PostProcessParams post_process {};
    post_process.tone_mapper = 3;
    post_process.exposure_mode = 1;
    post_process.exposure_enabled = false;
    post_process.exposure_compensation_ev = 1.25F;
    post_process.exposure_key = 11.0F;
    post_process.manual_exposure_ev = 5.5F;
    post_process.auto_exposure_min_ev = -3.0F;
    post_process.auto_exposure_max_ev = 14.0F;
    post_process.auto_exposure_speed_up = 4.0F;
    post_process.auto_exposure_speed_down = 2.0F;
    post_process.auto_exposure_metering_mode = 2;
    post_process.auto_exposure_low_percentile = 0.2F;
    post_process.auto_exposure_high_percentile = 0.8F;
    post_process.auto_exposure_min_log_luminance = -10.0F;
    post_process.auto_exposure_log_luminance_range = 20.0F;
    post_process.auto_exposure_target_luminance = 0.25F;
    post_process.auto_exposure_spot_meter_radius = 0.4F;
    post_process.bloom_intensity = 0.7F;
    post_process.bloom_threshold = 1.5F;
    post_process.saturation = 0.9F;
    post_process.contrast = 1.1F;
    post_process.vignette_intensity = 0.3F;
    post_process.display_gamma = 2.4F;

    SetEnvironmentCommand command(atmosphere, post_process);
    command.Execute(context);

    result = ReadEnvironmentSnapshot(*scene);
  });
}

auto RunDisabledAtmosphereDoesNotCreateMissingAtmosphere(
  EnvironmentSnapshot& result) -> void
{
  CaptureNative(result, [&result] {
    auto scene = CreateTestScene("EnvironmentCommandTest");
    auto context = BuildContext(*scene);

    SkyAtmosphereParams atmosphere {};
    atmosphere.enabled = false;
    PostProcessParams post_process {};
    SetEnvironmentCommand command(atmosphere, post_process);

    command.Execute(context);

    result = ReadEnvironmentSnapshot(*scene);
  });
}

auto RunExistingAtmosphereCanBeDisabledAndReEnabled(
  ExistingAtmosphereSnapshot& result) -> void
{
  namespace env = oxygen::scene::environment;

  CaptureNative(result, [&result] {
    auto scene = CreateTestScene("EnvironmentCommandTest");
    scene->SetEnvironment(
      std::make_unique<oxygen::scene::SceneEnvironment>());
    auto context = BuildContext(*scene);
    auto environment = scene->GetEnvironment();
    auto& authored_atmosphere = environment->AddSystem<env::SkyAtmosphere>();

    SkyAtmosphereParams disabled {};
    disabled.enabled = false;
    PostProcessParams post_process {};
    SetEnvironmentCommand disable_command(disabled, post_process);
    disable_command.Execute(context);

    result.disabled_enabled_value = authored_atmosphere.IsEnabled();
    result.disabled_planet_radius_m
      = authored_atmosphere.GetPlanetRadiusMeters();

    SkyAtmosphereParams enabled {};
    enabled.enabled = true;
    enabled.atmosphere_height_m = 100000.0F;
    enabled.aerial_perspective_start_depth_m = 100.0F;
    SetEnvironmentCommand enable_command(enabled, post_process);
    enable_command.Execute(context);

    result.reenabled_value = authored_atmosphere.IsEnabled();
    result.reenabled_atmosphere_height_m
      = authored_atmosphere.GetAtmosphereHeightMeters();
    result.reenabled_aerial_start_depth_m
      = authored_atmosphere.GetAerialPerspectiveStartDepthMeters();
    result.reenabled_transform_mode
      = static_cast<int>(authored_atmosphere.GetTransformMode());
    result.reenabled_render_in_main_pass
      = authored_atmosphere.GetRenderInMainPass();
  });
}

auto RunAttachDirectionalSunConfiguresAtmosphereLightRole(
  DirectionalLightSnapshot& result) -> void
{
  CaptureNative(result, [&result] {
    auto scene = CreateTestScene("DirectionalLightCommandTest");
    auto node = scene->CreateNode("Sun");
    auto context = BuildContext(*scene);

    LightCommonParams common {};
    common.affects_world = true;
    common.casts_shadows = true;
    AttachLightCommand command(node.GetHandle(), LightKind::kDirectional,
      common, 100000.0F, 0.0F, 0.0F, 0.0F, 0.0F, 2.0F, 0.0095F, true,
      true);

    command.Execute(context);

    auto light = node.GetLightAs<oxygen::scene::DirectionalLight>();
    result.has_light = light.has_value();
    if (light) {
      result.atmosphere_slot
        = static_cast<int>(light->get().GetAtmosphereLightSlot());
      result.per_pixel_transmittance
        = light->get().GetUsePerPixelAtmosphereTransmittance();
      result.disk_luminance_scale_y
        = light->get().GetAtmosphereDiskLuminanceScale().y;
    }
  });
}

auto RunAttachDirectionalNonSunClearsAtmosphereLightRole(
  DirectionalLightSnapshot& result) -> void
{
  CaptureNative(result, [&result] {
    auto scene = CreateTestScene("DirectionalLightCommandTest");
    auto node = scene->CreateNode("Fill");
    auto context = BuildContext(*scene);

    LightCommonParams common {};
    common.affects_world = true;
    AttachLightCommand command(node.GetHandle(), LightKind::kDirectional,
      common, 5000.0F, 0.0F, 0.0F, 0.0F, 0.0F, 2.0F, 0.0095F, false,
      false);

    command.Execute(context);

    auto light = node.GetLightAs<oxygen::scene::DirectionalLight>();
    result.has_light = light.has_value();
    if (light) {
      result.atmosphere_slot
        = static_cast<int>(light->get().GetAtmosphereLightSlot());
      result.per_pixel_transmittance
        = light->get().GetUsePerPixelAtmosphereTransmittance();
      result.disk_luminance_scale_y
        = light->get().GetAtmosphereDiskLuminanceScale().y;
    }
  });
}

auto RunSetPropertiesDirectionalLightEditInvalidatesResolvedSun(
  DirectionalLightPropertySnapshot& result) -> void
{
  CaptureNative(result, [&result] {
    auto scene = CreateTestScene("DirectionalLightPropertyCommandTest");
    auto node = scene->CreateNode("Sun");
    auto context = BuildContext(*scene);

    LightCommonParams common {};
    common.affects_world = true;
    AttachLightCommand attach_command(node.GetHandle(), LightKind::kDirectional,
      common, 5000.0F, 0.0F, 0.0F, 0.0F, 0.0F, 2.0F, 0.0095F, false,
      false);
    attach_command.Execute(context);

    scene->Update(false);
    scene->SyncObservers();
    result.primary_sun_resolved_before
      = scene->GetDirectionalLightResolver().ResolvePrimarySun().has_value();

    PropertyApplierRegistry::Bootstrap();
    std::vector<PropertyEntry> entries {
      { ComponentId::kDirectionalLight,
        static_cast<std::uint16_t>(
          DirectionalLightField::kEnvironmentContribution),
        1.0F },
      { ComponentId::kDirectionalLight,
        static_cast<std::uint16_t>(DirectionalLightField::kIsSunLight),
        1.0F },
      { ComponentId::kDirectionalLight,
        static_cast<std::uint16_t>(DirectionalLightField::kIntensityLux),
        90000.0F },
    };
    SetPropertiesCommand edit_command(node.GetHandle(), std::move(entries));
    edit_command.Execute(context);

    const auto resolved
      = scene->GetDirectionalLightResolver().ResolvePrimarySun();
    result.primary_sun_resolved_after = resolved.has_value();

    auto light = node.GetLightAs<oxygen::scene::DirectionalLight>();
    if (light) {
      result.environment_contribution
        = light->get().GetEnvironmentContribution();
      result.is_sun_light = light->get().IsSunLight();
      result.intensity_lux = light->get().GetIntensityLux();
    }
  });
}

} // namespace

#pragma managed

using namespace Microsoft::VisualStudio::TestTools::UnitTesting;

namespace {

auto NativeMessage(const NativeStatus& status) -> System::String^
{
  return gcnew System::String(
    status.error_message != nullptr ? status.error_message : "");
}

auto AssertSucceeded(const NativeStatus& status) -> void
{
  Assert::IsTrue(status.succeeded, NativeMessage(status));
}

} // namespace

namespace InteropTests {

[TestClass]
public ref class EnvironmentCommandCliTests {
public:
  [TestMethod]
  void ExecuteWithMissingSceneIsNoOp()
  {
    AssertSucceeded(RunExecuteWithMissingSceneIsNoOp());
  }

  [TestMethod]
  void EnabledAtmosphereCreatesRuntimeEnvironmentWithAuthoredValues()
  {
    EnvironmentSnapshot snapshot {};
    RunEnabledAtmosphereCreatesRuntimeEnvironmentWithAuthoredValues(snapshot);

    AssertSucceeded(snapshot.status);
    Assert::IsTrue(snapshot.environment_exists);
    Assert::IsTrue(snapshot.sky_exists);
    Assert::IsTrue(snapshot.sky_enabled);
    Assert::IsFalse(snapshot.sky_sun_disk_enabled);
    Assert::AreEqual(6400000.0F, snapshot.sky_planet_radius_m, 0.0001F);
    Assert::AreEqual(90000.0F, snapshot.sky_atmosphere_height_m, 0.0001F);
    Assert::AreEqual(0.2F, snapshot.sky_ground_albedo_y, 0.0001F);
    Assert::AreEqual(7000.0F, snapshot.sky_rayleigh_scale_height_m, 0.0001F);
    Assert::AreEqual(1400.0F, snapshot.sky_mie_scale_height_m, 0.0001F);
    Assert::AreEqual(0.7F, snapshot.sky_mie_anisotropy, 0.0001F);
    Assert::AreEqual(1.1F, snapshot.sky_luminance_factor_x, 0.0001F);
    Assert::AreEqual(1.1F, snapshot.sky_aerial_luminance_factor_x, 0.0001F);
    Assert::AreEqual(1.2F, snapshot.sky_aerial_distance_scale, 0.0001F);
    Assert::AreEqual(0.8F, snapshot.sky_aerial_scattering_strength, 0.0001F);
    Assert::AreEqual(40.0F, snapshot.sky_aerial_start_depth_m, 0.0001F);
    Assert::AreEqual(0.6F, snapshot.sky_height_fog_contribution, 0.0001F);
    Assert::AreEqual(1.0F, snapshot.sky_trace_sample_count_scale, 0.0001F);
    Assert::AreEqual(-90.0F, snapshot.sky_min_light_elevation_deg, 0.0001F);
    Assert::IsTrue(snapshot.sky_render_in_main_pass);

    Assert::IsTrue(snapshot.sky_light_exists);
    Assert::IsTrue(snapshot.sky_light_enabled);
    Assert::IsTrue(snapshot.sky_light_realtime_capture);
    Assert::IsTrue(snapshot.sky_light_affects_reflections);

    Assert::IsTrue(snapshot.post_exists);
    Assert::AreEqual(
      static_cast<int>(oxygen::engine::ToneMapper::kReinhard),
      snapshot.tone_mapper);
    Assert::AreEqual(
      static_cast<int>(oxygen::engine::ExposureMode::kManualCamera),
      snapshot.exposure_mode);
    Assert::IsFalse(snapshot.exposure_enabled);
    Assert::AreEqual(1.25F, snapshot.exposure_compensation_ev, 0.0001F);
    Assert::AreEqual(11.0F, snapshot.exposure_key, 0.0001F);
    Assert::AreEqual(5.5F, snapshot.manual_exposure_ev, 0.0001F);
    Assert::AreEqual(-3.0F, snapshot.auto_exposure_min_ev, 0.0001F);
    Assert::AreEqual(14.0F, snapshot.auto_exposure_max_ev, 0.0001F);
    Assert::AreEqual(4.0F, snapshot.auto_exposure_speed_up, 0.0001F);
    Assert::AreEqual(2.0F, snapshot.auto_exposure_speed_down, 0.0001F);
    Assert::AreEqual(static_cast<int>(oxygen::engine::MeteringMode::kSpot),
      snapshot.auto_exposure_metering_mode);
    Assert::AreEqual(0.2F, snapshot.auto_exposure_low_percentile, 0.0001F);
    Assert::AreEqual(0.8F, snapshot.auto_exposure_high_percentile, 0.0001F);
    Assert::AreEqual(
      -10.0F, snapshot.auto_exposure_min_log_luminance, 0.0001F);
    Assert::AreEqual(
      20.0F, snapshot.auto_exposure_log_luminance_range, 0.0001F);
    Assert::AreEqual(
      0.25F, snapshot.auto_exposure_target_luminance, 0.0001F);
    Assert::AreEqual(
      0.4F, snapshot.auto_exposure_spot_meter_radius, 0.0001F);
    Assert::AreEqual(0.7F, snapshot.bloom_intensity, 0.0001F);
    Assert::AreEqual(1.5F, snapshot.bloom_threshold, 0.0001F);
    Assert::AreEqual(0.9F, snapshot.saturation, 0.0001F);
    Assert::AreEqual(1.1F, snapshot.contrast, 0.0001F);
    Assert::AreEqual(0.3F, snapshot.vignette_intensity, 0.0001F);
    Assert::AreEqual(2.4F, snapshot.display_gamma, 0.0001F);
  }

  [TestMethod]
  void DisabledAtmosphereDoesNotCreateMissingAtmosphere()
  {
    EnvironmentSnapshot snapshot {};
    RunDisabledAtmosphereDoesNotCreateMissingAtmosphere(snapshot);

    AssertSucceeded(snapshot.status);
    Assert::IsTrue(snapshot.environment_exists);
    Assert::IsFalse(snapshot.sky_exists);
    Assert::IsTrue(snapshot.sky_light_exists);
    Assert::IsTrue(snapshot.sky_light_enabled);
    Assert::IsTrue(snapshot.sky_light_realtime_capture);
    Assert::IsTrue(snapshot.post_exists);
    Assert::AreEqual(static_cast<int>(oxygen::engine::ExposureMode::kManual),
      snapshot.exposure_mode);
    Assert::AreEqual(
      oxygen::engine::kExposureCalibrationKey, snapshot.exposure_key, 0.0001F);
    Assert::AreEqual(13.0F, snapshot.manual_exposure_ev, 0.0001F);
    Assert::IsTrue(snapshot.fog_exists);
    Assert::IsFalse(snapshot.fog_enabled);
  }

  [TestMethod]
  void ExistingAtmosphereCanBeDisabledAndReEnabled()
  {
    ExistingAtmosphereSnapshot snapshot {};
    RunExistingAtmosphereCanBeDisabledAndReEnabled(snapshot);

    AssertSucceeded(snapshot.status);
    Assert::IsFalse(snapshot.disabled_enabled_value);
    Assert::AreEqual(oxygen::engine::atmos::kDefaultPlanetRadiusM,
      snapshot.disabled_planet_radius_m, 0.0001F);
    Assert::IsTrue(snapshot.reenabled_value);
    Assert::AreEqual(
      100000.0F, snapshot.reenabled_atmosphere_height_m, 0.0001F);
    Assert::AreEqual(
      100.0F, snapshot.reenabled_aerial_start_depth_m, 0.0001F);
    Assert::AreEqual(
      static_cast<int>(oxygen::scene::environment::
          SkyAtmosphereTransformMode::kPlanetTopAtAbsoluteWorldOrigin),
      snapshot.reenabled_transform_mode);
    Assert::IsTrue(snapshot.reenabled_render_in_main_pass);
  }

  [TestMethod]
  void AttachDirectionalSunConfiguresAtmosphereLightRole()
  {
    DirectionalLightSnapshot snapshot {};
    RunAttachDirectionalSunConfiguresAtmosphereLightRole(snapshot);

    AssertSucceeded(snapshot.status);
    Assert::IsTrue(snapshot.has_light);
    Assert::AreEqual(
      static_cast<int>(oxygen::scene::AtmosphereLightSlot::kPrimary),
      snapshot.atmosphere_slot);
    Assert::IsTrue(snapshot.per_pixel_transmittance);
    Assert::AreEqual(0.95F, snapshot.disk_luminance_scale_y, 0.0001F);
  }

  [TestMethod]
  void AttachDirectionalNonSunClearsAtmosphereLightRole()
  {
    DirectionalLightSnapshot snapshot {};
    RunAttachDirectionalNonSunClearsAtmosphereLightRole(snapshot);

    AssertSucceeded(snapshot.status);
    Assert::IsTrue(snapshot.has_light);
    Assert::AreEqual(
      static_cast<int>(oxygen::scene::AtmosphereLightSlot::kNone),
      snapshot.atmosphere_slot);
    Assert::IsFalse(snapshot.per_pixel_transmittance);
    Assert::AreEqual(1.0F, snapshot.disk_luminance_scale_y, 0.0001F);
  }

  [TestMethod]
  void SetPropertiesDirectionalLightEditInvalidatesResolvedSun()
  {
    DirectionalLightPropertySnapshot snapshot {};
    RunSetPropertiesDirectionalLightEditInvalidatesResolvedSun(snapshot);

    AssertSucceeded(snapshot.status);
    Assert::IsFalse(snapshot.primary_sun_resolved_before);
    Assert::IsTrue(snapshot.primary_sun_resolved_after);
    Assert::IsTrue(snapshot.environment_contribution);
    Assert::IsTrue(snapshot.is_sun_light);
    Assert::AreEqual(90000.0F, snapshot.intensity_lux, 0.0001F);
  }
};

} // namespace InteropTests
