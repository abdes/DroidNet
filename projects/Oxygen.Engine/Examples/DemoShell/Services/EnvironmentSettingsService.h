//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Scene/SceneNode.h>

#include <glm/vec3.hpp>

#include <Oxygen/Content/ResourceKey.h>
#include <Oxygen/Scene/Environment/Sun.h>

#include "DemoShell/Services/DomainService.h"

namespace oxygen {
namespace engine {
  class Renderer;
}
namespace data {
  class SceneAsset;
}
namespace scene {
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
  observer_ptr<engine::Renderer> renderer { nullptr };
  std::function<void()> on_atmosphere_params_changed {};
  std::function<void()> on_exposure_changed {};
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

  //! Returns whether LUTs are valid and dirty (renderer state).
  [[nodiscard]] virtual auto GetAtmosphereLutStatus() const
    -> std::pair<bool, bool>;

  //! Returns the current settings epoch.
  [[nodiscard]] auto GetEpoch() const noexcept -> std::uint64_t override;

  auto OnFrameStart(const engine::FrameContext& context) -> void override;
  auto OnSceneActivated(scene::Scene& scene) -> void override;
  auto OnMainViewReady(const engine::FrameContext& context,
    const CompositionView& view) -> void override;

  // SkyAtmosphere
  [[nodiscard]] virtual auto GetSkyAtmosphereEnabled() const -> bool;
  virtual auto SetSkyAtmosphereEnabled(bool enabled) -> void;

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

  [[nodiscard]] virtual auto GetSunDiskEnabled() const -> bool;
  virtual auto SetSunDiskEnabled(bool enabled) -> void;

  [[nodiscard]] virtual auto GetAerialPerspectiveScale() const -> float;
  virtual auto SetAerialPerspectiveScale(float value) -> void;

  [[nodiscard]] virtual auto GetAerialScatteringStrength() const -> float;
  virtual auto SetAerialScatteringStrength(float value) -> void;

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

  [[nodiscard]] virtual auto GetSkyLightIntensity() const -> float;
  virtual auto SetSkyLightIntensity(float value) -> void;

  [[nodiscard]] virtual auto GetSkyLightDiffuse() const -> float;
  virtual auto SetSkyLightDiffuse(float value) -> void;

  [[nodiscard]] virtual auto GetSkyLightSpecular() const -> float;
  virtual auto SetSkyLightSpecular(float value) -> void;

  // Fog
  [[nodiscard]] virtual auto GetFogEnabled() const -> bool;
  virtual auto SetFogEnabled(bool enabled) -> void;

  [[nodiscard]] virtual auto GetFogModel() const -> int;
  virtual auto SetFogModel(int model) -> void;

  [[nodiscard]] virtual auto GetFogDensity() const -> float;
  virtual auto SetFogDensity(float value) -> void;

  [[nodiscard]] virtual auto GetFogHeightFalloff() const -> float;
  virtual auto SetFogHeightFalloff(float value) -> void;

  [[nodiscard]] virtual auto GetFogHeightOffsetMeters() const -> float;
  virtual auto SetFogHeightOffsetMeters(float value) -> void;

  [[nodiscard]] virtual auto GetFogStartDistanceMeters() const -> float;
  virtual auto SetFogStartDistanceMeters(float value) -> void;

  [[nodiscard]] virtual auto GetFogMaxOpacity() const -> float;
  virtual auto SetFogMaxOpacity(float value) -> void;

  [[nodiscard]] virtual auto GetFogAlbedo() const -> glm::vec3;
  virtual auto SetFogAlbedo(const glm::vec3& value) -> void;

  // Sun
  [[nodiscard]] virtual auto GetSunPresent() const -> bool;
  [[nodiscard]] virtual auto GetSunEnabled() const -> bool;
  virtual auto SetSunEnabled(bool enabled) -> void;

  [[nodiscard]] virtual auto GetSunSource() const -> int;
  virtual auto SetSunSource(int source) -> void;

  [[nodiscard]] virtual auto GetSunAzimuthDeg() const -> float;
  virtual auto SetSunAzimuthDeg(float value) -> void;

  [[nodiscard]] virtual auto GetSunElevationDeg() const -> float;
  virtual auto SetSunElevationDeg(float value) -> void;

  [[nodiscard]] virtual auto GetSunColorRgb() const -> glm::vec3;
  virtual auto SetSunColorRgb(const glm::vec3& value) -> void;

  [[nodiscard]] virtual auto GetSunIntensityLux() const -> float;
  virtual auto SetSunIntensityLux(float value) -> void;

  [[nodiscard]] virtual auto GetSunUseTemperature() const -> bool;
  virtual auto SetSunUseTemperature(bool enabled) -> void;

  [[nodiscard]] virtual auto GetSunTemperatureKelvin() const -> float;
  virtual auto SetSunTemperatureKelvin(float value) -> void;

  [[nodiscard]] virtual auto GetSunDiskRadiusDeg() const -> float;
  virtual auto SetSunDiskRadiusDeg(float value) -> void;

