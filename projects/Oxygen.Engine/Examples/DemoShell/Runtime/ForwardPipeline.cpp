//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstdint>
#include <map>
#include <mutex>
#include <span>
#include <vector>

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
#include <Oxygen/Renderer/Passes/DepthPrePass.h>
#include <Oxygen/Renderer/Passes/LightCullingPass.h>
#include <Oxygen/Renderer/Passes/ShaderPass.h>
#include <Oxygen/Renderer/Passes/SkyAtmosphereLutComputePass.h>
#include <Oxygen/Renderer/Passes/SkyPass.h>
#include <Oxygen/Renderer/Passes/ToneMapPass.h>
#include <Oxygen/Renderer/Passes/TransparentPass.h>
#include <Oxygen/Renderer/Passes/WireframePass.h>
#include <Oxygen/Renderer/Renderer.h>
#include <Oxygen/Renderer/SceneCameraViewResolver.h>
#include <Oxygen/Renderer/Types/CompositingTask.h>
#include <Oxygen/Scene/Environment/SceneEnvironment.h>
#include <Oxygen/Scene/Environment/SkyAtmosphere.h>
#include <Oxygen/Scene/Environment/SkySphere.h>
#include <Oxygen/Scene/Scene.h>

#include "DemoShell/Runtime/ForwardPipeline.h"

namespace oxygen::examples {

//! Internal state for a single active composition view.
struct CompositionViewImpl {
  CompositionView intent;
  uint32_t submission_index { 0 };
  frame::SequenceNumber last_seen_frame;

  // GPU Resources
  std::shared_ptr<graphics::Texture> hdr_texture;
  std::shared_ptr<graphics::Framebuffer> hdr_framebuffer;
  std::shared_ptr<graphics::Texture> sdr_texture;
  std::shared_ptr<graphics::Framebuffer> sdr_framebuffer;

  uint32_t width { 0 };
  uint32_t height { 0 };
  bool has_hdr { false };
  graphics::Color clear_color { 0.0F, 0.0F, 0.0F, 1.0F };

  // Engine Link
  ViewId engine_vid { kInvalidViewId };
  bool registered_with_renderer { false };

  void Sync(const CompositionView& desc, uint32_t index,
    frame::SequenceNumber frame_seq)
  {
    intent = desc;
    submission_index = index;
    last_seen_frame = frame_seq;
  }

  void EnsureResources(Graphics& graphics)
  {
    const uint32_t target_w
      = std::max(1U, static_cast<uint32_t>(intent.view.viewport.width));
    const uint32_t target_h
      = std::max(1U, static_cast<uint32_t>(intent.view.viewport.height));
    const bool needs_hdr = intent.enable_hdr;
    const graphics::Color& target_clear = intent.clear_color;

    if (width == target_w && height == target_h && has_hdr == needs_hdr
      && clear_color == target_clear) {
      if (needs_hdr && hdr_texture) {
        return;
      }
      if (!needs_hdr && sdr_texture) {
        return;
      }
    }

    LOG_F(INFO,
      "Configuring View '{}' (ID: {}) -> {}x{}, HDR: {}, "
      "Clear: ({}, {}, {}, {})",
      intent.name, intent.id, target_w, target_h, needs_hdr, target_clear.r,
      target_clear.g, target_clear.b, target_clear.a);

    width = target_w;
    height = target_h;
    has_hdr = needs_hdr;
    clear_color = target_clear;

    if (needs_hdr) {
      graphics::TextureDesc hdr_desc;
      hdr_desc.width = target_w;
      hdr_desc.height = target_h;
      hdr_desc.format = oxygen::Format::kRGBA16Float;
      hdr_desc.texture_type = oxygen::TextureType::kTexture2D;
      hdr_desc.is_render_target = true;
      hdr_desc.is_shader_resource = true;
      hdr_desc.use_clear_value = true;
      hdr_desc.clear_value = target_clear;
      hdr_desc.initial_state = graphics::ResourceStates::kCommon;
      hdr_desc.debug_name = "Forward_HDR_Intermediate";
      hdr_texture = graphics.CreateTexture(hdr_desc);

      graphics::FramebufferDesc hdr_fb_desc;
      hdr_fb_desc.AddColorAttachment({ .texture = hdr_texture });

      graphics::TextureDesc depth_desc;
      depth_desc.width = target_w;
      depth_desc.height = target_h;
      depth_desc.format = oxygen::Format::kDepth32;
      depth_desc.texture_type = oxygen::TextureType::kTexture2D;
      depth_desc.is_render_target = true;
      depth_desc.is_shader_resource = true;
      depth_desc.use_clear_value = true;
      depth_desc.clear_value = { 1.0F, 0.0F, 0.0F, 0.0F };
      depth_desc.initial_state = graphics::ResourceStates::kCommon;
      depth_desc.debug_name = "Forward_HDR_Depth";
      hdr_fb_desc.SetDepthAttachment(graphics.CreateTexture(depth_desc));
      hdr_framebuffer = graphics.CreateFramebuffer(hdr_fb_desc);
    } else {
      hdr_texture = nullptr;
      hdr_framebuffer = nullptr;
    }

    graphics::TextureDesc sdr_desc;
    sdr_desc.width = target_w;
    sdr_desc.height = target_h;
    sdr_desc.format = oxygen::Format::kRGBA8UNorm;
    sdr_desc.texture_type = oxygen::TextureType::kTexture2D;
    sdr_desc.is_render_target = true;
    sdr_desc.is_shader_resource = true;
    sdr_desc.use_clear_value = true;
    sdr_desc.clear_value = target_clear;
    sdr_desc.initial_state = graphics::ResourceStates::kCommon;
    sdr_desc.debug_name = "Forward_SDR_Intermediate";
    sdr_texture = graphics.CreateTexture(sdr_desc);

    graphics::FramebufferDesc sdr_fb_desc;
    sdr_fb_desc.AddColorAttachment({ .texture = sdr_texture });
    sdr_framebuffer = graphics.CreateFramebuffer(sdr_fb_desc);
  }
};

namespace {
  enum class WireframeTarget : uint8_t {
    kHdr,
    kSdr,
  };

