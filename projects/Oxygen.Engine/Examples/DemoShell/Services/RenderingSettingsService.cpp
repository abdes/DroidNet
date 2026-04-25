//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <optional>
#include <string_view>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Vortex/Renderer.h>

#include "DemoShell/Services/RenderingSettingsService.h"
#include "DemoShell/Services/SettingsService.h"

namespace oxygen::examples {

namespace {

  auto ParseShadowQualityTierString(std::string_view value)
    -> std::optional<ShadowQualityTier>
  {
    if (value == "low") {
      return ShadowQualityTier::kLow;
    }
    if (value == "medium") {
      return ShadowQualityTier::kMedium;
    }
    if (value == "high") {
      return ShadowQualityTier::kHigh;
    }
    if (value == "ultra") {
      return ShadowQualityTier::kUltra;
    }
    return std::nullopt;
  }

  auto ShadowQualityTierToString(const ShadowQualityTier tier)
    -> std::string_view
  {
    switch (tier) {
    case ShadowQualityTier::kLow:
      return "low";
    case ShadowQualityTier::kMedium:
      return "medium";
    case ShadowQualityTier::kHigh:
      return "high";
    case ShadowQualityTier::kUltra:
      return "ultra";
    default:
      return "ultra";
    }
  }

  auto ClampShadowQualityTierIndex(const float value) -> ShadowQualityTier
  {
    const auto clamped = static_cast<int>(std::clamp(value, 0.0F, 3.0F) + 0.5F);
    return static_cast<ShadowQualityTier>(clamped);
  }

  auto ToVortexDebugMode(const engine::ShaderDebugMode mode)
    -> vortex::ShaderDebugMode
  {
    return static_cast<vortex::ShaderDebugMode>(mode);
  }

  auto SupportsVortexDebugMode(const engine::ShaderDebugMode mode) -> bool
  {
    using engine::ShaderDebugMode;
    switch (mode) {
    case ShaderDebugMode::kDisabled:
    case ShaderDebugMode::kLightCullingHeatMap:
    case ShaderDebugMode::kDepthSlice:
    case ShaderDebugMode::kClusterIndex:
    case ShaderDebugMode::kIblSpecular:
    case ShaderDebugMode::kIblRawSky:
    case ShaderDebugMode::kIblIrradiance:
    case ShaderDebugMode::kBaseColor:
    case ShaderDebugMode::kUv0:
    case ShaderDebugMode::kOpacity:
    case ShaderDebugMode::kWorldNormals:
    case ShaderDebugMode::kRoughness:
    case ShaderDebugMode::kMetalness:
    case ShaderDebugMode::kIblFaceIndex:
    case ShaderDebugMode::kIblNoBrdfLut:
    case ShaderDebugMode::kDirectLightingOnly:
    case ShaderDebugMode::kIblOnly:
    case ShaderDebugMode::kDirectPlusIbl:
    case ShaderDebugMode::kDirectLightingFull:
    case ShaderDebugMode::kDirectLightGates:
    case ShaderDebugMode::kDirectBrdfCore:
    case ShaderDebugMode::kDirectionalShadowMask:
    case ShaderDebugMode::kSceneDepthRaw:
    case ShaderDebugMode::kSceneDepthLinear:
    case ShaderDebugMode::kSceneDepthMismatch:
    case ShaderDebugMode::kMaskedAlphaCoverage:
      return true;
    default:
      return false;
    }
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
  const auto mode = static_cast<engine::ShaderDebugMode>(std::stoi(val));
  if ((pipeline_ == nullptr) && (vortex_renderer_ != nullptr)
    && !SupportsDebugMode(mode)) {
    return engine::ShaderDebugMode::kDisabled;
  }
  return mode;
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
  if ((pipeline_ == nullptr) && (vortex_renderer_ != nullptr)
    && !SupportsDebugMode(mode)) {
    mode = engine::ShaderDebugMode::kDisabled;
  }

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

auto RenderingSettingsService::GetShadowQualityTier() const -> ShadowQualityTier
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  if (const auto value = settings->GetString(kShadowQualityTierKey)) {
    if (const auto parsed = ParseShadowQualityTierString(*value)) {
      return *parsed;
    }
  }
  if (const auto value = settings->GetFloat(kShadowQualityTierKey)) {
    return ClampShadowQualityTierIndex(*value);
  }
  // Keep RenderScene's current startup behavior unless the user overrides it.
  return ShadowQualityTier::kUltra;
}

auto RenderingSettingsService::SetShadowQualityTier(
  const ShadowQualityTier tier) -> void
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  settings->SetString(
    kShadowQualityTierKey, std::string(ShadowQualityTierToString(tier)));
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
  return false;
}

auto RenderingSettingsService::SupportsWireframeColorControl() const -> bool
{
  return false;
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
  return (vortex_renderer_ != nullptr) && SupportsVortexDebugMode(mode);
}

auto RenderingSettingsService::IsVortexRuntimeBound() const -> bool
{
  return (pipeline_ == nullptr) && (vortex_renderer_ != nullptr);
}

auto RenderingSettingsService::ApplyVortexSettings() -> void
{
  if ((pipeline_ != nullptr) || (vortex_renderer_ == nullptr)) {
    return;
  }

  vortex_renderer_->SetShaderDebugMode(ToVortexDebugMode(GetDebugMode()));
}

} // namespace oxygen::examples
