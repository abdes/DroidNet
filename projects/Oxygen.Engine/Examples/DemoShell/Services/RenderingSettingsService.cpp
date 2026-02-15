//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Renderer/Passes/ShaderPass.h>
#include <Oxygen/Renderer/Pipeline/RenderingPipeline.h>

#include "DemoShell/Services/RenderingSettingsService.h"
#include "DemoShell/Services/SettingsService.h"

namespace oxygen::examples {

auto RenderingSettingsService::Initialize(
  observer_ptr<renderer::RenderingPipeline> pipeline) -> void
{
  DCHECK_NOTNULL_F(pipeline);
  pipeline_ = pipeline;
}

auto RenderingSettingsService::GetRenderMode() const -> renderer::RenderMode
{
  using renderer::RenderMode;
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);

  auto val = settings->GetString(kViewModeKey).value_or("solid");
  if (val == "wireframe") {
    return RenderMode::kWireframe;
  }
  if (val == "overlay_wireframe") {
    return RenderMode::kOverlayWireframe;
  }
  return RenderMode::kSolid;
}

auto RenderingSettingsService::SetRenderMode(renderer::RenderMode mode) -> void
{
  using renderer::RenderMode;
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  const char* value = "solid";
  switch (mode) {
  case RenderMode::kWireframe:
    value = "wireframe";
    break;
  case RenderMode::kOverlayWireframe:
    value = "overlay_wireframe";
    break;
  case RenderMode::kSolid:
  default:
    value = "solid";
    break;
  }
  settings->SetString(kViewModeKey, value);
  epoch_++;
}

auto RenderingSettingsService::GetWireframeColor() const -> graphics::Color
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  const auto r = settings->GetFloat(kWireColorRKey).value_or(1.0F);
  const auto g = settings->GetFloat(kWireColorGKey).value_or(1.0F);
  const auto b = settings->GetFloat(kWireColorBKey).value_or(1.0F);
  return graphics::Color { r, g, b, 1.0F };
}

auto RenderingSettingsService::SetWireframeColor(const graphics::Color& color)
  -> void
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  LOG_F(INFO, "RenderingSettingsService: SetWireframeColor ({}, {}, {}, {})",
    color.r, color.g, color.b, color.a);
  settings->SetFloat(kWireColorRKey, color.r);
  settings->SetFloat(kWireColorGKey, color.g);
  settings->SetFloat(kWireColorBKey, color.b);
  epoch_++;
}

auto RenderingSettingsService::GetDebugMode() const -> engine::ShaderDebugMode
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  auto val = settings->GetString(kDebugModeKey).value_or("0");
  return static_cast<engine::ShaderDebugMode>(std::stoi(val));
}

auto RenderingSettingsService::GetGpuDebugPassEnabled() const -> bool
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  return settings->GetBool(kGpuDebugPassEnabledKey).value_or(true);
}

auto RenderingSettingsService::SetDebugMode(engine::ShaderDebugMode mode)
  -> void
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  settings->SetString(kDebugModeKey, std::to_string(static_cast<int>(mode)));
  epoch_++;
}
auto RenderingSettingsService::SetGpuDebugPassEnabled(bool enabled) -> void
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  settings->SetBool(kGpuDebugPassEnabledKey, enabled);
  epoch_++;
}

auto RenderingSettingsService::GetAtmosphereBlueNoiseEnabled() const -> bool
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  return settings->GetBool(kAtmosphereBlueNoiseEnabledKey).value_or(true);
}

auto RenderingSettingsService::SetAtmosphereBlueNoiseEnabled(bool enabled)
  -> void
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  settings->SetBool(kAtmosphereBlueNoiseEnabledKey, enabled);
  epoch_++;
}
auto RenderingSettingsService::GetEpoch() const noexcept -> std::uint64_t
{
  return epoch_.load(std::memory_order_acquire);
}

auto RenderingSettingsService::OnFrameStart(
  const engine::FrameContext& /*context*/) -> void
{
}

auto RenderingSettingsService::OnSceneActivated(scene::Scene& /*scene*/) -> void
{
}

auto RenderingSettingsService::OnMainViewReady(
  const engine::FrameContext& /*context*/,
  const renderer::CompositionView& /*view*/) -> void
{
  if (!pipeline_) {
    return;
  }

  pipeline_->SetRenderMode(GetRenderMode());
  pipeline_->SetWireframeColor(GetWireframeColor());
  pipeline_->SetShaderDebugMode(GetDebugMode());
  pipeline_->SetGpuDebugPassEnabled(GetGpuDebugPassEnabled());
  pipeline_->SetAtmosphereBlueNoiseEnabled(GetAtmosphereBlueNoiseEnabled());
}

} // namespace oxygen::examples