  struct ViewRenderPlan {
    RenderMode effective_render_mode { RenderMode::kSolid };
    bool has_scene { false };
    bool hdr_path_enabled { false };
    bool sdr_path_enabled { false };
    bool require_neutral_tonemap { false };
    bool allow_overlay_wireframe { false };
    bool allow_sky_visuals { false };
    bool allow_sky_lut { false };
    bool wireframe_apply_exposure_compensation { false };
    WireframeTarget wireframe_target { WireframeTarget::kHdr };
  };

  struct DebugModeIntent {
    bool is_non_ibl { false };
    bool force_manual_exposure { false };
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
      case engine::ShaderDebugMode::kDisabled:
      default:
        return false;
      }
    }();

    return DebugModeIntent {
      .is_non_ibl = is_non_ibl,
      .force_manual_exposure = is_non_ibl,
    };
  }

  auto GetWireframeTargetTexture(const ViewRenderPlan& plan,
    const CompositionViewImpl& view) -> std::shared_ptr<const graphics::Texture>
  {
    if (plan.wireframe_target == WireframeTarget::kSdr) {
      DCHECK_NOTNULL_F(view.sdr_texture.get());
      return view.sdr_texture;
    }
    return view.hdr_texture;
  }

  class ToneMapOverrideGuard {
  public:
    ToneMapOverrideGuard(engine::ToneMapPassConfig& config, bool enable_neutral)
      : config_(config)
      , saved_exposure_mode_(config.exposure_mode)
      , saved_manual_exposure_(config.manual_exposure)
      , saved_tone_mapper_(config.tone_mapper)
      , active_(enable_neutral)
    {
      if (!active_) {
        return;
      }

      config_.exposure_mode = engine::ExposureMode::kManual;
      config_.manual_exposure = 1.0F;
      config_.tone_mapper = engine::ToneMapper::kNone;
    }

    ~ToneMapOverrideGuard()
    {
      if (!active_) {
        return;
      }

      config_.exposure_mode = saved_exposure_mode_;
      config_.manual_exposure = saved_manual_exposure_;
      config_.tone_mapper = saved_tone_mapper_;
    }

    OXYGEN_MAKE_NON_COPYABLE(ToneMapOverrideGuard)
    OXYGEN_MAKE_NON_MOVABLE(ToneMapOverrideGuard)

  private:
    engine::ToneMapPassConfig& config_;
    engine::ExposureMode saved_exposure_mode_;
    float saved_manual_exposure_;
    engine::ToneMapper saved_tone_mapper_;
    bool active_;
  };
} // namespace

struct ForwardPipeline::Impl {
  observer_ptr<AsyncEngine> engine;

  // Persistent workers
  std::map<ViewId, CompositionViewImpl> view_pool;
  // Frame active views (sorted)
  std::vector<CompositionViewImpl*> sorted_views;

  // Pass Configs
  std::shared_ptr<engine::DepthPrePassConfig> depth_pass_config;
  std::shared_ptr<engine::ShaderPassConfig> shader_pass_config;
  std::shared_ptr<engine::WireframePassConfig> wireframe_pass_config;
  std::shared_ptr<engine::SkyPassConfig> sky_pass_config;
  std::shared_ptr<engine::TransparentPassConfig> transparent_pass_config;
  std::shared_ptr<engine::LightCullingPassConfig> light_culling_pass_config;
  std::shared_ptr<engine::SkyAtmosphereLutComputePassConfig>
    sky_atmo_lut_pass_config;
  std::shared_ptr<engine::ToneMapPassConfig> tone_map_pass_config;

