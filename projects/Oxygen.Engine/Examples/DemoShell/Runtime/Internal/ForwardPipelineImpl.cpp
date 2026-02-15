//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "DemoShell/Runtime/Internal/ForwardPipelineImpl.h"
#include "DemoShell/Runtime/Internal/FramePlanBuilder.h"

namespace oxygen::examples::internal {

namespace {
  struct DebugModeIntent {
    bool is_non_ibl { false };
    bool force_manual_exposure { false };
    bool force_exposure_one { false };
  };

  auto EvaluateDebugModeIntent(engine::ShaderDebugMode mode) -> DebugModeIntent
  {
    const auto is_non_ibl = [&] {
      switch (mode) {
      case engine::ShaderDebugMode::kLightCullingHeatMap:
      case engine::ShaderDebugMode::kDepthSlice:
      case engine::ShaderDebugMode::kClusterIndex:
      case engine::ShaderDebugMode::kBaseColor:
      case engine::ShaderDebugMode::kUv0:
      case engine::ShaderDebugMode::kOpacity:
      case engine::ShaderDebugMode::kWorldNormals:
      case engine::ShaderDebugMode::kRoughness:
      case engine::ShaderDebugMode::kMetalness:
        return true;
      case engine::ShaderDebugMode::kIblSpecular:
      case engine::ShaderDebugMode::kIblRawSky:
      case engine::ShaderDebugMode::kIblIrradiance:
      case engine::ShaderDebugMode::kIblFaceIndex:
      case engine::ShaderDebugMode::kDisabled:
      default:
        return false;
      }
    }();

    const auto is_ibl_debug = [&] {
      switch (mode) {
      case engine::ShaderDebugMode::kIblSpecular:
      case engine::ShaderDebugMode::kIblRawSky:
      case engine::ShaderDebugMode::kIblIrradiance:
      case engine::ShaderDebugMode::kIblFaceIndex:
        return true;
      case engine::ShaderDebugMode::kDisabled:
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

auto ForwardPipelineImpl::ConfigureWireframePass(const ViewRenderPlan& plan,
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

void ForwardPipelineImpl::TrackViewResources(
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

void ForwardPipelineImpl::ConfigurePassTargets(
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
  if (ground_grid_pass_config) {
    ground_grid_pass_config->color_texture = ctx.view->GetHdrTexture();
  }
  if (transparent_pass_config) {
    transparent_pass_config->color_texture = ctx.view->GetHdrTexture();
    transparent_pass_config->depth_texture = ctx.depth_texture;
  }
}

void ForwardPipelineImpl::BindHdrAndClear(
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

void ForwardPipelineImpl::BindSdrAndMaybeClear(
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

auto ForwardPipelineImpl::RenderWireframeScene(const ViewRenderContext& ctx,
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

auto ForwardPipelineImpl::RunScenePasses(const ViewRenderContext& ctx,
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

auto ForwardPipelineImpl::RenderGpuDebugOverlay(ViewRenderContext& ctx,
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

auto ForwardPipelineImpl::ToneMapToSdr(ViewRenderContext& ctx,
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

  rec.RequireResourceState(
    *ctx.view->GetHdrTexture(), graphics::ResourceStates::kShaderResource);
  rec.RequireResourceState(
    *ctx.view->GetSdrTexture(), graphics::ResourceStates::kRenderTarget);
  rec.FlushBarriers();
  ctx.sdr_in_render_target = true;

  co_await tone_map_pass->PrepareResources(rc, rec);
  co_await tone_map_pass->Execute(rc, rec);
}

void ForwardPipelineImpl::EnsureSdrBoundForOverlays(
  ViewRenderContext& ctx, graphics::CommandRecorder& rec) const
{
  if (!ctx.plan.HasCompositePath() || ctx.sdr_in_render_target) {
    return;
  }

  rec.RequireResourceState(
    *ctx.view->GetSdrTexture(), graphics::ResourceStates::kRenderTarget);
  rec.FlushBarriers();
  ctx.sdr_in_render_target = true;
}

auto ForwardPipelineImpl::RenderOverlayWireframe(const ViewRenderContext& ctx,
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

void ForwardPipelineImpl::RenderViewOverlay(
  const ViewRenderContext& ctx, graphics::CommandRecorder& rec) const
{
  rec.BindFrameBuffer(*ctx.view->GetSdrFramebuffer());
  if (ctx.view->GetDescriptor().on_overlay) {
    ctx.view->GetDescriptor().on_overlay(rec);
  }
}

auto ForwardPipelineImpl::RenderToolsImGui(const ViewRenderContext& ctx,
  graphics::CommandRecorder& rec) const -> co::Co<>
{
  if (ctx.view->GetDescriptor().z_order != CompositionView::kZOrderTools) {
    co_return;
  }

  if (auto imgui = GetImGuiPass()) {
    co_await imgui->Render(rec);
  }
}

void ForwardPipelineImpl::TransitionSdrToShaderRead(
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

auto ForwardPipelineImpl::ExecuteRegisteredView(ViewId id,
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

      if (ground_grid_pass && ground_grid_pass_config
        && ground_grid_pass_config->enabled) {
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

void ForwardPipelineImpl::PublishView(
  std::span<const CompositionView> view_descs,
  observer_ptr<graphics::Framebuffer> composite_target, Graphics& graphics,
  engine::FrameContext& context, engine::Renderer& renderer)
{
  EnsureViewLifecycleService(renderer);
  view_lifecycle_service->SyncActiveViews(
    context, view_descs, composite_target, graphics);
  view_lifecycle_service->PublishViews(context);
}

void ForwardPipelineImpl::EnsureViewLifecycleService(engine::Renderer& renderer)
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

ForwardPipelineImpl::ForwardPipelineImpl(observer_ptr<AsyncEngine> engine_ptr)
  : engine(engine_ptr)
  , frame_plan_builder(std::make_unique<FramePlanBuilder>())
  , composition_planner(observer_ptr { frame_plan_builder.get() })
{
  depth_pass_config = std::make_shared<engine::DepthPrePassConfig>();
  shader_pass_config = std::make_shared<engine::ShaderPassConfig>();
  wireframe_pass_config = std::make_shared<engine::WireframePassConfig>();
  sky_pass_config = std::make_shared<engine::SkyPassConfig>();
  ground_grid_pass_config = std::make_shared<engine::GroundGridPassConfig>();
  transparent_pass_config = std::make_shared<engine::TransparentPassConfig>();
  light_culling_pass_config
    = std::make_shared<engine::LightCullingPassConfig>();
  tone_map_pass_config = std::make_shared<engine::ToneMapPassConfig>();
  auto_exposure_config = std::make_shared<engine::AutoExposurePassConfig>();

  depth_pass = std::make_shared<engine::DepthPrePass>(depth_pass_config);
  shader_pass = std::make_shared<engine::ShaderPass>(shader_pass_config);
  wireframe_pass
    = std::make_shared<engine::WireframePass>(wireframe_pass_config);
  sky_pass = std::make_shared<engine::SkyPass>(sky_pass_config);
  ground_grid_pass
    = std::make_shared<engine::GroundGridPass>(ground_grid_pass_config);
  transparent_pass
    = std::make_shared<engine::TransparentPass>(transparent_pass_config);

  auto graphics = engine->GetGraphics().lock();
  light_culling_pass = std::make_shared<engine::LightCullingPass>(
    observer_ptr { graphics.get() }, light_culling_pass_config);
  tone_map_pass = std::make_shared<engine::ToneMapPass>(tone_map_pass_config);
  auto_exposure_pass = std::make_shared<engine::AutoExposurePass>(
    observer_ptr { graphics.get() }, auto_exposure_config);
  gpu_debug_clear_pass = std::make_shared<engine::GpuDebugClearPass>(
    observer_ptr { graphics.get() });
  gpu_debug_draw_pass = std::make_shared<engine::GpuDebugDrawPass>(
    observer_ptr { graphics.get() });

  settings_draft.ground_grid_config.enabled = false;
  frame_settings.ground_grid_config.enabled = false;
}

ForwardPipelineImpl::~ForwardPipelineImpl() = default;

void ForwardPipelineImpl::ApplySettings()
{
  if (!settings_draft.dirty) {
    return;
  }
  auto commit = settings_draft.Commit();
  frame_settings = commit.settings;
  pending_auto_exposure_reset = commit.auto_exposure_reset_ev;
  ApplyCommittedSettings(frame_settings);
}

void ForwardPipelineImpl::ApplyCommittedSettings(
  const PipelineSettings& settings)
{
  DLOG_F(1, "ApplySettings wire_color=({}, {}, {}, {})", settings.wire_color.r,
    settings.wire_color.g, settings.wire_color.b, settings.wire_color.a);

  if (shader_pass_config) {
    shader_pass_config->debug_mode = (settings.light_culling_debug_mode
                                       != engine::ShaderDebugMode::kDisabled)
      ? settings.light_culling_debug_mode
      : settings.shader_debug_mode;
    shader_pass_config->fill_mode = graphics::FillMode::kSolid;
  }

  if (transparent_pass_config) {
    transparent_pass_config->debug_mode = shader_pass_config
      ? shader_pass_config->debug_mode
      : engine::ShaderDebugMode::kDisabled;
    transparent_pass_config->fill_mode = graphics::FillMode::kSolid;
  }

  if (light_culling_pass_config) {
    light_culling_pass_config->cluster.cluster_dim_z
      = settings.cluster_depth_slices;
  }

  if (wireframe_pass) {
    wireframe_pass->SetWireColor(settings.wire_color);
  } else if (wireframe_pass_config) {
    wireframe_pass_config->wire_color = settings.wire_color;
  }

  if (ground_grid_pass_config) {
    static std::atomic<bool> logged_once { false };
    if (!logged_once.exchange(true)) {
      DLOG_F(1,
        "ForwardPipeline: Ground grid config initialized "
        "(spacing={}, major_every={}, line_thickness={}, major_thickness={})",
        settings.ground_grid_config.spacing,
        settings.ground_grid_config.major_every,
        settings.ground_grid_config.line_thickness,
        settings.ground_grid_config.major_thickness);
    }
    *ground_grid_pass_config = settings.ground_grid_config;
  }

  if (tone_map_pass_config) {
    const auto debug_mode = shader_pass_config
      ? shader_pass_config->debug_mode
      : engine::ShaderDebugMode::kDisabled;
    const auto debug_intent = EvaluateDebugModeIntent(debug_mode);
    tone_map_pass_config->exposure_mode = debug_intent.force_manual_exposure
      ? engine::ExposureMode::kManual
      : settings.exposure_mode;
    tone_map_pass_config->manual_exposure = debug_intent.force_exposure_one
      ? 1.0F
      : (debug_intent.force_manual_exposure ? 1.0F : settings.exposure_value);
    tone_map_pass_config->tone_mapper = settings.tonemapping_mode;
    tone_map_pass_config->gamma = settings.gamma;

    const bool config_changed = last_applied_tonemap_config.exposure_mode
        != tone_map_pass_config->exposure_mode
      || last_applied_tonemap_config.manual_exposure
        != tone_map_pass_config->manual_exposure
      || last_applied_tonemap_config.tone_mapper
        != tone_map_pass_config->tone_mapper
      || last_applied_tonemap_config.debug_mode != debug_mode;
    if (config_changed) {
      DLOG_F(1,
        "ForwardPipeline: ToneMap config applied "
        "(debug_mode={}, exp_mode={}, manual_exp={}, tone_mapper={})",
        static_cast<uint32_t>(debug_mode), tone_map_pass_config->exposure_mode,
        tone_map_pass_config->manual_exposure,
        tone_map_pass_config->tone_mapper);

      last_applied_tonemap_config.exposure_mode
        = tone_map_pass_config->exposure_mode;
      last_applied_tonemap_config.manual_exposure
        = tone_map_pass_config->manual_exposure;
      last_applied_tonemap_config.tone_mapper
        = tone_map_pass_config->tone_mapper;
      last_applied_tonemap_config.debug_mode = debug_mode;
    }
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

void ForwardPipelineImpl::SetShaderDebugMode(engine::ShaderDebugMode mode)
{
  settings_draft.shader_debug_mode = mode;
  settings_draft.dirty = true;
}

void ForwardPipelineImpl::SetRenderMode(RenderMode mode)
{
  settings_draft.render_mode = mode;
  settings_draft.dirty = true;
}

void ForwardPipelineImpl::SetGpuDebugPassEnabled(bool enabled)
{
  settings_draft.gpu_debug_pass_enabled = enabled;
  settings_draft.dirty = true;
}

void ForwardPipelineImpl::SetAtmosphereBlueNoiseEnabled(bool enabled)
{
  if (settings_draft.atmosphere_blue_noise_enabled == enabled) {
    return;
  }
  settings_draft.atmosphere_blue_noise_enabled = enabled;
  settings_draft.dirty = true;
}

void ForwardPipelineImpl::SetGpuDebugMouseDownPosition(
  std::optional<SubPixelPosition> position)
{
  settings_draft.gpu_debug_mouse_down_position = position;
  settings_draft.dirty = true;
}

void ForwardPipelineImpl::SetWireframeColor(const graphics::Color& color)
{
  settings_draft.wire_color = color;
  settings_draft.dirty = true;
}

void ForwardPipelineImpl::SetLightCullingVisualizationMode(
  engine::ShaderDebugMode mode)
{
  settings_draft.light_culling_debug_mode = mode;
  settings_draft.dirty = true;
}

void ForwardPipelineImpl::SetClusterDepthSlices(uint32_t slices)
{
  settings_draft.cluster_depth_slices = slices;
  settings_draft.dirty = true;
}

void ForwardPipelineImpl::SetExposureMode(engine::ExposureMode mode)
{
  if (mode == settings_draft.exposure_mode) {
    return;
  }
  settings_draft.exposure_mode = mode;
  settings_draft.dirty = true;
}

void ForwardPipelineImpl::SetExposureValue(float value)
{
  settings_draft.exposure_value = value;
  settings_draft.dirty = true;
}

void ForwardPipelineImpl::SetToneMapper(engine::ToneMapper mode)
{
  settings_draft.tonemapping_mode = mode;
  settings_draft.dirty = true;
}

void ForwardPipelineImpl::SetGroundGridConfig(
  const engine::GroundGridPassConfig& config)
{
  settings_draft.ground_grid_config = config;
  settings_draft.dirty = true;
}

void ForwardPipelineImpl::SetAutoExposureAdaptationSpeedUp(float speed)
{
  settings_draft.auto_exposure_adaptation_speed_up = speed;
  settings_draft.dirty = true;
}

void ForwardPipelineImpl::SetAutoExposureAdaptationSpeedDown(float speed)
{
  settings_draft.auto_exposure_adaptation_speed_down = speed;
  settings_draft.dirty = true;
}

void ForwardPipelineImpl::SetAutoExposureLowPercentile(float percentile)
{
  settings_draft.auto_exposure_low_percentile = percentile;
  settings_draft.dirty = true;
}

void ForwardPipelineImpl::SetAutoExposureHighPercentile(float percentile)
{
  settings_draft.auto_exposure_high_percentile = percentile;
  settings_draft.dirty = true;
}

void ForwardPipelineImpl::SetAutoExposureMinLogLuminance(float luminance)
{
  settings_draft.auto_exposure_min_log_luminance = luminance;
  settings_draft.dirty = true;
}

void ForwardPipelineImpl::SetAutoExposureLogLuminanceRange(float range)
{
  settings_draft.auto_exposure_log_luminance_range = range;
  settings_draft.dirty = true;
}

void ForwardPipelineImpl::SetAutoExposureTargetLuminance(float luminance)
{
  settings_draft.auto_exposure_target_luminance = luminance;
  settings_draft.dirty = true;
}

void ForwardPipelineImpl::SetAutoExposureSpotMeterRadius(float radius)
{
  settings_draft.auto_exposure_spot_meter_radius = radius;
  settings_draft.dirty = true;
}

void ForwardPipelineImpl::SetAutoExposureMeteringMode(engine::MeteringMode mode)
{
  settings_draft.auto_exposure_metering = mode;
  settings_draft.dirty = true;
}

void ForwardPipelineImpl::ResetAutoExposure(float initial_ev)
{
  settings_draft.auto_exposure_reset_pending = true;
  settings_draft.auto_exposure_reset_ev = initial_ev;
  settings_draft.dirty = true;
}

void ForwardPipelineImpl::SetGamma(float gamma)
{
  settings_draft.gamma = gamma;
  settings_draft.dirty = true;
}

void ForwardPipelineImpl::ClearBackbufferReferences() const
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
  if (ground_grid_pass_config) {
    ground_grid_pass_config->color_texture.reset();
  }
  if (transparent_pass_config) {
    transparent_pass_config->color_texture.reset();
    transparent_pass_config->depth_texture.reset();
  }
  if (tone_map_pass_config) {
    tone_map_pass_config->source_texture.reset();
    tone_map_pass_config->output_texture.reset();
  }
  if (auto_exposure_config) {
    auto_exposure_config->source_texture.reset();
  }
}

auto ForwardPipelineImpl::GetImGuiPass() const -> observer_ptr<imgui::ImGuiPass>
{
  std::call_once(imgui_flag, [&] {
    if (auto mod = engine->GetModule<imgui::ImGuiModule>()) {
      imgui_pass = mod->get().GetRenderPass();
    }
  });
  return imgui_pass;
}

} // namespace oxygen::examples::internal
