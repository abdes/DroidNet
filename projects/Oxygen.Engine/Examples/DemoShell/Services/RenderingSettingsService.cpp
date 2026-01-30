//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "DemoShell/Services/RenderingSettingsService.h"
#include "DemoShell/Services/SettingsService.h"

namespace oxygen::examples {

auto RenderingSettingsService::GetViewMode() const -> ViewMode
{
  const auto settings = ResolveSettings();
  if (!settings) {
    return ViewMode::kSolid;
  }

  if (const auto value = settings->GetString(kViewModeKey)) {
    if (*value == "wireframe") {
      return ViewMode::kWireframe;
    }
  }
  return ViewMode::kSolid;
}

auto RenderingSettingsService::SetViewMode(ViewMode mode) -> void
{
  const auto settings = ResolveSettings();
  if (!settings) {
    return;
  }

  const char* value = (mode == ViewMode::kWireframe) ? "wireframe" : "solid";
  settings->SetString(kViewModeKey, value);
  ++epoch_;
}

auto RenderingSettingsService::GetDebugMode() const -> engine::ShaderDebugMode
{
  const auto settings = ResolveSettings();
  if (!settings) {
    return engine::ShaderDebugMode::kDisabled;
  }

  if (const auto value = settings->GetFloat(kDebugModeKey)) {
    return static_cast<engine::ShaderDebugMode>(static_cast<int>(*value));
  }
  return engine::ShaderDebugMode::kDisabled;
}

auto RenderingSettingsService::SetDebugMode(engine::ShaderDebugMode mode)
  -> void
{
  const auto settings = ResolveSettings();
  if (!settings) {
    return;
  }

  settings->SetFloat(kDebugModeKey, static_cast<float>(static_cast<int>(mode)));
  ++epoch_;
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
