//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "DemoShell/Services/LightCullingSettingsService.h"
#include "DemoShell/Runtime/RenderingPipeline.h"
#include "DemoShell/Services/SettingsService.h"

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Renderer/Passes/ShaderPass.h>

namespace oxygen::examples {

auto LightCullingSettingsService::Initialize(
  observer_ptr<RenderingPipeline> pipeline) -> void
{
  DCHECK_NOTNULL_F(pipeline);
  pipeline_ = pipeline;

  // Push initial state
  pipeline_->SetClusteredCullingEnabled(GetUseClusteredCulling());
  pipeline_->SetClusterDepthSlices(static_cast<uint32_t>(GetDepthSlices()));
  pipeline_->SetLightCullingVisualizationMode(GetVisualizationMode());
}

auto LightCullingSettingsService::GetUseClusteredCulling() const -> bool
{
  const auto settings = ResolveSettings();
  if (settings) {
    return settings->GetBool(kModeKey).value_or(true); // Default to Clustered
  }
  return true;
}

auto LightCullingSettingsService::SetUseClusteredCulling(bool enabled) -> void
{
  const auto settings = ResolveSettings();
  if (settings) {
    settings->SetBool(kModeKey, enabled);
    epoch_++;

    if (pipeline_) {
      pipeline_->SetClusteredCullingEnabled(enabled);
    }
  }
}

auto LightCullingSettingsService::GetDepthSlices() const -> int
{
  const auto settings = ResolveSettings();
  if (settings) {
    auto val = settings->GetString(kDepthSlicesKey)
                 .value_or(std::to_string(kDefaultDepthSlices));
    return std::stoi(val);
  }
  return kDefaultDepthSlices;
}

auto LightCullingSettingsService::SetDepthSlices(int slices) -> void
{
  const auto settings = ResolveSettings();
  if (settings) {
    settings->SetString(kDepthSlicesKey, std::to_string(slices));
    epoch_++;

    if (pipeline_) {
      pipeline_->SetClusterDepthSlices(static_cast<uint32_t>(slices));
    }
  }
}

auto LightCullingSettingsService::GetUseCameraZ() const -> bool
{
  const auto settings = ResolveSettings();
  if (settings) {
    return settings->GetBool(kUseCameraZKey).value_or(true);
  }
  return true;
}

auto LightCullingSettingsService::SetUseCameraZ(bool use_camera) -> void
{
  const auto settings = ResolveSettings();
  if (settings) {
    settings->SetBool(kUseCameraZKey, use_camera);
    epoch_++;
  }
}

auto LightCullingSettingsService::GetZNear() const -> float
{
  const auto settings = ResolveSettings();
  if (settings) {
    return settings->GetFloat(kZNearKey).value_or(kDefaultZNear);
  }
  return kDefaultZNear;
}

auto LightCullingSettingsService::SetZNear(float z_near) -> void
{
  const auto settings = ResolveSettings();
  if (settings) {
    settings->SetFloat(kZNearKey, z_near);
    epoch_++;
  }
}

auto LightCullingSettingsService::GetZFar() const -> float
{
  const auto settings = ResolveSettings();
  if (settings) {
    return settings->GetFloat(kZFarKey).value_or(kDefaultZFar);
  }
  return kDefaultZFar;
}

auto LightCullingSettingsService::SetZFar(float z_far) -> void
{
  const auto settings = ResolveSettings();
  if (settings) {
    settings->SetFloat(kZFarKey, z_far);
    epoch_++;
  }
}

auto LightCullingSettingsService::GetVisualizationMode() const
  -> engine::ShaderDebugMode
{
  const auto settings = ResolveSettings();
  if (settings) {
    auto val = settings->GetString(kVisualizationModeKey).value_or("0");
    return static_cast<engine::ShaderDebugMode>(std::stoi(val));
  }
  return engine::ShaderDebugMode::kDisabled;
}

auto LightCullingSettingsService::SetVisualizationMode(
  engine::ShaderDebugMode mode) -> void
{
  const auto settings = ResolveSettings();
  if (settings) {
    settings->SetString(
      kVisualizationModeKey, std::to_string(static_cast<int>(mode)));
    epoch_++;

    if (pipeline_) {
      pipeline_->SetLightCullingVisualizationMode(mode);
    }
  }
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
