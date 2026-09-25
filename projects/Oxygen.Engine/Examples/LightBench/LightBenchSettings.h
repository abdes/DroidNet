//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>

#include "LightBench/LightScene.h"

#include <Oxygen/Base/Result.h>
#include <Oxygen/Scene/Camera/CameraExposure.h>
#include <Oxygen/Scene/ExposureSettings.h>

namespace oxygen::examples::light_bench {

enum class LightBenchPreset : std::uint8_t {
  kNeutralReference,
  kPointFalloff,
  kSpotCone,
  kMaterialLighting,
  kAutoAdaptation,
  kIndoor,
  kOutdoorDaylight,
};

struct PresetInfo {
  LightBenchPreset preset;
  std::string_view id;
  //! Static, null-terminated label for ImGui controls.
  const char* name;
  std::string_view description;
};
auto GetPresets() -> std::span<const PresetInfo>;
auto GetPresetInfo(LightBenchPreset preset) -> const PresetInfo&;

struct SettingsError {
  std::string message;
};

//! Only the controls authored by this demo; no GPU identities or history.
struct LightBenchSettings {
  LightBenchPreset preset { LightBenchPreset::kNeutralReference };
  float initial_auto_ev { 8.0F };
  std::array<LightScene::SceneObjectState, 6> objects;
  LightScene::DirectionalLightState directional;
  LightScene::PointLightState point;
  LightScene::SpotLightState spot;
  Vec3 camera_position { 0.0F, 6.0F, 1.0F };
  Quat camera_rotation { 1.0F, 0.0F, 0.0F, 0.0F };
  scene::CameraExposure camera_exposure;
  std::array<float, 6> camera_extents { -4.0F, 4.0F, -2.25F, 2.25F, 0.1F,
    100.0F };
  bool camera_fit_to_view { true };
  scene::ExposureSettings exposure;
  engine::ToneMapper tone_mapper { engine::ToneMapper::kNone };
  float gamma { 2.2F };
};

auto ReferenceSettings() -> LightBenchSettings;
auto PresetSettings(LightBenchPreset preset) -> LightBenchSettings;
auto IsPresetSettings(const LightBenchSettings& settings) -> bool;
auto CaptureSettings(LightScene& scene, scene::SceneNode& camera)
  -> LightBenchSettings;
//! Apply only to an unpublished scene and camera after DecodeSettings succeeds.
auto ApplySettings(const LightBenchSettings& settings, LightScene& scene,
  scene::SceneNode& camera) -> void;
auto IsReferenceSettings(const LightBenchSettings& settings) -> bool;
auto EncodeSettings(const LightBenchSettings& settings)
  -> Result<std::string, SettingsError>;
auto DecodeSettings(std::string_view encoded)
  -> Result<LightBenchSettings, SettingsError>;

//! Publish a complete validated snapshot without truncating the previous file.
auto SaveSettingsFile(const std::filesystem::path& path,
  const LightBenchSettings& settings) -> Result<void, SettingsError>;
//! Read at most 1 MiB, then validate before returning replacement settings.
auto LoadSettingsFile(const std::filesystem::path& path)
  -> Result<LightBenchSettings, SettingsError>;

} // namespace oxygen::examples::light_bench
