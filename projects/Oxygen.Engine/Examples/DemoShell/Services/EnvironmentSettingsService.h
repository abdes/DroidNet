//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Scene/SceneNode.h>

#include <glm/vec3.hpp>

#include <Oxygen/Content/ResourceKey.h>
#include <Oxygen/Core/Types/Atmosphere.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Scene/Environment/Sun.h>
#include <Oxygen/Scene/Light/DirectionalLight.h>
#include <Oxygen/Scene/Light/LightCommon.h>

#include "DemoShell/Services/DomainService.h"

namespace oxygen {
namespace vortex {
  class Renderer;
}
namespace data {
  class SceneAsset;
}
namespace scene {
  class DirectionalLight;
  class Scene;
  class SceneEnvironment;
}
} // namespace oxygen

namespace oxygen::examples {
class SettingsService;
class SkyboxService;

//! Runtime dependencies for environment settings application.
struct EnvironmentRuntimeConfig {
  observer_ptr<scene::Scene> scene { nullptr };
  observer_ptr<SkyboxService> skybox_service { nullptr };
  observer_ptr<vortex::Renderer> renderer { nullptr };
  std::function<void()> on_atmosphere_params_changed;
  std::function<void()> on_exposure_changed;
};

//! Settings persistence and runtime apply logic for the environment panel.
/*!
 Owns environment UI settings, persists them via `SettingsService`, and
 applies them to the active scene and renderer.

### Key Features

- **Persistent settings**: Reads and writes through SettingsService.
- **Runtime apply**: Pushes settings into scene environment systems.
- **Lazy configuration**: Accepts runtime dependencies at any time.

 @see SettingsService
*/
class EnvironmentSettingsService : public DomainService {
public:
  EnvironmentSettingsService() = default;
  virtual ~EnvironmentSettingsService() = default;

  OXYGEN_MAKE_NON_COPYABLE(EnvironmentSettingsService)
  OXYGEN_MAKE_NON_MOVABLE(EnvironmentSettingsService)

  //! Hydrate runtime environment systems from a scene asset.
  static auto HydrateEnvironment(scene::SceneEnvironment& target,
    const data::SceneAsset& source_asset) -> void;

  //! Updates the runtime configuration used for applying settings.
  virtual auto SetRuntimeConfig(const EnvironmentRuntimeConfig& config) -> void;

  //! Returns true when a scene is available.
  [[nodiscard]] virtual auto HasScene() const noexcept -> bool;

  //! Requests a resync from the current scene.
  virtual auto RequestResync() -> void;

  //! Syncs cached values from the scene if needed.
  virtual auto SyncFromSceneIfNeeded() -> void;

  //! Returns true if there are pending changes to apply.
  [[nodiscard]] virtual auto HasPendingChanges() const -> bool;

  //! Applies pending changes to the current scene.
  virtual auto ApplyPendingChanges() -> void;

  //! Starts a batch update of settings.
  virtual auto BeginUpdate() -> void;

  //! Ends a batch update of settings.
  virtual auto EndUpdate() -> void;

  //! Gets the index of the currently active preset.
  [[nodiscard]] virtual auto GetPresetIndex() const -> int;

  //! Sets the index of the currently active preset.
  virtual auto SetPresetIndex(int index) -> void;
  virtual auto ActivateUseSceneMode() -> void;
  virtual auto ActivateCustomMode() -> void;

  //! Returns whether LUTs are valid and dirty (renderer state).
  [[nodiscard]] virtual auto GetAtmosphereLutStatus() const
    -> std::pair<bool, bool>;

  //! Returns the current settings epoch.
  [[nodiscard]] auto GetEpoch() const noexcept -> std::uint64_t override;

  auto OnFrameStart(const engine::FrameContext& context) -> void override;
  auto OnSceneActivated(scene::Scene& scene) -> void override;
  auto OnMainViewReady(const engine::FrameContext& context,
    const vortex::CompositionView& view) -> void override;
  auto OnRuntimeMainViewReady(ViewId view_id) -> void;

  // SkyAtmosphere
  [[nodiscard]] virtual auto GetSkyAtmosphereEnabled() const -> bool;
  virtual auto SetSkyAtmosphereEnabled(bool enabled) -> void;
  [[nodiscard]] virtual auto GetSkyAtmosphereTransformMode() const -> int;
  virtual auto SetSkyAtmosphereTransformMode(int value) -> void;

  [[nodiscard]] virtual auto GetPlanetRadiusKm() const -> float;
  virtual auto SetPlanetRadiusKm(float value) -> void;

  [[nodiscard]] virtual auto GetAtmosphereHeightKm() const -> float;
  virtual auto SetAtmosphereHeightKm(float value) -> void;

  [[nodiscard]] virtual auto GetGroundAlbedo() const -> glm::vec3;
  virtual auto SetGroundAlbedo(const glm::vec3& value) -> void;

  [[nodiscard]] virtual auto GetRayleighScaleHeightKm() const -> float;
  virtual auto SetRayleighScaleHeightKm(float value) -> void;

