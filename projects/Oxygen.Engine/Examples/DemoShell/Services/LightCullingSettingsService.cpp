//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Vortex/Renderer.h>

#include "DemoShell/Services/LightCullingSettingsService.h"
#include "DemoShell/Services/SettingsService.h"

namespace oxygen::examples {

auto LightCullingSettingsService::Initialize(
  observer_ptr<renderer::RenderingPipeline> pipeline) -> void
{
  DCHECK_NOTNULL_F(pipeline);
  pipeline_ = pipeline;
}

auto LightCullingSettingsService::BindVortexRenderer(
  observer_ptr<vortex::Renderer> renderer) -> void
{
  DCHECK_NOTNULL_F(renderer);
  vortex_renderer_ = renderer;
  ApplyVisualizationSettings();
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
  const engine::FrameContext& /*context*/,
  const vortex::CompositionView& /*view*/) -> void
{
  ApplyVisualizationSettings();
}

auto LightCullingSettingsService::ApplyVisualizationSettings() -> void
{
  if (vortex_renderer_ != nullptr) {
    const auto mode = GetVisualizationMode();
    if (engine::IsLightCullingDebugMode(mode)) {
      vortex_renderer_->SetShaderDebugMode(
        static_cast<vortex::ShaderDebugMode>(mode));
    }
  }
}

} // namespace oxygen::examples
