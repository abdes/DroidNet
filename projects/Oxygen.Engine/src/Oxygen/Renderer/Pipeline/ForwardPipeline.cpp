//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <atomic>

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
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/Renderer/ImGui/ImGuiModule.h>
#include <Oxygen/Renderer/ImGui/ImGuiPass.h>
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
#include <Oxygen/Renderer/Pipeline/ForwardPipeline.h>
#include <Oxygen/Renderer/Pipeline/Internal/CompositionPlanner.h>
#include <Oxygen/Renderer/Pipeline/Internal/CompositionViewImpl.h>
#include <Oxygen/Renderer/Pipeline/Internal/FramePlanBuilder.h>
#include <Oxygen/Renderer/Pipeline/Internal/FrameViewPacket.h>
#include <Oxygen/Renderer/Pipeline/Internal/PipelineSettings.h>
#include <Oxygen/Renderer/Pipeline/Internal/ViewLifecycleService.h>
#include <Oxygen/Renderer/Renderer.h>
#include <Oxygen/Renderer/Types/CompositingTask.h>
#include <Oxygen/Scene/Environment/PostProcessVolume.h>
#include <Oxygen/Scene/Environment/SceneEnvironment.h>
#include <Oxygen/Scene/Environment/SkyAtmosphere.h>
#include <Oxygen/Scene/Environment/SkySphere.h>
#include <Oxygen/Scene/Scene.h>

namespace oxygen::renderer {

using internal::CompositionPlanner;
using internal::CompositionViewImpl;
using internal::FramePlanBuilder;
using internal::FrameViewPacket;
using internal::PipelineSettings;
using internal::PipelineSettingsDraft;
using internal::ToneMapPolicy;
using internal::ViewLifecycleService;
using internal::ViewRenderPlan;

class ForwardPipeline::Impl {
public:
  explicit Impl(observer_ptr<AsyncEngine> engine_ptr);
  ~Impl() = default;

  OXYGEN_MAKE_NON_COPYABLE(Impl)
  OXYGEN_MAKE_NON_MOVABLE(Impl)

  void ApplySettings();
  [[nodiscard]] auto IsAtmosphereBlueNoiseEnabled() const noexcept -> bool
  {
    return frame_settings.atmosphere_blue_noise_enabled;
  }
  auto SyncAutoExposureMeteringFromScene(const scene::Scene& scene) -> void;
  [[nodiscard]] auto AcquireGraphics() const -> std::shared_ptr<Graphics>;

  void PublishViews(std::span<const CompositionView> view_descs,
    observer_ptr<graphics::Framebuffer> composite_target, Graphics& graphics,
    engine::FrameContext& context, engine::Renderer& renderer);
  void RegisterRenderGraphs(engine::Renderer& renderer);
  void BuildFramePlan(observer_ptr<scene::Scene> scene);
  void UnpublishView(engine::FrameContext& context, engine::Renderer& renderer);
  void PlanCompositingTasks();
  auto BuildCompositionSubmission(
    std::shared_ptr<graphics::Framebuffer> final_output)
    -> engine::CompositionSubmission;

  void UpdateShaderPassConfig(const engine::ShaderPassConfig& config);
  void UpdateTransparentPassConfig(const engine::TransparentPassConfig& config);
  void UpdateLightCullingPassConfig(
    const engine::LightCullingPassConfig& config);

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

private:
  // Per-view execution context owned by a single ExecuteRegisteredView()
  // coroutine invocation. It is constructed after frame packet resolution and
  // destroyed when that coroutine invocation completes/tears down.
  // This object is intentionally passed by reference through helper calls so
  // all awaits in the same coroutine observe one mutable state instance.
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
  void ApplyCommittedSettings(const PipelineSettings& settings);

  observer_ptr<AsyncEngine> engine;

  std::unique_ptr<ViewLifecycleService> view_lifecycle_service;
  std::unique_ptr<FramePlanBuilder> frame_plan_builder;
  CompositionPlanner composition_planner;

  // Pass configs
  std::shared_ptr<engine::DepthPrePassConfig> depth_pass_config;
  std::shared_ptr<engine::ShaderPassConfig> shader_pass_config;
  std::shared_ptr<engine::WireframePassConfig> wireframe_pass_config;
  std::shared_ptr<engine::SkyPassConfig> sky_pass_config;
  std::shared_ptr<engine::GroundGridPassConfig> grid_pass_config;
  std::shared_ptr<engine::TransparentPassConfig> trans_pass_config;
  std::shared_ptr<engine::LightCullingPassConfig> light_culling_pass_config;
  std::shared_ptr<engine::ToneMapPassConfig> tone_map_pass_config;
  std::shared_ptr<engine::AutoExposurePassConfig> auto_exposure_config;

  // Pass instances
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

  // Runtime settings state
  std::optional<float> pending_auto_exposure_reset;
  PipelineSettings frame_settings {};
  PipelineSettingsDraft settings_draft {};

  // ImGui lazy loading
  mutable std::once_flag imgui_flag;
  mutable observer_ptr<renderer::imgui::ImGuiPass> imgui_pass;
};

namespace {
  struct DebugModeIntent {
    bool is_non_ibl { false };
    bool force_manual_exposure { false };
    bool force_exposure_one { false };
  };