  [[nodiscard]] virtual auto GetMieScaleHeightKm() const -> float;
  virtual auto SetMieScaleHeightKm(float value) -> void;

  [[nodiscard]] virtual auto GetMieAnisotropy() const -> float;
  virtual auto SetMieAnisotropy(float value) -> void;

  //! Gets Mie absorption scale (multiplier on default Earth aerosol
  //! absorption).
  [[nodiscard]] virtual auto GetMieAbsorptionScale() const -> float;
  //! Sets Mie absorption scale (1.0 = default Earth absorption, 0 = no
  //! absorption).
  virtual auto SetMieAbsorptionScale(float value) -> void;

  [[nodiscard]] virtual auto GetMultiScattering() const -> float;
  virtual auto SetMultiScattering(float value) -> void;
  [[nodiscard]] virtual auto GetSkyLuminanceFactor() const -> glm::vec3;
  virtual auto SetSkyLuminanceFactor(const glm::vec3& value) -> void;
  [[nodiscard]] virtual auto GetSkyAndAerialPerspectiveLuminanceFactor() const
    -> glm::vec3;
  virtual auto SetSkyAndAerialPerspectiveLuminanceFactor(const glm::vec3& value)
    -> void;

  // Absorption
  // Ozone Profile
  [[nodiscard]] virtual auto GetOzoneRgb() const -> glm::vec3;
  virtual auto SetOzoneRgb(const glm::vec3& value) -> void;

  [[nodiscard]] virtual auto GetOzoneDensityProfile() const
    -> engine::atmos::DensityProfile;
  virtual auto SetOzoneDensityProfile(
    const engine::atmos::DensityProfile& profile) -> void;

  [[nodiscard]] virtual auto GetSunDiskEnabled() const -> bool;
  virtual auto SetSunDiskEnabled(bool enabled) -> void;

  [[nodiscard]] virtual auto GetAerialPerspectiveScale() const -> float;
  virtual auto SetAerialPerspectiveScale(float value) -> void;
  [[nodiscard]] virtual auto GetAerialPerspectiveStartDepthMeters() const
    -> float;
  virtual auto SetAerialPerspectiveStartDepthMeters(float value) -> void;

  [[nodiscard]] virtual auto GetAerialScatteringStrength() const -> float;
  virtual auto SetAerialScatteringStrength(float value) -> void;
  [[nodiscard]] virtual auto GetHeightFogContribution() const -> float;
  virtual auto SetHeightFogContribution(float value) -> void;
  [[nodiscard]] virtual auto GetTraceSampleCountScale() const -> float;
  virtual auto SetTraceSampleCountScale(float value) -> void;
  [[nodiscard]] virtual auto GetTransmittanceMinLightElevationDeg() const
    -> float;
  virtual auto SetTransmittanceMinLightElevationDeg(float value) -> void;
  [[nodiscard]] virtual auto GetAtmosphereHoldout() const -> bool;
  virtual auto SetAtmosphereHoldout(bool enabled) -> void;
  [[nodiscard]] virtual auto GetAtmosphereRenderInMainPass() const -> bool;
  virtual auto SetAtmosphereRenderInMainPass(bool enabled) -> void;

  // Sky-View LUT slicing
  [[nodiscard]] virtual auto GetSkyViewLutSlices() const -> int;
  virtual auto SetSkyViewLutSlices(int value) -> void;

  [[nodiscard]] virtual auto GetSkyViewAltMappingMode() const -> int;
  virtual auto SetSkyViewAltMappingMode(int value) -> void;

  //! Requests that the sky-view LUT be regenerated on the next frame.
  virtual auto RequestRegenerateLut() -> void;

  // SkySphere
  [[nodiscard]] virtual auto GetSkySphereEnabled() const -> bool;
  virtual auto SetSkySphereEnabled(bool enabled) -> void;

  [[nodiscard]] virtual auto GetSkySphereSource() const -> int;
  virtual auto SetSkySphereSource(int source) -> void;

  [[nodiscard]] virtual auto GetSkySphereSolidColor() const -> glm::vec3;
  virtual auto SetSkySphereSolidColor(const glm::vec3& value) -> void;

  [[nodiscard]] virtual auto GetSkyIntensity() const -> float;
  virtual auto SetSkyIntensity(float value) -> void;

  [[nodiscard]] virtual auto GetSkySphereRotationDeg() const -> float;
  virtual auto SetSkySphereRotationDeg(float value) -> void;

  // Skybox
  [[nodiscard]] virtual auto GetSkyboxPath() const -> std::string;
  virtual auto SetSkyboxPath(std::string_view path) -> void;

  [[nodiscard]] virtual auto GetSkyboxLayoutIndex() const -> int;
  virtual auto SetSkyboxLayoutIndex(int index) -> void;

  [[nodiscard]] virtual auto GetSkyboxOutputFormatIndex() const -> int;
  virtual auto SetSkyboxOutputFormatIndex(int index) -> void;

