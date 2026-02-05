//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <filesystem>

#include "DemoShell/Services/FileBrowserService.h"
#include "DemoShell/UI/EnvironmentVm.h"

namespace oxygen::examples::ui {

EnvironmentVm::EnvironmentVm(observer_ptr<EnvironmentSettingsService> service,
  observer_ptr<FileBrowserService> file_browser_service)
  : service_(service)
  , file_browser_(file_browser_service)
{
}

auto EnvironmentVm::SetRuntimeConfig(const EnvironmentRuntimeConfig& config)
  -> void
{
  service_->SetRuntimeConfig(config);
}

auto EnvironmentVm::HasScene() const -> bool { return service_->HasScene(); }

auto EnvironmentVm::RequestResync() -> void { service_->RequestResync(); }

auto EnvironmentVm::SyncFromSceneIfNeeded() -> void
{
  service_->SyncFromSceneIfNeeded();
}

auto EnvironmentVm::HasPendingChanges() const -> bool
{
  return service_->HasPendingChanges();
}

auto EnvironmentVm::ApplyPendingChanges() -> void
{
  service_->ApplyPendingChanges();
}

auto EnvironmentVm::GetAtmosphereLutStatus() const -> std::pair<bool, bool>
{
  return service_->GetAtmosphereLutStatus();
}

auto EnvironmentVm::GetSkyAtmosphereEnabled() const -> bool
{
  return service_->GetSkyAtmosphereEnabled();
}

auto EnvironmentVm::SetSkyAtmosphereEnabled(bool enabled) -> void
{
  service_->SetSkyAtmosphereEnabled(enabled);
}

auto EnvironmentVm::GetPlanetRadiusKm() const -> float
{
  return service_->GetPlanetRadiusKm();
}

auto EnvironmentVm::SetPlanetRadiusKm(float value) -> void
{
  service_->SetPlanetRadiusKm(value);
}

auto EnvironmentVm::GetAtmosphereHeightKm() const -> float
{
  return service_->GetAtmosphereHeightKm();
}

auto EnvironmentVm::SetAtmosphereHeightKm(float value) -> void
{
  service_->SetAtmosphereHeightKm(value);
}

auto EnvironmentVm::GetGroundAlbedo() const -> glm::vec3
{
  return service_->GetGroundAlbedo();
}

auto EnvironmentVm::SetGroundAlbedo(const glm::vec3& value) -> void
{
  service_->SetGroundAlbedo(value);
}

auto EnvironmentVm::GetRayleighScaleHeightKm() const -> float
{
  return service_->GetRayleighScaleHeightKm();
}

auto EnvironmentVm::SetRayleighScaleHeightKm(float value) -> void
{
  service_->SetRayleighScaleHeightKm(value);
}

auto EnvironmentVm::GetMieScaleHeightKm() const -> float
{
  return service_->GetMieScaleHeightKm();
}

auto EnvironmentVm::SetMieScaleHeightKm(float value) -> void
{
  service_->SetMieScaleHeightKm(value);
}

auto EnvironmentVm::GetMieAnisotropy() const -> float
{
  return service_->GetMieAnisotropy();
}

auto EnvironmentVm::SetMieAnisotropy(float value) -> void
{
  service_->SetMieAnisotropy(value);
}

auto EnvironmentVm::GetMultiScattering() const -> float
{
  return service_->GetMultiScattering();
}

auto EnvironmentVm::SetMultiScattering(float value) -> void
{
  service_->SetMultiScattering(value);
}

auto EnvironmentVm::GetSunDiskEnabled() const -> bool
{
  return service_->GetSunDiskEnabled();
}

auto EnvironmentVm::SetSunDiskEnabled(bool enabled) -> void
{
  service_->SetSunDiskEnabled(enabled);
}

auto EnvironmentVm::GetAtmosphereSunDiskRadiusDeg() const -> float
{
  return service_->GetAtmosphereSunDiskRadiusDeg();
}

auto EnvironmentVm::SetAtmosphereSunDiskRadiusDeg(float value) -> void
{
  service_->SetAtmosphereSunDiskRadiusDeg(value);
}

auto EnvironmentVm::GetAerialPerspectiveScale() const -> float
{
  return service_->GetAerialPerspectiveScale();
}

auto EnvironmentVm::SetAerialPerspectiveScale(float value) -> void
{
  service_->SetAerialPerspectiveScale(value);
}

auto EnvironmentVm::GetAerialScatteringStrength() const -> float
{
  return service_->GetAerialScatteringStrength();
}

auto EnvironmentVm::SetAerialScatteringStrength(float value) -> void
{
  service_->SetAerialScatteringStrength(value);
}

auto EnvironmentVm::GetSkySphereEnabled() const -> bool
{
  return service_->GetSkySphereEnabled();
}

auto EnvironmentVm::SetSkySphereEnabled(bool enabled) -> void
{
  service_->SetSkySphereEnabled(enabled);
}

auto EnvironmentVm::GetSkySphereSource() const -> int
{
  return service_->GetSkySphereSource();
}

auto EnvironmentVm::SetSkySphereSource(int source) -> void
{
  service_->SetSkySphereSource(source);
}

auto EnvironmentVm::GetSkySphereSolidColor() const -> glm::vec3
{
  return service_->GetSkySphereSolidColor();
}

auto EnvironmentVm::SetSkySphereSolidColor(const glm::vec3& value) -> void
{
  service_->SetSkySphereSolidColor(value);
}

auto EnvironmentVm::GetSkyIntensity() const -> float
{
  return service_->GetSkyIntensity();
}

auto EnvironmentVm::SetSkyIntensity(const float value) -> void
{
  service_->SetSkyIntensity(value);
}

auto EnvironmentVm::GetSkySphereRotationDeg() const -> float
{
  return service_->GetSkySphereRotationDeg();
}

auto EnvironmentVm::SetSkySphereRotationDeg(float value) -> void
{
  service_->SetSkySphereRotationDeg(value);
}

auto EnvironmentVm::GetSkyboxPath() const -> std::string
{
  return service_->GetSkyboxPath();
}

auto EnvironmentVm::SetSkyboxPath(std::string_view path) -> void
{
  service_->SetSkyboxPath(path);
}

auto EnvironmentVm::GetSkyboxLayoutIndex() const -> int
{
  return service_->GetSkyboxLayoutIndex();
}

auto EnvironmentVm::SetSkyboxLayoutIndex(int index) -> void
{
  service_->SetSkyboxLayoutIndex(index);
}

auto EnvironmentVm::GetSkyboxOutputFormatIndex() const -> int
{
  return service_->GetSkyboxOutputFormatIndex();
}

auto EnvironmentVm::SetSkyboxOutputFormatIndex(int index) -> void
{
  service_->SetSkyboxOutputFormatIndex(index);
}

auto EnvironmentVm::GetSkyboxFaceSize() const -> int
{
  return service_->GetSkyboxFaceSize();
}

auto EnvironmentVm::SetSkyboxFaceSize(int size) -> void
{
  service_->SetSkyboxFaceSize(size);
}

auto EnvironmentVm::GetSkyboxFlipY() const -> bool
{
  return service_->GetSkyboxFlipY();
}

auto EnvironmentVm::SetSkyboxFlipY(bool flip) -> void
{
  service_->SetSkyboxFlipY(flip);
}

auto EnvironmentVm::GetSkyboxTonemapHdrToLdr() const -> bool
{
  return service_->GetSkyboxTonemapHdrToLdr();
}

auto EnvironmentVm::SetSkyboxTonemapHdrToLdr(bool enabled) -> void
{
  service_->SetSkyboxTonemapHdrToLdr(enabled);
}

auto EnvironmentVm::GetSkyboxHdrExposureEv() const -> float
{
  return service_->GetSkyboxHdrExposureEv();
}

auto EnvironmentVm::SetSkyboxHdrExposureEv(float value) -> void
{
  service_->SetSkyboxHdrExposureEv(value);
}

auto EnvironmentVm::GetSkyboxStatusMessage() const -> std::string_view
{
  return service_->GetSkyboxStatusMessage();
}

auto EnvironmentVm::GetSkyboxLastFaceSize() const -> int
{
  return service_->GetSkyboxLastFaceSize();
}

auto EnvironmentVm::GetSkyboxLastResourceKey() const -> content::ResourceKey
{
  return service_->GetSkyboxLastResourceKey();
}

auto EnvironmentVm::LoadSkybox(std::string_view path, int layout_index,
  int output_format_index, int face_size, bool flip_y, bool tonemap_hdr_to_ldr,
  float hdr_exposure_ev) -> void
{
  service_->LoadSkybox(path, layout_index, output_format_index, face_size,
    flip_y, tonemap_hdr_to_ldr, hdr_exposure_ev);
}

auto EnvironmentVm::BeginSkyboxBrowse(std::string_view current_path) -> void
{
  if (!file_browser_) {
    return;
  }

  const auto roots = file_browser_->GetContentRoots();
  auto picker_config = MakeSkyboxFileBrowserConfig(roots);

  if (current_path.size() != 0U) {
    const std::filesystem::path current_path_fs { std::string(current_path) };
    if (current_path_fs.has_parent_path()) {
      picker_config.initial_directory = current_path_fs.parent_path();
    }
  }

  skybox_browse_request_id_ = file_browser_->Open(picker_config);
}

auto EnvironmentVm::ConsumeSkyboxBrowseResult()
  -> std::optional<std::filesystem::path>
{
  if (!file_browser_ || skybox_browse_request_id_ == 0) {
    return std::nullopt;
  }

  const auto result = file_browser_->ConsumeResult(skybox_browse_request_id_);
  if (!result) {
    return std::nullopt;
  }

  skybox_browse_request_id_ = 0;
  if (result->kind != FileBrowserService::ResultKind::kSelected) {
    return std::nullopt;
  }

  const auto path = result->path;
  const auto path_string = path.string();
  service_->SetSkyboxPath(path_string);
  return path;
}

auto EnvironmentVm::GetSkyLightEnabled() const -> bool
{
  return service_->GetSkyLightEnabled();
}

auto EnvironmentVm::SetSkyLightEnabled(bool enabled) -> void
{
  service_->SetSkyLightEnabled(enabled);
}

auto EnvironmentVm::GetSkyLightSource() const -> int
{
  return service_->GetSkyLightSource();
}

auto EnvironmentVm::SetSkyLightSource(int source) -> void
{
  service_->SetSkyLightSource(source);
}

auto EnvironmentVm::GetSkyLightTint() const -> glm::vec3
{
  return service_->GetSkyLightTint();
}

auto EnvironmentVm::SetSkyLightTint(const glm::vec3& value) -> void
{
  service_->SetSkyLightTint(value);
}

auto EnvironmentVm::GetSkyLightIntensity() const -> float
{
  return service_->GetSkyLightIntensity();
}

auto EnvironmentVm::SetSkyLightIntensity(const float value) -> void
{
  service_->SetSkyLightIntensity(value);
}

auto EnvironmentVm::GetSkyLightDiffuse() const -> float
{
  return service_->GetSkyLightDiffuse();
}

auto EnvironmentVm::SetSkyLightDiffuse(float value) -> void
{
  service_->SetSkyLightDiffuse(value);
}

auto EnvironmentVm::GetSkyLightSpecular() const -> float
{
  return service_->GetSkyLightSpecular();
}

auto EnvironmentVm::SetSkyLightSpecular(float value) -> void
{
  service_->SetSkyLightSpecular(value);
}

auto EnvironmentVm::GetSunPresent() const -> bool
{
  return service_->GetSunPresent();
}

auto EnvironmentVm::GetSunEnabled() const -> bool
{
  return service_->GetSunEnabled();
}

auto EnvironmentVm::SetSunEnabled(bool enabled) -> void
{
  service_->SetSunEnabled(enabled);
}

auto EnvironmentVm::GetSunSource() const -> int
{
  return service_->GetSunSource();
}

auto EnvironmentVm::SetSunSource(int source) -> void
{
  service_->SetSunSource(source);
}

auto EnvironmentVm::GetSunAzimuthDeg() const -> float
{
  return service_->GetSunAzimuthDeg();
}

auto EnvironmentVm::SetSunAzimuthDeg(float value) -> void
{
  service_->SetSunAzimuthDeg(value);
}

auto EnvironmentVm::GetSunElevationDeg() const -> float
{
  return service_->GetSunElevationDeg();
}

auto EnvironmentVm::SetSunElevationDeg(float value) -> void
{
  service_->SetSunElevationDeg(value);
}

auto EnvironmentVm::GetSunColorRgb() const -> glm::vec3
{
  return service_->GetSunColorRgb();
}

auto EnvironmentVm::SetSunColorRgb(const glm::vec3& value) -> void
{
  service_->SetSunColorRgb(value);
}

auto EnvironmentVm::GetSunIntensityLux() const -> float
{
  return service_->GetSunIntensityLux();
}

auto EnvironmentVm::SetSunIntensityLux(float value) -> void
{
  service_->SetSunIntensityLux(value);
}

auto EnvironmentVm::GetSunUseTemperature() const -> bool
{
  return service_->GetSunUseTemperature();
}

auto EnvironmentVm::SetSunUseTemperature(bool enabled) -> void
{
  service_->SetSunUseTemperature(enabled);
}

auto EnvironmentVm::GetSunTemperatureKelvin() const -> float
{
  return service_->GetSunTemperatureKelvin();
}

auto EnvironmentVm::SetSunTemperatureKelvin(float value) -> void
{
  service_->SetSunTemperatureKelvin(value);
}

auto EnvironmentVm::GetSunDiskRadiusDeg() const -> float
{
  return service_->GetSunDiskRadiusDeg();
}

auto EnvironmentVm::SetSunDiskRadiusDeg(float value) -> void
{
  service_->SetSunDiskRadiusDeg(value);
}

auto EnvironmentVm::GetSunLightAvailable() const -> bool
{
  return service_->GetSunLightAvailable();
}

auto EnvironmentVm::UpdateSunLightCandidate() -> void
{
  service_->UpdateSunLightCandidate();
}

auto EnvironmentVm::EnableSyntheticSun() -> void
{
  service_->EnableSyntheticSun();
}

auto EnvironmentVm::GetUseLut() const -> bool { return service_->GetUseLut(); }

auto EnvironmentVm::SetUseLut(bool enabled) -> void
{
  service_->SetUseLut(enabled);
}

auto EnvironmentVm::GetVisualizeLut() const -> bool
{
  return service_->GetVisualizeLut();
}

auto EnvironmentVm::SetVisualizeLut(bool enabled) -> void
{
  service_->SetVisualizeLut(enabled);
}

auto EnvironmentVm::GetForceAnalytic() const -> bool
{
  return service_->GetForceAnalytic();
}

auto EnvironmentVm::SetForceAnalytic(bool enabled) -> void
{
  service_->SetForceAnalytic(enabled);
}

} // namespace oxygen::examples::ui
