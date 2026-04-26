//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <exception>
#include <string>
#include <string_view>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Vortex/Diagnostics/ShaderDebugModeRegistry.h>
#include <Oxygen/Vortex/Renderer.h>

#include "DemoShell/Services/RenderingSettingsService.h"
#include "DemoShell/Services/SettingsService.h"

namespace oxygen::examples {

namespace {

  auto ToVortexDebugMode(const engine::ShaderDebugMode mode)
    -> vortex::ShaderDebugMode
  {
    return static_cast<vortex::ShaderDebugMode>(mode);
  }

  auto ParseShaderDebugModeOrDisabled(std::string_view value)
    -> engine::ShaderDebugMode
  {
    try {
      return static_cast<engine::ShaderDebugMode>(
        std::stoi(std::string(value)));
    } catch (const std::exception&) {
      return engine::ShaderDebugMode::kDisabled;
    }
  }

  auto SupportsVortexDebugMode(const engine::ShaderDebugMode mode,
    const vortex::CapabilitySet capabilities) -> bool
  {
    const auto* info = vortex::FindShaderDebugModeInfo(ToVortexDebugMode(mode));
    if (info == nullptr || !info->supported) {
      return false;
    }
    return vortex::HasAllCapabilities(
      capabilities, info->required_capabilities);
  }

} // namespace

auto RenderingSettingsService::Initialize(
  observer_ptr<renderer::RenderingPipeline> pipeline) -> void
{
  DCHECK_NOTNULL_F(pipeline);
  pipeline_ = pipeline;
}

auto RenderingSettingsService::BindVortexRenderer(
  observer_ptr<vortex::Renderer> renderer) -> void
{
  DCHECK_NOTNULL_F(renderer);
  vortex_renderer_ = renderer;
  ApplyVortexSettings();
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
  ApplyVortexSettings();
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
  ApplyVortexSettings();
}

auto RenderingSettingsService::GetDebugMode() const -> engine::ShaderDebugMode
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  auto val = settings->GetString(kDebugModeKey).value_or("0");
  return ParseShaderDebugModeOrDisabled(val);
}

auto RenderingSettingsService::GetEffectiveDebugMode() const
  -> engine::ShaderDebugMode
{
  const auto requested = GetDebugMode();
  if ((pipeline_ == nullptr) && (vortex_renderer_ != nullptr)
    && !SupportsDebugMode(requested)) {
    return engine::ShaderDebugMode::kDisabled;
  }
  return requested;
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
  ApplyVortexSettings();
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
  ApplyVortexSettings();
}

auto RenderingSettingsService::OnSceneActivated(scene::Scene& /*scene*/) -> void
{
}

auto RenderingSettingsService::OnMainViewReady(
  const engine::FrameContext& /*context*/,
  const vortex::CompositionView& /*view*/) -> void
{
  ApplyVortexSettings();
}

auto RenderingSettingsService::SupportsRenderModeControls() const -> bool
{
  return IsVortexRuntimeBound();
}

auto RenderingSettingsService::SupportsWireframeColorControl() const -> bool
{
  return IsVortexRuntimeBound();
}

auto RenderingSettingsService::SupportsGpuDebugPassControl() const -> bool
{
  return false;
}

auto RenderingSettingsService::SupportsAtmosphereBlueNoiseControl() const
  -> bool
{
  return false;
}

auto RenderingSettingsService::SupportsDebugMode(
  const engine::ShaderDebugMode mode) const -> bool
{
  if (pipeline_ != nullptr) {
    return true;
  }
  return (vortex_renderer_ != nullptr)
    && SupportsVortexDebugMode(mode, vortex_renderer_->GetCapabilityFamilies());
}

auto RenderingSettingsService::IsVortexRuntimeBound() const -> bool
{
  return (pipeline_ == nullptr) && (vortex_renderer_ != nullptr);
}

auto RenderingSettingsService::GetRendererCapabilities() const
  -> vortex::CapabilitySet
{
  if (vortex_renderer_ == nullptr) {
    return vortex::RendererCapabilityFamily::kNone;
  }
  return vortex_renderer_->GetCapabilityFamilies();
}

auto RenderingSettingsService::ApplyVortexSettings() -> void
{
  if ((pipeline_ != nullptr) || (vortex_renderer_ == nullptr)) {
    return;
  }

  vortex_renderer_->SetShaderDebugMode(
    ToVortexDebugMode(GetEffectiveDebugMode()));
  vortex_renderer_->SetRenderMode(GetRenderMode());
  vortex_renderer_->SetWireframeColor(GetWireframeColor());
}

} // namespace oxygen::examples
