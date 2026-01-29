//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/Surface.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/Color.h>
#include <Oxygen/Renderer/Renderer.h>
#include <Oxygen/Scene/Camera/Perspective.h>

#include "MultiView/DemoView.h"
#include "MultiView/MainView.h"
#include "MultiView/OffscreenCompositor.h"

namespace oxygen::examples::multiview {

MainView::MainView()
  : DemoView(ViewConfig {
      .name = "MainView",
      .purpose = "Main_Solid",
      .clear_color = graphics::Color { 0.1F, 0.2F, 0.38F, 1.0F },
      .wireframe = false,
    })
{
}

void MainView::Initialize(scene::Scene& scene)
{
  EnsureCamera(scene, "MainCamera");

  // Set initial transform
  CameraNodeRef().GetTransform().SetLocalPosition({ 0.0F, 0.0F, 5.0F });

  // Log setup
  if (const auto pos = CameraNodeRef().GetTransform().GetLocalPosition()) {
    LOG_F(INFO, "[MainView] Camera positioned at ({}, {}, {})", pos->x, pos->y,
      pos->z);
  }
}

void MainView::OnSceneMutation()
{
  const auto width = static_cast<float>(GetSurface().Width());
  const auto height = static_cast<float>(GetSurface().Height());

  // Update camera
  const auto cam_ref = CameraNodeRef().GetCameraAs<scene::PerspectiveCamera>();
  if (cam_ref) {
    auto& cam = cam_ref->get();
    const float aspect = height > 0 ? (width / height) : 1.0F;
    cam.SetFieldOfView(glm::radians(45.0F));
    cam.SetAspectRatio(aspect);
    cam.SetNearPlane(0.1F);
    cam.SetFarPlane(100.0F);
    cam.SetViewport(ViewPort { .top_left_x = 0.0F,
      .top_left_y = 0.0F,
      .width = width,
      .height = height,
      .min_depth = 0.0F,
      .max_depth = 1.0F });
  }

  // Ensure resources - views need to provide their own recorder when needed
  EnsureMainRenderTargets();

  // Mark view as ready for rendering
  SetViewReady(true);

  // Register view
  ViewPort viewport { .top_left_x = 0.0F,
    .top_left_y = 0.0F,
    .width = width,
    .height = height,
    .min_depth = 0.0F,
    .max_depth = 1.0F };

  Scissors scissor { .left = 0,
    .top = 0,
    .right = static_cast<int32_t>(width),
    .bottom = static_cast<int32_t>(height) };

  AddViewToFrameContext(viewport, scissor);
}

auto MainView::OnPreRender(engine::Renderer& renderer) -> co::Co<>
{
  (void)renderer;

  // Configure the renderer each frame with current view data
  LOG_F(INFO,
    "[MainView] OnPreRender: color_tex={}, depth_tex={}, "
    "renderer_configured={}",
    static_cast<bool>(ColorTextureRef()), static_cast<bool>(DepthTextureRef()),
    RendererRef().IsConfigured());

  CHECK_F(static_cast<bool>(ColorTextureRef()),
    "MainView requires a color render target");
  CHECK_F(static_cast<bool>(DepthTextureRef()),
    "MainView requires a depth render target");

  LOG_F(INFO, "[MainView] Configuring renderer with clear_color=({},{},{},{})",
    Config().clear_color.r, Config().clear_color.g, Config().clear_color.b,
    Config().clear_color.a);
  ViewRenderer::ViewRenderData data {
    .color_texture = ColorTextureRef(),
    .depth_texture = DepthTextureRef(),
    .clear_color = Config().clear_color,
    .wireframe = Config().wireframe,
    .render_gui = true,
  };
  RendererRef().Configure(data);
  LOG_F(INFO, "[MainView] Renderer configured successfully");

  co_return;
}

auto MainView::RenderFrame(const engine::RenderContext& render_ctx,
  graphics::CommandRecorder& recorder) -> co::Co<>
{
  if (RendererRef().IsConfigured() && FramebufferRef()) {
    co_await RendererRef().Render(render_ctx, recorder);
  }
  co_return;
}

void MainView::Composite(
  graphics::CommandRecorder& recorder, graphics::Texture& backbuffer)
{
  if (IsViewReady() && ColorTextureRef()) {
    OffscreenCompositor compositor;
    compositor.CompositeFullscreen(recorder, *ColorTextureRef(), backbuffer);
  }
}

void MainView::OnReleaseResources()
{
  // No derived GPU resources to schedule here in MainView; if future
  // derived resources are added they should be deferred-released here.
  DemoView::OnReleaseResources();
}

void MainView::EnsureMainRenderTargets()
{
  auto& gfx = GetGraphics();
  const auto& surface = GetSurface();
  auto& recorder = GetRecorder();

  const auto width = surface.Width();
  const auto height = surface.Height();

  // Check if we need to recreate resources
  bool recreate = !FramebufferRef() || !ColorTextureRef() || !DepthTextureRef();
  if (!recreate && ColorTextureRef()) {
    const auto& desc = ColorTextureRef()->GetDescriptor();
    if (desc.width != width || desc.height != height) {
      recreate = true;
    }
  }

  if (!recreate) {
    return;
  }

  LOG_F(INFO, "[MainView] Creating render targets ({}x{})", width, height);

  // Release old GPU resources ONLY
  RendererRef().ResetConfiguration();
  SetViewReady(false);
  ColorTextureRef() = nullptr;
  DepthTextureRef() = nullptr;
  FramebufferRef() = nullptr;

  // Create color texture
  graphics::TextureDesc color_desc;
  color_desc.width = width;
  color_desc.height = height;
  color_desc.format = Format::kRGBA8UNorm;
  color_desc.is_render_target = true;
  color_desc.is_shader_resource = true;
  color_desc.debug_name = "MainView_Color";
  color_desc.texture_type = TextureType::kTexture2D;
  color_desc.mip_levels = 1;
  color_desc.array_size = 1;
  color_desc.sample_count = 1;
  color_desc.depth = 1;
  color_desc.use_clear_value = true;
  color_desc.clear_value = Config().clear_color;
  ColorTextureRef() = gfx.CreateTexture(color_desc);

  // Create depth texture
  graphics::TextureDesc depth_desc;
  depth_desc.width = width;
  depth_desc.height = height;
  depth_desc.format = Format::kDepth32;
  depth_desc.is_render_target = true;
  depth_desc.texture_type = TextureType::kTexture2D;
  depth_desc.mip_levels = 1;
  depth_desc.array_size = 1;
  depth_desc.sample_count = 1;
  depth_desc.depth = 1;
  depth_desc.is_shader_resource = false;
  depth_desc.use_clear_value = true;
  depth_desc.clear_value = graphics::Color { 1.0F, 0.0F, 0.0F, 0.0F };
  depth_desc.debug_name = "MainView_Depth";
  DepthTextureRef() = gfx.CreateTexture(depth_desc);

  // Create framebuffer
  graphics::FramebufferDesc fb_desc;
  fb_desc.AddColorAttachment({ .texture = ColorTextureRef(),
    .sub_resources = graphics::TextureSubResourceSet::EntireTexture(),
    .format = ColorTextureRef()->GetDescriptor().format });
  fb_desc.depth_attachment.texture = DepthTextureRef();
  fb_desc.depth_attachment.sub_resources = graphics::TextureSubResourceSet {};
  FramebufferRef() = gfx.CreateFramebuffer(fb_desc);

  // Transition textures to appropriate initial states using the stored
  // recorder CRITICAL: Begin tracking resources before requiring their states
  recorder.BeginTrackingResourceState(
    static_cast<const graphics::Texture&>(*ColorTextureRef()),
    graphics::ResourceStates::kRenderTarget);
  recorder.BeginTrackingResourceState(
    static_cast<const graphics::Texture&>(*DepthTextureRef()),
    graphics::ResourceStates::kUndefined);

  // Color: RenderTarget, Depth: DepthWrite
  recorder.RequireResourceState(
    static_cast<const graphics::Texture&>(*DepthTextureRef()),
    graphics::ResourceStates::kDepthWrite);
}

} // namespace oxygen::examples::multiview