  [[nodiscard]] virtual auto GetSkyboxFaceSize() const -> int;
  virtual auto SetSkyboxFaceSize(int size) -> void;

  [[nodiscard]] virtual auto GetSkyboxFlipY() const -> bool;
  virtual auto SetSkyboxFlipY(bool flip) -> void;

  [[nodiscard]] virtual auto GetSkyboxTonemapHdrToLdr() const -> bool;
  virtual auto SetSkyboxTonemapHdrToLdr(bool enabled) -> void;

  [[nodiscard]] virtual auto GetSkyboxHdrExposureEv() const -> float;
  virtual auto SetSkyboxHdrExposureEv(float value) -> void;

  [[nodiscard]] virtual auto GetSkyboxStatusMessage() const -> std::string_view;
  [[nodiscard]] virtual auto GetSkyboxLastFaceSize() const -> int;
  [[nodiscard]] virtual auto GetSkyboxLastResourceKey() const
    -> content::ResourceKey;

  virtual auto LoadSkybox(std::string_view path, int layout_index,
    int output_format_index, int face_size, bool flip_y,
    bool tonemap_hdr_to_ldr, float hdr_exposure_ev) -> void;

  // SkyLight
  [[nodiscard]] virtual auto GetSkyLightEnabled() const -> bool;
  virtual auto SetSkyLightEnabled(bool enabled) -> void;

  [[nodiscard]] virtual auto GetSkyLightSource() const -> int;
  virtual auto SetSkyLightSource(int source) -> void;

  [[nodiscard]] virtual auto GetSkyLightTint() const -> glm::vec3;
  virtual auto SetSkyLightTint(const glm::vec3& value) -> void;

  [[nodiscard]] virtual auto GetSkyLightIntensityMul() const -> float;
  virtual auto SetSkyLightIntensityMul(float value) -> void;

  [[nodiscard]] virtual auto GetSkyLightDiffuse() const -> float;
  virtual auto SetSkyLightDiffuse(float value) -> void;

  [[nodiscard]] virtual auto GetSkyLightSpecular() const -> float;
  virtual auto SetSkyLightSpecular(float value) -> void;
  [[nodiscard]] virtual auto GetSkyLightRealTimeCaptureEnabled() const -> bool;
  virtual auto SetSkyLightRealTimeCaptureEnabled(bool enabled) -> void;
  [[nodiscard]] virtual auto GetSkyLightLowerHemisphereColor() const
    -> glm::vec3;
  virtual auto SetSkyLightLowerHemisphereColor(const glm::vec3& value) -> void;
  [[nodiscard]] virtual auto GetSkyLightVolumetricScatteringIntensity() const
    -> float;
  virtual auto SetSkyLightVolumetricScatteringIntensity(float value) -> void;
  [[nodiscard]] virtual auto GetSkyLightAffectReflections() const -> bool;
  virtual auto SetSkyLightAffectReflections(bool enabled) -> void;
  [[nodiscard]] virtual auto GetSkyLightAffectGlobalIllumination() const
    -> bool;
  virtual auto SetSkyLightAffectGlobalIllumination(bool enabled) -> void;

  // Fog
  [[nodiscard]] virtual auto GetFogEnabled() const -> bool;
  virtual auto SetFogEnabled(bool enabled) -> void;

  [[nodiscard]] virtual auto GetFogModel() const -> int;
  virtual auto SetFogModel(int model) -> void;

  [[nodiscard]] virtual auto GetFogExtinctionSigmaTPerMeter() const -> float;
  virtual auto SetFogExtinctionSigmaTPerMeter(float value) -> void;

  [[nodiscard]] virtual auto GetFogHeightFalloffPerMeter() const -> float;
  virtual auto SetFogHeightFalloffPerMeter(float value) -> void;

  [[nodiscard]] virtual auto GetFogHeightOffsetMeters() const -> float;
  virtual auto SetFogHeightOffsetMeters(float value) -> void;

  [[nodiscard]] virtual auto GetFogStartDistanceMeters() const -> float;
  virtual auto SetFogStartDistanceMeters(float value) -> void;

  [[nodiscard]] virtual auto GetSecondFogDensity() const -> float;
  virtual auto SetSecondFogDensity(float value) -> void;
  [[nodiscard]] virtual auto GetSecondFogHeightFalloff() const -> float;
  virtual auto SetSecondFogHeightFalloff(float value) -> void;
  [[nodiscard]] virtual auto GetSecondFogHeightOffset() const -> float;
  virtual auto SetSecondFogHeightOffset(float value) -> void;

  [[nodiscard]] virtual auto GetFogMaxOpacity() const -> float;
  virtual auto SetFogMaxOpacity(float value) -> void;