  // Pass Instances
  std::shared_ptr<engine::DepthPrePass> depth_pass;
  std::shared_ptr<engine::ShaderPass> shader_pass;
  std::shared_ptr<engine::WireframePass> wireframe_pass;
  std::shared_ptr<engine::SkyPass> sky_pass;
  std::shared_ptr<engine::TransparentPass> transparent_pass;
  std::shared_ptr<engine::LightCullingPass> light_culling_pass;
  std::shared_ptr<engine::SkyAtmosphereLutComputePass> sky_atmo_lut_pass;
  std::shared_ptr<engine::ToneMapPass> tone_map_pass;

  // ImGui lazy loading
  mutable std::once_flag imgui_flag;
  mutable observer_ptr<imgui::ImGuiPass> imgui_pass;

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
  } staged;

  struct ViewRenderContext {
    CompositionViewImpl& view;
    ViewRenderPlan plan;
    std::shared_ptr<const graphics::Texture> depth_texture;
    bool sdr_in_render_target { false };
  };

  struct SkyState {
    bool sky_atmo_enabled { false };
    bool sky_sphere_enabled { false };
  };

  auto EvaluateSkyState(const engine::RenderContext& rc) const -> SkyState
  {
    SkyState state {};
    if (const auto scene = rc.GetScene()) {
      if (const auto env = scene->GetEnvironment()) {
        if (const auto atmo
          = env->TryGetSystem<scene::environment::SkyAtmosphere>();
          atmo && atmo->IsEnabled()) {
          state.sky_atmo_enabled = true;
        }
        if (const auto sphere
          = env->TryGetSystem<scene::environment::SkySphere>();
          sphere && sphere->IsEnabled()) {
          state.sky_sphere_enabled = true;
        }
      }
    }
    return state;
  }

  auto EvaluateViewRenderPlan(const CompositionViewImpl& view,
    const engine::RenderContext& rc) const -> ViewRenderPlan
  {
    ViewRenderPlan plan {};
    plan.effective_render_mode = staged.render_mode;
    if (view.intent.force_wireframe) {
      plan.effective_render_mode = RenderMode::kWireframe;
    }

    plan.has_scene = view.intent.camera.has_value();
    plan.hdr_path_enabled
      = view.has_hdr && view.hdr_texture && view.hdr_framebuffer;
    plan.sdr_path_enabled = view.sdr_texture && view.sdr_framebuffer;
    plan.require_neutral_tonemap = plan.has_scene
      && (plan.effective_render_mode == RenderMode::kWireframe);
    plan.allow_overlay_wireframe = plan.has_scene
      && (staged.render_mode == RenderMode::kOverlayWireframe)
      && (plan.effective_render_mode != RenderMode::kWireframe);
    plan.wireframe_apply_exposure_compensation = false;

    plan.wireframe_target
      = (plan.allow_overlay_wireframe || !plan.hdr_path_enabled)
      ? WireframeTarget::kSdr
      : WireframeTarget::kHdr;

    const auto debug_mode = shader_pass_config
      ? shader_pass_config->debug_mode
      : engine::ShaderDebugMode::kDisabled;
    const auto debug_intent = EvaluateDebugModeIntent(debug_mode);
    const auto sky_state = EvaluateSkyState(rc);
    const bool run_scene_passes = plan.has_scene && plan.hdr_path_enabled
      && (plan.effective_render_mode != RenderMode::kWireframe);
    const bool allow_sky_visuals = run_scene_passes
      && (sky_state.sky_atmo_enabled || sky_state.sky_sphere_enabled)
      && !debug_intent.is_non_ibl;
    plan.allow_sky_visuals = allow_sky_visuals;
    plan.allow_sky_lut = run_scene_passes && sky_state.sky_atmo_enabled;

    DLOG_F(2,
      "ViewRenderPlan view='{}' mode={} scene={} hdr={} "
      "sdr={} overlay={} neutral={} sky={} lut={}",
      view.intent.name, to_string(plan.effective_render_mode), plan.has_scene,
      plan.hdr_path_enabled, plan.sdr_path_enabled,
      plan.allow_overlay_wireframe, plan.require_neutral_tonemap,
      plan.allow_sky_visuals, plan.allow_sky_lut);

    return plan;
  }

  auto ConfigureWireframePass(const ViewRenderPlan& plan,
    const CompositionViewImpl& view, bool clear_color, bool clear_depth,
    bool depth_write_enable) const -> void
  {
    if (!wireframe_pass_config) {
      return;
    }

    wireframe_pass_config->clear_color_target = clear_color;
    wireframe_pass_config->clear_depth_target = clear_depth;
    wireframe_pass_config->depth_write_enable = depth_write_enable;
    wireframe_pass_config->apply_exposure_compensation
      = plan.wireframe_apply_exposure_compensation;
    wireframe_pass_config->color_texture
      = GetWireframeTargetTexture(plan, view);

    if (wireframe_pass) {
      wireframe_pass->SetWireColor(staged.wire_color);
    } else {
      wireframe_pass_config->wire_color = staged.wire_color;
    }
  }

  void TrackViewResources(
    ViewRenderContext& ctx, graphics::CommandRecorder& rec) const
  {
    if (!ctx.plan.hdr_path_enabled) {
      return;
    }

    const auto fb = ctx.view.hdr_framebuffer;
    const auto& fb_desc = fb->GetDescriptor();
    if (fb_desc.depth_attachment.IsValid()) {
      ctx.depth_texture = fb_desc.depth_attachment.texture;
    }

    if (ctx.view.hdr_texture && !rec.IsResourceTracked(*ctx.view.hdr_texture)) {
      rec.BeginTrackingResourceState(
        *ctx.view.hdr_texture, graphics::ResourceStates::kCommon, true);
    }
    if (ctx.depth_texture && !rec.IsResourceTracked(*ctx.depth_texture)) {
      rec.BeginTrackingResourceState(
        *ctx.depth_texture, graphics::ResourceStates::kCommon, true);
    }
    if (ctx.view.sdr_texture && !rec.IsResourceTracked(*ctx.view.sdr_texture)) {
      rec.BeginTrackingResourceState(
        *ctx.view.sdr_texture, graphics::ResourceStates::kCommon, true);
    }
  }

  void ConfigurePassTargets(const ViewRenderContext& ctx) const
  {
    if (!ctx.plan.hdr_path_enabled) {
      return;
    }

    if (depth_pass_config) {
      depth_pass_config->depth_texture = ctx.depth_texture;
    }
    if (shader_pass_config) {
      shader_pass_config->color_texture = ctx.view.hdr_texture;
    }
    if (wireframe_pass_config) {
      wireframe_pass_config->color_texture = ctx.view.hdr_texture;
    }
    if (sky_pass_config) {
      sky_pass_config->color_texture = ctx.view.hdr_texture;
    }
    if (transparent_pass_config) {
      transparent_pass_config->color_texture = ctx.view.hdr_texture;
      transparent_pass_config->depth_texture = ctx.depth_texture;
    }
  }

  void ConfigureSkyLutManager(
    const ViewRenderContext& ctx, engine::Renderer& renderer) const
  {
    if (!sky_atmo_lut_pass_config) {
      return;
    }

    sky_atmo_lut_pass_config->lut_manager = ctx.plan.allow_sky_lut
      ? renderer.GetSkyAtmosphereLutManager()
      : nullptr;
  }

  void BindHdrAndClear(
    ViewRenderContext& ctx, graphics::CommandRecorder& rec) const
  {
    if (!ctx.plan.hdr_path_enabled) {
      return;
    }

    rec.RequireResourceState(
      *ctx.view.hdr_texture, graphics::ResourceStates::kRenderTarget);
    if (ctx.depth_texture) {
      rec.RequireResourceState(
        *ctx.depth_texture, graphics::ResourceStates::kDepthWrite);
    }
    rec.FlushBarriers();

    rec.BindFrameBuffer(*ctx.view.hdr_framebuffer);
    const auto hdr_clear = ctx.view.hdr_framebuffer->GetDescriptor()
                             .color_attachments[0]
                             .ResolveClearColor(std::nullopt);
    rec.ClearFramebuffer(*ctx.view.hdr_framebuffer,
      std::vector<std::optional<graphics::Color>> { hdr_clear }, 1.0F);
  }

  void BindSdrAndMaybeClear(
    ViewRenderContext& ctx, graphics::CommandRecorder& rec) const
  {
    if (!ctx.plan.sdr_path_enabled || ctx.plan.hdr_path_enabled) {
      return;
    }

    rec.RequireResourceState(
      *ctx.view.sdr_texture, graphics::ResourceStates::kRenderTarget);
    rec.FlushBarriers();
    ctx.sdr_in_render_target = true;
    rec.BindFrameBuffer(*ctx.view.sdr_framebuffer);
    if (ctx.view.intent.should_clear) {
      const auto sdr_clear = ctx.view.sdr_framebuffer->GetDescriptor()
                               .color_attachments[0]
                               .ResolveClearColor(std::nullopt);
      rec.ClearFramebuffer(*ctx.view.sdr_framebuffer,
        std::vector<std::optional<graphics::Color>> { sdr_clear });
    }
  }

  auto RenderWireframeScene(const ViewRenderContext& ctx,
    const engine::RenderContext& rc, graphics::CommandRecorder& rec) const
    -> co::Co<>
  {
    if (!wireframe_pass_config || !wireframe_pass) {
      co_return;
    }

    const bool is_forced = ctx.view.intent.force_wireframe;
    ConfigureWireframePass(ctx.plan, ctx.view, !is_forced, true, true);
    co_await wireframe_pass->PrepareResources(rc, rec);
    co_await wireframe_pass->Execute(rc, rec);
  }

  auto RunScenePasses(const ViewRenderContext& ctx,
    const engine::RenderContext& rc, graphics::CommandRecorder& rec) const
    -> co::Co<>
  {
    if (depth_pass && ctx.depth_texture) {
      co_await depth_pass->PrepareResources(rc, rec);
      co_await depth_pass->Execute(rc, rec);
      rc.RegisterPass<engine::DepthPrePass>(depth_pass.get());
    }

    if (ctx.plan.allow_sky_lut && sky_atmo_lut_pass && sky_atmo_lut_pass_config
      && sky_atmo_lut_pass_config->lut_manager) {
      co_await sky_atmo_lut_pass->PrepareResources(rc, rec);
      co_await sky_atmo_lut_pass->Execute(rc, rec);
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

    if (ctx.plan.allow_sky_visuals && sky_pass) {
      co_await sky_pass->PrepareResources(rc, rec);
      co_await sky_pass->Execute(rc, rec);
    }

    if (transparent_pass) {
      co_await transparent_pass->PrepareResources(rc, rec);
      co_await transparent_pass->Execute(rc, rec);
      rc.RegisterPass<engine::TransparentPass>(transparent_pass.get());
    }
  }

  auto ToneMapToSdr(ViewRenderContext& ctx, const engine::RenderContext& rc,
    graphics::CommandRecorder& rec) const -> co::Co<>
  {
    const bool should_tonemap
      = ctx.plan.hdr_path_enabled && ctx.plan.sdr_path_enabled;
    if (!tone_map_pass || !tone_map_pass_config || !should_tonemap) {
      co_return;
    }

    tone_map_pass_config->source_texture = ctx.view.hdr_texture;
    tone_map_pass_config->output_texture = ctx.view.sdr_texture;
    ToneMapOverrideGuard override_guard(
      *tone_map_pass_config, ctx.plan.require_neutral_tonemap);

    rec.RequireResourceState(
      *ctx.view.hdr_texture, graphics::ResourceStates::kShaderResource);
    rec.RequireResourceState(
      *ctx.view.sdr_texture, graphics::ResourceStates::kRenderTarget);
    rec.FlushBarriers();
    ctx.sdr_in_render_target = true;

    co_await tone_map_pass->PrepareResources(rc, rec);
    co_await tone_map_pass->Execute(rc, rec);
  }

  void EnsureSdrBoundForOverlays(
    ViewRenderContext& ctx, graphics::CommandRecorder& rec) const
  {
    if (!ctx.plan.sdr_path_enabled || ctx.sdr_in_render_target) {
      return;
    }

    rec.RequireResourceState(
      *ctx.view.sdr_texture, graphics::ResourceStates::kRenderTarget);
    rec.FlushBarriers();
    ctx.sdr_in_render_target = true;
  }

  auto RenderOverlayWireframe(const ViewRenderContext& ctx,
    const engine::RenderContext& rc, graphics::CommandRecorder& rec) const
    -> co::Co<>
  {
    if (!ctx.plan.allow_overlay_wireframe || !wireframe_pass_config
      || !wireframe_pass) {
      co_return;
    }

    const auto scene = rc.GetScene();
    DCHECK_NOTNULL_F(scene, "Overlay wireframe requires an active scene");
    DCHECK_F(ctx.view.intent.camera.has_value(),
      "Overlay wireframe requires a camera node");
    auto camera_node = *ctx.view.intent.camera;
    DCHECK_F(camera_node.IsAlive(), "Overlay wireframe requires a live camera");
    DCHECK_F(
      camera_node.HasCamera(), "Overlay wireframe requires a camera component");
    DCHECK_F(scene->Contains(camera_node),
      "Overlay wireframe camera is not in the active scene");

    ConfigureWireframePass(ctx.plan, ctx.view, false, false, false);
    co_await wireframe_pass->PrepareResources(rc, rec);
    co_await wireframe_pass->Execute(rc, rec);
  }

  void RenderViewOverlay(
    const ViewRenderContext& ctx, graphics::CommandRecorder& rec) const
  {
    rec.BindFrameBuffer(*ctx.view.sdr_framebuffer);
    if (ctx.view.intent.on_overlay) {
      ctx.view.intent.on_overlay(rec);
    }
  }

  auto RenderToolsImGui(const ViewRenderContext& ctx,
    graphics::CommandRecorder& rec) const -> co::Co<>
  {
    if (ctx.view.intent.z_order != CompositionView::kZOrderTools) {
      co_return;
    }

    if (auto imgui = GetImGuiPass()) {
      co_await imgui->Render(rec);
    }
  }

  void TransitionSdrToShaderRead(
    ViewRenderContext& ctx, graphics::CommandRecorder& rec) const
  {
    if (!ctx.plan.sdr_path_enabled) {
      return;
    }

    rec.RequireResourceState(
      *ctx.view.sdr_texture, graphics::ResourceStates::kShaderResource);
    rec.FlushBarriers();
    ctx.sdr_in_render_target = false;
  }

  explicit Impl(observer_ptr<AsyncEngine> engine_ptr)
    : engine(engine_ptr)
  {
    // config init
    depth_pass_config = std::make_shared<engine::DepthPrePassConfig>();
    shader_pass_config = std::make_shared<engine::ShaderPassConfig>();
    wireframe_pass_config = std::make_shared<engine::WireframePassConfig>();
    sky_pass_config = std::make_shared<engine::SkyPassConfig>();
    transparent_pass_config = std::make_shared<engine::TransparentPassConfig>();
    light_culling_pass_config
      = std::make_shared<engine::LightCullingPassConfig>();
    sky_atmo_lut_pass_config
      = std::make_shared<engine::SkyAtmosphereLutComputePassConfig>();
    tone_map_pass_config = std::make_shared<engine::ToneMapPassConfig>();

    // pass init
    depth_pass = std::make_shared<engine::DepthPrePass>(depth_pass_config);
    shader_pass = std::make_shared<engine::ShaderPass>(shader_pass_config);
    wireframe_pass
      = std::make_shared<engine::WireframePass>(wireframe_pass_config);
    sky_pass = std::make_shared<engine::SkyPass>(sky_pass_config);
    transparent_pass
      = std::make_shared<engine::TransparentPass>(transparent_pass_config);

    auto graphics = engine->GetGraphics().lock();
    light_culling_pass = std::make_shared<engine::LightCullingPass>(
      observer_ptr { graphics.get() }, light_culling_pass_config);
    sky_atmo_lut_pass = std::make_shared<engine::SkyAtmosphereLutComputePass>(
      observer_ptr { graphics.get() }, sky_atmo_lut_pass_config);
    tone_map_pass = std::make_shared<engine::ToneMapPass>(tone_map_pass_config);
  }

  void ApplySettings()
  {
    if (!staged.dirty) {
      return;
    }

    DLOG_F(1, "ApplySettings wire_color=({}, {}, {}, {})", staged.wire_color.r,
      staged.wire_color.g, staged.wire_color.b, staged.wire_color.a);

    // Resolve Debug Mode: Priority to Light Culling Visualization if active
    if (shader_pass_config) {
      shader_pass_config->debug_mode = (staged.light_culling_debug_mode
                                         != engine::ShaderDebugMode::kDisabled)
        ? staged.light_culling_debug_mode
        : staged.shader_debug_mode;
      shader_pass_config->fill_mode = graphics::FillMode::kSolid;
    }

    if (light_culling_pass_config) {
      light_culling_pass_config->cluster.depth_slices
        = staged.clustered_culling_enabled ? staged.cluster_depth_slices : 1;
    }

    if (wireframe_pass) {
      wireframe_pass->SetWireColor(staged.wire_color);
    } else if (wireframe_pass_config) {
      wireframe_pass_config->wire_color = staged.wire_color;
    }

    if (tone_map_pass_config) {
      const auto debug_mode = shader_pass_config
        ? shader_pass_config->debug_mode
        : engine::ShaderDebugMode::kDisabled;
      const auto debug_intent = EvaluateDebugModeIntent(debug_mode);
      tone_map_pass_config->exposure_mode = debug_intent.force_manual_exposure
        ? engine::ExposureMode::kManual
        : staged.exposure_mode;
      tone_map_pass_config->manual_exposure = 1.0F;
      tone_map_pass_config->tone_mapper = staged.tonemapping_mode;
    }

    staged.dirty = false;
  }

  void ClearBackbufferReferences() const
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
    if (transparent_pass_config) {
      transparent_pass_config->color_texture.reset();
      transparent_pass_config->depth_texture.reset();
    }
    if (tone_map_pass_config) {
      tone_map_pass_config->source_texture.reset();
      tone_map_pass_config->output_texture.reset();
    }
  }

  auto GetImGuiPass() const -> observer_ptr<imgui::ImGuiPass>
  {
    std::call_once(imgui_flag, [&] {
      if (auto mod = engine->GetModule<imgui::ImGuiModule>()) {
        imgui_pass = mod->get().GetRenderPass();
      }
    });
    return imgui_pass;
  }

  void ReapResources(frame::SequenceNumber current_frame,
    observer_ptr<engine::FrameContext> context, engine::Renderer& renderer)
  {
    static constexpr frame::SequenceNumber kMaxIdleFrames { 60 };
    for (auto it = view_pool.begin(); it != view_pool.end();) {
      if (current_frame - it->second.last_seen_frame > kMaxIdleFrames) {
        LOG_F(INFO, "Reaping View resources for ID {}", it->first);

        if (it->second.engine_vid != kInvalidViewId) {
          LOG_F(INFO,
            "Unregistering View '{}' (EngineVID: {}) from Engine and Renderer",
            it->second.intent.name, it->second.engine_vid.get());
          context->RemoveView(it->second.engine_vid);
          renderer.UnregisterView(it->second.engine_vid);
        }

        it = view_pool.erase(it);
      } else {
        ++it;
      }
    }
  }
};

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
  observer_ptr<engine::FrameContext> /*context*/,
  engine::Renderer& /*renderer*/) -> void
{
  impl_->ApplySettings();
}