  auto EvaluateDebugModeIntent(engine::ShaderDebugMode mode) -> DebugModeIntent
  {
    using enum engine::ShaderDebugMode;
    const auto is_non_ibl = [&] {
      switch (mode) {
      case kLightCullingHeatMap:
      case kDepthSlice:
      case kClusterIndex:
      case kBaseColor:
      case kUv0:
      case kOpacity:
      case kWorldNormals:
      case kRoughness:
      case kMetalness:
        return true;
      case kIblSpecular:
      case kIblRawSky:
      case kIblIrradiance:
      case kIblFaceIndex:
      case kDisabled:
      default:
        return false;
      }
    }();

    const auto is_ibl_debug = [&] {
      switch (mode) {
      case kIblSpecular:
      case kIblRawSky:
      case kIblIrradiance:
      case kIblFaceIndex:
        return true;
      case kDisabled:
      default:
        return false;
      }
    }();

    const bool force_exposure_one
      = (mode == engine::ShaderDebugMode::kIblRawSky);

    return DebugModeIntent {
      .is_non_ibl = is_non_ibl,
      .force_manual_exposure = is_non_ibl || is_ibl_debug || force_exposure_one,
      .force_exposure_one = force_exposure_one,
    };
  }

  auto GetWireframeTargetTexture(const ViewRenderPlan& plan,
    const CompositionViewImpl& view) -> std::shared_ptr<const graphics::Texture>
  {
    const bool wireframe_in_sdr
      = plan.RunOverlayWireframe() || !plan.HasSceneLinearPath();
    if (wireframe_in_sdr) {
      DCHECK_NOTNULL_F(view.GetSdrTexture().get());
      return view.GetSdrTexture();
    }
    return view.GetHdrTexture();
  }

  class ToneMapOverrideGuard {
  public:
    ToneMapOverrideGuard(engine::ToneMapPassConfig& config, bool enable_neutral)
      : config_(observer_ptr { &config })
      , saved_exposure_mode_(config.exposure_mode)
      , saved_manual_exposure_(config.manual_exposure)
      , saved_tone_mapper_(config.tone_mapper)
      , active_(enable_neutral)
    {
      DCHECK_NOTNULL_F(config_.get());
      if (!active_) {
        return;
      }
      config_->exposure_mode = engine::ExposureMode::kManual;
      config_->manual_exposure = 1.0F;
      config_->tone_mapper = engine::ToneMapper::kNone;
    }

    ~ToneMapOverrideGuard()
    {
      if (!active_) {
        return;
      }
      DCHECK_NOTNULL_F(config_.get());
      config_->exposure_mode = saved_exposure_mode_;
      config_->manual_exposure = saved_manual_exposure_;
      config_->tone_mapper = saved_tone_mapper_;
    }

    OXYGEN_MAKE_NON_COPYABLE(ToneMapOverrideGuard)
    OXYGEN_MAKE_NON_MOVABLE(ToneMapOverrideGuard)