  [[nodiscard]] virtual auto GetFogSingleScatteringAlbedoRgb() const
    -> glm::vec3;
  virtual auto SetFogSingleScatteringAlbedoRgb(const glm::vec3& value) -> void;
  [[nodiscard]] virtual auto GetFogInscatteringLuminance() const -> glm::vec3;
  virtual auto SetFogInscatteringLuminance(const glm::vec3& value) -> void;
  [[nodiscard]] virtual auto
  GetSkyAtmosphereAmbientContributionColorScale() const -> glm::vec3;
  virtual auto SetSkyAtmosphereAmbientContributionColorScale(
    const glm::vec3& value) -> void;
  [[nodiscard]] virtual auto GetInscatteringColorCubemapAngle() const -> float;
  virtual auto SetInscatteringColorCubemapAngle(float value) -> void;
  [[nodiscard]] virtual auto GetInscatteringTextureTint() const -> glm::vec3;
  virtual auto SetInscatteringTextureTint(const glm::vec3& value) -> void;
  [[nodiscard]] virtual auto
  GetFullyDirectionalInscatteringColorDistance() const -> float;
  virtual auto SetFullyDirectionalInscatteringColorDistance(float value)
    -> void;
  [[nodiscard]] virtual auto GetNonDirectionalInscatteringColorDistance() const
    -> float;
  virtual auto SetNonDirectionalInscatteringColorDistance(float value) -> void;
  [[nodiscard]] virtual auto GetDirectionalInscatteringLuminance() const
    -> glm::vec3;
  virtual auto SetDirectionalInscatteringLuminance(const glm::vec3& value)
    -> void;
  [[nodiscard]] virtual auto GetDirectionalInscatteringExponent() const
    -> float;
  virtual auto SetDirectionalInscatteringExponent(float value) -> void;
  [[nodiscard]] virtual auto GetDirectionalInscatteringStartDistance() const
    -> float;
  virtual auto SetDirectionalInscatteringStartDistance(float value) -> void;
  [[nodiscard]] virtual auto GetFogEndDistanceMeters() const -> float;
  virtual auto SetFogEndDistanceMeters(float value) -> void;
  [[nodiscard]] virtual auto GetFogCutoffDistanceMeters() const -> float;
  virtual auto SetFogCutoffDistanceMeters(float value) -> void;
  [[nodiscard]] virtual auto GetVolumetricFogScatteringDistribution() const
    -> float;
  virtual auto SetVolumetricFogScatteringDistribution(float value) -> void;
  [[nodiscard]] virtual auto GetVolumetricFogAlbedo() const -> glm::vec3;
  virtual auto SetVolumetricFogAlbedo(const glm::vec3& value) -> void;
  [[nodiscard]] virtual auto GetVolumetricFogEmissive() const -> glm::vec3;
  virtual auto SetVolumetricFogEmissive(const glm::vec3& value) -> void;
  [[nodiscard]] virtual auto GetVolumetricFogExtinctionScale() const -> float;
  virtual auto SetVolumetricFogExtinctionScale(float value) -> void;
  [[nodiscard]] virtual auto GetVolumetricFogDistanceMeters() const -> float;
  virtual auto SetVolumetricFogDistanceMeters(float value) -> void;
  [[nodiscard]] virtual auto GetVolumetricFogStartDistanceMeters() const
    -> float;
  virtual auto SetVolumetricFogStartDistanceMeters(float value) -> void;
  [[nodiscard]] virtual auto GetVolumetricFogNearFadeInDistanceMeters() const
    -> float;
  virtual auto SetVolumetricFogNearFadeInDistanceMeters(float value) -> void;
  [[nodiscard]] virtual auto
  GetVolumetricFogStaticLightingScatteringIntensity() const -> float;
  virtual auto SetVolumetricFogStaticLightingScatteringIntensity(float value)
    -> void;
  [[nodiscard]] virtual auto
  GetOverrideLightColorsWithFogInscatteringColors() const -> bool;
  virtual auto SetOverrideLightColorsWithFogInscatteringColors(bool enabled)
    -> void;
  [[nodiscard]] virtual auto GetFogHoldout() const -> bool;
  virtual auto SetFogHoldout(bool enabled) -> void;
  [[nodiscard]] virtual auto GetFogRenderInMainPass() const -> bool;
  virtual auto SetFogRenderInMainPass(bool enabled) -> void;
  [[nodiscard]] virtual auto GetFogVisibleInReflectionCaptures() const -> bool;
  virtual auto SetFogVisibleInReflectionCaptures(bool enabled) -> void;
  [[nodiscard]] virtual auto GetFogVisibleInRealTimeSkyCaptures() const -> bool;
  virtual auto SetFogVisibleInRealTimeSkyCaptures(bool enabled) -> void;