auto ForwardPipeline::OnSceneMutation(
  observer_ptr<engine::FrameContext> context, engine::Renderer& renderer,
  scene::Scene& /*scene*/, std::span<const CompositionView> view_descs,
  graphics::Framebuffer* target_framebuffer) -> co::Co<>
{
  impl_->sorted_views.clear();
  impl_->sorted_views.reserve(view_descs.size());

  auto graphics = impl_->engine->GetGraphics().lock();
  const auto frame_seq = context->GetFrameSequenceNumber();

  uint32_t index = 0;
  for (auto desc : view_descs) { // Copy so we can modify viewport
    // Resolution: if viewport is empty, try to derive from target_framebuffer
    // or default to 1280x720
    if (desc.view.viewport.width <= 0 || desc.view.viewport.height <= 0) {
      if (target_framebuffer != nullptr) {
        const auto& fb_desc = target_framebuffer->GetDescriptor();
        if (!fb_desc.color_attachments.empty()
          && fb_desc.color_attachments[0].texture) {
          desc.view.viewport.width = static_cast<float>(
            fb_desc.color_attachments[0].texture->GetDescriptor().width);
          desc.view.viewport.height = static_cast<float>(
            fb_desc.color_attachments[0].texture->GetDescriptor().height);
        }
      } else {
        // Fallback to 720p if absolutely nothing else
        desc.view.viewport.width = 1280.0f;
        desc.view.viewport.height = 720.0f;
      }
    }

    auto& view_impl = impl_->view_pool[desc.id];
    view_impl.Sync(desc, index++, frame_seq);
    view_impl.EnsureResources(*graphics);
    impl_->sorted_views.push_back(&view_impl);
  }

  // Stable sort: Z-Order first, then Submission Index
  std::stable_sort(impl_->sorted_views.begin(), impl_->sorted_views.end(),
    [](const CompositionViewImpl* a, const CompositionViewImpl* b) {
      if (a->intent.z_order != b->intent.z_order) {
        return a->intent.z_order < b->intent.z_order;
      }
      return a->submission_index < b->submission_index;
    });

  // Register with engine renderer
  for (auto* view : impl_->sorted_views) {
    // Register View Metadata with FrameContext
    engine::ViewContext view_ctx;
    view_ctx.view = view->intent.view;
    const bool has_scene = view->intent.camera.has_value();
    view_ctx.metadata = { .name = std::string(view->intent.name),
      .purpose = has_scene ? "scene" : "overlay" };
    if (view->has_hdr && view->hdr_framebuffer) {
      view_ctx.output = observer_ptr { view->hdr_framebuffer.get() };
    } else {
      view_ctx.output = observer_ptr { view->sdr_framebuffer.get() };
    }

    // Maintain stable link to engine's internal view registry
    if (view->engine_vid == kInvalidViewId) {
      view->engine_vid = context->RegisterView(std::move(view_ctx));
      LOG_F(INFO,
        "Registered View '{}' (IntentID: {}) with Engine (EngineVID: {})",
        view->intent.name, view->intent.id.get(), view->engine_vid.get());
    } else {
      context->UpdateView(view->engine_vid, std::move(view_ctx));
      DLOG_F(1, "Updated View '{}' (EngineVID: {})", view->intent.name,
        view->engine_vid.get());
    }

    const auto engine_vid = view->engine_vid;

    if (!view->registered_with_renderer) {
      LOG_F(INFO,
        "Registering RenderGraph for View '{}' (EngineVID: {}) with Renderer",
        view->intent.name, engine_vid.get());
      renderer.RegisterView(
        engine_vid,
        [view](const engine::ViewContext& vc) -> ResolvedView {
          renderer::SceneCameraViewResolver resolver(
            [view](const ViewId&) -> scene::SceneNode {
              return view->intent.camera.value_or(scene::SceneNode {});
            });
          return resolver(vc.id);
        },
        [self = impl_.get(), view](ViewId /*id*/,
          const engine::RenderContext& rc,
          graphics::CommandRecorder& rec) -> co::Co<> {
          auto& renderer = rc.GetRenderer();
          Impl::ViewRenderContext ctx { *view,
            self->EvaluateViewRenderPlan(*view, rc), nullptr, false };
          const bool run_scene_passes = ctx.plan.has_scene
            && ctx.plan.hdr_path_enabled
            && (ctx.plan.effective_render_mode != RenderMode::kWireframe);
          DCHECK_F(!ctx.plan.allow_overlay_wireframe
            || ctx.plan.wireframe_target == WireframeTarget::kSdr);

          if (ctx.plan.hdr_path_enabled) {
            // Phase: scene production into HDR (or wireframe-only).
            self->TrackViewResources(ctx, rec);
            self->ConfigurePassTargets(ctx);
            self->ConfigureSkyLutManager(ctx, renderer);
            self->BindHdrAndClear(ctx, rec);

            if (!run_scene_passes) {
              co_await self->RenderWireframeScene(ctx, rc, rec);
            } else {
              co_await self->RunScenePasses(ctx, rc, rec);
            }

            co_await self->ToneMapToSdr(ctx, rc, rec);
          } else {
            // Phase: SDR-only output for non-HDR views.
            self->BindSdrAndMaybeClear(ctx, rec);
          }

          if (ctx.plan.sdr_path_enabled) {
            // Phase: overlays and compositing preparation.
            self->EnsureSdrBoundForOverlays(ctx, rec);
            co_await self->RenderOverlayWireframe(ctx, rc, rec);
            self->RenderViewOverlay(ctx, rec);
            co_await self->RenderToolsImGui(ctx, rec);
            self->TransitionSdrToShaderRead(ctx, rec);
          }
          co_return;
        });
      view->registered_with_renderer = true;
    }
  }

  impl_->ReapResources(frame_seq, context, renderer);
  co_return;
}

