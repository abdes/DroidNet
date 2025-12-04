//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "MainView.h"

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/Surface.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Renderer/Renderer.h>
#include <Oxygen/Scene/Camera/Perspective.h>

#include "OffscreenCompositor.h"

namespace oxygen::examples::multiview {

MainView::MainView()
  : DemoView(ViewConfig {
      .name = "MainView",
      .purpose = "Main_Solid",
      .clear_color = graphics::Color { 0.1f, 0.2f, 0.38f, 1.0f },
      .wireframe = false,
    })
{
}

void MainView::Initialize(scene::Scene& scene)
{
  EnsureCamera(scene, "MainCamera");

  // Set initial transform
  camera_node_.GetTransform().SetLocalPosition({ 0.0f, 0.0f, 5.0f });

  // Log setup
  if (const auto pos = camera_node_.GetTransform().GetLocalPosition()) {
    LOG_F(INFO, "[MainView] Camera positioned at ({}, {}, {})", pos->x, pos->y,
      pos->z);
  }
}

void MainView::OnSceneMutation()
{
  const auto width = static_cast<float>(GetSurface().Width());
  const auto height = static_cast<float>(GetSurface().Height());

  // Update camera
  const auto cam_ref = camera_node_.GetCameraAs<scene::PerspectiveCamera>();
  if (cam_ref) {
    auto& cam = cam_ref->get();
    const float aspect = height > 0 ? (width / height) : 1.0f;
    cam.SetFieldOfView(glm::radians(45.0f));
    cam.SetAspectRatio(aspect);
    cam.SetNearPlane(0.1f);
    cam.SetFarPlane(100.0f);
    cam.SetViewport(ViewPort { .top_left_x = 0.0f,
      .top_left_y = 0.0f,
      .width = width,
      .height = height,
      .min_depth = 0.0f,
      .max_depth = 1.0f });
  }

  // Ensure resources - views need to provide their own recorder when needed
  EnsureMainRenderTargets();

  // Mark view as ready for rendering
  view_ready_ = true;

  // Register view
  ViewPort viewport { .top_left_x = 0.0f,
    .top_left_y = 0.0f,
    .width = width,
    .height = height,
    .min_depth = 0.0f,
    .max_depth = 1.0f };

  Scissors scissor { .left = 0,
    .top = 0,
    .right = static_cast<int32_t>(width),
    .bottom = static_cast<int32_t>(height) };

  RegisterView(viewport, scissor);
}

auto MainView::OnPreRender(oxygen::engine::Renderer& renderer) -> co::Co<void>
{
  (void)renderer;

  // Configure the renderer if not already configured
  LOG_F(INFO,
    "[MainView] OnPreRender: color_tex={}, depth_tex={}, "
    "renderer_configured={}",
    static_cast<bool>(color_texture_), static_cast<bool>(depth_texture_),
    renderer_.IsConfigured());

  if (color_texture_ && depth_texture_ && !renderer_.IsConfigured()) {
    LOG_F(INFO,
      "[MainView] Configuring renderer with clear_color=({},{},{},{})",
      config_.clear_color.r, config_.clear_color.g, config_.clear_color.b,
      config_.clear_color.a);
    ViewRenderer::Config config {
      .color_texture = color_texture_,
      .depth_texture = depth_texture_,
      .clear_color = config_.clear_color,
      .wireframe = config_.wireframe,
    };
    renderer_.Configure(config);
    LOG_F(INFO, "[MainView] Renderer configured successfully");
  }

  co_return;
}

auto MainView::RenderToFramebuffer(const engine::RenderContext& render_ctx,
  graphics::CommandRecorder& recorder, const graphics::Framebuffer& framebuffer)
  -> co::Co<void>
{
  if (renderer_.IsConfigured() && framebuffer_) {
    co_await renderer_.Render(render_ctx, recorder, *framebuffer_);
  }
  co_return;
}

void MainView::Composite(
  graphics::CommandRecorder& recorder, graphics::Texture& backbuffer)
{
  if (view_ready_ && color_texture_) {
    OffscreenCompositor compositor;
    compositor.CompositeFullscreen(recorder, *color_texture_, backbuffer);
  }
}

void MainView::ReleaseResources() { DemoView::ReleaseResources(); }

void MainView::EnsureMainRenderTargets()
{
  auto& gfx = GetGraphics();
  auto& surface = GetSurface();
  auto& recorder = GetRecorder();

  const auto width = surface.Width();
  const auto height = surface.Height();

  // Check if we need to recreate resources
  bool recreate = !framebuffer_ || !color_texture_ || !depth_texture_;
  if (!recreate && color_texture_) {
    const auto& desc = color_texture_->GetDescriptor();
    if (desc.width != width || desc.height != height) {
      recreate = true;
    }
  }

  if (!recreate) {
    return;
  }

  LOG_F(INFO, "[MainView] Creating render targets ({}x{})", width, height);

  // Release old GPU resources ONLY
  renderer_.ResetConfiguration();
  view_ready_ = false;
  color_texture_ = nullptr;
  depth_texture_ = nullptr;
  framebuffer_ = nullptr;

  // Create color texture
  graphics::TextureDesc color_desc;
  color_desc.width = width;
  color_desc.height = height;
  color_desc.format = oxygen::Format::kRGBA8UNorm;
  color_desc.is_render_target = true;
  color_desc.is_shader_resource = true;
  color_desc.debug_name = "MainView_Color";
  color_desc.texture_type = oxygen::TextureType::kTexture2D;
  color_desc.mip_levels = 1;
  color_desc.array_size = 1;
  color_desc.sample_count = 1;
  color_desc.depth = 1;
  color_desc.use_clear_value = true;
  color_desc.clear_value = config_.clear_color;
  color_texture_ = gfx.CreateTexture(color_desc);

  // Create depth texture
  graphics::TextureDesc depth_desc;
  depth_desc.width = width;
  depth_desc.height = height;
  depth_desc.format = oxygen::Format::kDepth32;
  depth_desc.is_render_target = true;
  depth_desc.texture_type = oxygen::TextureType::kTexture2D;
  depth_desc.mip_levels = 1;
  depth_desc.array_size = 1;
  depth_desc.sample_count = 1;
  depth_desc.depth = 1;
  depth_desc.is_shader_resource = false;
  depth_desc.use_clear_value = true;
  depth_desc.clear_value = graphics::Color { 1.0f, 0.0f, 0.0f, 0.0f };
  depth_desc.debug_name = "MainView_Depth";
  depth_texture_ = gfx.CreateTexture(depth_desc);

  // Create framebuffer
  graphics::FramebufferDesc fb_desc;
  fb_desc.AddColorAttachment({ .texture = color_texture_,
    .sub_resources = graphics::TextureSubResourceSet::EntireTexture(),
    .format = color_texture_->GetDescriptor().format });
  fb_desc.depth_attachment.texture = depth_texture_;
  fb_desc.depth_attachment.sub_resources = graphics::TextureSubResourceSet {};
  framebuffer_ = gfx.CreateFramebuffer(fb_desc);

  // Transition textures to appropriate initial states using the stored
  // recorder CRITICAL: Begin tracking resources before requiring their states
  recorder.BeginTrackingResourceState(
    static_cast<const graphics::Texture&>(*color_texture_),
    graphics::ResourceStates::kUndefined);
  recorder.BeginTrackingResourceState(
    static_cast<const graphics::Texture&>(*depth_texture_),
    graphics::ResourceStates::kUndefined);

  // Color: RenderTarget, Depth: DepthWrite
  recorder.RequireResourceState(
    static_cast<const graphics::Texture&>(*color_texture_),
    graphics::ResourceStates::kRenderTarget);
  recorder.RequireResourceState(
    static_cast<const graphics::Texture&>(*depth_texture_),
    graphics::ResourceStates::kDepthWrite);
}

} // namespace oxygen::examples::multiview