  // Local fog volumes
  [[nodiscard]] virtual auto GetLocalFogVolumeCount() const -> int;
  [[nodiscard]] virtual auto GetSelectedLocalFogVolumeIndex() const -> int;
  virtual auto SetSelectedLocalFogVolumeIndex(int index) -> void;
  virtual auto AddLocalFogVolume() -> void;
  virtual auto RemoveSelectedLocalFogVolume() -> void;
  [[nodiscard]] virtual auto GetSelectedLocalFogVolumeEnabled() const -> bool;
  virtual auto SetSelectedLocalFogVolumeEnabled(bool enabled) -> void;
  [[nodiscard]] virtual auto
  GetSelectedLocalFogVolumeRadialFogExtinction() const -> float;
  virtual auto SetSelectedLocalFogVolumeRadialFogExtinction(float value)
    -> void;
  [[nodiscard]] virtual auto
  GetSelectedLocalFogVolumeHeightFogExtinction() const -> float;
  virtual auto SetSelectedLocalFogVolumeHeightFogExtinction(float value)
    -> void;
  [[nodiscard]] virtual auto GetSelectedLocalFogVolumeHeightFogFalloff() const
    -> float;
  virtual auto SetSelectedLocalFogVolumeHeightFogFalloff(float value) -> void;
  [[nodiscard]] virtual auto GetSelectedLocalFogVolumeHeightFogOffset() const
    -> float;
  virtual auto SetSelectedLocalFogVolumeHeightFogOffset(float value) -> void;
  [[nodiscard]] virtual auto GetSelectedLocalFogVolumeFogPhaseG() const
    -> float;
  virtual auto SetSelectedLocalFogVolumeFogPhaseG(float value) -> void;
  [[nodiscard]] virtual auto GetSelectedLocalFogVolumeFogAlbedo() const
    -> glm::vec3;
  virtual auto SetSelectedLocalFogVolumeFogAlbedo(const glm::vec3& value)
    -> void;
  [[nodiscard]] virtual auto GetSelectedLocalFogVolumeFogEmissive() const
    -> glm::vec3;
  virtual auto SetSelectedLocalFogVolumeFogEmissive(const glm::vec3& value)
    -> void;
  [[nodiscard]] virtual auto GetSelectedLocalFogVolumeSortPriority() const
    -> int;
  virtual auto SetSelectedLocalFogVolumeSortPriority(int value) -> void;

  // Sun
  [[nodiscard]] virtual auto GetSunPresent() const -> bool;
  [[nodiscard]] virtual auto GetSunEnabled() const -> bool;
  virtual auto SetSunEnabled(bool enabled) -> void;

  [[nodiscard]] virtual auto GetSunAzimuthDeg() const -> float;
  virtual auto SetSunAzimuthDeg(float value) -> void;

  [[nodiscard]] virtual auto GetSunElevationDeg() const -> float;
  virtual auto SetSunElevationDeg(float value) -> void;

  [[nodiscard]] virtual auto GetSunColorRgb() const -> glm::vec3;
  virtual auto SetSunColorRgb(const glm::vec3& value) -> void;

  [[nodiscard]] virtual auto GetSunIlluminanceLx() const -> float;
  virtual auto SetSunIlluminanceLx(float value) -> void;

  [[nodiscard]] virtual auto GetSunUseTemperature() const -> bool;
  virtual auto SetSunUseTemperature(bool enabled) -> void;

  [[nodiscard]] virtual auto GetSunTemperatureKelvin() const -> float;
  virtual auto SetSunTemperatureKelvin(float value) -> void;

  [[nodiscard]] virtual auto GetSunDiskRadiusDeg() const -> float;
  virtual auto SetSunDiskRadiusDeg(float value) -> void;
  [[nodiscard]] virtual auto GetSunAtmosphereLightSlot() const -> int;
  virtual auto SetSunAtmosphereLightSlot(int value) -> void;
  [[nodiscard]] virtual auto GetSunUsePerPixelAtmosphereTransmittance() const
    -> bool;
  virtual auto SetSunUsePerPixelAtmosphereTransmittance(bool enabled) -> void;
  [[nodiscard]] virtual auto GetSunAtmosphereDiskLuminanceScale() const
    -> glm::vec3;
  virtual auto SetSunAtmosphereDiskLuminanceScale(const glm::vec3& value)
    -> void;

  [[nodiscard]] virtual auto GetSunShadowBias() const -> float;
  virtual auto SetSunShadowBias(float value) -> void;

  [[nodiscard]] virtual auto GetSunShadowNormalBias() const -> float;
  virtual auto SetSunShadowNormalBias(float value) -> void;

  [[nodiscard]] virtual auto GetSunShadowResolutionHint() const -> int;
  virtual auto SetSunShadowResolutionHint(int value) -> void;

  [[nodiscard]] virtual auto GetSunShadowCascadeCount() const -> int;
  virtual auto SetSunShadowCascadeCount(int value) -> void;

  [[nodiscard]] virtual auto GetSunShadowSplitMode() const -> int;
  virtual auto SetSunShadowSplitMode(int value) -> void;

  [[nodiscard]] virtual auto GetSunShadowMaxDistance() const -> float;
  virtual auto SetSunShadowMaxDistance(float value) -> void;

  [[nodiscard]] virtual auto GetSunShadowDistributionExponent() const -> float;
  virtual auto SetSunShadowDistributionExponent(float value) -> void;

  [[nodiscard]] virtual auto GetSunShadowTransitionFraction() const -> float;
  virtual auto SetSunShadowTransitionFraction(float value) -> void;

  [[nodiscard]] virtual auto GetSunShadowDistanceFadeoutFraction() const
    -> float;
  virtual auto SetSunShadowDistanceFadeoutFraction(float value) -> void;

