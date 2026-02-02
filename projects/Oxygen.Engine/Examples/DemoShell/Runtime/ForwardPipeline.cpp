//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <mutex>
#include <span>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Engine/AsyncEngine.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Graphics/Common/PipelineState.h>
#include <Oxygen/ImGui/ImGuiModule.h>
#include <Oxygen/ImGui/ImGuiPass.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/Renderer/Renderer.h>
#include <Oxygen/Renderer/SceneCameraViewResolver.h>
#include <Oxygen/Scene/Environment/SceneEnvironment.h>
#include <Oxygen/Scene/Environment/SkyAtmosphere.h>
#include <Oxygen/Scene/Environment/SkySphere.h>
#include <Oxygen/Scene/Scene.h>

#include "DemoShell/Runtime/DemoView.h"
#include "DemoShell/Runtime/ForwardPipeline.h"

using namespace oxygen;

namespace oxygen::examples {

ForwardPipeline::ForwardPipeline(observer_ptr<AsyncEngine> engine) noexcept
  : engine_(engine)
{
  DCHECK_NOTNULL_F(engine_, "Expecting a valid engine pointer");
  // Initialize Pass Configs

  // DepthPrePass config
  depth_pass_config_ = std::make_shared<engine::DepthPrePassConfig>();
  depth_pass_config_->debug_name = "DepthPrePass";

  // ShaderPass config
  shader_pass_config_ = std::make_shared<engine::ShaderPassConfig>();
  shader_pass_config_->debug_name = "ForwardPass";
  shader_pass_config_->clear_color = graphics::Color { 0.0F, 0.0F, 0.0F, 1.0F };
  shader_pass_config_->clear_color_target = true;

  // WireframePass config
  wireframe_pass_config_ = std::make_shared<engine::WireframePassConfig>();
  wireframe_pass_config_->debug_name = "WireframePass";
  wireframe_pass_config_->clear_color
    = graphics::Color { 0.0F, 0.0F, 0.0F, 1.0F };

  // SkyPass config
  sky_pass_config_ = std::make_shared<engine::SkyPassConfig>();
  sky_pass_config_->debug_name = "SkyPass";

  // TransparentPass config
  transparent_pass_config_ = std::make_shared<engine::TransparentPassConfig>();
  transparent_pass_config_->debug_name = "TransparentPass";

  // LightCullingPass config
  light_culling_pass_config_
    = std::make_shared<engine::LightCullingPassConfig>();
  light_culling_pass_config_->debug_name = "LightCullingPass";

  // SkyAtmosphere LUT compute config
  sky_atmo_lut_pass_config_
    = std::make_shared<engine::SkyAtmosphereLutComputePassConfig>();
  sky_atmo_lut_pass_config_->debug_name = "SkyAtmosphereLutComputePass";

  // Initialize Passes
  depth_pass_ = std::make_shared<engine::DepthPrePass>(depth_pass_config_);
  shader_pass_ = std::make_shared<engine::ShaderPass>(shader_pass_config_);
  wireframe_pass_
    = std::make_shared<engine::WireframePass>(wireframe_pass_config_);
  sky_pass_ = std::make_shared<engine::SkyPass>(sky_pass_config_);
  transparent_pass_
    = std::make_shared<engine::TransparentPass>(transparent_pass_config_);
  // FIXME: the graphics pointer is weak
  light_culling_pass_ = std::make_shared<engine::LightCullingPass>(
    observer_ptr { engine_->GetGraphics().lock().get() },
    light_culling_pass_config_);
  sky_atmo_lut_pass_ = std::make_shared<engine::SkyAtmosphereLutComputePass>(
    observer_ptr { engine_->GetGraphics().lock().get() },
    sky_atmo_lut_pass_config_);
}

ForwardPipeline::~ForwardPipeline() = default;

auto ForwardPipeline::GetImGuiPass() -> observer_ptr<imgui::ImGuiPass>
{
  DCHECK_NOTNULL_F(engine_);
  // ImGuiPass lazy initialization; will happen once during the first render
  std::call_once(flag_, [&] {
    auto imgui_module_ref = engine_->GetModule<imgui::ImGuiModule>();
    if (imgui_module_ref) {
      auto& imgui_module = imgui_module_ref->get();
      imgui_pass_ = imgui_module.GetRenderPass();
    }
  });
  return imgui_pass_;
}

auto ForwardPipeline::GetSupportedFeatures() const -> PipelineFeature
{
  return PipelineFeature::kOpaqueShading | PipelineFeature::kTransparentShading
    | PipelineFeature::kLightCulling;
}

auto ForwardPipeline::OnFrameStart(
  engine::FrameContext& /*context*/, engine::Renderer& /*renderer*/) -> void
{
  ApplyStagedSettings();
}

auto ForwardPipeline::SetShaderDebugMode(engine::ShaderDebugMode mode) -> void
{
  staged_.shader_debug_mode = mode;
  staged_.dirty = true;
}

auto ForwardPipeline::SetRenderMode(RenderMode mode) -> void
{
  staged_.render_mode = mode;
  staged_.dirty = true;
}

auto ForwardPipeline::SetWireframeColor(const graphics::Color& color) -> void
{
  staged_.wire_color = color;
  staged_.dirty = true;
}

auto ForwardPipeline::SetLightCullingVisualizationMode(
  engine::ShaderDebugMode mode) -> void
{
  staged_.light_culling_debug_mode = mode;
  staged_.dirty = true;
}

auto ForwardPipeline::SetClusteredCullingEnabled(bool enabled) -> void
{
  staged_.clustered_culling_enabled = enabled;
  staged_.dirty = true;
}

auto ForwardPipeline::SetClusterDepthSlices(uint32_t slices) -> void
{
  staged_.cluster_depth_slices = slices;
  staged_.dirty = true;
}

auto ForwardPipeline::SetExposureMode(engine::ExposureMode mode) -> void
{
  staged_.exposure_mode = mode;
  staged_.dirty = true;
}

auto ForwardPipeline::SetExposureValue(float value) -> void
{
  staged_.exposure_value = value;
  staged_.dirty = true;
}

auto ForwardPipeline::SetToneMapper(engine::ToneMapper mode) -> void
{
  staged_.tonemapping_mode = mode;
  staged_.dirty = true;
}

auto ForwardPipeline::ApplyStagedSettings() -> void
{
  if (!staged_.dirty) {
    return;
  }

  // Commit Shading Settings
  if (shader_pass_config_) {
    shader_pass_config_->fill_mode = graphics::FillMode::kSolid;

    // Resolve Debug Mode: Priority to Light Culling Visualization if active
    if (staged_.light_culling_debug_mode
      != engine::ShaderDebugMode::kDisabled) {
      shader_pass_config_->debug_mode = staged_.light_culling_debug_mode;
    } else {
      shader_pass_config_->debug_mode = staged_.shader_debug_mode;
    }
  }

  // Commit Light Culling Settings
  if (light_culling_pass_config_) {
    auto& cluster = light_culling_pass_config_->cluster;
    if (staged_.clustered_culling_enabled) {
      cluster.depth_slices = staged_.cluster_depth_slices;
    } else {
      cluster.depth_slices = 1; // Tile-based
    }
  }

  if (wireframe_pass_config_) {
    wireframe_pass_config_->wire_color = staged_.wire_color;
  }

  render_mode_ = staged_.render_mode;
  wire_color_ = staged_.wire_color;

  // NOTE: Post-process settings (Exposure, Tonemapping) are typically applied
  // to the Scene's environment data or a global post-process pass.
  // In this ForwardPipeline, we might need a separate PostProcessPass
  // or apply them to the shading pass if it handles them.
  // For now, we stage them; actual integration depends on where
  // the engine reads these values (usually EnvironmentStaticData).

  staged_.dirty = false;
}

auto ForwardPipeline::UpdateShaderPassConfig(
  const engine::ShaderPassConfig& config) -> void
{
  if (shader_pass_config_) {
    *shader_pass_config_ = config;
  }
}

auto ForwardPipeline::UpdateTransparentPassConfig(
  const engine::TransparentPassConfig& config) -> void
{
  if (transparent_pass_config_) {
    *transparent_pass_config_ = config;
  }
}

auto ForwardPipeline::UpdateLightCullingPassConfig(
  const engine::LightCullingPassConfig& config) -> void
{
  if (light_culling_pass_config_) {
    *light_culling_pass_config_ = config;
  }
}

auto ForwardPipeline::OnSceneMutation(
  [[maybe_unused]] engine::FrameContext& context,
  [[maybe_unused]] engine::Renderer& renderer,
  [[maybe_unused]] scene::Scene& scene, std::span<DemoView*> views,
  [[maybe_unused]] graphics::Framebuffer* target_framebuffer) -> co::Co<>
{

  for (auto* view : views) {
    if (view) {
      co_await RenderView(context, renderer, *view);
    }
  }
}

auto ForwardPipeline::RenderView(engine::FrameContext& /*fc*/,
  engine::Renderer& renderer, DemoView& view) -> co::Co<>
{
  const auto camera_opt = view.GetCamera();
  if (!camera_opt) {
    co_return; // No camera, skip
  }
  const auto& camera = *camera_opt;

  // Use the pre-registered ViewId from the DemoView
  const ViewId vid = view.GetViewId();
  if (vid == kInvalidViewId) {
    LOG_F(WARNING, "RenderView: DemoView has invalid ViewId; skipping");
    co_return;
  }

  // Only register render graph with Renderer once per view lifetime
  if (view.IsRendererRegistered()) {
    co_return; // Already registered, nothing more to do this frame
  }

  // Register with Renderer (once per view lifetime)
  renderer.RegisterView(
    vid,
    [=](const engine::ViewContext& vc) -> ResolvedView {
      renderer::SceneCameraViewResolver resolver(
        [camera](const ViewId&) { return camera; });
      return resolver(vc.id);
    },
    // Capture render passes and configs for the lambda
    [depth_pass = depth_pass_, depth_config = depth_pass_config_,
      light_pass = light_culling_pass_, opaque_pass = shader_pass_,
      opaque_config = shader_pass_config_, wire_pass = wireframe_pass_,
      wire_config = wireframe_pass_config_, sky_pass = sky_pass_,
      sky_config = sky_pass_config_, trans_pass = transparent_pass_,
      trans_config = transparent_pass_config_,
      sky_atmo_pass = sky_atmo_lut_pass_,
      sky_atmo_config = sky_atmo_lut_pass_config_, imgui_pass = GetImGuiPass(),
      self = this, &renderer](ViewId /*id*/, const engine::RenderContext& rc,
      graphics::CommandRecorder& rec) -> co::Co<> {
      // Get framebuffer to extract render target textures
      const auto fb = rc.framebuffer;
      if (!fb) {
        LOG_F(WARNING, "RenderView: No framebuffer in RenderContext; skipping");
        co_return;
      }

      const auto& fb_desc = fb->GetDescriptor();

      // Extract color and depth textures from framebuffer
      std::shared_ptr<const graphics::Texture> color_tex;
      std::shared_ptr<const graphics::Texture> depth_tex;

      if (!fb_desc.color_attachments.empty()) {
        color_tex = fb_desc.color_attachments[0].texture;
      }
      if (fb_desc.depth_attachment.IsValid()) {
        depth_tex = fb_desc.depth_attachment.texture;
      }

      // Configure pass render targets
      if (depth_config && depth_tex) {
        depth_config->depth_texture = depth_tex;
      }
      if (opaque_config && color_tex) {
        opaque_config->color_texture = color_tex;
      }
      if (wire_config && color_tex) {
        wire_config->color_texture = color_tex;
      }
      if (sky_config && color_tex) {
        sky_config->color_texture = color_tex;
      }
      if (trans_config) {
        if (color_tex)
          trans_config->color_texture = color_tex;
        if (depth_tex)
          trans_config->depth_texture = depth_tex;
      }

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

      const bool should_run_sky = sky_atmo_enabled || sky_sphere_enabled;
      const bool should_run_lut = sky_atmo_enabled;

      if (sky_atmo_config && should_run_lut) {
        sky_atmo_config->lut_manager = renderer.GetSkyAtmosphereLutManager();
      } else if (sky_atmo_config) {
        sky_atmo_config->lut_manager = nullptr;
      }

      // Execute passes in order: Depth -> LightCulling -> Shader -> Transparent

      const auto render_mode = self->render_mode_;
      const auto wire_color = self->wire_color_;
      if (render_mode == RenderMode::kWireframe) {
        if (depth_pass && depth_tex) {
          co_await depth_pass->PrepareResources(rc, rec);
          co_await depth_pass->Execute(rc, rec);
          rc.RegisterPass<engine::DepthPrePass>(depth_pass.get());
        }
        if (wire_config) {
          wire_config->clear_color_target = true;
          wire_config->depth_write_enable = true;
          wire_config->wire_color = wire_color;
        }
        if (wire_pass && color_tex) {
          co_await wire_pass->PrepareResources(rc, rec);
          co_await wire_pass->Execute(rc, rec);
        }
      } else {
        // 1. DepthPrePass (required by LightCullingPass)
        if (depth_pass && depth_tex) {
          co_await depth_pass->PrepareResources(rc, rec);
          co_await depth_pass->Execute(rc, rec);
          // Register so LightCullingPass can find it
          rc.RegisterPass<engine::DepthPrePass>(depth_pass.get());
        }

        // 2. SkyAtmosphere LUT compute pass (before any sky rendering)
        if (should_run_lut && sky_atmo_pass && sky_atmo_config
          && sky_atmo_config->lut_manager) {
          co_await sky_atmo_pass->PrepareResources(rc, rec);
          co_await sky_atmo_pass->Execute(rc, rec);
        }

        // 3. LightCullingPass (after depth, before shading)
        if (light_pass) {
          co_await light_pass->PrepareResources(rc, rec);
          co_await light_pass->Execute(rc, rec);
          rc.RegisterPass<engine::LightCullingPass>(light_pass.get());
        }

        // 4. ShaderPass (opaque geometry)
        if (opaque_pass) {
          co_await opaque_pass->PrepareResources(rc, rec);
          co_await opaque_pass->Execute(rc, rec);
        }

        // 5. SkyPass (after opaque, before transparent)
        if (should_run_sky && sky_pass && color_tex) {
          co_await sky_pass->PrepareResources(rc, rec);
          co_await sky_pass->Execute(rc, rec);
        }

        // 6. TransparentPass
        if (trans_pass && color_tex && depth_tex) {
          co_await trans_pass->PrepareResources(rc, rec);
          co_await trans_pass->Execute(rc, rec);
        }

        if (render_mode == RenderMode::kOverlayWireframe) {
          if (wire_config) {
            wire_config->clear_color_target = false;
            wire_config->depth_write_enable = false;
            wire_config->wire_color = wire_color;
          }
          if (wire_pass && color_tex) {
            co_await wire_pass->PrepareResources(rc, rec);
            co_await wire_pass->Execute(rc, rec);
          }
        }
      }

      // 7. Global ImGui
      if (imgui_pass) {
        // Bind framebuffer for ImGui - ImGui D3D12 backend expects an active
        // render target when RenderDrawData is called
        rec.BindFrameBuffer(*fb);
        co_await imgui_pass->Render(rec);
      } else {
        DLOG_F(WARNING, "No ImGui module available for overlays");
      }
    });

  view.SetRendererRegistered(true);
}

auto ForwardPipeline::OnPreRender(engine::FrameContext& /*frame_ctx*/,
  engine::Renderer& /*renderer*/, std::span<DemoView*> /*views*/) -> co::Co<>
{
  co_return;
}

auto ForwardPipeline::OnCompositing(engine::FrameContext& /*frame_ctx*/,
  engine::Renderer& /*renderer*/, graphics::Framebuffer* /*final_output*/)
  -> co::Co<>
{
  co_return;
}

auto ForwardPipeline::ClearBackbufferReferences() -> void
{
  if (depth_pass_config_) {
    depth_pass_config_->depth_texture.reset();
  }
  if (shader_pass_config_) {
    shader_pass_config_->color_texture.reset();
  }
  if (wireframe_pass_config_) {
    wireframe_pass_config_->color_texture.reset();
  }
  if (sky_pass_config_) {
    sky_pass_config_->color_texture.reset();
  }
  if (transparent_pass_config_) {
    transparent_pass_config_->color_texture.reset();
    transparent_pass_config_->depth_texture.reset();
  }
}

} // namespace oxygen::examples
