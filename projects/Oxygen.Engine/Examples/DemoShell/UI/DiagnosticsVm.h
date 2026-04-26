//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <mutex>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Config/RendererConfig.h>
#include <Oxygen/Vortex/RendererCapability.h>

#include "DemoShell/Runtime/RendererUiTypes.h"

#include "DemoShell/Services/RenderingSettingsService.h"

namespace oxygen::examples::ui {

//! View model for the Vortex diagnostics panel state.
/*!
 Caches rendering settings retrieved from `RenderingSettingsService`,
 invalidating the cache based on the service epoch and applying UI changes back
 to the service.

### Key Features

- **Epoch-driven refresh**: Reacquires state when stale.
- **Immediate persistence**: Setters forward changes to the service.
- **Pass config sync**: Applies debug mode changes to the shader pass config.
- **Thread-safe**: Protected by a mutex for access from UI and render threads.

@see oxygen::examples::RenderingSettingsService
*/
class DiagnosticsVm {
public:
  //! Creates a view model backed by the provided settings service.
  explicit DiagnosticsVm(observer_ptr<RenderingSettingsService> service);

  //! Returns the cached view mode.
  [[nodiscard]] auto GetRenderMode() -> renderer::RenderMode;

  //! Returns the requested shader debug mode stored by the UI.
  [[nodiscard]] auto GetRequestedDebugMode() -> engine::ShaderDebugMode;

  //! Returns the shader debug mode accepted by the Vortex renderer.
  [[nodiscard]] auto GetEffectiveDebugMode() -> engine::ShaderDebugMode;

  //! Returns the renderer capabilities currently visible to the UI.
  [[nodiscard]] auto GetRendererCapabilities() -> vortex::CapabilitySet;

  //! Returns whether the GPU debug pass is currently enabled.
  [[nodiscard]] auto GetGpuDebugPassEnabled() -> bool;
  //! Returns whether atmosphere blue-noise jitter is currently enabled.
  [[nodiscard]] auto GetAtmosphereBlueNoiseEnabled() -> bool;
  //! Returns the persisted directional shadow quality tier.
  [[nodiscard]] auto GetShadowQualityTier() -> ShadowQualityTier;
  [[nodiscard]] auto SupportsRenderModeControls() const -> bool;
  [[nodiscard]] auto SupportsWireframeColorControl() const -> bool;
  [[nodiscard]] auto SupportsGpuDebugPassControl() const -> bool;
  [[nodiscard]] auto SupportsAtmosphereBlueNoiseControl() const -> bool;
  [[nodiscard]] auto SupportsDebugMode(engine::ShaderDebugMode mode) const
    -> bool;
  [[nodiscard]] auto IsVortexRuntimeBound() const -> bool;

  //! Sets view mode and forwards changes to the service.
  auto SetRenderMode(renderer::RenderMode mode) -> void;

  //! Sets the requested debug mode and forwards changes to the service.
  auto SetDebugMode(engine::ShaderDebugMode mode) -> void;

  //! Toggles the GPU debug pass and persists the change.
  auto SetGpuDebugPassEnabled(bool enabled) -> void;
  //! Toggles atmosphere blue-noise jitter and persists the change.
  auto SetAtmosphereBlueNoiseEnabled(bool enabled) -> void;
  //! Persists the shadow quality tier for the next renderer initialization.
  auto SetShadowQualityTier(ShadowQualityTier tier) -> void;

  [[nodiscard]] auto GetWireframeColor() -> graphics::Color;
  auto SetWireframeColor(const graphics::Color& color) -> void;

private:
  auto Refresh() -> void;
  [[nodiscard]] auto IsStale() const -> bool;

  mutable std::mutex mutex_ {};
  observer_ptr<RenderingSettingsService> service_;
  std::uint64_t epoch_ { 0 };
  renderer::RenderMode render_mode_ { renderer::RenderMode::kSolid };
  engine::ShaderDebugMode requested_debug_mode_ {
    engine::ShaderDebugMode::kDisabled
  };
  engine::ShaderDebugMode effective_debug_mode_ {
    engine::ShaderDebugMode::kDisabled
  };
  vortex::CapabilitySet renderer_capabilities_ {
    vortex::RendererCapabilityFamily::kNone
  };
  graphics::Color wire_color_ { 1.0F, 1.0F, 1.0F, 1.0F };
  bool gpu_debug_pass_enabled_ { true };
  bool atmosphere_blue_noise_enabled_ { true };
  ShadowQualityTier shadow_quality_tier_ { ShadowQualityTier::kUltra };
};

} // namespace oxygen::examples::ui