  [[nodiscard]] virtual auto GetSunLightAvailable() const -> bool;
  virtual auto UpdateSunLightCandidate() -> void;

  virtual auto EnableSyntheticSun() -> void;

  // Renderer debug flags
  [[nodiscard]] virtual auto GetUseLut() const -> bool;
  virtual auto SetUseLut(bool enabled) -> void;

  [[nodiscard]] virtual auto GetVisualizeLut() const -> bool;
  virtual auto SetVisualizeLut(bool enabled) -> void;

  [[nodiscard]] virtual auto GetForceAnalytic() const -> bool;
  virtual auto SetForceAnalytic(bool enabled) -> void;

private:
  struct SunUiSettings {
    bool enabled { true };
    float azimuth_deg { scene::environment::Sun::kDefaultAzimuthDeg };
    float elevation_deg { scene::environment::Sun::kDefaultElevationDeg };
    glm::vec3 color_rgb { 1.0F, 1.0F, 1.0F };
    float intensity_lux { scene::environment::Sun::kDefaultIntensityLux };
    bool use_temperature { false };
    float temperature_kelvin { 6500.0F };
    float disk_radius_deg {
      scene::environment::Sun::kDefaultDiskAngularRadiusRad * math::RadToDeg
    };
  };

  auto SyncFromScene() -> void;
  auto SyncDebugFlagsFromRenderer() -> void;
  auto LoadSettings() -> void;
  auto SaveSettings() const -> void;
  auto MarkDirty() -> void;
  auto NormalizeSkySystems() -> void;
  auto MaybeAutoLoadSkybox() -> void;
  auto ApplySavedSunSourcePreference() -> void;
  auto ResetSunUiToDefaults() -> void;
  auto FindSunLightCandidate() const -> std::optional<scene::SceneNode>;
  auto EnsureSyntheticSunLight() -> void;
  auto DestroySyntheticSunLight() -> void;
  auto GetSunSettingsForSource(int source) -> SunUiSettings&;
  auto LoadSunSettingsFromProfile(int source) -> void;
  auto SaveSunSettingsToProfile(int source) -> void;
  [[nodiscard]] auto GetAtmosphereFlags() const -> uint32_t;

  static constexpr float kDefaultPlanetRadiusKm = 6360.0F;
  static constexpr float kDefaultAtmosphereHeightKm = 80.0F;

  EnvironmentRuntimeConfig config_ {};

  int update_depth_ { 0 };
  bool settings_loaded_ { false };
  bool pending_changes_ { false };
  bool needs_sync_ { true };

  bool apply_saved_sun_on_next_sync_ { false };
  std::optional<int> saved_sun_source_ {};

  // SkyAtmosphere
  bool sky_atmo_enabled_ { false };
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
  bool sun_disk_enabled_ { true };
  float aerial_perspective_scale_ { 1.0F };
  float aerial_scattering_strength_ { 1.0F };
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
  std::string skybox_status_message_ {};
  int skybox_last_face_size_ { 0 };
  content::ResourceKey skybox_last_resource_key_ { 0U };
  bool skybox_dirty_ { false };
  std::string last_loaded_skybox_path_ {};
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
  float sky_light_intensity_ { 1.0F };
  float sky_light_diffuse_ { 1.0F };
  float sky_light_specular_ { 1.0F };

  // Fog
  bool fog_enabled_ { false };
  int fog_model_ { 0 }; // 0 = exponential height, 1 = volumetric
  float fog_density_ { 0.01F };
  float fog_height_falloff_ { 0.2F };
  float fog_height_offset_m_ { 0.0F };
  float fog_start_distance_m_ { 0.0F };
  float fog_max_opacity_ { 1.0F };
  glm::vec3 fog_albedo_ { 1.0F, 1.0F, 1.0F };

  // Sun component
  bool sun_present_ { false };
  bool sun_enabled_ { true };
  int sun_source_ { 0 };
  float sun_azimuth_deg_ { scene::environment::Sun::kDefaultAzimuthDeg };
  float sun_elevation_deg_ { scene::environment::Sun::kDefaultElevationDeg };
  glm::vec3 sun_color_rgb_ { 1.0F, 1.0F, 1.0F };
  float sun_intensity_lux_ { scene::environment::Sun::kDefaultIntensityLux };
  bool sun_use_temperature_ { false };
  float sun_temperature_kelvin_ { 6500.0F };
  float sun_component_disk_radius_deg_ {
    scene::environment::Sun::kDefaultDiskAngularRadiusRad * math::RadToDeg
  };
  scene::SceneNode sun_light_node_ {};
  bool sun_light_available_ { false };
  scene::SceneNode synthetic_sun_light_node_ {};
  bool synthetic_sun_light_created_ { false };

  SunUiSettings sun_scene_settings_ {};
  SunUiSettings sun_synthetic_settings_ {};

  // Atmosphere debug flags
  bool use_lut_ { true };
  bool visualize_lut_ { false };
  bool force_analytic_ { false };

  std::atomic_uint64_t epoch_ { 0 };
};

} // namespace oxygen::examples