auto ForwardPipeline::OnPreRender(
  observer_ptr<engine::FrameContext> /*context*/,
  engine::Renderer& /*renderer*/,
  std::span<const CompositionView> /*view_descs*/) -> co::Co<>
{
  co_return;
}

auto ForwardPipeline::OnCompositing(
  observer_ptr<engine::FrameContext> /*frame_ctx*/,
  engine::Renderer& /*renderer*/, graphics::Framebuffer* final_output)
  -> co::Co<engine::CompositionSubmission>
{
  if (final_output == nullptr) {
    co_return {};
  }
  const auto& target_desc = final_output->GetDescriptor();
  if (target_desc.color_attachments.empty()
    || !target_desc.color_attachments[0].texture) {
    co_return {};
  }

  const auto& back_desc
    = target_desc.color_attachments[0].texture->GetDescriptor();
  const ViewPort fullscreen_viewport {
    .top_left_x = 0.0F,
    .top_left_y = 0.0F,
    .width = static_cast<float>(back_desc.width),
    .height = static_cast<float>(back_desc.height),
    .min_depth = 0.0F,
    .max_depth = 1.0F,
  };

  engine::CompositingTaskList tasks;
  tasks.reserve(impl_->sorted_views.size());
  for (auto* view : impl_->sorted_views) {
    if (!view->sdr_texture) {
      continue;
    }
    const auto& viewport = view->intent.view.viewport.IsValid()
      ? view->intent.view.viewport
      : fullscreen_viewport;
    tasks.push_back(engine::CompositingTask::MakeTextureBlend(
      view->sdr_texture, viewport, view->intent.opacity));
  }

  engine::CompositionSubmission submission;
  submission.target_framebuffer = std::shared_ptr<graphics::Framebuffer>(
    final_output, [](graphics::Framebuffer*) { });
  submission.tasks = std::move(tasks);

  co_return submission;
}

