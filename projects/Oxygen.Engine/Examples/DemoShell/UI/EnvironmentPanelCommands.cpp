//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "DemoShell/PanelCommands.h"
#include "DemoShell/UI/EnvironmentPanelCommands.h"
#include "DemoShell/UI/EnvironmentVm.h"
#include <fmt/format.h>
#include <glm/vec3.hpp>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Commands/Command.h>
#include <Oxygen/Commands/Params.h>
#include <Oxygen/Core/Types/Atmosphere.h>

// NOLINTBEGIN(*-magic-numbers): the limits are the panel's widget ranges.

namespace oxygen::examples::ui {

namespace {

  // NOLINTNEXTLINE(google-build-using-namespace) - binds every name in it
  using namespace env_commands;
  using commands::CommandArgs;
  using commands::CommandContext;
  using commands::CommandParam;
  using commands::CommandResult;
  using commands::ParamType;

  auto Name(const std::string_view name) -> std::string
  {
    return std::string(name);
  }

  template <std::size_t N>
  auto Names(const std::array<std::string_view, N>& values)
    -> std::vector<std::string>
  {
    return { values.begin(), values.end() };
  }

  auto Number(std::string name, std::string help, const double min,
    const double max) -> CommandParam
  {
    return CommandParam {
      .name = std::move(name),
      .type = ParamType::kDouble,
      .help = std::move(help),
      .required = true,
      .min_value = min,
      .max_value = max,
    };
  }

  auto Float(const CommandArgs& args, const std::string_view name) -> float
  {
    return static_cast<float>(args.GetDouble(name));
  }

  //! Index of the bound enum value; the binder admits only listed values.
  template <std::size_t N>
  auto IndexOf(const std::array<std::string_view, N>& values,
    const std::string& value) -> int
  {
    for (std::size_t i = 0; i < N; ++i) {
      if (values.at(i) == value) {
        return static_cast<int>(i);
      }
    }
    return 0;
  }

  //! Registers an enumerated setting whose VM setter takes the value's index.
  template <std::size_t N, typename Set>
  auto AddChoice(PanelCommands& commands, const std::string_view name,
    std::string label, std::string help,
    const std::array<std::string_view, N>& values, Set set) -> void
  {
    commands.AddEnum(Name(name), std::move(label), std::move(help),
      Names(values), [set = std::move(set)](const std::size_t index) -> void {
        set(static_cast<int>(index));
      });
  }

  auto RegisterProfileAndSun(
    PanelCommands& commands, observer_ptr<EnvironmentVm> vm) -> void
  {
    std::vector<std::string> presets;
    presets.reserve(vm->GetPresetCount());
    for (int i = 0; i < vm->GetPresetCount(); ++i) {
      presets.emplace_back(EnvironmentVm::GetPresetKey(i));
    }
    commands.AddEnum(Name(kPreset), "Environment Profile",
      "scene uses the scene's environment; custom keeps the edited one",
      std::move(presets), [vm](const std::size_t i) -> void {
        vm->ApplyPreset(static_cast<int>(i));
      });
    commands.AddBool(Name(kPreviewSunEnabled), "Preview Sun",
      "Light the scene with a preview sun when it has none of its own",
      [vm](const bool v) -> void { vm->SetPreviewSunEnabled(v); });

    commands.AddBool(Name(kSunEnabled), "Sun", "Enable the sun light",
      [vm](const bool v) -> void { vm->SetSunEnabled(v); });
    commands.AddFloat(
      Name(kSunAzimuthDeg), "Sun Azimuth", "Sun azimuth in degrees",
      [vm](const float v) -> void { vm->SetSunAzimuthDeg(v); }, 0.0F, 360.0F);
    commands.AddFloat(
      Name(kSunElevationDeg), "Sun Elevation",
      "Sun elevation above the horizon in degrees",
      [vm](const float v) -> void { vm->SetSunElevationDeg(v); }, -90.0F,
      90.0F);
    commands.AddFloat(
      Name(kSunIlluminanceLx), "Sun Illuminance", "Sun illuminance in lux",
      [vm](const float v) -> void { vm->SetSunIlluminanceLx(v); }, 0.0F,
      1000000.0F);
    commands.AddBool(Name(kSunUseTemperature), "Sun Color From Temperature",
      "Derive the sun color from its temperature instead of its RGB color",
      [vm](const bool v) -> void { vm->SetSunUseTemperature(v); });
    commands.AddFloat(
      Name(kSunTemperatureKelvin), "Sun Temperature",
      "Sun color temperature in kelvin",
      [vm](const float v) -> void { vm->SetSunTemperatureKelvin(v); }, 1000.0F,
      40000.0F);
    commands.AddRgb(Name(kSunColorRgb), "Sun Color", "Linear sun color",
      [vm](const glm::vec3& v) -> void { vm->SetSunColorRgb(v); });
    commands.AddFloat(
      Name(kSunSourceAngleDeg), "Sun Angular Diameter",
      "Apparent size of the sun disk in degrees",
      [vm](const float v) -> void { vm->SetSunSourceAngleDeg(v); }, 0.01F,
      5.0F);
    AddChoice(commands, kSunAtmosphereLightSlot, "Sun Atmosphere Slot",
      "Which atmosphere light the sun drives", kAtmosphereLightSlots,
      [vm](const int v) -> void { vm->SetSunAtmosphereLightSlot(v); });
    commands.AddBool(Name(kSunUsePerPixelAtmosphereTransmittance),
      "Per-Pixel Sun Transmittance",
      "Attenuate sunlight through the atmosphere per pixel",
      [vm](const bool v) -> void {
        vm->SetSunUsePerPixelAtmosphereTransmittance(v);
      });
    commands.AddBool(Name(kSunDiskEnabled), "Sun Disk",
      "Draw the sun disk in the sky",
      [vm](const bool v) -> void { vm->SetSunDiskEnabled(v); });
    commands.AddRgb(Name(kSunAtmosphereDiskLuminanceScale),
      "Sun Disk Color Scale", "Linear multiplier of the visible sun disk only",
      [vm](const glm::vec3& v) -> void {
        vm->SetSunAtmosphereDiskLuminanceScale(v);
      });
  }

