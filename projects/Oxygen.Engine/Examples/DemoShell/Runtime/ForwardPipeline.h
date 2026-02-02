//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <span>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Composition/TypedObject.h>
#include <Oxygen/ImGui/ImGuiPass.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/Renderer/Passes/DepthPrePass.h>
#include <Oxygen/Renderer/Passes/LightCullingPass.h>
#include <Oxygen/Renderer/Passes/ShaderPass.h>
#include <Oxygen/Renderer/Passes/SkyAtmosphereLutComputePass.h>
#include <Oxygen/Renderer/Passes/SkyPass.h>
#include <Oxygen/Renderer/Passes/TransparentPass.h>
#include <Oxygen/Renderer/Passes/WireframePass.h>

#include "DemoShell/Runtime/RenderingPipeline.h"

namespace oxygen {
class AsyncEngine;
class Graphics;
namespace imgui {
  class ImGuiModule;
} // namespace imgui
} // namespace oxygen

namespace oxygen::examples {

class DemoView;

//! Implements a standard forward rendering pipeline.
/*!
  Manages the configuration and execution of a forward rendering pass sequence
  (Light Culling -> Z-Prepass -> Opaque -> Transparent) for multiple views.
*/
class ForwardPipeline : public RenderingPipeline {
  OXYGEN_TYPED(ForwardPipeline)
public:
  explicit ForwardPipeline(observer_ptr<AsyncEngine> engine) noexcept;

  ~ForwardPipeline() override;

  // Pipeline Interface
  auto OnFrameStart(engine::FrameContext& context, engine::Renderer& renderer)
    -> void override;
  auto OnSceneMutation(engine::FrameContext& context,
    engine::Renderer& renderer, scene::Scene& scene, std::span<DemoView*> views,
    graphics::Framebuffer* target_framebuffer) -> co::Co<> override;

  auto OnPreRender(engine::FrameContext& context, engine::Renderer& renderer,
    std::span<DemoView*> views) -> co::Co<> override;

  auto OnCompositing(engine::FrameContext& context, engine::Renderer& renderer,
    graphics::Framebuffer* target_framebuffer) -> co::Co<> override;

  auto ClearBackbufferReferences() -> void override;

  // Configuration
  [[nodiscard]] auto GetSupportedFeatures() const -> PipelineFeature override;

  auto SetShaderDebugMode(engine::ShaderDebugMode mode) -> void override;
  auto SetRenderMode(RenderMode mode) -> void override;
  auto SetWireframeColor(const graphics::Color& color) -> void override;
  auto SetLightCullingVisualizationMode(engine::ShaderDebugMode mode)
    -> void override;
  auto SetClusteredCullingEnabled(bool enabled) -> void override;
  auto SetClusterDepthSlices(uint32_t slices) -> void override;
  auto SetExposureMode(engine::ExposureMode mode) -> void override;
  auto SetExposureValue(float value) -> void override;
  auto SetToneMapper(engine::ToneMapper mode) -> void override;

  auto UpdateShaderPassConfig(const engine::ShaderPassConfig& config)
    -> void override;
  auto UpdateTransparentPassConfig(const engine::TransparentPassConfig& config)
    -> void override;
  auto UpdateLightCullingPassConfig(
    const engine::LightCullingPassConfig& config) -> void override;

  [[nodiscard]] auto GetShaderPassConfig() const { return shader_pass_config_; }

private:
  auto ApplyStagedSettings() -> void;
  auto RenderView(engine::FrameContext& fc, engine::Renderer& renderer,
    DemoView& view) -> co::Co<>;

  observer_ptr<AsyncEngine> engine_;

  // Pass Configurations (Owned by shared_ptrs to be shared with Passes)
  std::shared_ptr<engine::DepthPrePassConfig> depth_pass_config_;
  std::shared_ptr<engine::ShaderPassConfig> shader_pass_config_;
  std::shared_ptr<engine::WireframePassConfig> wireframe_pass_config_;
  std::shared_ptr<engine::SkyPassConfig> sky_pass_config_;
  std::shared_ptr<engine::TransparentPassConfig> transparent_pass_config_;
  std::shared_ptr<engine::LightCullingPassConfig> light_culling_pass_config_;
  std::shared_ptr<engine::SkyAtmosphereLutComputePassConfig>
    sky_atmo_lut_pass_config_;

  // Staged Settings (Buffers)
  struct {
    engine::ShaderDebugMode shader_debug_mode {
      engine::ShaderDebugMode::kDisabled
    };
    RenderMode render_mode { RenderMode::kSolid };
    graphics::Color wire_color { 1.0F, 1.0F, 1.0F, 1.0F };
    engine::ShaderDebugMode light_culling_debug_mode {
      engine::ShaderDebugMode::kDisabled
    };
    bool clustered_culling_enabled { false };
    uint32_t cluster_depth_slices { 24 };
    engine::ExposureMode exposure_mode { engine::ExposureMode::kManual };
    float exposure_value { 1.0F };
    engine::ToneMapper tonemapping_mode { engine::ToneMapper::kAcesFitted };
    bool dirty { true };
  } staged_;

  RenderMode render_mode_ { RenderMode::kSolid };
  graphics::Color wire_color_ { 1.0F, 1.0F, 1.0F, 1.0F };

  // Pass instances
  std::shared_ptr<engine::DepthPrePass> depth_pass_;
  std::shared_ptr<engine::ShaderPass> shader_pass_;
  std::shared_ptr<engine::WireframePass> wireframe_pass_;
  std::shared_ptr<engine::SkyPass> sky_pass_;
  std::shared_ptr<engine::TransparentPass> transparent_pass_;
  std::shared_ptr<engine::LightCullingPass> light_culling_pass_;
  std::shared_ptr<engine::SkyAtmosphereLutComputePass> sky_atmo_lut_pass_;

  // Optional ImGui Pass, lazily initialized only if ImGui is available
  mutable std::once_flag flag_;
  mutable observer_ptr<imgui::ImGuiPass> imgui_pass_;
  auto GetImGuiPass() -> observer_ptr<imgui::ImGuiPass>;
};

} // namespace oxygen::examples
