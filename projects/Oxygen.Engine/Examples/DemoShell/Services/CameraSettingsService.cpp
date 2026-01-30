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

  if (const auto value = settings->GetString(kControlModeKey)) {
    if (*value == "fly") {
      return CameraControlMode::kFly;
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

  const char* value = (mode == CameraControlMode::kFly) ? "fly" : "orbit";
  settings->SetString(kControlModeKey, value);
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

  const char* value = (mode == OrbitMode::kTrackball) ? "trackball" : "turntable";
  settings->SetString(kOrbitModeKey, value);
  ++epoch_;
}

auto CameraSettingsService::GetFlyMoveSpeed() const -> float
{
  const auto settings = ResolveSettings();
  if (!settings) {
    return 5.0f;
  }

  if (const auto value = settings->GetFloat(kFlyMoveSpeedKey)) {
    return *value;
  }
  return 5.0f;
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
