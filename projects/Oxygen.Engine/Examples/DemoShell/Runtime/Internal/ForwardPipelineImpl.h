//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <span>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Engine/AsyncEngine.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/PipelineState.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/ImGui/ImGuiModule.h>
#include <Oxygen/ImGui/ImGuiPass.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/Renderer/Internal/EnvironmentDynamicDataManager.h>
#include <Oxygen/Renderer/Internal/SkyAtmosphereLutManager.h>
#include <Oxygen/Renderer/Passes/AutoExposurePass.h>
#include <Oxygen/Renderer/Passes/DepthPrePass.h>
#include <Oxygen/Renderer/Passes/GpuDebugClearPass.h>
#include <Oxygen/Renderer/Passes/GpuDebugDrawPass.h>
#include <Oxygen/Renderer/Passes/GroundGridPass.h>
#include <Oxygen/Renderer/Passes/LightCullingPass.h>
#include <Oxygen/Renderer/Passes/ShaderPass.h>
#include <Oxygen/Renderer/Passes/SkyPass.h>
#include <Oxygen/Renderer/Passes/ToneMapPass.h>
#include <Oxygen/Renderer/Passes/TransparentPass.h>
#include <Oxygen/Renderer/Passes/WireframePass.h>
#include <Oxygen/Renderer/Renderer.h>
#include <Oxygen/Renderer/Types/CompositingTask.h>
#include <Oxygen/Scene/Environment/PostProcessVolume.h>
#include <Oxygen/Scene/Environment/SceneEnvironment.h>
#include <Oxygen/Scene/Environment/SkyAtmosphere.h>
#include <Oxygen/Scene/Environment/SkySphere.h>
#include <Oxygen/Scene/Scene.h>

#include "DemoShell/Runtime/ForwardPipeline.h"
#include "DemoShell/Runtime/Internal/CompositionPlanner.h"
#include "DemoShell/Runtime/Internal/CompositionViewImpl.h"
#include "DemoShell/Runtime/Internal/FramePlanBuilder.h"
#include "DemoShell/Runtime/Internal/FrameViewPacket.h"
#include "DemoShell/Runtime/Internal/PipelineSettings.h"
#include "DemoShell/Runtime/Internal/ViewLifecycleService.h"

namespace oxygen::examples::internal {

class FramePlanBuilder;

class ForwardPipelineImpl {
public:
  observer_ptr<AsyncEngine> engine;

  struct {
    std::optional<engine::ExposureMode> exposure_mode;
    std::optional<float> manual_exposure;
    std::optional<engine::ToneMapper> tone_mapper;
    std::optional<engine::ShaderDebugMode> debug_mode;
  } last_applied_tonemap_config;
  std::unique_ptr<ViewLifecycleService> view_lifecycle_service;
  std::unique_ptr<FramePlanBuilder> frame_plan_builder;
  CompositionPlanner composition_planner;

  // Pass Configs
  std::shared_ptr<engine::DepthPrePassConfig> depth_pass_config;
  std::shared_ptr<engine::ShaderPassConfig> shader_pass_config;
  std::shared_ptr<engine::WireframePassConfig> wireframe_pass_config;
  std::shared_ptr<engine::SkyPassConfig> sky_pass_config;
  std::shared_ptr<engine::GroundGridPassConfig> ground_grid_pass_config;
  std::shared_ptr<engine::TransparentPassConfig> transparent_pass_config;
  std::shared_ptr<engine::LightCullingPassConfig> light_culling_pass_config;
  std::shared_ptr<engine::ToneMapPassConfig> tone_map_pass_config;
  std::shared_ptr<engine::AutoExposurePassConfig> auto_exposure_config;

  // Pass Instances
  std::shared_ptr<engine::DepthPrePass> depth_pass;
  std::shared_ptr<engine::ShaderPass> shader_pass;
  std::shared_ptr<engine::WireframePass> wireframe_pass;
  std::shared_ptr<engine::SkyPass> sky_pass;
  std::shared_ptr<engine::GroundGridPass> ground_grid_pass;
  std::shared_ptr<engine::TransparentPass> transparent_pass;
  std::shared_ptr<engine::LightCullingPass> light_culling_pass;
  std::shared_ptr<engine::ToneMapPass> tone_map_pass;
  std::shared_ptr<engine::AutoExposurePass> auto_exposure_pass;
  std::shared_ptr<engine::GpuDebugClearPass> gpu_debug_clear_pass;
  std::shared_ptr<engine::GpuDebugDrawPass> gpu_debug_draw_pass;

  // ImGui lazy loading
  mutable std::once_flag imgui_flag;
  mutable observer_ptr<imgui::ImGuiPass> imgui_pass;

  std::optional<float> pending_auto_exposure_reset;
  PipelineSettings frame_settings {};
  PipelineSettingsDraft settings_draft {};

  struct ViewRenderContext {
    observer_ptr<const CompositionViewImpl> view;
    ViewRenderPlan plan;
    std::shared_ptr<const graphics::Texture> depth_texture;
    bool sdr_in_render_target { false };
  };

  auto ConfigureWireframePass(const ViewRenderPlan& plan,
    const CompositionViewImpl& view, bool clear_color, bool clear_depth,
    bool depth_write_enable) const -> void;

  void TrackViewResources(
    ViewRenderContext& ctx, graphics::CommandRecorder& rec) const;

  void ConfigurePassTargets(const ViewRenderContext& ctx) const;