  auto RegisterSunShadows(
    PanelCommands& commands, observer_ptr<EnvironmentVm> vm) -> void
  {
    commands.AddFloat(
      Name(kSunShadowBias), "Sun Shadow Bias",
      "Depth bias of the sun's shadow map",
      [vm](const float v) -> void { vm->SetSunShadowBias(v); }, 0.0F, 1.0F);
    commands.AddFloat(
      Name(kSunShadowNormalBias), "Sun Shadow Normal Bias",
      "Normal offset of the sun's shadow lookups",
      [vm](const float v) -> void { vm->SetSunShadowNormalBias(v); }, 0.0F,
      1.0F);
    AddChoice(commands, kSunShadowResolutionHint, "Sun Shadow Resolution",
      "Shadow map resolution the renderer aims for", kShadowResolutions,
      [vm](const int v) -> void { vm->SetSunShadowResolutionHint(v); });
    commands.AddInt(
      Name(kSunShadowCascadeCount), "Sun Shadow Cascades",
      "Number of shadow cascades",
      [vm](const int v) -> void { vm->SetSunShadowCascadeCount(v); }, 1, 4);
    AddChoice(commands, kSunShadowSplitMode, "Sun Shadow Splits",
      "generated spreads the cascades by an exponent; manual uses the "
      "cascade distances",
      kShadowSplitModes,
      [vm](const int v) -> void { vm->SetSunShadowSplitMode(v); });
    commands.AddFloat(
      Name(kSunShadowMaxDistance), "Sun Shadow Distance",
      "Distance the generated cascades cover, in meters",
      [vm](const float v) -> void { vm->SetSunShadowMaxDistance(v); }, 1.0F,
      100000.0F);
    commands.AddFloat(
      Name(kSunShadowDistributionExponent), "Sun Shadow Distribution",
      "Exponent of the generated split distribution; higher favors near "
      "cascades",
      [vm](const float v) -> void { vm->SetSunShadowDistributionExponent(v); },
      1.0F, 16.0F);
    commands.AddFloat(
      Name(kSunShadowTransitionFraction), "Sun Shadow Transition",
      "Fraction of a cascade blended with the next one",
      [vm](const float v) -> void { vm->SetSunShadowTransitionFraction(v); },
      0.0F, 1.0F);
    commands.AddFloat(
      Name(kSunShadowDistanceFadeoutFraction), "Sun Shadow Fadeout",
      "Fraction of the shadow distance over which shadows fade out",
      [vm](
        const float v) -> void { vm->SetSunShadowDistanceFadeoutFraction(v); },
      0.0F, 1.0F);
    commands.Add({
      .name = Name(kSunShadowCascadeDistance),
      .label = "Sun Shadow Cascade Distance",
      .help = "End distance of one manual shadow cascade, in meters",
      .params = {
        CommandParam {
          .name = "cascade",
          .type = ParamType::kInt,
          .help = "Cascade index",
          .required = true,
          .min_value = 0.0,
          .max_value = 3.0,
        },
        Number("distance", "End distance in meters", 0.0, 100000.0),
      },
      .handler = [vm](const CommandArgs& args,
                   const CommandContext&) -> CommandResult {
        const auto cascade = static_cast<int>(args.GetInt("cascade"));
        const float distance = Float(args, "distance");
        vm->SetSunShadowCascadeDistance(cascade, distance);
        return CommandResult::Success(fmt::format(
          "{} cascade {} = {}", kSunShadowCascadeDistance, cascade, distance));
      },
    });
  }

