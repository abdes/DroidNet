//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
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
      if (needs_hdr && hdr_texture)
        return;
      if (!needs_hdr && sdr_texture)
        return;
    }

    LOG_F(INFO,
      "ForwardPipeline: Configuring View '{}' (ID: {}) -> {}x{}, HDR: {}, "
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
  struct RenderPolicy {
    RenderMode effective_render_mode { RenderMode::kSolid };
    bool overlay_wireframe { false };
    bool run_scene_passes { true };
    bool force_neutral_tonemap { false };
    bool wireframe_after_tonemap { false };
    bool wireframe_apply_exposure_compensation { false };
  };

  auto GetWireframeTargetTexture(const RenderPolicy& policy,
    const CompositionViewImpl& view) -> std::shared_ptr<const graphics::Texture>
  {
    if (policy.wireframe_after_tonemap) {
      DCHECK_NOTNULL_F(view.sdr_texture.get());
    }
    if (policy.wireframe_after_tonemap || !view.hdr_texture) {
      return view.sdr_texture;
    }
    return view.hdr_texture;
  }
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

  auto BuildRenderPolicy(const CompositionViewImpl& view) const -> RenderPolicy
  {
    RenderPolicy policy {};
    policy.effective_render_mode = staged.render_mode;
    if (view.intent.force_wireframe) {
      policy.effective_render_mode = RenderMode::kWireframe;
    }

    policy.overlay_wireframe
      = (staged.render_mode == RenderMode::kOverlayWireframe)
      && (policy.effective_render_mode != RenderMode::kWireframe);
    policy.run_scene_passes
      = policy.effective_render_mode != RenderMode::kWireframe;
    policy.force_neutral_tonemap
      = policy.effective_render_mode == RenderMode::kWireframe;
    policy.wireframe_after_tonemap = policy.overlay_wireframe;
    policy.wireframe_apply_exposure_compensation = false;

    LOG_F(INFO,
      "ForwardPipeline: RenderPolicy view='{}' mode={} overlay={} "
      "scene_passes={} neutral_tonemap={} wireframe_after_tonemap={}",
      view.intent.name, to_string(policy.effective_render_mode),
      policy.overlay_wireframe, policy.run_scene_passes,
      policy.force_neutral_tonemap, policy.wireframe_after_tonemap);

    return policy;
  }

  auto ConfigureWireframePass(const RenderPolicy& policy,
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
      = policy.wireframe_apply_exposure_compensation;
    wireframe_pass_config->color_texture
      = GetWireframeTargetTexture(policy, view);

    if (wireframe_pass) {
      wireframe_pass->SetWireColor(staged.wire_color);
    } else {
      wireframe_pass_config->wire_color = staged.wire_color;
    }
  }

  struct ToneMapOverrides {
    engine::ExposureMode exposure_mode { engine::ExposureMode::kManual };
    float manual_exposure { 1.0F };
    engine::ToneMapper tone_mapper { engine::ToneMapper::kAcesFitted };
  };

  auto ApplyToneMapPolicy(
    const RenderPolicy& policy, ToneMapOverrides& saved) const -> void
  {
    if (!tone_map_pass_config) {
      return;
    }

    saved.exposure_mode = tone_map_pass_config->exposure_mode;
    saved.manual_exposure = tone_map_pass_config->manual_exposure;
    saved.tone_mapper = tone_map_pass_config->tone_mapper;

    if (policy.force_neutral_tonemap) {
      tone_map_pass_config->exposure_mode = engine::ExposureMode::kManual;
      tone_map_pass_config->manual_exposure = 1.0F;
      tone_map_pass_config->tone_mapper = engine::ToneMapper::kNone;
    }
  }

  auto RestoreToneMapPolicy(const ToneMapOverrides& saved) const -> void
  {
    if (!tone_map_pass_config) {
      return;
    }

    tone_map_pass_config->exposure_mode = saved.exposure_mode;
    tone_map_pass_config->manual_exposure = saved.manual_exposure;
    tone_map_pass_config->tone_mapper = saved.tone_mapper;
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

    LOG_F(INFO, "ForwardPipeline: ApplySettings wire_color=({}, {}, {}, {})",
      staged.wire_color.r, staged.wire_color.g, staged.wire_color.b,
      staged.wire_color.a);

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
      const auto IsNonIblDebug = [](engine::ShaderDebugMode mode) -> bool {
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
      };

      if (IsNonIblDebug(debug_mode)) {
        tone_map_pass_config->exposure_mode = engine::ExposureMode::kManual;
        tone_map_pass_config->manual_exposure = 1.0F;
      } else {
        tone_map_pass_config->exposure_mode = staged.exposure_mode;
        tone_map_pass_config->manual_exposure = staged.exposure_value;
      }
      tone_map_pass_config->tone_mapper = staged.tonemapping_mode;
    }

    staged.dirty = false;
  }

  void ClearBackbufferReferences()
  {
    if (depth_pass_config)
      depth_pass_config->depth_texture.reset();
    if (shader_pass_config)
      shader_pass_config->color_texture.reset();
    if (wireframe_pass_config)
      wireframe_pass_config->color_texture.reset();
    if (sky_pass_config)
      sky_pass_config->color_texture.reset();
    if (transparent_pass_config) {
      transparent_pass_config->color_texture.reset();
      transparent_pass_config->depth_texture.reset();
    }
    if (tone_map_pass_config) {
      tone_map_pass_config->source_texture.reset();
      tone_map_pass_config->output_texture.reset();
    }
  }

  auto GetImGuiPass() -> observer_ptr<imgui::ImGuiPass>
  {
    std::call_once(imgui_flag, [&] {
      if (auto mod = engine->GetModule<imgui::ImGuiModule>()) {
        imgui_pass = mod->get().GetRenderPass();
      }
    });
    return imgui_pass;
  }

  void ReapResources(frame::SequenceNumber current_frame,
    engine::FrameContext& context, engine::Renderer& renderer)
  {
    static constexpr frame::SequenceNumber kMaxIdleFrames { 60 };
    for (auto it = view_pool.begin(); it != view_pool.end();) {
      if (current_frame - it->second.last_seen_frame > kMaxIdleFrames) {
        LOG_F(
          INFO, "ForwardPipeline: Reaping View resources for ID {}", it->first);

        if (it->second.engine_vid != kInvalidViewId) {
          LOG_F(INFO,
            "ForwardPipeline: Unregistering View '{}' (EngineVID: {}) from "
            "Engine and Renderer",
            it->second.intent.name, it->second.engine_vid.get());
          context.RemoveView(it->second.engine_vid);
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
  engine::FrameContext& /*context*/, engine::Renderer& /*renderer*/) -> void
{
  impl_->ApplySettings();
}

auto ForwardPipeline::OnSceneMutation(engine::FrameContext& context,
  engine::Renderer& renderer, scene::Scene& /*scene*/,
  std::span<const CompositionView> view_descs,
  graphics::Framebuffer* target_framebuffer) -> co::Co<>
{
  impl_->sorted_views.clear();
  impl_->sorted_views.reserve(view_descs.size());

  auto graphics = impl_->engine->GetGraphics().lock();
  const auto frame_seq = context.GetFrameSequenceNumber();

  uint32_t index = 0;
  for (auto desc : view_descs) { // Copy so we can modify viewport
    // Resolution: if viewport is empty, try to derive from target_framebuffer
    // or default to 1280x720
    if (desc.view.viewport.width <= 0 || desc.view.viewport.height <= 0) {
      if (target_framebuffer) {
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
    const auto vid = view->intent.id;
    const auto camera = view->intent.camera;

    // Register View Metadata with FrameContext
    engine::ViewContext view_ctx;
    view_ctx.view = view->intent.view;
    view_ctx.metadata
      = { .name = std::string(view->intent.name), .purpose = "composed_layer" };
    if (view->has_hdr && view->hdr_framebuffer) {
      view_ctx.output = observer_ptr { view->hdr_framebuffer.get() };
    } else {
      view_ctx.output = observer_ptr { view->sdr_framebuffer.get() };
    }

    // Maintain stable link to engine's internal view registry
    if (view->engine_vid == kInvalidViewId) {
      view->engine_vid = context.RegisterView(std::move(view_ctx));
      LOG_F(INFO,
        "ForwardPipeline: Registered View '{}' (IntentID: {}) with Engine "
        "(EngineVID: {})",
        view->intent.name, view->intent.id.get(), view->engine_vid.get());
    } else {
      context.UpdateView(view->engine_vid, std::move(view_ctx));
      DLOG_F(1, "ForwardPipeline: Updated View '{}' (EngineVID: {})",
        view->intent.name, view->engine_vid.get());
    }

    const auto engine_vid = view->engine_vid;

    if (!view->registered_with_renderer) {
      LOG_F(INFO,
        "ForwardPipeline: Registering RenderGraph for View '{}' (EngineVID: "
        "{}) with Renderer",
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
          bool sdr_in_render_target = false;
          std::shared_ptr<const graphics::Texture> depth_tex;
          const auto policy = self->BuildRenderPolicy(*view);
          DCHECK_F(!policy.overlay_wireframe || policy.wireframe_after_tonemap);

          if (view->has_hdr && view->hdr_texture && view->hdr_framebuffer) {
            const auto fb = view->hdr_framebuffer;
            const auto& fb_desc = fb->GetDescriptor();

            std::shared_ptr<const graphics::Texture> color_tex
              = view->hdr_texture;
            if (fb_desc.depth_attachment.IsValid()) {
              depth_tex = fb_desc.depth_attachment.texture;
            }

            if (view->hdr_texture
              && !rec.IsResourceTracked(*view->hdr_texture)) {
              rec.BeginTrackingResourceState(
                *view->hdr_texture, graphics::ResourceStates::kCommon, true);
            }
            if (depth_tex && !rec.IsResourceTracked(*depth_tex)) {
              rec.BeginTrackingResourceState(
                *depth_tex, graphics::ResourceStates::kCommon, true);
            }
            if (view->sdr_texture
              && !rec.IsResourceTracked(*view->sdr_texture)) {
              rec.BeginTrackingResourceState(
                *view->sdr_texture, graphics::ResourceStates::kCommon, true);
            }

            // Configure pass render targets
            if (self->depth_pass_config)
              self->depth_pass_config->depth_texture = depth_tex;
            if (self->shader_pass_config)
              self->shader_pass_config->color_texture = color_tex;
            if (self->wireframe_pass_config)
              self->wireframe_pass_config->color_texture = color_tex;
            if (self->sky_pass_config)
              self->sky_pass_config->color_texture = color_tex;
            if (self->transparent_pass_config) {
              self->transparent_pass_config->color_texture = color_tex;
              self->transparent_pass_config->depth_texture = depth_tex;
            }

            // Determine Sky requirements
            const auto scene = rc.GetScene();
            bool sky_atmo_enabled = false;
            bool sky_sphere_enabled = false;
            if (scene) {
              if (const auto env = scene->GetEnvironment()) {
                if (const auto atmo
                  = env->TryGetSystem<scene::environment::SkyAtmosphere>();
                  atmo && atmo->IsEnabled()) {
                  sky_atmo_enabled = true;
                }
                if (const auto sphere
                  = env->TryGetSystem<scene::environment::SkySphere>();
                  sphere && sphere->IsEnabled()) {
                  sky_sphere_enabled = true;
                }
              }
            }

            const auto debug_mode = self->shader_pass_config
              ? self->shader_pass_config->debug_mode
              : engine::ShaderDebugMode::kDisabled;
            const auto IsNonIblDebug
              = [](engine::ShaderDebugMode mode) -> bool {
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
            };

            const bool should_run_sky = (sky_atmo_enabled || sky_sphere_enabled)
              && !IsNonIblDebug(debug_mode);
            const bool should_run_lut = sky_atmo_enabled;

            if (self->sky_atmo_lut_pass_config) {
              self->sky_atmo_lut_pass_config->lut_manager = should_run_lut
                ? renderer.GetSkyAtmosphereLutManager()
                : nullptr;
            }

            // Transitions
            rec.RequireResourceState(
              *view->hdr_texture, graphics::ResourceStates::kRenderTarget);
            if (depth_tex) {
              rec.RequireResourceState(
                *depth_tex, graphics::ResourceStates::kDepthWrite);
            }
            rec.FlushBarriers();

            rec.BindFrameBuffer(*view->hdr_framebuffer);
            const auto hdr_clear = view->hdr_framebuffer->GetDescriptor()
                                     .color_attachments[0]
                                     .ResolveClearColor(std::nullopt);
            rec.ClearFramebuffer(*view->hdr_framebuffer,
              std::vector<std::optional<graphics::Color>> { hdr_clear }, 1.0F);
            // Pass clears are handled by the passes themselves based on config.
            // However, if no pass clears it, we might need a manual clear.
            // ShaderPass by default has clear_color_target=true in Impl init.

            if (!policy.run_scene_passes) {
              if (self->wireframe_pass_config) {
                // For pure wireframe, we DO clear the background to the intent
                // clear color and we DO NOT run any material or sky passes.
                const bool is_forced = view->intent.force_wireframe;
                self->ConfigureWireframePass(
                  policy, *view, !is_forced, true, true);
              }
              if (self->wireframe_pass) {
                co_await self->wireframe_pass->PrepareResources(rc, rec);
                co_await self->wireframe_pass->Execute(rc, rec);
              }
            } else {
              // 1. DepthPrePass
              if (self->depth_pass && depth_tex) {
                co_await self->depth_pass->PrepareResources(rc, rec);
                co_await self->depth_pass->Execute(rc, rec);
                rc.RegisterPass<engine::DepthPrePass>(self->depth_pass.get());
              }

              // 2. SkyAtmosphere LUT
              if (should_run_lut && self->sky_atmo_lut_pass
                && self->sky_atmo_lut_pass_config->lut_manager) {
                co_await self->sky_atmo_lut_pass->PrepareResources(rc, rec);
                co_await self->sky_atmo_lut_pass->Execute(rc, rec);
              }

              // 3. LightCullingPass
              if (self->light_culling_pass) {
                co_await self->light_culling_pass->PrepareResources(rc, rec);
                co_await self->light_culling_pass->Execute(rc, rec);
                rc.RegisterPass<engine::LightCullingPass>(
                  self->light_culling_pass.get());
              }

              // 4. ShaderPass (opaque)
              if (self->shader_pass) {
                co_await self->shader_pass->PrepareResources(rc, rec);
                co_await self->shader_pass->Execute(rc, rec);
                rc.RegisterPass<engine::ShaderPass>(self->shader_pass.get());
              }

              // 5. SkyPass
              if (should_run_sky && self->sky_pass) {
                co_await self->sky_pass->PrepareResources(rc, rec);
                co_await self->sky_pass->Execute(rc, rec);
              }

              // 6. TransparentPass
              if (self->transparent_pass) {
                co_await self->transparent_pass->PrepareResources(rc, rec);
                co_await self->transparent_pass->Execute(rc, rec);
                rc.RegisterPass<engine::TransparentPass>(
                  self->transparent_pass.get());
              }
            }

            // Tonemap to SDR
            if (self->tone_map_pass && view->sdr_texture) {
              self->tone_map_pass_config->source_texture = view->hdr_texture;
              self->tone_map_pass_config->output_texture = view->sdr_texture;

              Impl::ToneMapOverrides tone_map_overrides {};
              self->ApplyToneMapPolicy(policy, tone_map_overrides);

              rec.RequireResourceState(
                *view->hdr_texture, graphics::ResourceStates::kShaderResource);
              rec.RequireResourceState(
                *view->sdr_texture, graphics::ResourceStates::kRenderTarget);
              rec.FlushBarriers();
              sdr_in_render_target = true;

              co_await self->tone_map_pass->PrepareResources(rc, rec);
              co_await self->tone_map_pass->Execute(rc, rec);

              if (policy.force_neutral_tonemap) {
                self->RestoreToneMapPolicy(tone_map_overrides);
              }
            }
          } else if (view->sdr_texture && view->sdr_framebuffer) {
            rec.RequireResourceState(
              *view->sdr_texture, graphics::ResourceStates::kRenderTarget);
            rec.FlushBarriers();
            sdr_in_render_target = true;
            rec.BindFrameBuffer(*view->sdr_framebuffer);
            if (view->intent.should_clear) {
              const auto sdr_clear = view->sdr_framebuffer->GetDescriptor()
                                       .color_attachments[0]
                                       .ResolveClearColor(std::nullopt);
              rec.ClearFramebuffer(*view->sdr_framebuffer,
                std::vector<std::optional<graphics::Color>> { sdr_clear });
            }
          }

          // SDR Overlays (Applies to both HDR and SDR main paths)
          if (view->sdr_texture && view->sdr_framebuffer) {
            if (!sdr_in_render_target) {
              rec.RequireResourceState(
                *view->sdr_texture, graphics::ResourceStates::kRenderTarget);
              rec.FlushBarriers();
              sdr_in_render_target = true;
            }
            if (policy.overlay_wireframe && self->wireframe_pass_config
              && self->wireframe_pass != nullptr) {
              self->ConfigureWireframePass(policy, *view, false, false, false);
              co_await self->wireframe_pass->PrepareResources(rc, rec);
              co_await self->wireframe_pass->Execute(rc, rec);
            }

            rec.BindFrameBuffer(*view->sdr_framebuffer);
            if (view->intent.on_overlay) {
              view->intent.on_overlay(rec);
            }

            // If this is the tools view, also render Global ImGui
            if (view->intent.z_order == CompositionView::kZOrderTools) {
              if (auto imgui = self->GetImGuiPass()) {
                co_await imgui->Render(rec);
              }
            }

            rec.RequireResourceState(
              *view->sdr_texture, graphics::ResourceStates::kShaderResource);
            rec.FlushBarriers();
            sdr_in_render_target = false;
          }
          co_return;
        });
      view->registered_with_renderer = true;
    }
  }

  impl_->ReapResources(frame_seq, context, renderer);
  co_return;
}

auto ForwardPipeline::OnPreRender(engine::FrameContext& /*context*/,
  engine::Renderer& /*renderer*/,
  std::span<const CompositionView> /*view_descs*/) -> co::Co<>
{
  co_return;
}

auto ForwardPipeline::OnCompositing(engine::FrameContext& /*frame_ctx*/,
  engine::Renderer& /*renderer*/, graphics::Framebuffer* final_output)
  -> co::Co<engine::CompositionSubmission>
{
  if (!final_output)
    co_return {};
  const auto& target_desc = final_output->GetDescriptor();
  if (target_desc.color_attachments.empty()
    || !target_desc.color_attachments[0].texture)
    co_return {};

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
  LOG_F(INFO, "ForwardPipeline: SetWireframeColor ({}, {}, {}, {})", color.r,
    color.g, color.b, color.a);
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
  if (impl_->shader_pass_config)
    *impl_->shader_pass_config = config;
}
auto ForwardPipeline::UpdateTransparentPassConfig(
  const engine::TransparentPassConfig& config) -> void
{
  if (impl_->transparent_pass_config)
    *impl_->transparent_pass_config = config;
}
auto ForwardPipeline::UpdateLightCullingPassConfig(
  const engine::LightCullingPassConfig& config) -> void
{
  if (impl_->light_culling_pass_config)
    *impl_->light_culling_pass_config = config;
}

} // namespace oxygen::examples