  [[nodiscard]] virtual auto GetSunShadowCascadeDistance(int index) const
    -> float;
  virtual auto SetSunShadowCascadeDistance(int index, float value) -> void;

  [[nodiscard]] virtual auto GetSunLightAvailable() const -> bool;
  virtual auto UpdateSunLightCandidate() -> void;

  // Renderer debug flags
  [[nodiscard]] virtual auto GetUseLut() const -> bool;
  virtual auto SetUseLut(bool enabled) -> void;

private:
  struct AtmosphereCanonicalState {
    bool enabled { false };
    int transform_mode { 0 };
    float planet_radius_km { 0.0F };
    float atmosphere_height_km { 0.0F };
    glm::vec3 ground_albedo { 0.0F };
    float rayleigh_scale_height_km { 0.0F };
    float mie_scale_height_km { 0.0F };
    float mie_anisotropy { 0.0F };
    float mie_absorption_scale { 0.0F };
    float multi_scattering { 0.0F };
    glm::vec3 sky_luminance_factor { 1.0F, 1.0F, 1.0F };
    glm::vec3 sky_and_aerial_luminance_factor { 1.0F, 1.0F, 1.0F };
    glm::vec3 ozone_rgb { 0.0F };
    engine::atmos::DensityProfile ozone_profile {};
    bool sun_disk_enabled { false };
    float aerial_perspective_scale { 0.0F };
    float aerial_perspective_start_depth_m { 100.0F };
    float aerial_scattering_strength { 0.0F };
    float height_fog_contribution { 1.0F };
    float trace_sample_count_scale { 1.0F };
    float transmittance_min_light_elevation_deg { -6.0F };
    bool holdout { false };
    bool render_in_main_pass { true };
  };

  struct LocalFogVolumeUiState {
    scene::SceneNode node {};
    bool enabled { true };
    float radial_fog_extinction { 1.0F };
    float height_fog_extinction { 1.0F };
    float height_fog_falloff { 1000.0F };
    float height_fog_offset { 0.0F };
    float fog_phase_g { 0.2F };
    glm::vec3 fog_albedo { 1.0F, 1.0F, 1.0F };
    glm::vec3 fog_emissive { 0.0F, 0.0F, 0.0F };
    int sort_priority { 0 };
  };

  enum class DirtyDomain : uint32_t {
    kNone = 0u,
    kAtmosphereModel = 1u << 0u,
    kAtmosphereLights = 1u << 1u,
    kHeightFog = 1u << 2u,
    kVolumetricFog = 1u << 3u,
    kLocalFogVolumes = 1u << 4u,
    kSkyLight = 1u << 5u,
    kSkybox = 1u << 6u,
    kSkySphere = 1u << 7u,
    kSun = 1u << 8u,
    kRendererFlags = 1u << 9u,
    kPreset = 1u << 10u,
    kAtmosphere = kAtmosphereModel,
    kFog = kHeightFog,
    kAll = 0xFFFFFFFFu,
  };

  static constexpr auto ToMask(DirtyDomain domain) noexcept -> uint32_t
  {
    return static_cast<uint32_t>(domain);
  }
  static constexpr auto HasDirty(
    const uint32_t mask, const DirtyDomain domain) noexcept -> bool
  {
    return (mask & ToMask(domain)) != 0U;
  }

  auto SyncFromScene() -> void;
  auto LoadSettings() -> void;
  auto SaveSettings() const -> void;
  auto PersistSettingsIfDirty() -> void;
  auto ValidateAndClampState() -> void;
  auto MarkDirty(uint32_t dirty_domains = ToMask(DirtyDomain::kAll)) -> void;
  auto NormalizeSkySystems() -> void;
  auto MaybeAutoLoadSkybox() -> void;
  auto SyncLocalFogVolumesFromScene() -> void;
  auto GetSelectedLocalFogVolumeMutable() -> LocalFogVolumeUiState*;
  [[nodiscard]] auto GetSelectedLocalFogVolume() const
    -> const LocalFogVolumeUiState*;
  auto ResetSunUiToDefaults() -> void;
  auto EnsureSceneHasSunAtActivation() -> void;
  auto FindSunLightCandidate() const -> std::optional<scene::SceneNode>;
  auto CaptureSunShadowSettingsFromLight(const scene::DirectionalLight& light)
    -> void;
  auto ApplySunShadowSettingsToLight(scene::DirectionalLight& light) const
    -> void;
  [[nodiscard]] auto CaptureAtmosphereCanonicalState() const
    -> AtmosphereCanonicalState;
  [[nodiscard]] auto CaptureSceneAtmosphereCanonicalState() const
    -> std::optional<AtmosphereCanonicalState>;
  [[nodiscard]] static auto HashAtmosphereState(
    const AtmosphereCanonicalState& state) -> std::uint64_t;

  static constexpr float kDefaultPlanetRadiusKm
    = engine::atmos::kDefaultPlanetRadiusM * 0.001F;
  static constexpr float kDefaultAtmosphereHeightKm
    = engine::atmos::kDefaultAtmosphereHeightM * 0.001F;

