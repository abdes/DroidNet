//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <glm/vec3.hpp>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Content/ResourceKey.h>

#include "DemoShell/Services/EnvironmentSettingsService.h"
#include "DemoShell/Services/FileBrowserService.h"

namespace oxygen::examples::ui {

//! View model for environment panel state.
/*!
 Bridges UI with `EnvironmentSettingsService` by forwarding state changes and
 exposing cached environment settings.

### Key Features

- **Centralized settings**: Uses EnvironmentSettingsService for persistence.
- **Single VM**: All environment sections are owned together.

@see oxygen::examples::EnvironmentSettingsService
*/
class EnvironmentVm {
public:
  //! Creates a view model backed by the provided settings service.
  explicit EnvironmentVm(observer_ptr<EnvironmentSettingsService> service,
    observer_ptr<FileBrowserService> file_browser_service);

  auto SetRuntimeConfig(const EnvironmentRuntimeConfig& config) -> void;
  [[nodiscard]] auto HasScene() const -> bool;

  auto RequestResync() -> void;
  auto SyncFromSceneIfNeeded() -> void;
  [[nodiscard]] auto HasPendingChanges() const -> bool;
  auto ApplyPendingChanges() -> void;

  [[nodiscard]] auto GetAtmosphereLutStatus() const -> std::pair<bool, bool>;

  [[nodiscard]] auto GetPresetCount() const -> int;
  [[nodiscard]] auto GetPresetName(int index) const -> std::string_view;
  [[nodiscard]] auto GetPresetLabel() const -> std::string_view;
  [[nodiscard]] auto GetPresetIndex() const -> int;
  auto ApplyPreset(int index) -> void;

  // SkyAtmosphere
  [[nodiscard]] auto GetSkyAtmosphereEnabled() const -> bool;
  auto SetSkyAtmosphereEnabled(bool enabled) -> void;
  [[nodiscard]] auto GetPlanetRadiusKm() const -> float;
  auto SetPlanetRadiusKm(float value) -> void;
  [[nodiscard]] auto GetAtmosphereHeightKm() const -> float;
  auto SetAtmosphereHeightKm(float value) -> void;
  [[nodiscard]] auto GetGroundAlbedo() const -> glm::vec3;
  auto SetGroundAlbedo(const glm::vec3& value) -> void;
  [[nodiscard]] auto GetRayleighScaleHeightKm() const -> float;
  auto SetRayleighScaleHeightKm(float value) -> void;
  [[nodiscard]] auto GetMieScaleHeightKm() const -> float;
  auto SetMieScaleHeightKm(float value) -> void;
  [[nodiscard]] auto GetMieAnisotropy() const -> float;
  auto SetMieAnisotropy(float value) -> void;
  [[nodiscard]] auto GetMultiScattering() const -> float;
  auto SetMultiScattering(float value) -> void;
  [[nodiscard]] auto GetSunDiskEnabled() const -> bool;
  auto SetSunDiskEnabled(bool enabled) -> void;
  [[nodiscard]] auto GetAerialPerspectiveScale() const -> float;
  auto SetAerialPerspectiveScale(float value) -> void;
  [[nodiscard]] auto GetAerialScatteringStrength() const -> float;
  auto SetAerialScatteringStrength(float value) -> void;

  // Sky-View LUT slicing
  [[nodiscard]] auto GetSkyViewLutSlices() const -> int;
  auto SetSkyViewLutSlices(int value) -> void;
  [[nodiscard]] auto GetSkyViewAltMappingMode() const -> int;
  auto SetSkyViewAltMappingMode(int value) -> void;
  auto RequestRegenerateLut() -> void;

  // SkySphere
  [[nodiscard]] auto GetSkySphereEnabled() const -> bool;
  auto SetSkySphereEnabled(bool enabled) -> void;
  [[nodiscard]] auto GetSkySphereSource() const -> int;
  auto SetSkySphereSource(int source) -> void;
  [[nodiscard]] auto GetSkySphereSolidColor() const -> glm::vec3;
  auto SetSkySphereSolidColor(const glm::vec3& value) -> void;
  [[nodiscard]] auto GetSkyIntensity() const -> float;
  auto SetSkyIntensity(float value) -> void;
  [[nodiscard]] auto GetSkySphereRotationDeg() const -> float;
  auto SetSkySphereRotationDeg(float value) -> void;