  auto RegisterAtmosphere(
    PanelCommands& commands, observer_ptr<EnvironmentVm> vm) -> void
  {
    commands.AddBool(Name(kSkyAtmosphereEnabled), "Sky Atmosphere",
      "Enable the physically based sky atmosphere",
      [vm](const bool v) -> void { vm->SetSkyAtmosphereEnabled(v); });
    AddChoice(commands, kSkyAtmosphereTransformMode, "Atmosphere Placement",
      "Where the planet sits relative to the world or the component",
      kTransformModes,
      [vm](const int v) -> void { vm->SetSkyAtmosphereTransformMode(v); });
    commands.AddFloat(
      Name(kPlanetRadiusKm), "Planet Radius", "Planet radius in kilometers",
      [vm](const float v) -> void { vm->SetPlanetRadiusKm(v); }, 10.0F,
      100000.0F);
    commands.AddFloat(
      Name(kAtmosphereHeightKm), "Atmosphere Height",
      "Atmosphere thickness in kilometers",
      [vm](const float v) -> void { vm->SetAtmosphereHeightKm(v); }, 1.0F,
      1000.0F);
    commands.AddRgb(
      Name(kGroundAlbedo), "Ground Albedo",
      "Planet ground albedo seen by the atmosphere",
      [vm](const glm::vec3& v) -> void { vm->SetGroundAlbedo(v); }, 1.0F);
    commands.AddFloat(
      Name(kRayleighScaleHeightKm), "Rayleigh Scale Height",
      "Height at which Rayleigh density falls to 1/e, in kilometers",
      [vm](const float v) -> void { vm->SetRayleighScaleHeightKm(v); }, 0.1F,
      50.0F);
    commands.AddFloat(
      Name(kMieScaleHeightKm), "Mie Scale Height",
      "Height at which Mie density falls to 1/e, in kilometers",
      [vm](const float v) -> void { vm->SetMieScaleHeightKm(v); }, 0.05F,
      20.0F);
    commands.AddFloat(
      Name(kMieAnisotropy), "Mie Anisotropy",
      "Forward scattering of aerosols, from 0 (even) to 0.99",
      [vm](const float v) -> void { vm->SetMieAnisotropy(v); }, 0.0F, 0.99F);
    commands.AddFloat(
      Name(kMieAbsorptionScale), "Mie Absorption",
      "Scale of the aerosol absorption",
      [vm](const float v) -> void { vm->SetMieAbsorptionScale(v); }, 0.0F,
      5.0F);
    commands.AddFloat(
      Name(kMultiScattering), "Multiple Scattering",
      "Scale of the multiple-scattering contribution",
      [vm](const float v) -> void { vm->SetMultiScattering(v); }, 0.0F, 5.0F);
    commands.AddRgb(Name(kSkyLuminanceFactor), "Sky Luminance Factor",
      "Linear multiplier of the sky's luminance",
      [vm](const glm::vec3& v) -> void { vm->SetSkyLuminanceFactor(v); });
    commands.AddRgb(Name(kSkyAndAerialPerspectiveLuminanceFactor),
      "Sky And Aerial Perspective Luminance",
      "Linear multiplier of the sky and the aerial perspective together",
      [vm](const glm::vec3& v) -> void {
        vm->SetSkyAndAerialPerspectiveLuminanceFactor(v);
      });
    commands.AddRgb(
      Name(kOzoneRgb), "Ozone Absorption",
      "Ozone absorption per channel in 1/m (about 1e-6)",
      [vm](const glm::vec3& v) -> void { vm->SetOzoneRgb(v); }, 1.0e-5F);
    commands.Add({
      .name = Name(kOzoneDensityProfile),
      .label = "Ozone Density Profile",
      .help = "Two-layer tent profile: peak altitude, then the lower and upper "
              "layers' slope and offset",
      .params = {
        Number("peak_km", "Peak altitude in km", 0.0, 120.0),
        Number("lower_slope", "Lower layer slope in 1/km", -1.0, 1.0),
        Number("lower_offset", "Lower layer density offset", -8.0, 8.0),
        Number("upper_slope", "Upper layer slope in 1/km", -1.0, 1.0),
        Number("upper_offset", "Upper layer density offset", -8.0, 8.0),
      },
      .handler = [vm](const CommandArgs& args,
                   const CommandContext&) -> CommandResult {
        constexpr float kKmToMeters = engine::atmos::kSkyUnitToM;
        auto profile = engine::atmos::DensityProfile {};
        profile.layers.at(0) = {
          .width_m = Float(args, "peak_km") * kKmToMeters,
          .exp_term = 0.0F,
          .linear_term = Float(args, "lower_slope") / kKmToMeters,
          .constant_term = Float(args, "lower_offset"),
        };
        profile.layers.at(1) = {
          .width_m = 0.0F,
          .exp_term = 0.0F,
          .linear_term = Float(args, "upper_slope") / kKmToMeters,
          .constant_term = Float(args, "upper_offset"),
        };
        vm->SetOzoneDensityProfile(profile);
        return CommandResult::Success(
          fmt::format("{} = {}", kOzoneDensityProfile, args.ToJson().dump()));
      },
    });
    commands.AddBool(Name(kAtmosphereHoldout), "Atmosphere Holdout",
      "Render the atmosphere as a holdout: it occludes but adds no light",
      [vm](const bool v) -> void { vm->SetAtmosphereHoldout(v); });
    commands.AddBool(Name(kAtmosphereRenderInMainPass),
      "Atmosphere In Main Pass", "Draw the atmosphere in the main view",
      [vm](const bool v) -> void { vm->SetAtmosphereRenderInMainPass(v); });

    commands.AddBool(Name(kAerialPerspectiveEnabled), "Aerial Perspective",
      "Apply the atmosphere between the camera and the scene",
      [vm](const bool v) -> void { vm->SetAerialPerspectiveEnabled(v); });
    commands.AddFloat(
      Name(kAerialPerspectiveScale), "Aerial Perspective Distance Scale",
      "Multiplier of the distances aerial perspective sees",
      [vm](const float v) -> void { vm->SetAerialPerspectiveScale(v); }, 0.0F,
      10000.0F);
    commands.AddFloat(
      Name(kAerialPerspectiveStartDepthMeters), "Aerial Perspective Start",
      "Distance from the camera where aerial perspective starts, in meters",
      [vm](
        const float v) -> void { vm->SetAerialPerspectiveStartDepthMeters(v); },
      0.0F, 1000000.0F);
    commands.AddFloat(
      Name(kAerialScatteringStrength), "Haze Strength",
      "Strength of the aerial scattering; zero removes the haze but keeps "
      "the sky",
      [vm](const float v) -> void { vm->SetAerialScatteringStrength(v); }, 0.0F,
      100.0F);
    commands.AddFloat(
      Name(kHeightFogContribution), "Height Fog Contribution",
      "Share of the height fog in the aerial perspective",
      [vm](const float v) -> void { vm->SetHeightFogContribution(v); }, 0.0F,
      1.0F);
    commands.AddFloat(
      Name(kTraceSampleCountScale), "Trace Sample Scale",
      "Multiplier of the ray-march sample counts",
      [vm](const float v) -> void { vm->SetTraceSampleCountScale(v); }, 0.25F,
      8.0F);
    commands.AddFloat(
      Name(kTransmittanceMinLightElevationDeg), "Min Light Elevation",
      "Lowest light elevation the transmittance accounts for, in degrees",
      [vm](
        const float v) -> void { vm->SetTransmittanceMinLightElevationDeg(v); },
      -90.0F, 90.0F);
  }

