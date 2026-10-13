//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <atomic>
#include <cstdint>

#include "DemoShell/Services/RenderingSettingsService.h"
#include "DemoShell/Services/SettingsService.h"

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Core/Types/ViewPort.h>
#include <Oxygen/Graphics/Common/Types/Color.h>
#include <Oxygen/Scene/SceneNode.h>
#include <Oxygen/Vortex/Diagnostics/DiagnosticsService.h>
#include <Oxygen/Vortex/Diagnostics/DiagnosticsTypes.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/RendererCapability.h>

namespace oxygen::examples {

auto RenderingSettingsService::BindVortexRenderer(
  observer_ptr<vortex::Renderer> renderer) -> void
{
  DCHECK_NOTNULL_F(renderer);
  vortex_renderer_ = renderer;
  ApplyVortexSettings();
}

auto RenderingSettingsService::IsVortexRuntimeBound() const -> bool
{
  return vortex_renderer_ != nullptr;
}

auto RenderingSettingsService::SupportsDiagnostics() const -> bool
{
  return vortex_renderer_
    && vortex::HasAllCapabilities(vortex_renderer_->GetCapabilityFamilies(),
      vortex::RendererCapabilityFamily::kDiagnosticsAndProfiling);
}

auto RenderingSettingsService::GetFrameDiagnosticsEnabled() const -> bool
{
  return vortex_renderer_
    && vortex::HasAnyFeature(
      vortex_renderer_->GetDiagnosticsService().GetEnabledFeatures(),
      vortex::DiagnosticsFeature::kFrameLedger);
}

auto RenderingSettingsService::SetFrameDiagnosticsEnabled(const bool enabled)
  -> void
{
  if (!vortex_renderer_) {
    return;
  }
  auto& diagnostics = vortex_renderer_->GetDiagnosticsService();
  const auto features = diagnostics.GetRequestedFeatures();
  diagnostics.SetEnabledFeatures(enabled
      ? features | vortex::DiagnosticsFeature::kFrameLedger
      : features & ~vortex::DiagnosticsFeature::kFrameLedger);
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
  settings->SetFloat(kWireColorRKey, color.r);
  settings->SetFloat(kWireColorGKey, color.g);
  settings->SetFloat(kWireColorBKey, color.b);
  epoch_++;
  ApplyVortexSettings();
}

auto RenderingSettingsService::GetGpuDebugPassEnabled() const -> bool
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  return settings->GetBool(kGpuDebugPassEnabledKey).value_or(false);
}

auto RenderingSettingsService::SetGpuDebugPassEnabled(const bool enabled)
  -> void
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  settings->SetBool(kGpuDebugPassEnabledKey, enabled);
  epoch_++;
  ApplyVortexSettings();
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

auto RenderingSettingsService::OnMainViewReady(ViewId /*view_id*/,
  const scene::SceneNode& /*camera*/, const ViewPort& /*viewport*/) -> void
{
}

auto RenderingSettingsService::ApplyVortexSettings() -> void
{
  if (vortex_renderer_ == nullptr) {
    return;
  }

  vortex_renderer_->SetWireframeColor(GetWireframeColor());

  // The GPU debug pass shows the draw bounds producer's lines.
  auto& diagnostics = vortex_renderer_->GetDiagnosticsService();
  const auto requested = diagnostics.GetRequestedFeatures();
  const auto gpu_debug = GetGpuDebugPassEnabled();
  const auto features = gpu_debug
    ? requested | vortex::DiagnosticsFeature::kGpuDebugPrimitives
    : requested & ~vortex::DiagnosticsFeature::kGpuDebugPrimitives;
  if (features != requested) {
    diagnostics.SetEnabledFeatures(features);
  }
  diagnostics.SetGpuDebugPrimitiveSources(gpu_debug
      ? vortex::GpuDebugPrimitiveSource::kDrawBounds
      : vortex::GpuDebugPrimitiveSource::kNone);
}

} // namespace oxygen::examples
