//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "DemoShell/DemoShell.h"
#include "DemoShell/Runtime/DemoModuleBase.h"
#include "MultiView/CompositingMode.h"
#include "MultiView/SceneBootstrapper.h"

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Types/ViewPort.h>
#include <Oxygen/Host/HostConfig.h>
#include <Oxygen/Host/SceneStage.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/Platform/Window.h>

namespace oxygen {
class IAsyncEngine;
} // namespace oxygen

namespace oxygen::examples::multiview {

enum class ExposureProofScenario : uint8_t {
  kNone,
  kIndependent,
  kShared,
  kMainOnly,
  kPipOnly,
  kReordered,
  kSourceLoss,
  kViewport,
  kLifetime,
  kWindowResize,
  kModes,
  kAtmosphere,
  kAtmosphereLit,
  kConsumerVisual,
  kLayouts,
  kMixed,
  kInteractions,
};

struct MainModuleConfig {
  CompositingMode compositing_mode { CompositingMode::kBlend };
  bool pip_force_wireframe { true };
  uint32_t pip_scissor_inset_px { 0U };
  bool proof_layout { false };
  bool aux_proof_layout { false };
  bool offscreen_proof_layout { false };
  bool feature_variant_proof_layout { false };
  bool proof_wireframe_overlay { false };
  bool point_light_enabled { true };
  bool spot_light_enabled { true };
  bool directional_array_proof { false };
  ExposureProofScenario exposure_proof { ExposureProofScenario::kNone };
  std::string exposure_view_only;
  VisualFogMode visual_fog_mode { VisualFogMode::kVolumetric };
  bool visual_fog_cycle { false };
  bool visual_fog_hold_local { false };
  bool visual_fog_jitter { true };
};

//! Multi-view rendering example demonstrating Phase 2 features.
/*!
 This example showcases multi-view rendering with:
 - Main view: Full-screen solid-shaded sphere
 - PiP view: Top-right corner (45% size)

 Both views render the same scene with different cameras.
 Demonstrates PrepareView/RenderView APIs and per-view state isolation.

 Integrates with DemoShell for architectural consistency and enables the
 camera controls panel for interactive navigation.
*/
class MainModule final : public examples::DemoModuleBase {
  OXYGEN_TYPED(MainModule)

public:
  using Base = examples::DemoModuleBase;

  MainModule(const host::HostContext& host, MainModuleConfig config) noexcept;
  ~MainModule() override = default;

  OXYGEN_MAKE_NON_COPYABLE(MainModule);
  OXYGEN_MAKE_NON_MOVABLE(MainModule);

  [[nodiscard]] auto GetName() const noexcept -> std::string_view override
  {
    return "MultiViewExample";
  }

  [[nodiscard]] auto GetPriority() const noexcept
    -> engine::ModulePriority override
  {
    constexpr int32_t kPriority = 500;
    return engine::ModulePriority { kPriority };
  }

  [[nodiscard]] auto GetSupportedPhases() const noexcept
    -> engine::ModulePhaseMask override
  {
    return kDemoPhases
      | engine::MakeModuleMask<core::PhaseId::kPreRender,
        core::PhaseId::kFrameEnd>();
  }

  [[nodiscard]] auto IsCritical() const noexcept -> bool override
  {
    return true; // Critical module
  }

  auto OnAttachedImpl(observer_ptr<IAsyncEngine> engine) noexcept
    -> std::unique_ptr<DemoShell> override;
  void OnShutdown() noexcept override;

  auto OnFrameStart(observer_ptr<engine::FrameContext> context)
    -> void override;
  auto OnSceneMutation(observer_ptr<engine::FrameContext> context)
    -> co::Co<> override;
  auto OnGuiUpdate(observer_ptr<engine::FrameContext> context)
    -> co::Co<> override;
  auto OnPreRender(observer_ptr<engine::FrameContext> context)
    -> co::Co<> override;
  auto OnFrameEnd(observer_ptr<engine::FrameContext> context) -> void override;

protected:
  [[nodiscard]] auto BuildWindowProperties() const
    -> platform::window::Properties override;
  [[nodiscard]] auto RetainInactiveView(ViewId view_id) const noexcept
    -> bool override;
  auto OnScenePublished(engine::FrameContext& context) -> void override;
  auto BuildViews(engine::FrameContext& context, host::HostViews& views)
    -> void override;

private:
  //! Stages the scene with every camera the layout uses.
  auto StageScene() -> void;
  auto BuildComposition(engine::FrameContext& context,
    std::vector<vortex::CompositionView>& views) -> void;

  struct OffscreenProofProduct {
    std::shared_ptr<graphics::Framebuffer> framebuffer;
    uint32_t width { 0U };
    uint32_t height { 0U };
  };

  [[nodiscard]] auto ResolveRenderExtent() const -> platform::window::ExtentT;
  auto UpdateCameras(const platform::window::ExtentT& extent) -> void;
  auto EnsureOffscreenProofProduct(OffscreenProofProduct& product,
    uint32_t width, uint32_t height, std::string_view debug_name) -> bool;
  auto RenderOffscreenProofProducts(engine::FrameContext& context) -> void;
  auto AppendCompositionLayers(engine::FrameContext& context,
    vortex::Renderer::RuntimeCompositionInput& input) -> void override;
  auto DrawFeatureVariantProofOverlay() -> void;
  auto DrawConsumerVisualControls() -> void;
  auto ApplyInteractionProof(engine::FrameContext& context,
    std::vector<vortex::CompositionView>& views) -> void;
  auto DrawInteractionProofOverlay(std::uint64_t frame) -> void;

  SceneBootstrapper scene_bootstrapper_;
  host::ActiveScene active_scene_;

  // View identifiers
  ViewId main_view_id_ { kInvalidViewId };
  ViewId pip_view_id_ { kInvalidViewId };
  ViewId top_view_id_ { kInvalidViewId };
  ViewId debug_view_id_ { kInvalidViewId };
  ViewId shadow_view_id_ { kInvalidViewId };
  ViewId diagnostics_view_id_ { kInvalidViewId };

  // Cameras
  scene::SceneNode main_camera_node_;
  scene::SceneNode pip_camera_node_;
  scene::SceneNode top_camera_node_;
  scene::SceneNode debug_camera_node_;
  scene::SceneNode shadow_camera_node_;
  scene::SceneNode diagnostics_camera_node_;
  scene::SceneNode offscreen_preview_camera_node_;
  scene::SceneNode offscreen_capture_camera_node_;

  OffscreenProofProduct offscreen_preview_;
  OffscreenProofProduct offscreen_capture_;

  //! The view the camera controls drive this frame.
  std::optional<ViewId> main_view_;
  MainModuleConfig config_ {};
  std::optional<VisualFogMode> pending_visual_fog_mode_;
  bool viewport_proof_resized_ { false };
  bool viewport_proof_scissored_ { false };
  bool retain_inactive_pip_ { false };
  platform::window::ExtentT window_proof_initial_extent_ { .width = 0U,
    .height = 0U };
  platform::window::ExtentT last_viewport_ { .width = 0, .height = 0 };
};

} // namespace oxygen::examples::multiview