  void BindHdrAndClear(
    ViewRenderContext& ctx, graphics::CommandRecorder& rec) const;

  void BindSdrAndMaybeClear(
    ViewRenderContext& ctx, graphics::CommandRecorder& rec) const;

  auto RenderWireframeScene(const ViewRenderContext& ctx,
    const engine::RenderContext& rc, graphics::CommandRecorder& rec) const
    -> co::Co<>;

  auto RunScenePasses(const ViewRenderContext& ctx,
    const engine::RenderContext& rc, graphics::CommandRecorder& rec) const
    -> co::Co<>;

  auto RenderGpuDebugOverlay(ViewRenderContext& ctx,
    const engine::RenderContext& rc, graphics::CommandRecorder& rec) const
    -> co::Co<>;

  auto ToneMapToSdr(ViewRenderContext& ctx, const engine::RenderContext& rc,
    graphics::CommandRecorder& rec) const -> co::Co<>;

  void EnsureSdrBoundForOverlays(
    ViewRenderContext& ctx, graphics::CommandRecorder& rec) const;

  auto RenderOverlayWireframe(const ViewRenderContext& ctx,
    const engine::RenderContext& rc, graphics::CommandRecorder& rec) const
    -> co::Co<>;

  void RenderViewOverlay(
    const ViewRenderContext& ctx, graphics::CommandRecorder& rec) const;

  auto RenderToolsImGui(const ViewRenderContext& ctx,
    graphics::CommandRecorder& rec) const -> co::Co<>;

  void TransitionSdrToShaderRead(
    ViewRenderContext& ctx, graphics::CommandRecorder& rec) const;

  auto ExecuteRegisteredView(ViewId id, const engine::RenderContext& rc,
    graphics::CommandRecorder& rec) -> co::Co<>;

  void EnsureViewLifecycleService(engine::Renderer& renderer);

  void PublishView(std::span<const CompositionView> view_descs,
    observer_ptr<graphics::Framebuffer> composite_target, Graphics& graphics,
    engine::FrameContext& context, engine::Renderer& renderer);
  void RegisterRenderGraphs(engine::Renderer& renderer)
  {
    EnsureViewLifecycleService(renderer);
    view_lifecycle_service->RegisterRenderGraphs();
  }

  void BuildFramePlan(observer_ptr<scene::Scene> scene)
  {
    const FramePlanBuilder::Inputs inputs {
      .frame_settings = frame_settings,
      .pending_auto_exposure_reset = pending_auto_exposure_reset,
      .tone_map_pass_config = observer_ptr { tone_map_pass_config.get() },
      .shader_pass_config = observer_ptr { shader_pass_config.get() },
    };
    frame_plan_builder->BuildFrameViewPackets(scene,
      view_lifecycle_service
        ? std::span<CompositionViewImpl* const> { view_lifecycle_service
              ->GetOrderedActiveViews() }
        : std::span<CompositionViewImpl* const> {},
      inputs);
  }
  void UnpublishView(engine::FrameContext& context, engine::Renderer& renderer)
  {
    EnsureViewLifecycleService(renderer);
    view_lifecycle_service->UnpublishStaleViews(context);
  }

  void PlanCompositingTasks() { composition_planner.PlanCompositingTasks(); }

  auto BuildCompositionSubmission(graphics::Framebuffer* final_output)
    -> engine::CompositionSubmission
  {
    return composition_planner.BuildCompositionSubmission(final_output);
  }

  explicit ForwardPipelineImpl(observer_ptr<AsyncEngine> engine_ptr);
  ~ForwardPipelineImpl();

  OXYGEN_MAKE_NON_COPYABLE(ForwardPipelineImpl)
  OXYGEN_MAKE_NON_MOVABLE(ForwardPipelineImpl)

  void ApplySettings();

private:
  void ApplyCommittedSettings(const PipelineSettings& settings);

public:
  void SetShaderDebugMode(engine::ShaderDebugMode mode);
  void SetRenderMode(RenderMode mode);
  void SetGpuDebugPassEnabled(bool enabled);
  void SetAtmosphereBlueNoiseEnabled(bool enabled);
  void SetGpuDebugMouseDownPosition(std::optional<SubPixelPosition> position);
  void SetWireframeColor(const graphics::Color& color);
  void SetLightCullingVisualizationMode(engine::ShaderDebugMode mode);
  void SetClusterDepthSlices(uint32_t slices);
  void SetExposureMode(engine::ExposureMode mode);
  void SetExposureValue(float value);
  void SetToneMapper(engine::ToneMapper mode);
  void SetGroundGridConfig(const engine::GroundGridPassConfig& config);
  void SetAutoExposureAdaptationSpeedUp(float speed);
  void SetAutoExposureAdaptationSpeedDown(float speed);
  void SetAutoExposureLowPercentile(float percentile);
  void SetAutoExposureHighPercentile(float percentile);
  void SetAutoExposureMinLogLuminance(float luminance);
  void SetAutoExposureLogLuminanceRange(float range);
  void SetAutoExposureTargetLuminance(float luminance);
  void SetAutoExposureSpotMeterRadius(float radius);
  void SetAutoExposureMeteringMode(engine::MeteringMode mode);
  void ResetAutoExposure(float initial_ev);
  void SetGamma(float gamma);

  void ClearBackbufferReferences() const;

  auto GetImGuiPass() const -> observer_ptr<imgui::ImGuiPass>;
};

} // namespace oxygen::examples::internal