  auto RegisterFog(PanelCommands& commands, observer_ptr<EnvironmentVm> vm)
    -> void
  {
    commands.AddBool(Name(kFogEnabled), "Height Fog", "Enable the height fog",
      [vm](const bool v) -> void { vm->SetFogEnabled(v); });
    AddChoice(commands, kFogModel, "Fog Model",
      "exponential-height fog, or volumetric fog lit by the scene", kFogModels,
      [vm](const int v) -> void { vm->SetFogModel(v); });
    commands.AddBool(Name(kFogRenderInMainPass), "Fog In Main Pass",
      "Draw the fog in the main view",
      [vm](const bool v) -> void { vm->SetFogRenderInMainPass(v); });
    commands.AddBool(Name(kFogHoldout), "Fog Holdout",
      "Render the fog as a holdout: it occludes but adds no light",
      [vm](const bool v) -> void { vm->SetFogHoldout(v); });
    commands.AddBool(Name(kFogVisibleInReflectionCaptures),
      "Fog In Reflection Captures", "Include the fog in reflection captures",
      [vm](const bool v) -> void { vm->SetFogVisibleInReflectionCaptures(v); });
    commands.AddBool(Name(kFogVisibleInRealTimeSkyCaptures),
      "Fog In Sky Captures", "Include the fog in real-time sky captures",
      [vm](
        const bool v) -> void { vm->SetFogVisibleInRealTimeSkyCaptures(v); });
    commands.AddFloat(
      Name(kFogExtinctionSigmaTPerMeter), "Fog Density",
      "Extinction coefficient at the fog height, in 1/m",
      [vm](const float v) -> void { vm->SetFogExtinctionSigmaTPerMeter(v); },
      0.0F, 10.0F);
    commands.AddFloat(
      Name(kFogHeightFalloffPerMeter), "Fog Height Falloff",
      "How fast the density falls with height, in 1/m",
      [vm](const float v) -> void { vm->SetFogHeightFalloffPerMeter(v); }, 0.0F,
      10.0F);
    commands.AddFloat(
      Name(kFogHeightOffsetMeters), "Fog Height",
      "Height of the fog's reference density, in meters",
      [vm](const float v) -> void { vm->SetFogHeightOffsetMeters(v); },
      -100000.0F, 100000.0F);
    commands.AddFloat(
      Name(kFogStartDistanceMeters), "Fog Start Distance",
      "Distance from the camera where the fog starts, in meters",
      [vm](const float v) -> void { vm->SetFogStartDistanceMeters(v); }, 0.0F,
      1000000.0F);
    commands.AddFloat(
      Name(kFogEndDistanceMeters), "Fog End Distance",
      "Distance where the fog ends, in meters; zero for none",
      [vm](const float v) -> void { vm->SetFogEndDistanceMeters(v); }, 0.0F,
      1000000.0F);
    commands.AddFloat(
      Name(kFogCutoffDistanceMeters), "Fog Cutoff Distance",
      "Distance beyond which nothing is fogged, in meters; zero for none",
      [vm](const float v) -> void { vm->SetFogCutoffDistanceMeters(v); }, 0.0F,
      1000000.0F);
    commands.AddFloat(
      Name(kFogMaxOpacity), "Fog Max Opacity",
      "Upper limit of the fog's opacity",
      [vm](const float v) -> void { vm->SetFogMaxOpacity(v); }, 0.0F, 1.0F);
    commands.AddFloat(
      Name(kSecondFogDensity), "Second Fog Density",
      "Extinction of the second fog layer, in 1/m",
      [vm](const float v) -> void { vm->SetSecondFogDensity(v); }, 0.0F, 10.0F);
    commands.AddFloat(
      Name(kSecondFogHeightFalloff), "Second Fog Height Falloff",
      "Height falloff of the second fog layer, in 1/m",
      [vm](const float v) -> void { vm->SetSecondFogHeightFalloff(v); }, 0.0F,
      10.0F);
    commands.AddFloat(
      Name(kSecondFogHeightOffset), "Second Fog Height",
      "Height of the second fog layer, in meters",
      [vm](const float v) -> void { vm->SetSecondFogHeightOffset(v); },
      -100000.0F, 100000.0F);
    commands.AddRgb(
      Name(kFogInscatteringLuminance), "Fog Inscattering",
      "Luminance the fog adds, in cd/m2",
      [vm](const glm::vec3& v) -> void { vm->SetFogInscatteringLuminance(v); },
      100000.0F);
    commands.AddRgb(
      Name(kSkyAtmosphereAmbientContributionColorScale),
      "Fog Sky Ambient Scale",
      "Linear multiplier of the sky's ambient light in the fog",
      [vm](const glm::vec3& v) -> void {
        vm->SetSkyAtmosphereAmbientContributionColorScale(v);
      },
      16.0F);
    commands.AddRgb(
      Name(kDirectionalInscatteringLuminance), "Fog Directional Inscattering",
      "Luminance the fog adds toward the sun, in cd/m2",
      [vm](const glm::vec3& v) -> void {
        vm->SetDirectionalInscatteringLuminance(v);
      },
      100000.0F);
    commands.AddFloat(
      Name(kDirectionalInscatteringExponent), "Fog Directional Exponent",
      "Sharpness of the inscattering lobe toward the sun",
      [vm](
        const float v) -> void { vm->SetDirectionalInscatteringExponent(v); },
      0.0F, 128.0F);
    commands.AddFloat(
      Name(kDirectionalInscatteringStartDistance), "Fog Directional Start",
      "Distance where the directional inscattering starts, in meters",
      [vm](const float v) -> void {
        vm->SetDirectionalInscatteringStartDistance(v);
      },
      0.0F, 1000000.0F);
  }

