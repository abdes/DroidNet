//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "DemoShell/Services/CameraSettingsService.h"
#include "DemoShell/Services/SettingsService.h"

namespace oxygen::examples {

auto CameraSettingsService::GetCameraControlMode() const -> CameraControlMode
{
  const auto settings = ResolveSettings();
  if (!settings) {
    return CameraControlMode::kOrbit;
  }

  if (!active_camera_id_.empty()) {
    const std::string prefix = "camera_rig." + active_camera_id_;
    if (const auto value = settings->GetString(prefix + ".mode")) {
      if (*value == "fly") {
        return CameraControlMode::kFly;
      }
      if (*value == "drone") {
        return CameraControlMode::kDrone;
      }
      return CameraControlMode::kOrbit;
    }
  }
  return CameraControlMode::kOrbit;
}

auto CameraSettingsService::SetCameraControlMode(CameraControlMode mode) -> void
{
  const auto settings = ResolveSettings();
  if (!settings) {
    return;
  }

  if (active_camera_id_.empty()) {
    return;
  }

  const std::string prefix = "camera_rig." + active_camera_id_;
  auto value = "orbit";
  if (mode == CameraControlMode::kFly) {
    value = "fly";
  } else if (mode == CameraControlMode::kDrone) {
    value = "drone";
  }
  settings->SetString(prefix + ".mode", value);
  ++epoch_;
}

auto CameraSettingsService::SetActiveCameraId(std::string_view camera_id)
  -> void
{
  const std::string id(camera_id);
  if (active_camera_id_ == id) {
    return;
  }
  active_camera_id_ = id;
  ++epoch_;
}

auto CameraSettingsService::GetOrbitMode() const -> OrbitMode
{
  const auto settings = ResolveSettings();
  if (!settings) {
    return OrbitMode::kTurntable;
  }

  if (const auto value = settings->GetString(kOrbitModeKey)) {
    if (*value == "trackball") {
      return OrbitMode::kTrackball;
    }
  }
  return OrbitMode::kTurntable;
}

auto CameraSettingsService::SetOrbitMode(OrbitMode mode) -> void
{
  const auto settings = ResolveSettings();
  if (!settings) {
    return;
  }

  const char* value
    = (mode == OrbitMode::kTrackball) ? "trackball" : "turntable";
  settings->SetString(kOrbitModeKey, value);
  ++epoch_;
}

auto CameraSettingsService::GetFlyMoveSpeed() const -> float
{
  const auto settings = ResolveSettings();
  if (!settings) {
    return 5.0F;
  }

  if (const auto value = settings->GetFloat(kFlyMoveSpeedKey)) {
    return *value;
  }
  return 5.0F;
}

auto CameraSettingsService::SetFlyMoveSpeed(float speed) -> void
{
  const auto settings = ResolveSettings();
  if (!settings) {
    return;
  }

  settings->SetFloat(kFlyMoveSpeedKey, speed);
  ++epoch_;
}

auto CameraSettingsService::GetDroneSpeed() const -> float
{
  const auto settings = ResolveSettings();
  if (!settings)
    return 6.0F;
  if (active_camera_id_.empty())
    return 6.0F;
  const std::string prefix = "camera_rig." + active_camera_id_ + ".";
  return settings->GetFloat(prefix + kDroneSpeedKey).value_or(6.0F);
}

auto CameraSettingsService::SetDroneSpeed(float speed) -> void
{
  if (active_camera_id_.empty())
    return;
  if (const auto settings = ResolveSettings()) {
    const std::string prefix = "camera_rig." + active_camera_id_ + ".";
    settings->SetFloat(prefix + kDroneSpeedKey, speed);
    ++epoch_;
  }
}

auto CameraSettingsService::GetDroneDamping() const -> float
{
  const auto settings = ResolveSettings();
  if (!settings)
    return 8.0F;
  if (active_camera_id_.empty())
    return 8.0F;
  const std::string prefix = "camera_rig." + active_camera_id_ + ".";
  return settings->GetFloat(prefix + kDroneDampingKey).value_or(8.0F);
}

auto CameraSettingsService::SetDroneDamping(float damping) -> void
{
  if (active_camera_id_.empty())
    return;
  if (const auto settings = ResolveSettings()) {
    const std::string prefix = "camera_rig." + active_camera_id_ + ".";
    settings->SetFloat(prefix + kDroneDampingKey, damping);
    ++epoch_;
  }
}

auto CameraSettingsService::GetDroneFocusHeight() const -> float
{
  const auto settings = ResolveSettings();
  if (!settings)
    return 0.8F;
  if (active_camera_id_.empty())
    return 0.8F;
  const std::string prefix = "camera_rig." + active_camera_id_ + ".";
  return settings->GetFloat(prefix + kDroneFocusHeightKey).value_or(0.8F);
}

auto CameraSettingsService::SetDroneFocusHeight(float height) -> void
{
  if (active_camera_id_.empty())
    return;
  if (const auto settings = ResolveSettings()) {
    const std::string prefix = "camera_rig." + active_camera_id_ + ".";
    settings->SetFloat(prefix + kDroneFocusHeightKey, height);
    ++epoch_;
  }
}

auto CameraSettingsService::GetDroneFocusOffsetX() const -> float
{
  const auto settings = ResolveSettings();
  if (!settings)
    return 0.0F;
  if (active_camera_id_.empty())
    return 0.0F;
  const std::string prefix = "camera_rig." + active_camera_id_ + ".";
  return settings->GetFloat(prefix + kDroneFocusOffsetXKey).value_or(0.0F);
}

auto CameraSettingsService::SetDroneFocusOffsetX(float offset) -> void
{
  if (active_camera_id_.empty())
    return;
  if (const auto settings = ResolveSettings()) {
    const std::string prefix = "camera_rig." + active_camera_id_ + ".";
    settings->SetFloat(prefix + kDroneFocusOffsetXKey, offset);
    ++epoch_;
  }
}

auto CameraSettingsService::GetDroneFocusOffsetY() const -> float
{
  const auto settings = ResolveSettings();
  if (!settings)
    return 0.0F;
  if (active_camera_id_.empty())
    return 0.0F;
  const std::string prefix = "camera_rig." + active_camera_id_ + ".";
  return settings->GetFloat(prefix + kDroneFocusOffsetYKey).value_or(0.0F);
}

auto CameraSettingsService::SetDroneFocusOffsetY(float offset) -> void
{
  if (active_camera_id_.empty())
    return;
  if (const auto settings = ResolveSettings()) {
    const std::string prefix = "camera_rig." + active_camera_id_ + ".";
    settings->SetFloat(prefix + kDroneFocusOffsetYKey, offset);
    ++epoch_;
  }
}

auto CameraSettingsService::GetDroneRunning() const -> bool
{
  const auto settings = ResolveSettings();
  if (!settings)
    return true;
  if (active_camera_id_.empty())
    return true;
  const std::string prefix = "camera_rig." + active_camera_id_ + ".";
  return settings->GetBool(prefix + kDroneRunningKey).value_or(true);
}

auto CameraSettingsService::SetDroneRunning(bool running) -> void
{
  if (active_camera_id_.empty())
    return;
  if (const auto settings = ResolveSettings()) {
    const std::string prefix = "camera_rig." + active_camera_id_ + ".";
    settings->SetBool(prefix + kDroneRunningKey, running);
    ++epoch_;
  }
}

auto CameraSettingsService::GetDroneBobAmplitude() const -> float
{
  const auto settings = ResolveSettings();
  if (!settings)
    return 0.06F;
  if (active_camera_id_.empty())
    return 0.06F;
  const std::string prefix = "camera_rig." + active_camera_id_ + ".";
  return settings->GetFloat(prefix + kDroneBobAmpKey).value_or(0.06F);
}

auto CameraSettingsService::SetDroneBobAmplitude(float amp) -> void
{
  if (active_camera_id_.empty())
    return;
  if (const auto settings = ResolveSettings()) {
    const std::string prefix = "camera_rig." + active_camera_id_ + ".";
    settings->SetFloat(prefix + kDroneBobAmpKey, amp);
    ++epoch_;
  }
}

auto CameraSettingsService::GetDroneBobFrequency() const -> float
{
  const auto settings = ResolveSettings();
  if (!settings)
    return 1.6F;
  if (active_camera_id_.empty())
    return 1.6F;
  const std::string prefix = "camera_rig." + active_camera_id_ + ".";
  return settings->GetFloat(prefix + kDroneBobFreqKey).value_or(1.6F);
}

auto CameraSettingsService::SetDroneBobFrequency(float hz) -> void
{
  if (active_camera_id_.empty())
    return;
  if (const auto settings = ResolveSettings()) {
    const std::string prefix = "camera_rig." + active_camera_id_ + ".";
    settings->SetFloat(prefix + kDroneBobFreqKey, hz);
    ++epoch_;
  }
}

auto CameraSettingsService::GetDroneNoiseAmplitude() const -> float
{
  const auto settings = ResolveSettings();
  if (!settings)
    return 0.03F;
  if (active_camera_id_.empty())
    return 0.03F;
  const std::string prefix = "camera_rig." + active_camera_id_ + ".";
  return settings->GetFloat(prefix + kDroneNoiseAmpKey).value_or(0.03F);
}

auto CameraSettingsService::SetDroneNoiseAmplitude(float amp) -> void
{
  if (active_camera_id_.empty())
    return;
  if (const auto settings = ResolveSettings()) {
    const std::string prefix = "camera_rig." + active_camera_id_ + ".";
    settings->SetFloat(prefix + kDroneNoiseAmpKey, amp);
    ++epoch_;
  }
}

auto CameraSettingsService::GetDroneBankFactor() const -> float
{
  const auto settings = ResolveSettings();
  if (!settings)
    return 0.045F;
  if (active_camera_id_.empty())
    return 0.045F;
  const std::string prefix = "camera_rig." + active_camera_id_ + ".";
  return settings->GetFloat(prefix + kDroneBankFactorKey).value_or(0.045F);
}

auto CameraSettingsService::SetDroneBankFactor(float factor) -> void
{
  if (active_camera_id_.empty())
    return;
  if (const auto settings = ResolveSettings()) {
    const std::string prefix = "camera_rig." + active_camera_id_ + ".";
    settings->SetFloat(prefix + kDroneBankFactorKey, factor);
    ++epoch_;
  }
}

auto CameraSettingsService::GetDronePOISlowdownRadius() const -> float
{
  const auto settings = ResolveSettings();
  if (!settings)
    return 3.0F;
  if (active_camera_id_.empty())
    return 3.0F;
  const std::string prefix = "camera_rig." + active_camera_id_ + ".";
  return settings->GetFloat(prefix + kDronePOIRadiusKey).value_or(3.0F);
}

auto CameraSettingsService::SetDronePOISlowdownRadius(float radius) -> void
{
  if (active_camera_id_.empty())
    return;
  if (const auto settings = ResolveSettings()) {
    const std::string prefix = "camera_rig." + active_camera_id_ + ".";
    settings->SetFloat(prefix + kDronePOIRadiusKey, radius);
    ++epoch_;
  }
}

auto CameraSettingsService::GetDronePOIMinSpeed() const -> float
{
  const auto settings = ResolveSettings();
  if (!settings)
    return 0.3F;
  if (active_camera_id_.empty())
    return 0.3F;
  const std::string prefix = "camera_rig." + active_camera_id_ + ".";
  return settings->GetFloat(prefix + kDronePOIMinSpeedKey).value_or(0.3F);
}

auto CameraSettingsService::SetDronePOIMinSpeed(float factor) -> void
{
  if (active_camera_id_.empty())
    return;
  if (const auto settings = ResolveSettings()) {
    const std::string prefix = "camera_rig." + active_camera_id_ + ".";
    settings->SetFloat(prefix + kDronePOIMinSpeedKey, factor);
    ++epoch_;
  }
}

auto CameraSettingsService::GetDroneShowPath() const -> bool
{
  const auto settings = ResolveSettings();
  if (!settings)
    return false;
  if (active_camera_id_.empty())
    return false;
  const std::string prefix = "camera_rig." + active_camera_id_ + ".";
  return settings->GetBool(prefix + kDroneShowPathKey).value_or(false);
}

auto CameraSettingsService::SetDroneShowPath(bool show) -> void
{
  if (active_camera_id_.empty())
    return;
  if (const auto settings = ResolveSettings()) {
    const std::string prefix = "camera_rig." + active_camera_id_ + ".";
    settings->SetBool(prefix + kDroneShowPathKey, show);
    ++epoch_;
  }
}

auto CameraSettingsService::GetEpoch() const noexcept -> std::uint64_t
{
  return epoch_.load(std::memory_order_acquire);
}

auto CameraSettingsService::ResolveSettings() const noexcept
  -> observer_ptr<SettingsService>
{
  return SettingsService::Default();
}

} // namespace oxygen::examples
