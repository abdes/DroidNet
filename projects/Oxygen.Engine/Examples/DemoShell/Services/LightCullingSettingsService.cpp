//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "DemoShell/Services/LightCullingSettingsService.h"
#include "DemoShell/Services/SettingsService.h"

namespace oxygen::examples {

auto LightCullingSettingsService::GetUseClusteredCulling() const -> bool
{
  const auto settings = ResolveSettings();
  if (!settings) {
    return false;
  }

  if (const auto value = settings->GetString(kModeKey)) {
    return *value == "clustered";
  }
  return false;
}

auto LightCullingSettingsService::SetUseClusteredCulling(bool enabled) -> void
{
  const auto settings = ResolveSettings();
  if (!settings) {
    return;
  }

  settings->SetString(kModeKey, enabled ? "clustered" : "tiled");
  ++epoch_;
}

auto LightCullingSettingsService::GetDepthSlices() const -> int
{
  const auto settings = ResolveSettings();
  if (!settings) {
    return kDefaultDepthSlices;
  }

  if (const auto value = settings->GetFloat(kDepthSlicesKey)) {
    const int slices = static_cast<int>(*value);
    return (slices >= 2) ? slices : kDefaultDepthSlices;
  }
  return kDefaultDepthSlices;
}

auto LightCullingSettingsService::SetDepthSlices(int slices) -> void
{
  const auto settings = ResolveSettings();
  if (!settings) {
    return;
  }

  settings->SetFloat(kDepthSlicesKey, static_cast<float>(slices));
  ++epoch_;
}

auto LightCullingSettingsService::GetUseCameraZ() const -> bool
{
  const auto settings = ResolveSettings();
  if (!settings) {
    return true; // Default to using camera planes
  }

  if (const auto value = settings->GetBool(kUseCameraZKey)) {
    return *value;
  }
  return true;
}

auto LightCullingSettingsService::SetUseCameraZ(bool use_camera) -> void
{
  const auto settings = ResolveSettings();
  if (!settings) {
    return;
  }

  settings->SetBool(kUseCameraZKey, use_camera);
  ++epoch_;
}

auto LightCullingSettingsService::GetZNear() const -> float
{
  const auto settings = ResolveSettings();
  if (!settings) {
    return kDefaultZNear;
  }

  if (const auto value = settings->GetFloat(kZNearKey)) {
    return *value;
  }
  return kDefaultZNear;
}

auto LightCullingSettingsService::SetZNear(float z_near) -> void
{
  const auto settings = ResolveSettings();
  if (!settings) {
    return;
  }

  settings->SetFloat(kZNearKey, z_near);
  ++epoch_;
}

auto LightCullingSettingsService::GetZFar() const -> float
{
  const auto settings = ResolveSettings();
  if (!settings) {
    return kDefaultZFar;
  }

  if (const auto value = settings->GetFloat(kZFarKey)) {
    return *value;
  }
  return kDefaultZFar;
}

auto LightCullingSettingsService::SetZFar(float z_far) -> void
{
  const auto settings = ResolveSettings();
  if (!settings) {
    return;
  }

  settings->SetFloat(kZFarKey, z_far);
  ++epoch_;
}

auto LightCullingSettingsService::GetVisualizationMode() const
  -> engine::ShaderDebugMode
{
  const auto settings = ResolveSettings();
  if (!settings) {
    return engine::ShaderDebugMode::kDisabled;
  }

  if (const auto value = settings->GetFloat(kVisualizationModeKey)) {
    return static_cast<engine::ShaderDebugMode>(static_cast<int>(*value));
  }
  return engine::ShaderDebugMode::kDisabled;
}

auto LightCullingSettingsService::SetVisualizationMode(
  engine::ShaderDebugMode mode) -> void
{
  const auto settings = ResolveSettings();
  if (!settings) {
    return;
  }

  settings->SetFloat(
    kVisualizationModeKey, static_cast<float>(static_cast<int>(mode)));
  ++epoch_;
}

auto LightCullingSettingsService::GetEpoch() const noexcept -> std::uint64_t
{
  return epoch_.load(std::memory_order_acquire);
}

auto LightCullingSettingsService::ResolveSettings() const noexcept
  -> observer_ptr<SettingsService>
{
  return SettingsService::Default();
}

} // namespace oxygen::examples