  EnvironmentRuntimeConfig config_ {};

  int update_depth_ { 0 };
  bool settings_loaded_ { false };
  bool has_persisted_settings_ { false };
  bool settings_persist_dirty_ { false };
  bool pending_changes_ { false };
  bool applied_changes_this_frame_ { false };
  bool needs_sky_capture_ { false };
  bool needs_sync_ { true };
  bool force_scene_rebind_ { false };
  uint32_t dirty_domains_ { ToMask(DirtyDomain::kAll) };
  uint32_t batched_dirty_domains_ { ToMask(DirtyDomain::kNone) };
  std::uint64_t settings_revision_ { 0 };
  std::uint64_t last_persisted_settings_revision_ { 0 };
  std::optional<ViewId> main_view_id_ {};

  // SkyAtmosphere
  bool sky_atmo_enabled_ { false };
  int sky_atmo_transform_mode_ { 0 };
  float planet_radius_km_ { kDefaultPlanetRadiusKm };
  float atmosphere_height_km_ { kDefaultAtmosphereHeightKm };
  glm::vec3 ground_albedo_ { 0.1F, 0.1F, 0.1F };
  float rayleigh_scale_height_km_ { 8.0F };
  float mie_scale_height_km_ { 1.2F };
  float mie_anisotropy_ { 0.8F };
  float mie_absorption_scale_ {
    1.0F
  }; // 1.0 = Earth-like absorption (SSA ≈ 0.9)
  float multi_scattering_ { 1.0F };
  glm::vec3 sky_luminance_factor_ { 1.0F, 1.0F, 1.0F };
  glm::vec3 sky_and_aerial_perspective_luminance_factor_ {
    1.0F,
    1.0F,
    1.0F,
  };

  // New Ozone Profile (2-layer)
  glm::vec3 ozone_rgb_ { engine::atmos::kDefaultOzoneAbsorptionRgb };
  engine::atmos::DensityProfile ozone_profile_ {
    engine::atmos::kDefaultOzoneDensityProfile
  };
  bool sun_disk_enabled_ { true };
  float aerial_perspective_scale_ { 1.0F };
  float aerial_perspective_start_depth_m_ { 100.0F };
  float aerial_scattering_strength_ { 1.0F };
  float height_fog_contribution_ { 1.0F };
  float trace_sample_count_scale_ { 1.0F };
  float transmittance_min_light_elevation_deg_ { -6.0F };
  bool atmosphere_holdout_ { false };
  bool atmosphere_render_in_main_pass_ { true };
  int sky_view_lut_slices_ { 16 };
  int sky_view_alt_mapping_mode_ { 1 }; // 0 = linear, 1 = log
  bool regenerate_lut_requested_ { false };

  // SkySphere
  bool sky_sphere_enabled_ { false };
  int sky_sphere_source_ { 0 };
  glm::vec3 sky_sphere_solid_color_ { 0.2F, 0.3F, 0.5F };
  float sky_intensity_ { 1.0F };
  float sky_sphere_rotation_deg_ { 0.0F };

  // Skybox settings
  int skybox_layout_idx_ { 0 };
  int skybox_output_format_idx_ { 0 };
  int skybox_face_size_ { 512 };
  bool skybox_flip_y_ { false };
  bool skybox_tonemap_hdr_to_ldr_ { false };
  float skybox_hdr_exposure_ev_ { 0.0F };
  std::string skybox_path_ {};
  std::string skybox_status_message_;
  int skybox_last_face_size_ { 0 };
  content::ResourceKey skybox_last_resource_key_ { 0U };
  bool skybox_dirty_ { false };
  std::string last_loaded_skybox_path_;
  int last_loaded_skybox_layout_idx_ { -1 };
  int last_loaded_skybox_output_format_idx_ { -1 };
  int last_loaded_skybox_face_size_ { 0 };
  bool last_loaded_skybox_flip_y_ { false };
  bool last_loaded_skybox_tonemap_hdr_to_ldr_ { false };
  float last_loaded_skybox_hdr_exposure_ev_ { 0.0F };

  // SkyLight
  bool sky_light_enabled_ { false };
  int sky_light_source_ { 0 };
  glm::vec3 sky_light_tint_ { 1.0F, 1.0F, 1.0F };
  float sky_light_intensity_mul_ { 1.0F };
  float sky_light_diffuse_ { 1.0F };
  float sky_light_specular_ { 1.0F };
  bool sky_light_real_time_capture_enabled_ { false };
  glm::vec3 sky_light_lower_hemisphere_color_ { 0.0F, 0.0F, 0.0F };
  float sky_light_volumetric_scattering_intensity_ { 1.0F };
  bool sky_light_affect_reflections_ { true };
  bool sky_light_affect_global_illumination_ { true };