  auto RegisterLocalFog(PanelCommands& commands, observer_ptr<EnvironmentVm> vm)
    -> void
  {
    commands.AddAction(Name(kAddLocalFogVolume), "Add Local Fog Volume",
      "Add a local fog volume and select it",
      [vm] -> void { vm->AddLocalFogVolume(); });
    commands.AddAction(Name(kRemoveLocalFogVolume), "Remove Local Fog Volume",
      "Remove the selected local fog volume",
      [vm] -> void { vm->RemoveSelectedLocalFogVolume(); });
    commands.AddInt(
      Name(kSelectedLocalFogVolumeIndex), "Select Local Fog Volume",
      "Index of the local fog volume the other local fog commands edit",
      [vm](const int v) -> void { vm->SetSelectedLocalFogVolumeIndex(v); }, 0);
    commands.AddBool(Name(kSelectedLocalFogVolumeEnabled), "Local Fog Volume",
      "Enable the selected local fog volume",
      [vm](const bool v) -> void { vm->SetSelectedLocalFogVolumeEnabled(v); });
    commands.AddFloat(
      Name(kSelectedLocalFogVolumeRadialFogExtinction),
      "Local Fog Radial Extinction",
      "Extinction toward the volume's center, in 1/m",
      [vm](const float v) -> void {
        vm->SetSelectedLocalFogVolumeRadialFogExtinction(v);
      },
      0.0F, 10.0F);
    commands.AddFloat(
      Name(kSelectedLocalFogVolumeHeightFogExtinction),
      "Local Fog Height Extinction", "Extinction at the volume's base, in 1/m",
      [vm](const float v) -> void {
        vm->SetSelectedLocalFogVolumeHeightFogExtinction(v);
      },
      0.0F, 10.0F);
    commands.AddFloat(
      Name(kSelectedLocalFogVolumeHeightFogFalloff), "Local Fog Height Falloff",
      "How fast the density falls with height",
      [vm](const float v) -> void {
        vm->SetSelectedLocalFogVolumeHeightFogFalloff(v);
      },
      0.0F, 10.0F);
    commands.AddFloat(
      Name(kSelectedLocalFogVolumeHeightFogOffset), "Local Fog Height",
      "Height offset inside the volume, in meters",
      [vm](const float v) -> void {
        vm->SetSelectedLocalFogVolumeHeightFogOffset(v);
      },
      -1000.0F, 1000.0F);
    commands.AddFloat(
      Name(kSelectedLocalFogVolumeFogPhaseG), "Local Fog Phase",
      "Scattering anisotropy: negative back, zero even, positive forward",
      [vm](
        const float v) -> void { vm->SetSelectedLocalFogVolumeFogPhaseG(v); },
      -0.99F, 0.99F);
    commands.AddRgb(
      Name(kSelectedLocalFogVolumeFogAlbedo), "Local Fog Albedo",
      "Scattering color of the volume",
      [vm](const glm::vec3& v) -> void {
        vm->SetSelectedLocalFogVolumeFogAlbedo(v);
      },
      1.0F);
    commands.AddRgb(Name(kSelectedLocalFogVolumeFogEmissive),
      "Local Fog Emissive", "Light the volume emits",
      [vm](const glm::vec3& v) -> void {
        vm->SetSelectedLocalFogVolumeFogEmissive(v);
      });
    commands.AddInt(
      Name(kSelectedLocalFogVolumeSortPriority), "Local Fog Sort Priority",
      "Draw order among overlapping volumes",
      [vm](
        const int v) -> void { vm->SetSelectedLocalFogVolumeSortPriority(v); },
      -128, 128);
  }