  // Skybox
  [[nodiscard]] auto GetSkyboxPath() const -> std::string;
  auto SetSkyboxPath(std::string_view path) -> void;
  [[nodiscard]] auto GetSkyboxLayoutIndex() const -> int;
  auto SetSkyboxLayoutIndex(int index) -> void;
  [[nodiscard]] auto GetSkyboxOutputFormatIndex() const -> int;
  auto SetSkyboxOutputFormatIndex(int index) -> void;
  [[nodiscard]] auto GetSkyboxFaceSize() const -> int;
  auto SetSkyboxFaceSize(int size) -> void;
  [[nodiscard]] auto GetSkyboxFlipY() const -> bool;
  auto SetSkyboxFlipY(bool flip) -> void;
  [[nodiscard]] auto GetSkyboxTonemapHdrToLdr() const -> bool;
  auto SetSkyboxTonemapHdrToLdr(bool enabled) -> void;
  [[nodiscard]] auto GetSkyboxHdrExposureEv() const -> float;
  auto SetSkyboxHdrExposureEv(float value) -> void;
  [[nodiscard]] auto GetSkyboxStatusMessage() const -> std::string_view;
  [[nodiscard]] auto GetSkyboxLastFaceSize() const -> int;
  [[nodiscard]] auto GetSkyboxLastResourceKey() const -> content::ResourceKey;
  auto LoadSkybox(std::string_view path, int layout_index,
    int output_format_index, int face_size, bool flip_y,
    bool tonemap_hdr_to_ldr, float hdr_exposure_ev) -> void;
  auto BeginSkyboxBrowse(std::string_view current_path) -> void;
  [[nodiscard]] auto ConsumeSkyboxBrowseResult()
    -> std::optional<std::filesystem::path>;

  // SkyLight
  [[nodiscard]] auto GetSkyLightEnabled() const -> bool;
  auto SetSkyLightEnabled(bool enabled) -> void;
  [[nodiscard]] auto GetSkyLightSource() const -> int;
  auto SetSkyLightSource(int source) -> void;
  [[nodiscard]] auto GetSkyLightTint() const -> glm::vec3;
  auto SetSkyLightTint(const glm::vec3& value) -> void;
  [[nodiscard]] auto GetSkyLightIntensity() const -> float;
  auto SetSkyLightIntensity(float value) -> void;
  [[nodiscard]] auto GetSkyLightDiffuse() const -> float;
  auto SetSkyLightDiffuse(float value) -> void;
  [[nodiscard]] auto GetSkyLightSpecular() const -> float;
  auto SetSkyLightSpecular(float value) -> void;

  // Sun
  [[nodiscard]] auto GetSunPresent() const -> bool;
  [[nodiscard]] auto GetSunEnabled() const -> bool;
  auto SetSunEnabled(bool enabled) -> void;
  [[nodiscard]] auto GetSunSource() const -> int;
  auto SetSunSource(int source) -> void;
  [[nodiscard]] auto GetSunAzimuthDeg() const -> float;
  auto SetSunAzimuthDeg(float value) -> void;
  [[nodiscard]] auto GetSunElevationDeg() const -> float;
  auto SetSunElevationDeg(float value) -> void;
  [[nodiscard]] auto GetSunColorRgb() const -> glm::vec3;
  auto SetSunColorRgb(const glm::vec3& value) -> void;
  [[nodiscard]] auto GetSunIntensityLux() const -> float;
  auto SetSunIntensityLux(float value) -> void;
  [[nodiscard]] auto GetSunUseTemperature() const -> bool;
  auto SetSunUseTemperature(bool enabled) -> void;
  [[nodiscard]] auto GetSunTemperatureKelvin() const -> float;
  auto SetSunTemperatureKelvin(float value) -> void;
  [[nodiscard]] auto GetSunDiskRadiusDeg() const -> float;
  auto SetSunDiskRadiusDeg(float value) -> void;
  [[nodiscard]] auto GetSunLightAvailable() const -> bool;
  auto UpdateSunLightCandidate() -> void;
  auto EnableSyntheticSun() -> void;

  // Renderer debug flags
  [[nodiscard]] auto GetUseLut() const -> bool;
  auto SetUseLut(bool enabled) -> void;
  [[nodiscard]] auto GetVisualizeLut() const -> bool;
  auto SetVisualizeLut(bool enabled) -> void;
  [[nodiscard]] auto GetForceAnalytic() const -> bool;
  auto SetForceAnalytic(bool enabled) -> void;

private:
  observer_ptr<EnvironmentSettingsService> service_;
  observer_ptr<FileBrowserService> file_browser_ { nullptr };
  FileBrowserService::RequestId skybox_browse_request_id_ { 0 };
  int preset_index_ { 0 };
};

} // namespace oxygen::examples::ui
