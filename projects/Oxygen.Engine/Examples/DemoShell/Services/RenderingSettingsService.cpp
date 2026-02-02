//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "DemoShell/Services/RenderingSettingsService.h"
#include "DemoShell/Runtime/RenderingPipeline.h"
#include "DemoShell/Services/SettingsService.h"

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Renderer/Passes/ShaderPass.h>

namespace oxygen::examples {

auto RenderingSettingsService::Initialize(
  observer_ptr<RenderingPipeline> pipeline) -> void
{
  DCHECK_NOTNULL_F(pipeline);
  pipeline_ = pipeline;

  // Push initial state
  pipeline_->SetShaderDebugMode(GetDebugMode());
  pipeline_->SetRenderMode(GetRenderMode());
  pipeline_->SetWireframeColor(GetWireframeColor());
}

auto RenderingSettingsService::GetRenderMode() const -> RenderMode
{
  const auto settings = ResolveSettings();
  if (settings) {
    auto val = settings->GetString(kViewModeKey).value_or("solid");
    if (val == "wireframe") {
      return RenderMode::kWireframe;
    }
    if (val == "overlay_wireframe") {
      return RenderMode::kOverlayWireframe;
    }
  }
  return RenderMode::kSolid;
}

auto RenderingSettingsService::SetRenderMode(RenderMode mode) -> void
{
  const auto settings = ResolveSettings();
  if (settings) {
    settings->SetString(kViewModeKey, std::string(to_string(mode)));
    epoch_++;

    if (pipeline_) {
      pipeline_->SetRenderMode(mode);
    }
  }
}

auto RenderingSettingsService::GetWireframeColor() const -> graphics::Color
{
  const auto settings = ResolveSettings();
  const auto r
    = settings ? settings->GetFloat(kWireColorRKey).value_or(1.0F) : 1.0F;
  const auto g
    = settings ? settings->GetFloat(kWireColorGKey).value_or(1.0F) : 1.0F;
  const auto b
    = settings ? settings->GetFloat(kWireColorBKey).value_or(1.0F) : 1.0F;
  return graphics::Color { r, g, b, 1.0F };
}

auto RenderingSettingsService::SetWireframeColor(const graphics::Color& color)
  -> void
{
  const auto settings = ResolveSettings();
  if (settings) {
    settings->SetFloat(kWireColorRKey, color.r);
    settings->SetFloat(kWireColorGKey, color.g);
    settings->SetFloat(kWireColorBKey, color.b);
    epoch_++;

    if (pipeline_) {
      pipeline_->SetWireframeColor(color);
    }
  }
}

auto RenderingSettingsService::GetDebugMode() const -> engine::ShaderDebugMode
{
  const auto settings = ResolveSettings();
  if (settings) {
    auto val = settings->GetString(kDebugModeKey).value_or("0");
    return static_cast<engine::ShaderDebugMode>(std::stoi(val));
  }
  return engine::ShaderDebugMode::kDisabled;
}

auto RenderingSettingsService::SetDebugMode(engine::ShaderDebugMode mode)
  -> void
{
  const auto settings = ResolveSettings();
  if (settings) {
    settings->SetString(kDebugModeKey, std::to_string(static_cast<int>(mode)));
    epoch_++;

    if (pipeline_) {
      pipeline_->SetShaderDebugMode(mode);
    }
  }
}
auto RenderingSettingsService::GetEpoch() const noexcept -> std::uint64_t
{
  return epoch_.load(std::memory_order_acquire);
}

auto RenderingSettingsService::ResolveSettings() const noexcept
  -> observer_ptr<SettingsService>
{
  return SettingsService::Default();
}

} // namespace oxygen::examples
