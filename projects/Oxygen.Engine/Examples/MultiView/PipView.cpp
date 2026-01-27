//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cmath>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <utility>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/Surface.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Renderer/Renderer.h>
#include <Oxygen/Scene/Camera/Perspective.h>

#include "MultiView/OffscreenCompositor.h"
#include "MultiView/PipView.h"

namespace oxygen::examples::multiview {

namespace {
  constexpr float kPipWidthRatio = 0.45F;
  constexpr float kPipHeightRatio = 0.45F;
  constexpr float kPipMargin = 24.0F;
}

PipView::PipView()
  : DemoView(ViewConfig {
      .name = "WireframePiP",
      .purpose = "PiP_Wireframe",
      .clear_color = graphics::Color { 0.1F, 0.1F, 0.1F, 1.0F },
      .wireframe = true,
    })
{
}

void PipView::Initialize(scene::Scene& scene)
{
  EnsureCamera(scene, "PiPCamera");

  // Position the PiP camera
  constexpr glm::vec3 pip_position = glm::vec3(-5.0F, 0.4F, 4.0F);
  CameraNodeRef().GetTransform().SetLocalPosition(pip_position);

  // Compute LookAt rotation: point camera from pip_position toward target.
  // We use a hardcoded default target (0, 0, -2) which matches the sphere's
  // initial position. This is set during Initialize() before the scene is
  // fully populated. The target is adequate for the initial frame.
  glm::vec3 target = glm::vec3(0.0F, 0.0F, -2.0F);

  constexpr glm::vec3 world_up = glm::vec3(0.0F, 1.0F, 0.0F);
  const glm::mat4 view = glm::lookAt(pip_position, target, world_up);
  const glm::quat pip_rot = glm::quat_cast(glm::inverse(view));
  CameraNodeRef().GetTransform().SetLocalRotation(pip_rot);

  // Log setup
  if (const auto pos = CameraNodeRef().GetTransform().GetLocalPosition()) {
    LOG_F(INFO, "[PipView] Camera positioned at ({}, {}, {})", pos->x, pos->y,
      pos->z);
  }
}

void PipView::OnSceneMutation()
{
  const auto surface_width = static_cast<int>(GetSurface().Width());
  const auto surface_height = static_cast<int>(GetSurface().Height());

  // Compute PiP dimensions
  viewport_ = ComputePipViewport({ surface_width, surface_height });

  if (!viewport_.has_value() || !viewport_->IsValid()) {
    LOG_F(WARNING,
      "[PipView] Computed viewport isn't valid: {} (surface {}x{})",
      to_string(viewport_.value_or(ViewPort {})), surface_width,
      surface_height);
    // Ensure we don't proceed into creating resources with an invalid viewport.
    SetViewReady(false);
    return;
  }

  // Update camera
  const auto cam_ref = CameraNodeRef().GetCameraAs<scene::PerspectiveCamera>();
  if (cam_ref) {
    auto& cam = cam_ref->get();
    const float aspect
      = viewport_->height > 0 ? (viewport_->width / viewport_->height) : 1.0F;
    cam.SetFieldOfView(glm::radians(35.0F));
    cam.SetAspectRatio(aspect);
    cam.SetNearPlane(0.05F);
    cam.SetFarPlane(100.0F);
    cam.SetViewport(ViewPort {
      .top_left_x = 0.0F,
      .top_left_y = 0.0F,
      .width = (viewport_->width),
      .height = (viewport_->height),
      .min_depth = 0.0F,
      .max_depth = 1.0F,
    });
  }

  // Ensure resources with the stored recorder
  EnsurePipRenderTargets({ viewport_->width, viewport_->height });

  // Mark view as ready for rendering
  SetViewReady(true);

  // Register view
  if (viewport_) {
    const auto viewport = *viewport_;
    // Clamp scissor box to surface bounds to avoid invalid rectangles when
    // PiP might be larger than the surface (tiny/minimized windows).
    const auto sw = static_cast<int32_t>(surface_width);
    const auto sh = static_cast<int32_t>(surface_height);
    const auto left = static_cast<int32_t>(std::max(0.0F, viewport.top_left_x));
    const auto top = static_cast<int32_t>(std::max(0.0F, viewport.top_left_y));
    const auto right = static_cast<int32_t>(std::clamp(
      viewport.top_left_x + viewport.width, 0.0F, static_cast<float>(sw)));
    const auto bottom = static_cast<int32_t>(std::clamp(
      viewport.top_left_y + viewport.height, 0.0F, static_cast<float>(sh)));

    const Scissors scissor {
      .left = left, .top = top, .right = right, .bottom = bottom
    };

    AddViewToFrameContext(viewport, scissor);
  }
}

auto PipView::OnPreRender(engine::Renderer& renderer) -> co::Co<>
{
  (void)renderer;

  // Configure renderer if not already configured
  LOG_F(INFO,
    "[PipView] OnPreRender: color_tex={}, depth_tex={}, renderer_configured={}",
    static_cast<bool>(ColorTextureRef()), static_cast<bool>(DepthTextureRef()),
    RendererRef().IsConfigured());

  if (ColorTextureRef() && DepthTextureRef() && !RendererRef().IsConfigured()) {
    LOG_F(INFO, "[PipView] Configuring renderer with clear_color=({},{},{},{})",
      Config().clear_color.r, Config().clear_color.g, Config().clear_color.b,
      Config().clear_color.a);
    ViewRenderer::Config config {
      .color_texture = ColorTextureRef(),
      .depth_texture = DepthTextureRef(),
      .clear_color = Config().clear_color,
      .wireframe = Config().wireframe,
    };
    RendererRef().Configure(config);
    LOG_F(INFO, "[PipView] Renderer configured successfully");
  }

  co_return;
}

auto PipView::RenderFrame(const engine::RenderContext& render_ctx,
  graphics::CommandRecorder& recorder) -> co::Co<>
{
  if (RendererRef().IsConfigured() && FramebufferRef()) {
    co_await RendererRef().Render(render_ctx, recorder);
  }
  co_return;
}

void PipView::Composite(
  graphics::CommandRecorder& recorder, graphics::Texture& backbuffer)
{
  if (!IsViewReady() || !ColorTextureRef() || !viewport_) {
    return;
  }

  // Clip the PiP viewport to the backbuffer bounds so CopyTexture isn't asked
  // to write outside of the destination. If the clipped region becomes empty
  // we simply skip compositing.
  const auto& dst_desc = backbuffer.GetDescriptor();
  ViewPort clipped = *viewport_;
  clipped.top_left_x = std::max(0.0F, clipped.top_left_x);
  clipped.top_left_y = std::max(0.0F, clipped.top_left_y);

  if (clipped.top_left_x + clipped.width > static_cast<float>(dst_desc.width)) {
    clipped.width
      = std::max(0.0F, static_cast<float>(dst_desc.width) - clipped.top_left_x);
  }
  if (clipped.top_left_y + clipped.height
    > static_cast<float>(dst_desc.height)) {
    clipped.height = std::max(
      0.0F, static_cast<float>(dst_desc.height) - clipped.top_left_y);
  }

  // If the clipped area is less than one pixel in either dimension, skip.
  if (clipped.width < 1.0F || clipped.height < 1.0F) {
    LOG_F(INFO,
      "[PipView] Composite skipped: clipped viewport {}x{} at ({},{})",
      clipped.width, clipped.height, clipped.top_left_x, clipped.top_left_y);
    return;
  }

  OffscreenCompositor compositor;
  compositor.CompositeToRegion(
    recorder, *ColorTextureRef(), backbuffer, clipped);
}

void PipView::OnReleaseResources()
{
  // Derived-only cleanup (non-GPU state) for PiP view.
  viewport_.reset();

  // If PiP acquires any GPU resources local to the derived class in future
  // changes, schedule their deferred release here using graphics_weak_.

  // Call base hook (currently a no-op) for symmetry / future compatibility.
  DemoView::OnReleaseResources();
}

void PipView::EnsurePipRenderTargets(const SubPixelExtent& viewport_extent)
{
  auto& gfx = GetGraphics();
  auto& recorder = GetRecorder();

  // Round viewport size to integer pixels and clamp to the surface size so we
  // never create textures that are larger than the backbuffer we'll be
  // copying into. Keep a minimum of 1 pixel to avoid zero-sized textures.
  const auto surface_w = static_cast<int32_t>(GetSurface().Width());
  const auto surface_h = static_cast<int32_t>(GetSurface().Height());

  auto w = static_cast<int32_t>(std::lround(viewport_extent.width));
  auto h = static_cast<int32_t>(std::lround(viewport_extent.height));
  w = std::clamp(w, 1, std::max(1, surface_w));
  h = std::clamp(h, 1, std::max(1, surface_h));

  auto width = static_cast<uint32_t>(w);
  auto height = static_cast<uint32_t>(h);

  // Check if we need to recreate resources
  bool recreate = !FramebufferRef() || !ColorTextureRef() || !DepthTextureRef();
  if (!recreate && ColorTextureRef()) {
    const auto& desc = ColorTextureRef()->GetDescriptor();
    if (std::cmp_not_equal(desc.width, width)
      || std::cmp_not_equal(desc.height, height)) {
      recreate = true;
    }
  }

  if (!recreate) {
    return;
  }

  LOG_F(INFO, "[PipView] Creating render targets ({}x{})", width, height);

  // Release old GPU resources ONLY (don't touch viewport!)
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
  color_desc.debug_name = "PipView_Color";
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
  depth_desc.debug_name = "PipView_Depth";
  DepthTextureRef() = gfx.CreateTexture(depth_desc);

  // Create framebuffer
  graphics::FramebufferDesc fb_desc;
  fb_desc.AddColorAttachment({ .texture = ColorTextureRef(),
    .sub_resources = graphics::TextureSubResourceSet::EntireTexture(),
    .format = ColorTextureRef()->GetDescriptor().format });
  fb_desc.depth_attachment.texture = DepthTextureRef();
  fb_desc.depth_attachment.sub_resources = graphics::TextureSubResourceSet {};
  FramebufferRef() = gfx.CreateFramebuffer(fb_desc);

  // Transition textures to appropriate initial states
  recorder.BeginTrackingResourceState(
    *ColorTextureRef(), graphics::ResourceStates::kRenderTarget);
  recorder.BeginTrackingResourceState(
    *DepthTextureRef(), graphics::ResourceStates::kUndefined);

  // Color: RenderTarget, Depth: DepthWrite
  recorder.RequireResourceState(
    *DepthTextureRef(), graphics::ResourceStates::kDepthWrite);
}

auto PipView::ComputePipExtent(const PixelExtent& surface_extent) -> PixelExtent
{
  // Base size from ratio (rounded) but clamp to the available surface so we
  // never produce an extent larger than the surface. This is important for
  // very small surfaces and avoids creating a PiP that won't fit into the
  // backbuffer when compositing/copying.
  const int base_width = static_cast<int>(
    std::lround(static_cast<float>(surface_extent.width) * kPipWidthRatio));
  const int base_height = static_cast<int>(
    std::lround(static_cast<float>(surface_extent.height) * kPipHeightRatio));

  const auto pip_width = std::clamp(base_width, 1, surface_extent.width);
  const auto pip_height = std::clamp(base_height, 1, surface_extent.height);
  return { pip_width, pip_height };
}

auto PipView::ComputePipViewport(const PixelExtent& surface_extent) -> ViewPort
{
  auto pip_extent = ComputePipExtent(surface_extent);
  const auto max_width = static_cast<float>(surface_extent.width);
  const auto width = static_cast<float>(pip_extent.width);
  const auto height = static_cast<float>(pip_extent.height);

  // Place the PiP at the top-right with a fixed margin when possible. If the
  // selected size would overflow the surface (e.g. tiny windows), clamp the
  // top-left coordinates to guarantee the PiP is entirely inside the
  // surface.
  const float offset_x = std::max(0.0F, max_width - width - kPipMargin);

  // Y offset: prefer the fixed margin but clamp so top+height doesn't exceed
  // the surface (important for very short windows).
  const auto max_height = static_cast<float>(surface_extent.height);
  const float unclamped_y = kPipMargin;
  const float offset_y
    = std::clamp(unclamped_y, 0.0F, std::max(0.0F, max_height - height));

  return ViewPort { .top_left_x = offset_x,
    .top_left_y = offset_y,
    .width = width,
    .height = height,
    .min_depth = 0.0F,
    .max_depth = 1.0F };
}

} // namespace oxygen::examples::multiview