  private:
    observer_ptr<engine::ToneMapPassConfig> config_;
    engine::ExposureMode saved_exposure_mode_;
    float saved_manual_exposure_;
    engine::ToneMapper saved_tone_mapper_;
    bool active_;
  };
} // namespace

auto ForwardPipeline::Impl::ConfigureWireframePass(const ViewRenderPlan& plan,
  const CompositionViewImpl& view, bool clear_color, bool clear_depth,
  bool depth_write_enable) const -> void
{
  if (!wireframe_pass_config) {
    return;
  }

  wireframe_pass_config->clear_color_target = clear_color;
  wireframe_pass_config->clear_depth_target = clear_depth;
  wireframe_pass_config->depth_write_enable = depth_write_enable;
  // Wireframe is authored as a debug visualization pass and currently does not
  // apply scene exposure compensation in this pipeline variant.
  wireframe_pass_config->apply_exposure_compensation = false;
  wireframe_pass_config->color_texture = GetWireframeTargetTexture(plan, view);

  if (wireframe_pass) {
    wireframe_pass->SetWireColor(frame_plan_builder->WireColor());
  } else {
    wireframe_pass_config->wire_color = frame_plan_builder->WireColor();
  }
}

void ForwardPipeline::Impl::TrackViewResources(
  ViewRenderContext& ctx, graphics::CommandRecorder& rec) const
{
  if (!ctx.plan.HasSceneLinearPath()) {
    return;
  }

  const auto fb = ctx.view->GetHdrFramebuffer();
  const auto& fb_desc = fb->GetDescriptor();
  if (fb_desc.depth_attachment.IsValid()) {
    ctx.depth_texture = fb_desc.depth_attachment.texture;
  }

  if (ctx.view->GetHdrTexture()
    && !rec.IsResourceTracked(*ctx.view->GetHdrTexture())) {
    rec.BeginTrackingResourceState(
      *ctx.view->GetHdrTexture(), graphics::ResourceStates::kCommon, true);
  }
  if (ctx.depth_texture && !rec.IsResourceTracked(*ctx.depth_texture)) {
    rec.BeginTrackingResourceState(
      *ctx.depth_texture, graphics::ResourceStates::kCommon, true);
  }
  if (ctx.view->GetSdrTexture()
    && !rec.IsResourceTracked(*ctx.view->GetSdrTexture())) {
    rec.BeginTrackingResourceState(
      *ctx.view->GetSdrTexture(), graphics::ResourceStates::kCommon, true);
  }
}

void ForwardPipeline::Impl::ConfigurePassTargets(
  const ViewRenderContext& ctx) const
{
  if (!ctx.plan.HasSceneLinearPath()) {
    return;
  }

  if (depth_pass_config) {
    depth_pass_config->depth_texture = ctx.depth_texture;
  }
  if (shader_pass_config) {
    shader_pass_config->color_texture = ctx.view->GetHdrTexture();
  }
  if (wireframe_pass_config) {
    wireframe_pass_config->color_texture = ctx.view->GetHdrTexture();
  }
  if (sky_pass_config) {
    sky_pass_config->color_texture = ctx.view->GetHdrTexture();
    sky_pass_config->debug_mouse_down_position
      = frame_plan_builder->GpuDebugMouseDownPosition();
    sky_pass_config->debug_viewport_extent = SubPixelExtent {
      .width = ctx.view->GetDescriptor().view.viewport.width,
      .height = ctx.view->GetDescriptor().view.viewport.height,
    };
  }
  if (grid_pass_config) {
    grid_pass_config->color_texture = ctx.view->GetHdrTexture();
  }
  if (trans_pass_config) {
    trans_pass_config->color_texture = ctx.view->GetHdrTexture();
    trans_pass_config->depth_texture = ctx.depth_texture;
  }
}

void ForwardPipeline::Impl::BindHdrAndClear(
  ViewRenderContext& ctx, graphics::CommandRecorder& rec) const
{
  if (!ctx.plan.HasSceneLinearPath()) {
    return;
  }

  rec.RequireResourceState(
    *ctx.view->GetHdrTexture(), graphics::ResourceStates::kRenderTarget);
  if (ctx.depth_texture) {
    rec.RequireResourceState(
      *ctx.depth_texture, graphics::ResourceStates::kDepthWrite);
  }
  rec.FlushBarriers();

  rec.BindFrameBuffer(*ctx.view->GetHdrFramebuffer());
  const auto hdr_clear = ctx.view->GetHdrFramebuffer()
                           ->GetDescriptor()
                           .color_attachments[0]
                           .ResolveClearColor(std::nullopt);
  rec.ClearFramebuffer(*ctx.view->GetHdrFramebuffer(),
    std::vector<std::optional<graphics::Color>> { hdr_clear }, 1.0F);
}

void ForwardPipeline::Impl::BindSdrAndMaybeClear(
  ViewRenderContext& ctx, graphics::CommandRecorder& rec) const
{
  if (!ctx.plan.HasCompositePath() || ctx.plan.HasSceneLinearPath()) {
    return;
  }

  rec.RequireResourceState(
    *ctx.view->GetSdrTexture(), graphics::ResourceStates::kRenderTarget);
  rec.FlushBarriers();
  ctx.sdr_in_render_target = true;
  rec.BindFrameBuffer(*ctx.view->GetSdrFramebuffer());
  if (ctx.view->GetDescriptor().should_clear) {
    const auto sdr_clear = ctx.view->GetSdrFramebuffer()
                             ->GetDescriptor()
                             .color_attachments[0]
                             .ResolveClearColor(std::nullopt);
    rec.ClearFramebuffer(*ctx.view->GetSdrFramebuffer(),
      std::vector<std::optional<graphics::Color>> { sdr_clear });
  }
}

auto ForwardPipeline::Impl::RenderWireframeScene(
  const ViewRenderContext& ctx, // NOLINT(*-reference-coroutine-parameters)
  const engine::RenderContext& rc, graphics::CommandRecorder& rec) const
  -> co::Co<>
{
  if (!wireframe_pass_config || !wireframe_pass) {
    co_return;
  }

  const bool is_forced = ctx.view->GetDescriptor().force_wireframe;
  ConfigureWireframePass(ctx.plan, *ctx.view, !is_forced, true, true);
  co_await wireframe_pass->PrepareResources(rc, rec);
  co_await wireframe_pass->Execute(rc, rec);
}

auto ForwardPipeline::Impl::RunScenePasses(
  const ViewRenderContext& ctx, // NOLINT(*-reference-coroutine-parameters)
  const engine::RenderContext& rc, graphics::CommandRecorder& rec) const
  -> co::Co<>
{
  if (depth_pass && ctx.depth_texture) {
    co_await depth_pass->PrepareResources(rc, rec);
    co_await depth_pass->Execute(rc, rec);
    rc.RegisterPass<engine::DepthPrePass>(depth_pass.get());
  }

  // Sky must run after DepthPrePass so it can depth-test against the
  // populated depth buffer and only shade background pixels.
  if (ctx.plan.RunSkyPass() && sky_pass) {
    co_await sky_pass->PrepareResources(rc, rec);
    co_await sky_pass->Execute(rc, rec);
  }

  if (light_culling_pass) {
    co_await light_culling_pass->PrepareResources(rc, rec);
    co_await light_culling_pass->Execute(rc, rec);
    rc.RegisterPass<engine::LightCullingPass>(light_culling_pass.get());
  }

  if (shader_pass) {
    co_await shader_pass->PrepareResources(rc, rec);
    co_await shader_pass->Execute(rc, rec);
    rc.RegisterPass<engine::ShaderPass>(shader_pass.get());
  }

  if (transparent_pass) {
    co_await transparent_pass->PrepareResources(rc, rec);
    co_await transparent_pass->Execute(rc, rec);
    rc.RegisterPass<engine::TransparentPass>(transparent_pass.get());
  }
}

auto ForwardPipeline::Impl::RenderGpuDebugOverlay(
  ViewRenderContext& ctx, // NOLINT(*-reference-coroutine-parameters)
  const engine::RenderContext& rc, graphics::CommandRecorder& rec) const
  -> co::Co<>
{
  if (!frame_plan_builder->GpuDebugPassEnabled() || !gpu_debug_draw_pass) {
    co_return;
  }
  if (ctx.plan.EffectiveRenderMode() == RenderMode::kWireframe) {
    co_return;
  }
  if (!ctx.plan.HasCompositePath()) {
    co_return;
  }
  if (ctx.view->GetDescriptor().z_order != CompositionView::kZOrderScene
    || !ctx.view->GetDescriptor().camera.has_value()) {
    co_return;
  }

  EnsureSdrBoundForOverlays(ctx, rec);
  gpu_debug_draw_pass->SetColorTexture(ctx.view->GetSdrTexture());
  co_await gpu_debug_draw_pass->PrepareResources(rc, rec);
  co_await gpu_debug_draw_pass->Execute(rc, rec);
  rc.RegisterPass<engine::GpuDebugDrawPass>(gpu_debug_draw_pass.get());
}

auto ForwardPipeline::Impl::ToneMapToSdr(
  ViewRenderContext& ctx, // NOLINT(*-reference-coroutine-parameters)
  const engine::RenderContext& rc, graphics::CommandRecorder& rec) const
  -> co::Co<>
{
  const bool should_tonemap = ctx.plan.HasSceneLinearPath();
  if (!tone_map_pass || !tone_map_pass_config || !should_tonemap) {
    co_return;
  }

  tone_map_pass_config->source_texture = ctx.view->GetHdrTexture();
  tone_map_pass_config->output_texture = ctx.view->GetSdrTexture();
  ToneMapOverrideGuard override_guard(*tone_map_pass_config,
    ctx.plan.GetToneMapPolicy() == ToneMapPolicy::kNeutral);

  using enum graphics::ResourceStates;
  rec.RequireResourceState(*ctx.view->GetHdrTexture(), kShaderResource);
  rec.RequireResourceState(*ctx.view->GetSdrTexture(), kRenderTarget);
  rec.FlushBarriers();
  ctx.sdr_in_render_target = true;

  co_await tone_map_pass->PrepareResources(rc, rec);
  co_await tone_map_pass->Execute(rc, rec);
}

void ForwardPipeline::Impl::EnsureSdrBoundForOverlays(
  ViewRenderContext& ctx, graphics::CommandRecorder& rec) const
{
  if (!ctx.plan.HasCompositePath() || ctx.sdr_in_render_target) {
    return;
  }

  using enum graphics::ResourceStates;
  rec.RequireResourceState(*ctx.view->GetSdrTexture(), kRenderTarget);
  rec.FlushBarriers();
  ctx.sdr_in_render_target = true;
}

auto ForwardPipeline::Impl::RenderOverlayWireframe(
  const ViewRenderContext& ctx, // NOLINT(*-reference-coroutine-parameters)
  const engine::RenderContext& rc, graphics::CommandRecorder& rec) const
  -> co::Co<>
{
  if (!ctx.plan.RunOverlayWireframe() || !wireframe_pass_config
    || !wireframe_pass) {
    co_return;
  }

  const auto scene = rc.GetScene();

  DCHECK_NOTNULL_F(scene, "Overlay wireframe requires an active scene");
  DCHECK_F(ctx.view->GetDescriptor().camera.has_value(),
    "Overlay wireframe requires a camera node");
  auto camera_node = *ctx.view->GetDescriptor().camera;
  DCHECK_F(camera_node.IsAlive(), "Overlay wireframe requires a live camera");
  DCHECK_F(
    camera_node.HasCamera(), "Overlay wireframe requires a camera component");
  DCHECK_F(scene->Contains(camera_node),
    "Overlay wireframe camera is not in the active scene");

  ConfigureWireframePass(ctx.plan, *ctx.view, false, false, false);
  co_await wireframe_pass->PrepareResources(rc, rec);
  co_await wireframe_pass->Execute(rc, rec);
}

void ForwardPipeline::Impl::RenderViewOverlay(
  const ViewRenderContext& ctx, graphics::CommandRecorder& rec) const
{
  rec.BindFrameBuffer(*ctx.view->GetSdrFramebuffer());
  if (ctx.view->GetDescriptor().on_overlay) {
    ctx.view->GetDescriptor().on_overlay(rec);
  }
}

auto ForwardPipeline::Impl::RenderToolsImGui(
  const ViewRenderContext& ctx, // NOLINT(*-reference-coroutine-parameters)
  graphics::CommandRecorder& rec) const -> co::Co<>
{
  if (ctx.view->GetDescriptor().z_order != CompositionView::kZOrderTools) {
    co_return;
  }

  if (auto imgui = GetImGuiPass()) {
    co_await imgui->Render(rec);
  }
}

void ForwardPipeline::Impl::TransitionSdrToShaderRead(
  ViewRenderContext& ctx, graphics::CommandRecorder& rec) const
{
  if (!ctx.plan.HasCompositePath()) {
    return;
  }

  rec.RequireResourceState(
    *ctx.view->GetSdrTexture(), graphics::ResourceStates::kShaderResource);
  rec.FlushBarriers();
  ctx.sdr_in_render_target = false;
}

auto ForwardPipeline::Impl::ExecuteRegisteredView(ViewId id,
  const engine::RenderContext& rc, graphics::CommandRecorder& rec) -> co::Co<>
{
  const auto* frame_packet = frame_plan_builder->FindFrameViewPacket(id);
  if (frame_packet == nullptr) {
    LOG_F(ERROR,
      "ForwardPipeline: missing frame packet in render callback for view {}",
      id.get());
    co_return;
  }
  const auto& effective_view = frame_packet->View();

  ViewRenderContext ctx {
    .view = observer_ptr { &effective_view },
    .plan = frame_packet->Plan(),
    .depth_texture = nullptr,
    .sdr_in_render_target = false,
  };
  DCHECK_NOTNULL_F(ctx.view.get());
  const bool run_scene_passes = ctx.plan.HasSceneLinearPath()
    && (ctx.plan.EffectiveRenderMode() != RenderMode::kWireframe);
  DCHECK_F(!ctx.plan.RunOverlayWireframe() || ctx.plan.HasCompositePath());

  if (ctx.plan.HasSceneLinearPath()) {
    TrackViewResources(ctx, rec);
    ConfigurePassTargets(ctx);
    BindHdrAndClear(ctx, rec);

    if (!run_scene_passes) {
      co_await RenderWireframeScene(ctx, rc, rec);
    } else {
      if (frame_plan_builder->GpuDebugPassEnabled() && gpu_debug_clear_pass) {
        co_await gpu_debug_clear_pass->PrepareResources(rc, rec);
        co_await gpu_debug_clear_pass->Execute(rc, rec);
        rc.RegisterPass<engine::GpuDebugClearPass>(gpu_debug_clear_pass.get());
      }
      co_await RunScenePasses(ctx, rc, rec);

      if (frame_plan_builder->WantAutoExposure() && auto_exposure_pass) {
        if (frame_plan_builder->AutoExposureReset().has_value()) {
          const float k = 12.5F;
          const float ev = *frame_plan_builder->AutoExposureReset();
          const float lum = std::pow(2.0F, ev) * k / 100.0F;
          const auto vid = ctx.view->GetPublishedViewId();
          if (vid != kInvalidViewId) {
            auto_exposure_pass->ResetExposure(rec, vid, lum);
          }
        }

        auto_exposure_config->source_texture = effective_view.GetHdrTexture();
        co_await auto_exposure_pass->PrepareResources(rc, rec);
        co_await auto_exposure_pass->Execute(rc, rec);
        rc.RegisterPass<engine::AutoExposurePass>(auto_exposure_pass.get());
      }

      if (ground_grid_pass && grid_pass_config && grid_pass_config->enabled) {
        co_await ground_grid_pass->PrepareResources(rc, rec);
        co_await ground_grid_pass->Execute(rc, rec);
      }
    }

    co_await ToneMapToSdr(ctx, rc, rec);
  } else {
    BindSdrAndMaybeClear(ctx, rec);
  }

  if (ctx.plan.HasCompositePath()) {
    EnsureSdrBoundForOverlays(ctx, rec);
    co_await RenderOverlayWireframe(ctx, rc, rec);
    RenderViewOverlay(ctx, rec);
    co_await RenderToolsImGui(ctx, rec);
    co_await RenderGpuDebugOverlay(ctx, rc, rec);
    TransitionSdrToShaderRead(ctx, rec);
  }
  co_return;
}

void ForwardPipeline::Impl::PublishViews(
  std::span<const CompositionView> view_descs,
  observer_ptr<graphics::Framebuffer> composite_target, Graphics& graphics,
  engine::FrameContext& context, engine::Renderer& renderer)
{
  EnsureViewLifecycleService(renderer);
  view_lifecycle_service->SyncActiveViews(
    context, view_descs, composite_target, graphics);
  view_lifecycle_service->PublishViews(context);
}

void ForwardPipeline::Impl::RegisterRenderGraphs(engine::Renderer& renderer)
{
  EnsureViewLifecycleService(renderer);
  view_lifecycle_service->RegisterRenderGraphs();
}

void ForwardPipeline::Impl::BuildFramePlan(observer_ptr<scene::Scene> scene)
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

void ForwardPipeline::Impl::UnpublishView(
  engine::FrameContext& context, engine::Renderer& renderer)
{
  EnsureViewLifecycleService(renderer);
  view_lifecycle_service->UnpublishStaleViews(context);
}

void ForwardPipeline::Impl::PlanCompositingTasks()
{
  composition_planner.PlanCompositingTasks();
}

auto ForwardPipeline::Impl::BuildCompositionSubmission(
  std::shared_ptr<graphics::Framebuffer> final_output)
  -> engine::CompositionSubmission
{
  return composition_planner.BuildCompositionSubmission(
    std::move(final_output));
}

void ForwardPipeline::Impl::EnsureViewLifecycleService(
  engine::Renderer& renderer)
{
  if (view_lifecycle_service) {
    return;
  }
  view_lifecycle_service = std::make_unique<ViewLifecycleService>(renderer,
    [this](ViewId id, const engine::RenderContext& rc,
      graphics::CommandRecorder& rec) -> co::Co<> {
      co_await ExecuteRegisteredView(id, rc, rec);
    });
}

ForwardPipeline::Impl::Impl(observer_ptr<AsyncEngine> engine_ptr)
  : engine(engine_ptr)
  , frame_plan_builder(std::make_unique<FramePlanBuilder>())
  , composition_planner(observer_ptr { frame_plan_builder.get() })
{
  namespace p = engine;

  depth_pass_config = std::make_shared<p::DepthPrePassConfig>();
  shader_pass_config = std::make_shared<p::ShaderPassConfig>();
  wireframe_pass_config = std::make_shared<p::WireframePassConfig>();
  sky_pass_config = std::make_shared<p::SkyPassConfig>();
  grid_pass_config = std::make_shared<p::GroundGridPassConfig>();
  trans_pass_config = std::make_shared<p::TransparentPassConfig>();
  light_culling_pass_config = std::make_shared<p::LightCullingPassConfig>();
  tone_map_pass_config = std::make_shared<p::ToneMapPassConfig>();
  auto_exposure_config = std::make_shared<p::AutoExposurePassConfig>();

  depth_pass = std::make_shared<p::DepthPrePass>(depth_pass_config);
  shader_pass = std::make_shared<p::ShaderPass>(shader_pass_config);
  wireframe_pass = std::make_shared<p::WireframePass>(wireframe_pass_config);
  sky_pass = std::make_shared<p::SkyPass>(sky_pass_config);
  ground_grid_pass = std::make_shared<p::GroundGridPass>(grid_pass_config);
  transparent_pass = std::make_shared<p::TransparentPass>(trans_pass_config);

  auto gfx = engine->GetGraphics().lock();
  auto gfx_ptr = observer_ptr { gfx.get() };
  light_culling_pass
    = std::make_shared<p::LightCullingPass>(gfx_ptr, light_culling_pass_config);
  tone_map_pass = std::make_shared<p::ToneMapPass>(tone_map_pass_config);
  auto_exposure_pass
    = std::make_shared<p::AutoExposurePass>(gfx_ptr, auto_exposure_config);
  gpu_debug_clear_pass = std::make_shared<p::GpuDebugClearPass>(gfx_ptr);
  gpu_debug_draw_pass = std::make_shared<p::GpuDebugDrawPass>(gfx_ptr);

  settings_draft.ground_grid_config.enabled = false;
  frame_settings.ground_grid_config.enabled = false;
}

auto ForwardPipeline::Impl::AcquireGraphics() const -> std::shared_ptr<Graphics>
{
  auto graphics = engine->GetGraphics().lock();
  CHECK_NOTNULL_F(graphics.get(), "Graphics backend is not available");
  return graphics;
}

void ForwardPipeline::Impl::SyncAutoExposureMeteringFromScene(
  const scene::Scene& scene)
{
  if (!auto_exposure_config) {
    return;
  }
  if (const auto env = scene.GetEnvironment()) {
    if (const auto pp
      = env->TryGetSystem<scene::environment::PostProcessVolume>();
      pp && pp->IsEnabled()) {
      auto_exposure_config->metering_mode = pp->GetAutoExposureMeteringMode();
    }
  }
}

void ForwardPipeline::Impl::ApplySettings()
{
  if (!settings_draft.dirty) {
    return;
  }
  auto commit = settings_draft.Commit();
  frame_settings = commit.settings;
  pending_auto_exposure_reset = commit.auto_exposure_reset_ev;
  ApplyCommittedSettings(frame_settings);
}

void ForwardPipeline::Impl::ApplyCommittedSettings(
  const PipelineSettings& settings)
{
  const auto effective_debug_mode
    = (settings.light_culling_debug_mode != engine::ShaderDebugMode::kDisabled)
    ? settings.light_culling_debug_mode
    : settings.shader_debug_mode;

  if (shader_pass_config) {
    shader_pass_config->debug_mode = effective_debug_mode;
    shader_pass_config->fill_mode = graphics::FillMode::kSolid;
  }

  if (trans_pass_config) {
    trans_pass_config->debug_mode = effective_debug_mode;
    trans_pass_config->fill_mode = graphics::FillMode::kSolid;
  }

  if (light_culling_pass_config) {
    light_culling_pass_config->cluster.cluster_dim_z
      = settings.cluster_depth_slices;
  }

  if (wireframe_pass_config) {
    wireframe_pass_config->wire_color = settings.wire_color;
  }
  if (wireframe_pass) {
    wireframe_pass->SetWireColor(settings.wire_color);
  }

  if (grid_pass_config) {
    *grid_pass_config = settings.ground_grid_config;
  }

  if (tone_map_pass_config) {
    const auto debug_intent = EvaluateDebugModeIntent(effective_debug_mode);
    tone_map_pass_config->exposure_mode = debug_intent.force_manual_exposure
      ? engine::ExposureMode::kManual
      : settings.exposure_mode;
    if (debug_intent.force_exposure_one || debug_intent.force_manual_exposure) {
      tone_map_pass_config->manual_exposure = 1.0F;
    } else {
      tone_map_pass_config->manual_exposure = settings.exposure_value;
    }
    tone_map_pass_config->tone_mapper = settings.tonemapping_mode;
    tone_map_pass_config->gamma = settings.gamma;
  }

  if (auto_exposure_config) {
    auto_exposure_config->adaptation_speed_up
      = settings.auto_exposure_adaptation_speed_up;
    auto_exposure_config->adaptation_speed_down
      = settings.auto_exposure_adaptation_speed_down;
    auto_exposure_config->low_percentile
      = settings.auto_exposure_low_percentile;
    auto_exposure_config->high_percentile
      = settings.auto_exposure_high_percentile;
    auto_exposure_config->min_log_luminance
      = settings.auto_exposure_min_log_luminance;
    auto_exposure_config->log_luminance_range
      = settings.auto_exposure_log_luminance_range;
    auto_exposure_config->target_luminance
      = settings.auto_exposure_target_luminance;
    auto_exposure_config->spot_meter_radius
      = settings.auto_exposure_spot_meter_radius;
    auto_exposure_config->metering_mode = settings.auto_exposure_metering;
  }

  if (gpu_debug_draw_pass) {
    gpu_debug_draw_pass->SetMouseDownPosition(
      settings.gpu_debug_mouse_down_position);
  }
}

void ForwardPipeline::Impl::SetShaderDebugMode(engine::ShaderDebugMode mode)
{
  settings_draft.shader_debug_mode = mode;
  settings_draft.dirty = true;
}

void ForwardPipeline::Impl::SetRenderMode(RenderMode mode)
{
  settings_draft.render_mode = mode;
  settings_draft.dirty = true;
}

void ForwardPipeline::Impl::SetGpuDebugPassEnabled(bool enabled)
{
  settings_draft.gpu_debug_pass_enabled = enabled;
  settings_draft.dirty = true;
}

void ForwardPipeline::Impl::SetAtmosphereBlueNoiseEnabled(bool enabled)
{
  if (settings_draft.atmosphere_blue_noise_enabled == enabled) {
    return;
  }
  settings_draft.atmosphere_blue_noise_enabled = enabled;
  settings_draft.dirty = true;
}

void ForwardPipeline::Impl::SetGpuDebugMouseDownPosition(
  std::optional<SubPixelPosition> position)
{
  settings_draft.gpu_debug_mouse_down_position = position;
  settings_draft.dirty = true;
}

void ForwardPipeline::Impl::SetWireframeColor(const graphics::Color& color)
{
  settings_draft.wire_color = color;
  settings_draft.dirty = true;
}

void ForwardPipeline::Impl::SetLightCullingVisualizationMode(
  engine::ShaderDebugMode mode)
{
  settings_draft.light_culling_debug_mode = mode;
  settings_draft.dirty = true;
}

void ForwardPipeline::Impl::SetClusterDepthSlices(uint32_t slices)
{
  settings_draft.cluster_depth_slices = slices;
  settings_draft.dirty = true;
}

void ForwardPipeline::Impl::SetExposureMode(engine::ExposureMode mode)
{
  if (mode == settings_draft.exposure_mode) {
    return;
  }
  settings_draft.exposure_mode = mode;
  settings_draft.dirty = true;
}

void ForwardPipeline::Impl::SetExposureValue(float value)
{
  settings_draft.exposure_value = value;
  settings_draft.dirty = true;
}

void ForwardPipeline::Impl::SetToneMapper(engine::ToneMapper mode)
{
  settings_draft.tonemapping_mode = mode;
  settings_draft.dirty = true;
}

void ForwardPipeline::Impl::SetGroundGridConfig(
  const engine::GroundGridPassConfig& config)
{
  settings_draft.ground_grid_config = config;
  settings_draft.dirty = true;
}

void ForwardPipeline::Impl::SetAutoExposureAdaptationSpeedUp(float speed)
{
  settings_draft.auto_exposure_adaptation_speed_up = speed;
  settings_draft.dirty = true;
}

void ForwardPipeline::Impl::SetAutoExposureAdaptationSpeedDown(float speed)
{
  settings_draft.auto_exposure_adaptation_speed_down = speed;
  settings_draft.dirty = true;
}

void ForwardPipeline::Impl::SetAutoExposureLowPercentile(float percentile)
{
  settings_draft.auto_exposure_low_percentile = percentile;
  settings_draft.dirty = true;
}

void ForwardPipeline::Impl::SetAutoExposureHighPercentile(float percentile)
{
  settings_draft.auto_exposure_high_percentile = percentile;
  settings_draft.dirty = true;
}

void ForwardPipeline::Impl::SetAutoExposureMinLogLuminance(float luminance)
{
  settings_draft.auto_exposure_min_log_luminance = luminance;
  settings_draft.dirty = true;
}

void ForwardPipeline::Impl::SetAutoExposureLogLuminanceRange(float range)
{
  settings_draft.auto_exposure_log_luminance_range = range;
  settings_draft.dirty = true;
}

void ForwardPipeline::Impl::SetAutoExposureTargetLuminance(float luminance)
{
  settings_draft.auto_exposure_target_luminance = luminance;
  settings_draft.dirty = true;
}

void ForwardPipeline::Impl::SetAutoExposureSpotMeterRadius(float radius)
{
  settings_draft.auto_exposure_spot_meter_radius = radius;
  settings_draft.dirty = true;
}

void ForwardPipeline::Impl::SetAutoExposureMeteringMode(
  engine::MeteringMode mode)
{
  settings_draft.auto_exposure_metering = mode;
  settings_draft.dirty = true;
}

void ForwardPipeline::Impl::ResetAutoExposure(float initial_ev)
{
  settings_draft.auto_exposure_reset_pending = true;
  settings_draft.auto_exposure_reset_ev = initial_ev;
  settings_draft.dirty = true;
}

void ForwardPipeline::Impl::SetGamma(float gamma)
{
  settings_draft.gamma = gamma;
  settings_draft.dirty = true;
}

void ForwardPipeline::Impl::UpdateShaderPassConfig(
  const engine::ShaderPassConfig& config)
{
  if (shader_pass_config) {
    *shader_pass_config = config;
  }
}

void ForwardPipeline::Impl::UpdateTransparentPassConfig(
  const engine::TransparentPassConfig& config)
{
  if (trans_pass_config) {
    *trans_pass_config = config;
  }
}

void ForwardPipeline::Impl::UpdateLightCullingPassConfig(
  const engine::LightCullingPassConfig& config)
{
  if (light_culling_pass_config) {
    *light_culling_pass_config = config;
  }
}

void ForwardPipeline::Impl::ClearBackbufferReferences() const
{
  if (depth_pass_config) {
    depth_pass_config->depth_texture.reset();
  }
  if (shader_pass_config) {
    shader_pass_config->color_texture.reset();
  }
  if (wireframe_pass_config) {
    wireframe_pass_config->color_texture.reset();
  }
  if (sky_pass_config) {
    sky_pass_config->color_texture.reset();
  }
  if (grid_pass_config) {
    grid_pass_config->color_texture.reset();
  }
  if (trans_pass_config) {
    trans_pass_config->color_texture.reset();
    trans_pass_config->depth_texture.reset();
  }
  if (tone_map_pass_config) {
    tone_map_pass_config->source_texture.reset();
    tone_map_pass_config->output_texture.reset();
  }
  if (auto_exposure_config) {
    auto_exposure_config->source_texture.reset();
  }
}

auto ForwardPipeline::Impl::GetImGuiPass() const
  -> observer_ptr<renderer::imgui::ImGuiPass>
{
  std::call_once(imgui_flag, [&] {
    if (auto mod = engine->GetModule<engine::imgui::ImGuiModule>()) {
      imgui_pass = mod->get().GetRenderPass();
    }
  });
  return imgui_pass;
}

ForwardPipeline::ForwardPipeline(observer_ptr<AsyncEngine> engine) noexcept
  : impl_(std::make_unique<Impl>(engine))
{
}

ForwardPipeline::~ForwardPipeline() = default;

auto ForwardPipeline::GetSupportedFeatures() const -> PipelineFeature
{
  return PipelineFeature::kOpaqueShading | PipelineFeature::kTransparentShading
    | PipelineFeature::kLightCulling;
}

auto ForwardPipeline::OnFrameStart(
  observer_ptr<engine::FrameContext> /*context*/, engine::Renderer& renderer)
  -> void
{
  impl_->ApplySettings();
  renderer.SetAtmosphereBlueNoiseEnabled(impl_->IsAtmosphereBlueNoiseEnabled());
}

auto ForwardPipeline::OnPublishViews(
  observer_ptr<engine::FrameContext> frame_ctx, engine::Renderer& renderer,
  scene::Scene& scene, std::span<const CompositionView> view_descs,
  graphics::Framebuffer* composite_target) -> co::Co<>
{
  impl_->SyncAutoExposureMeteringFromScene(scene);
  auto graphics = impl_->AcquireGraphics();
  impl_->PublishViews(view_descs, observer_ptr { composite_target }, *graphics,
    *frame_ctx, renderer);
  impl_->UnpublishView(*frame_ctx, renderer);
  co_return;
}

auto ForwardPipeline::OnPreRender(observer_ptr<engine::FrameContext> frame_ctx,
  engine::Renderer& renderer, std::span<const CompositionView> /*view_descs*/)
  -> co::Co<>
{
  impl_->RegisterRenderGraphs(renderer);
  impl_->BuildFramePlan(frame_ctx->GetScene());
  impl_->PlanCompositingTasks();

  co_return;
}

auto ForwardPipeline::OnCompositing(
  observer_ptr<engine::FrameContext> frame_ctx,
  std::shared_ptr<graphics::Framebuffer> composite_target)
  -> co::Co<engine::CompositionSubmission>
{
  DCHECK_NOTNULL_F(frame_ctx);
  DCHECK_F(frame_ctx->GetCurrentPhase() == core::PhaseId::kCompositing);
  co_return impl_->BuildCompositionSubmission(std::move(composite_target));
}

auto ForwardPipeline::ClearBackbufferReferences() -> void
{
  impl_->ClearBackbufferReferences();
}

// Stubs for configuration methods
auto ForwardPipeline::SetShaderDebugMode(engine::ShaderDebugMode mode) -> void
{
  impl_->SetShaderDebugMode(mode);
}

auto ForwardPipeline::SetRenderMode(RenderMode mode) -> void
{
  impl_->SetRenderMode(mode);
}

auto ForwardPipeline::SetGpuDebugPassEnabled(bool enabled) -> void
{
  impl_->SetGpuDebugPassEnabled(enabled);
}

auto ForwardPipeline::SetAtmosphereBlueNoiseEnabled(bool enabled) -> void
{
  impl_->SetAtmosphereBlueNoiseEnabled(enabled);
}

auto ForwardPipeline::SetGpuDebugMouseDownPosition(
  std::optional<SubPixelPosition> position) -> void
{
  impl_->SetGpuDebugMouseDownPosition(position);
}

auto ForwardPipeline::SetWireframeColor(const graphics::Color& color) -> void
{
  DLOG_F(1, "SetWireframeColor ({}, {}, {}, {})", color.r, color.g, color.b,
    color.a);
  impl_->SetWireframeColor(color);
}

auto ForwardPipeline::SetLightCullingVisualizationMode(
  engine::ShaderDebugMode mode) -> void
{
  impl_->SetLightCullingVisualizationMode(mode);
}

auto ForwardPipeline::SetClusterDepthSlices(uint32_t slices) -> void
{
  impl_->SetClusterDepthSlices(slices);
}

auto ForwardPipeline::SetExposureMode(engine::ExposureMode mode) -> void
{
  DLOG_F(1, "SetExposureMode {}", mode);
  impl_->SetExposureMode(mode);
}

auto ForwardPipeline::SetExposureValue(float value) -> void
{
  DLOG_F(1, "SetExposureValue {}", value);
  impl_->SetExposureValue(value);
}

auto ForwardPipeline::SetToneMapper(engine::ToneMapper mode) -> void
{
  LOG_F(INFO, "ForwardPipeline: SetToneMapper {}", mode);
  impl_->SetToneMapper(mode);
}

auto ForwardPipeline::SetGroundGridConfig(
  const engine::GroundGridPassConfig& config) -> void
{
  static std::atomic<bool> logged_once { false };
  if (!logged_once.exchange(true)) {
    LOG_F(INFO, "ForwardPipeline: SetGroundGridConfig");
  }
  impl_->SetGroundGridConfig(config);
}

auto ForwardPipeline::SetAutoExposureAdaptationSpeedUp(float speed) -> void
{
  impl_->SetAutoExposureAdaptationSpeedUp(speed);
}

auto ForwardPipeline::SetAutoExposureAdaptationSpeedDown(float speed) -> void
{
  impl_->SetAutoExposureAdaptationSpeedDown(speed);
}

auto ForwardPipeline::SetAutoExposureLowPercentile(float percentile) -> void
{
  impl_->SetAutoExposureLowPercentile(percentile);
}

auto ForwardPipeline::SetAutoExposureHighPercentile(float percentile) -> void
{
  impl_->SetAutoExposureHighPercentile(percentile);
}

auto ForwardPipeline::SetAutoExposureMinLogLuminance(float luminance) -> void
{
  impl_->SetAutoExposureMinLogLuminance(luminance);
}
auto ForwardPipeline::SetAutoExposureLogLuminanceRange(float range) -> void
{
  impl_->SetAutoExposureLogLuminanceRange(range);
}
auto ForwardPipeline::SetAutoExposureTargetLuminance(float luminance) -> void
{
  impl_->SetAutoExposureTargetLuminance(luminance);
}
auto ForwardPipeline::SetAutoExposureSpotMeterRadius(float radius) -> void
{
  impl_->SetAutoExposureSpotMeterRadius(radius);
}
auto ForwardPipeline::SetAutoExposureMeteringMode(engine::MeteringMode mode)
  -> void
{
  impl_->SetAutoExposureMeteringMode(mode);
}

auto ForwardPipeline::ResetAutoExposure(float initial_ev) -> void
{
  impl_->ResetAutoExposure(initial_ev);
}

auto ForwardPipeline::UpdateShaderPassConfig(
  const engine::ShaderPassConfig& config) -> void
{
  impl_->UpdateShaderPassConfig(config);
}

auto ForwardPipeline::SetGamma(float gamma) -> void { impl_->SetGamma(gamma); }

auto ForwardPipeline::UpdateTransparentPassConfig(
  const engine::TransparentPassConfig& config) -> void
{
  impl_->UpdateTransparentPassConfig(config);
}

auto ForwardPipeline::UpdateLightCullingPassConfig(
  const engine::LightCullingPassConfig& config) -> void
{
  impl_->UpdateLightCullingPassConfig(config);
}

} // namespace oxygen::renderer