  auto RegisterSky(PanelCommands& commands, observer_ptr<EnvironmentVm> vm)
    -> void
  {
    commands.AddBool(Name(kSkySphereEnabled), "Sky Sphere",
      "Draw a cubemap or a solid color behind the scene",
      [vm](const bool v) -> void { vm->SetSkySphereEnabled(v); });
    AddChoice(commands, kSkySphereSource, "Sky Sphere Source",
      "What the sky sphere shows", kSkySphereSources,
      [vm](const int v) -> void { vm->SetSkySphereSource(v); });
    commands.AddRgb(Name(kSkySphereSolidColor), "Sky Sphere Color",
      "Color of a solid-color sky sphere",
      [vm](const glm::vec3& v) -> void { vm->SetSkySphereSolidColor(v); });
    commands.AddFloat(
      Name(kSkySphereIlluminanceLux), "Sky Illuminance",
      "Illuminance the sky cubemap is scaled to, in lux; zero keeps the raw "
      "image",
      [vm](const float v) -> void { vm->SetSkySphereIlluminanceLux(v); }, 0.0F,
      200000.0F);
    commands.AddFloat(
      Name(kSkyIntensity), "Sky Radiance Scale",
      "Linear multiplier of the sky sphere's radiance",
      [vm](const float v) -> void { vm->SetSkyIntensity(v); }, 0.0F, 100000.0F);
    commands.AddFloat(
      Name(kSkySphereRotationDeg), "Sky Rotation",
      "Rotation of the sky sphere about the up axis, in degrees",
      [vm](const float v) -> void { vm->SetSkySphereRotationDeg(v); }, 0.0F,
      360.0F);

    commands.AddText(Name(kSkyboxPath), "Skybox Path",
      "Image the skybox loader cooks into a cubemap",
      [vm](const std::string& path) -> void { vm->SetSkyboxPath(path); });
    AddChoice(commands, kSkyboxLayout, "Skybox Layout",
      "Face layout of the skybox image", kSkyboxLayouts,
      [vm](const int v) -> void { vm->SetSkyboxLayoutIndex(v); });
    AddChoice(commands, kSkyboxFormat, "Skybox Format",
      "Format of the cooked cubemap", kSkyboxFormats,
      [vm](const int v) -> void { vm->SetSkyboxOutputFormatIndex(v); });
    commands.AddInt(
      Name(kSkyboxFaceSize), "Skybox Face Size",
      "Face size of the cooked cubemap, in pixels",
      [vm](const int v) -> void { vm->SetSkyboxFaceSize(v); }, 16, 4096);
    commands.AddBool(Name(kSkyboxFlipY), "Skybox Flip Y",
      "Flip the skybox image vertically",
      [vm](const bool v) -> void { vm->SetSkyboxFlipY(v); });
    commands.AddBool(Name(kSkyboxTonemapHdrToLdr), "Skybox Tonemap",
      "Tonemap an HDR skybox image to LDR",
      [vm](const bool v) -> void { vm->SetSkyboxTonemapHdrToLdr(v); });
    commands.AddFloat(
      Name(kSkyboxHdrExposureEv), "Skybox HDR Exposure",
      "Exposure in EV applied when tonemapping an HDR skybox",
      [vm](const float v) -> void { vm->SetSkyboxHdrExposureEv(v); }, 0.0F,
      16.0F);
    commands.Add({
      .name = Name(kLoadSkybox),
      .label = "Load Skybox",
      .help = "Cook an image into a cubemap and bind it to the sky sphere",
      .params = {
        CommandParam {
          .name = "path",
          .type = ParamType::kPath,
          .help = "Image file",
          .required = true,
        },
        CommandParam {
          .name = "layout",
          .type = ParamType::kEnum,
          .help = "Face layout of the image",
          .required = true,
          .allowed_values = Names(kSkyboxLayouts),
        },
        CommandParam {
          .name = "format",
          .type = ParamType::kEnum,
          .help = "Format of the cooked cubemap",
          .required = true,
          .allowed_values = Names(kSkyboxFormats),
        },
        CommandParam {
          .name = "face_size",
          .type = ParamType::kInt,
          .help = "Face size in pixels",
          .required = true,
          .min_value = 16.0,
          .max_value = 4096.0,
        },
        CommandParam {
          .name = "flip_y",
          .type = ParamType::kBool,
          .help = "Flip vertically",
          .required = true,
        },
        CommandParam {
          .name = "tonemap",
          .type = ParamType::kBool,
          .help = "Tonemap HDR sources to LDR",
          .required = true,
        },
        Number("exposure_ev", "HDR exposure in EV", 0.0, 16.0),
      },
      .handler = [vm](const CommandArgs& args,
                   const CommandContext&) -> CommandResult {
        const auto& path = args.GetString("path");
        vm->LoadSkybox(path, IndexOf(kSkyboxLayouts, args.GetString("layout")),
          IndexOf(kSkyboxFormats, args.GetString("format")),
          static_cast<int>(args.GetInt("face_size")), args.GetBool("flip_y"),
          args.GetBool("tonemap"), Float(args, "exposure_ev"));
        return CommandResult::Success(fmt::format("loading skybox {}", path));
      },
    });
  }