  // Fog
  bool fog_enabled_ { false };
  int fog_model_ { 0 }; // 0 = exponential height, 1 = volumetric
  float fog_extinction_sigma_t_per_m_ { 0.01F };
  float fog_height_falloff_per_m_ { 0.2F };
  float fog_height_offset_m_ { 0.0F };
  float fog_start_distance_m_ { 0.0F };
  float second_fog_density_ { 0.0F };
  float second_fog_height_falloff_ { 0.0F };
  float second_fog_height_offset_ { 0.0F };
  float fog_max_opacity_ { 1.0F };
  glm::vec3 fog_single_scattering_albedo_rgb_ { 1.0F, 1.0F, 1.0F };
  glm::vec3 fog_inscattering_luminance_ { 1.0F, 1.0F, 1.0F };
  glm::vec3 sky_atmosphere_ambient_contribution_color_scale_ {
    1.0F,
    1.0F,
    1.0F,
  };
  float inscattering_color_cubemap_angle_ { 0.0F };
  glm::vec3 inscattering_texture_tint_ { 1.0F, 1.0F, 1.0F };
  float fully_directional_inscattering_color_distance_ { 0.0F };
  float non_directional_inscattering_color_distance_ { 0.0F };
  glm::vec3 directional_inscattering_luminance_ { 1.0F, 1.0F, 1.0F };
  float directional_inscattering_exponent_ { 0.0F };
  float directional_inscattering_start_distance_ { 0.0F };
  float fog_end_distance_m_ { 0.0F };
  float fog_cutoff_distance_m_ { 0.0F };
  float volumetric_fog_scattering_distribution_ { 0.0F };
  glm::vec3 volumetric_fog_albedo_ { 1.0F, 1.0F, 1.0F };
  glm::vec3 volumetric_fog_emissive_ { 0.0F, 0.0F, 0.0F };
  float volumetric_fog_extinction_scale_ { 1.0F };
  float volumetric_fog_distance_m_ { 0.0F };
  float volumetric_fog_start_distance_m_ { 0.0F };
  float volumetric_fog_near_fade_in_distance_m_ { 0.0F };
  float volumetric_fog_static_lighting_scattering_intensity_ { 1.0F };
  bool override_light_colors_with_fog_inscattering_colors_ { false };
  bool fog_holdout_ { false };
  bool fog_render_in_main_pass_ { true };
  bool fog_visible_in_reflection_captures_ { true };
  bool fog_visible_in_real_time_sky_captures_ { true };
  std::vector<LocalFogVolumeUiState> local_fog_volumes_ {};
  std::vector<scene::NodeHandle> removed_local_fog_nodes_ {};
  int selected_local_fog_volume_index_ { -1 };

  // Sun component
  bool sun_present_ { false };
  bool sun_enabled_ { true };
  float sun_azimuth_deg_ { scene::environment::Sun::kDefaultAzimuthDeg };
  float sun_elevation_deg_ { scene::environment::Sun::kDefaultElevationDeg };
  glm::vec3 sun_color_rgb_ { 1.0F, 1.0F, 1.0F };
  float sun_illuminance_lx_ { scene::environment::Sun::kDefaultIlluminanceLx };
  bool sun_use_temperature_ { false };
  float sun_temperature_kelvin_ { 6500.0F };
  float sun_component_disk_radius_deg_ {
    scene::environment::Sun::kDefaultDiskAngularRadiusRad * math::RadToDeg
  };
  int sun_atmosphere_light_slot_ { static_cast<int>(
    scene::AtmosphereLightSlot::kPrimary) };
  bool sun_use_per_pixel_atmosphere_transmittance_ { false };
  glm::vec3 sun_atmosphere_disk_luminance_scale_ { 1.0F, 1.0F, 1.0F };
  float sun_shadow_bias_ { scene::kDefaultShadowBias };
  float sun_shadow_normal_bias_ { scene::kDefaultShadowNormalBias };
  int sun_shadow_resolution_hint_ { static_cast<int>(
    scene::ShadowResolutionHint::kMedium) };
  int sun_shadow_cascade_count_ { static_cast<int>(scene::kMaxShadowCascades) };
  int sun_shadow_split_mode_ { static_cast<int>(
    scene::DirectionalCsmSplitMode::kGenerated) };
  float sun_shadow_max_distance_ {
    scene::kDefaultDirectionalMaxShadowDistance
  };
  std::array<float, scene::kMaxShadowCascades> sun_shadow_cascade_distances_ {
    scene::kDefaultDirectionalCascadeDistances
  };
  float sun_shadow_distribution_exponent_ {
    scene::kDefaultDirectionalDistributionExponent
  };
  float sun_shadow_transition_fraction_ {
    scene::kDefaultDirectionalTransitionFraction
  };
  float sun_shadow_distance_fadeout_fraction_ {
    scene::kDefaultDirectionalDistanceFadeoutFraction
  };
  scene::SceneNode sun_light_node_;
  bool sun_light_available_ { false };

  // Atmosphere debug flags
  bool use_lut_ { true };

  std::atomic_uint64_t epoch_ { 0 };

  int preset_index_ { -1 };
};

} // namespace oxygen::examples