auto ForwardPipeline::ClearBackbufferReferences() -> void
{
  impl_->ClearBackbufferReferences();
}

// Stubs for configuration methods
auto ForwardPipeline::SetShaderDebugMode(engine::ShaderDebugMode mode) -> void
{
  impl_->staged.shader_debug_mode = mode;
  impl_->staged.dirty = true;
}
auto ForwardPipeline::SetRenderMode(RenderMode mode) -> void
{
  impl_->staged.render_mode = mode;
  impl_->staged.dirty = true;
}
auto ForwardPipeline::SetWireframeColor(const graphics::Color& color) -> void
{
  DLOG_F(1, "SetWireframeColor ({}, {}, {}, {})", color.r, color.g, color.b,
    color.a);
  impl_->staged.wire_color = color;
  impl_->staged.dirty = true;
}
auto ForwardPipeline::SetLightCullingVisualizationMode(
  engine::ShaderDebugMode mode) -> void
{
  impl_->staged.light_culling_debug_mode = mode;
  impl_->staged.dirty = true;
}
auto ForwardPipeline::SetClusteredCullingEnabled(bool enabled) -> void
{
  impl_->staged.clustered_culling_enabled = enabled;
  impl_->staged.dirty = true;
}
auto ForwardPipeline::SetClusterDepthSlices(uint32_t slices) -> void
{
  impl_->staged.cluster_depth_slices = slices;
  impl_->staged.dirty = true;
}
auto ForwardPipeline::SetExposureMode(engine::ExposureMode mode) -> void
{
  impl_->staged.exposure_mode = mode;
  impl_->staged.dirty = true;
}
auto ForwardPipeline::SetExposureValue(float value) -> void
{
  impl_->staged.exposure_value = value;
  impl_->staged.dirty = true;
}
auto ForwardPipeline::SetToneMapper(engine::ToneMapper mode) -> void
{
  impl_->staged.tonemapping_mode = mode;
  impl_->staged.dirty = true;
}

auto ForwardPipeline::UpdateShaderPassConfig(
  const engine::ShaderPassConfig& config) -> void
{
  if (impl_->shader_pass_config) {
    *impl_->shader_pass_config = config;
  }
}
auto ForwardPipeline::UpdateTransparentPassConfig(
  const engine::TransparentPassConfig& config) -> void
{
  if (impl_->transparent_pass_config) {
    *impl_->transparent_pass_config = config;
  }
}
auto ForwardPipeline::UpdateLightCullingPassConfig(
  const engine::LightCullingPassConfig& config) -> void
{
  if (impl_->light_culling_pass_config) {
    *impl_->light_culling_pass_config = config;
  }
}

} // namespace oxygen::examples