  auto RegisterSkyLight(PanelCommands& commands, observer_ptr<EnvironmentVm> vm)
    -> void
  {
    commands.AddBool(Name(kSkyLightEnabled), "Sky Light",
      "Light the scene with the sky",
      [vm](const bool v) -> void { vm->SetSkyLightEnabled(v); });
    AddChoice(commands, kSkyLightSource, "Sky Light Source",
      "Capture the scene's sky, or use the specified cubemap", kSkyLightSources,
      [vm](const int v) -> void { vm->SetSkyLightSource(v); });
    commands.AddFloat(
      Name(kSkyLightIntensityMul), "Sky Light Intensity",
      "Multiplier of the sky light",
      [vm](const float v) -> void { vm->SetSkyLightIntensityMul(v); }, 0.0F,
      100000.0F);
    commands.AddFloat(
      Name(kSkyLightIlluminanceLux), "Sky Light Cubemap Illuminance",
      "Illuminance a specified cubemap is scaled to, in lux; zero keeps the "
      "raw image",
      [vm](const float v) -> void { vm->SetSkyLightIlluminanceLux(v); }, 0.0F,
      200000.0F);
    commands.AddRgb(Name(kSkyLightTint), "Sky Light Tint",
      "Linear tint of the sky light",
      [vm](const glm::vec3& v) -> void { vm->SetSkyLightTint(v); });
    commands.AddFloat(
      Name(kSkyLightDiffuse), "Sky Light Diffuse",
      "Multiplier of the diffuse indirect light",
      [vm](const float v) -> void { vm->SetSkyLightDiffuse(v); }, 0.0F, 6.0F);
    commands.AddFloat(
      Name(kSkyLightSpecular), "Sky Light Specular",
      "Multiplier of the specular indirect light",
      [vm](const float v) -> void { vm->SetSkyLightSpecular(v); }, 0.0F, 6.0F);
    commands.AddBool(Name(kSkyLightAffectReflections), "Sky Light Reflections",
      "Let the sky light reach reflections",
      [vm](const bool v) -> void { vm->SetSkyLightAffectReflections(v); });
    commands.AddBool(Name(kSkyLightLowerHemisphereOverride),
      "Sky Light Lower Hemisphere",
      "Replace the lower hemisphere of the sky light with a solid color",
      [vm](
        const bool v) -> void { vm->SetSkyLightLowerHemisphereOverride(v); });
    commands.AddRgb(Name(kSkyLightLowerHemisphereColor),
      "Sky Light Lower Hemisphere Color",
      "Color of the overridden lower hemisphere",
      [vm](const glm::vec3& v) -> void {
        vm->SetSkyLightLowerHemisphereColor(v);
      });
    commands.AddFloat(
      Name(kSkyLightLowerHemisphereBlend), "Sky Light Lower Hemisphere Blend",
      "Blend between the sky and the lower hemisphere color",
      [vm](const float v) -> void { vm->SetSkyLightLowerHemisphereBlend(v); },
      0.0F, 1.0F);
    commands.AddFloat(
      Name(kSkyLightVolumetricScatteringIntensity),
      "Sky Light Volumetric Scattering",
      "Multiplier of the sky light in volumetric fog",
      [vm](const float v) -> void {
        vm->SetSkyLightVolumetricScatteringIntensity(v);
      },
      0.0F, 4.0F);
  }

} // namespace

auto RegisterEnvironmentCommands(
  PanelCommands& commands, observer_ptr<EnvironmentVm> vm) -> void
{
  RegisterProfileAndSun(commands, vm);
  RegisterSunShadows(commands, vm);
  RegisterAtmosphere(commands, vm);
  RegisterFog(commands, vm);
  RegisterLocalFog(commands, vm);
  RegisterSky(commands, vm);
  RegisterSkyLight(commands, vm);
}

} // namespace oxygen::examples::ui

// NOLINTEND(*-magic-numbers)
