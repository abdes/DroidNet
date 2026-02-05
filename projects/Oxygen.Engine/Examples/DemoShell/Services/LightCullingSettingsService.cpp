//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Renderer/Passes/LightCullingPass.h>
#include <Oxygen/Renderer/Passes/ShaderPass.h>

#include "DemoShell/Runtime/RenderingPipeline.h"
#include "DemoShell/Services/LightCullingSettingsService.h"
#include "DemoShell/Services/SettingsService.h"

namespace oxygen::examples {

auto LightCullingSettingsService::Initialize(
  observer_ptr<RenderingPipeline> pipeline) -> void
{
  DCHECK_NOTNULL_F(pipeline);
  pipeline_ = pipeline;
}

auto LightCullingSettingsService::GetUseClusteredCulling() const -> bool
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  return settings->GetBool(kModeKey).value_or(true); // Default to Clustered
}

auto LightCullingSettingsService::SetUseClusteredCulling(bool enabled) -> void
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  settings->SetBool(kModeKey, enabled);
  epoch_++;
}

auto LightCullingSettingsService::GetDepthSlices() const -> int
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  auto val = settings->GetString(kDepthSlicesKey)
               .value_or(std::to_string(kDefaultDepthSlices));
  return std::stoi(val);
}

auto LightCullingSettingsService::SetDepthSlices(int slices) -> void
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  settings->SetString(kDepthSlicesKey, std::to_string(slices));
  epoch_++;
}

auto LightCullingSettingsService::GetUseCameraZ() const -> bool
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  return settings->GetBool(kUseCameraZKey).value_or(true);
}

auto LightCullingSettingsService::SetUseCameraZ(bool use_camera) -> void
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  settings->SetBool(kUseCameraZKey, use_camera);
  epoch_++;
}

auto LightCullingSettingsService::GetZNear() const -> float
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  return settings->GetFloat(kZNearKey).value_or(kDefaultZNear);
}

auto LightCullingSettingsService::SetZNear(float z_near) -> void
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  settings->SetFloat(kZNearKey, z_near);
  epoch_++;
}

auto LightCullingSettingsService::GetZFar() const -> float
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  return settings->GetFloat(kZFarKey).value_or(kDefaultZFar);
}

auto LightCullingSettingsService::SetZFar(float z_far) -> void
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  settings->SetFloat(kZFarKey, z_far);
  epoch_++;
}

auto LightCullingSettingsService::GetVisualizationMode() const
  -> engine::ShaderDebugMode
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  auto val = settings->GetString(kVisualizationModeKey).value_or("0");
  return static_cast<engine::ShaderDebugMode>(std::stoi(val));
}

auto LightCullingSettingsService::SetVisualizationMode(
  engine::ShaderDebugMode mode) -> void
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  settings->SetString(
    kVisualizationModeKey, std::to_string(static_cast<int>(mode)));
  epoch_++;
}

auto LightCullingSettingsService::GetEpoch() const noexcept -> std::uint64_t
{
  return epoch_.load(std::memory_order_acquire);
}

auto LightCullingSettingsService::OnFrameStart(
  const engine::FrameContext& /*context*/) -> void
{
}

auto LightCullingSettingsService::OnSceneActivated(scene::Scene& /*scene*/)
  -> void
{
}

auto LightCullingSettingsService::OnMainViewReady(
  const engine::FrameContext& /*context*/, const CompositionView& /*view*/)
  -> void
{
  ApplyPipelineSettings();
}

auto LightCullingSettingsService::ApplyPipelineSettings() -> void
{
  if (!pipeline_) {
    return;
  }

  const bool use_clustered = GetUseClusteredCulling();
  const auto depth_slices = static_cast<uint32_t>(GetDepthSlices());

  pipeline_->SetClusteredCullingEnabled(use_clustered);
  pipeline_->SetClusterDepthSlices(depth_slices);
  pipeline_->SetLightCullingVisualizationMode(GetVisualizationMode());

  engine::LightCullingPassConfig config {};
  config.cluster = engine::ClusterConfig::TileBased();
  config.cluster.depth_slices = use_clustered ? depth_slices : 1U;

  if (GetUseCameraZ()) {
    config.cluster.z_near = 0.0F;
    config.cluster.z_far = 0.0F;
  } else {
    config.cluster.z_near = GetZNear();
    config.cluster.z_far = GetZFar();
  }

  pipeline_->UpdateLightCullingPassConfig(config);
}

} // namespace oxygen::examples
