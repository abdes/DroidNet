//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "PipView.h"

#include <algorithm>
#include <cmath>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

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

namespace {
  constexpr float kPipWidthRatio = 0.33F;
  constexpr float kPipHeightRatio = 0.33F;
  constexpr float kPipMargin = 24.0F;
}

PipView::PipView()
  : DemoView(ViewConfig {
      .name = "WireframePiP",
      .purpose = "PiP_Wireframe",
      .clear_color = graphics::Color { 0.03f, 0.03f, 0.03f, 1.0f },
      .wireframe = true,
    })
{
}

void PipView::Initialize(scene::Scene& scene)
{
  EnsureCamera(scene, "PiPCamera");

  // Position the PiP camera
  const glm::vec3 pip_position = glm::vec3(-0.8f, 0.6f, 0.5f);
  camera_node_.GetTransform().SetLocalPosition(pip_position);

  // Compute LookAt rotation: point camera from pip_position toward target.
  // We use a hardcoded default target (0, 0, -2) which matches the sphere's
  // initial position. This is set during Initialize() before the scene is
  // fully populated. The target is adequate for the initial frame.
  glm::vec3 target = glm::vec3(0.0f, 0.0f, -2.0f);

  const glm::vec3 world_up = glm::vec3(0.0f, 1.0f, 0.0f);
  const glm::mat4 view = glm::lookAt(pip_position, target, world_up);
  const glm::quat pip_rot = glm::quat_cast(glm::inverse(view));
  camera_node_.GetTransform().SetLocalRotation(pip_rot);

  // Log setup
  if (const auto pos = camera_node_.GetTransform().GetLocalPosition()) {
    LOG_F(INFO, "[PipView] Camera positioned at ({}, {}, {})", pos->x, pos->y,
      pos->z);
  }
}

void PipView::OnSceneMutation()
{
  const auto surface_width = static_cast<int>(GetSurface().Width());
  const auto surface_height = static_cast<int>(GetSurface().Height());

  // Compute PiP dimensions
  const auto [pip_width, pip_height]
    = ComputePipExtent(surface_width, surface_height);
  target_width_ = pip_width;
  target_height_ = pip_height;
  destination_viewport_
    = ComputePipViewport(surface_width, surface_height, pip_width, pip_height);

  // Update camera
  const auto cam_ref = camera_node_.GetCameraAs<scene::PerspectiveCamera>();
  if (cam_ref) {
    auto& cam = cam_ref->get();
    const float aspect = pip_height > 0
      ? (static_cast<float>(pip_width) / static_cast<float>(pip_height))
      : 1.0f;
    cam.SetFieldOfView(glm::radians(35.0f));
    cam.SetAspectRatio(aspect);
    cam.SetNearPlane(0.05f);
    cam.SetFarPlane(100.0f);
    cam.SetViewport(ViewPort { .top_left_x = 0.0f,
      .top_left_y = 0.0f,
      .width = static_cast<float>(pip_width),
      .height = static_cast<float>(pip_height),
      .min_depth = 0.0f,
      .max_depth = 1.0f });
  }

  // Ensure resources with the stored recorder
  EnsurePipRenderTargets(pip_width, pip_height);

  // Mark view as ready for rendering
  view_ready_ = true;

  // Register view
  if (destination_viewport_) {
    ViewPort viewport = *destination_viewport_;
    Scissors scissor { .left = static_cast<int32_t>(viewport.top_left_x),
      .top = static_cast<int32_t>(viewport.top_left_y),
      .right = static_cast<int32_t>(viewport.top_left_x + viewport.width),
      .bottom = static_cast<int32_t>(viewport.top_left_y + viewport.height) };

    RegisterView(viewport, scissor);
  }
}

auto PipView::OnPreRender(oxygen::engine::Renderer& renderer) -> co::Co<void>
{
  (void)renderer;

  // Configure renderer if not already configured
  LOG_F(INFO,
    "[PipView] OnPreRender: color_tex={}, depth_tex={}, renderer_configured={}",
    static_cast<bool>(color_texture_), static_cast<bool>(depth_texture_),
    renderer_.IsConfigured());

  if (color_texture_ && depth_texture_ && !renderer_.IsConfigured()) {
    LOG_F(INFO, "[PipView] Configuring renderer with clear_color=({},{},{},{})",
      config_.clear_color.r, config_.clear_color.g, config_.clear_color.b,
      config_.clear_color.a);
    ViewRenderer::Config config {
      .color_texture = color_texture_,
      .depth_texture = depth_texture_,
      .clear_color = config_.clear_color,
      .wireframe = config_.wireframe,
    };
    renderer_.Configure(config);
    LOG_F(INFO, "[PipView] Renderer configured successfully");
  }

  co_return;
}

auto PipView::RenderToFramebuffer(const engine::RenderContext& render_ctx,
  graphics::CommandRecorder& recorder, const graphics::Framebuffer& framebuffer)
  -> co::Co<void>
{
  if (renderer_.IsConfigured() && framebuffer_) {
    co_await renderer_.Render(render_ctx, recorder, *framebuffer_);
  }
  co_return;
}

void PipView::Composite(
  graphics::CommandRecorder& recorder, graphics::Texture& backbuffer)
{
  if (view_ready_ && color_texture_ && destination_viewport_) {
    OffscreenCompositor compositor;
    compositor.CompositeToRegion(
      recorder, *color_texture_, backbuffer, *destination_viewport_);
  }
}

void PipView::ReleaseResources()
{
  DemoView::ReleaseResources();
  target_width_ = 0;
  target_height_ = 0;
  destination_viewport_.reset();
}

void PipView::EnsurePipRenderTargets(uint32_t width, uint32_t height)
{
  auto& gfx = GetGraphics();
  auto& recorder = GetRecorder();

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

  LOG_F(INFO, "[PipView] Creating render targets ({}x{})", width, height);

  // Release old GPU resources ONLY (don't touch viewport!)
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
  color_desc.debug_name = "PipView_Color";
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
  depth_desc.debug_name = "PipView_Depth";
  depth_texture_ = gfx.CreateTexture(depth_desc);

  // Create framebuffer
  graphics::FramebufferDesc fb_desc;
  fb_desc.AddColorAttachment({ .texture = color_texture_,
    .sub_resources = graphics::TextureSubResourceSet::EntireTexture(),
    .format = color_texture_->GetDescriptor().format });
  fb_desc.depth_attachment.texture = depth_texture_;
  fb_desc.depth_attachment.sub_resources = graphics::TextureSubResourceSet {};
  framebuffer_ = gfx.CreateFramebuffer(fb_desc);

  // Transition textures to appropriate initial states
  recorder.BeginTrackingResourceState(
    static_cast<const graphics::Texture&>(*color_texture_),
    graphics::ResourceStates::kRenderTarget);
  recorder.BeginTrackingResourceState(
    static_cast<const graphics::Texture&>(*depth_texture_),
    graphics::ResourceStates::kUndefined);

  // Color: RenderTarget, Depth: DepthWrite
  recorder.RequireResourceState(
    static_cast<const graphics::Texture&>(*depth_texture_),
    graphics::ResourceStates::kDepthWrite);
}

auto PipView::ComputePipExtent(const int surface_width,
  const int surface_height) -> std::pair<uint32_t, uint32_t>
{
  const auto pip_width = std::max(1,
    static_cast<int>(
      std::lround(static_cast<float>(surface_width) * kPipWidthRatio)));
  const auto pip_height = std::max(1,
    static_cast<int>(
      std::lround(static_cast<float>(surface_height) * kPipHeightRatio)));
  return { static_cast<uint32_t>(pip_width),
    static_cast<uint32_t>(pip_height) };
}

auto PipView::ComputePipViewport(const int surface_width,
  const int surface_height, const uint32_t pip_width, const uint32_t pip_height)
  -> ViewPort
{
  (void)surface_height;
  const float max_width = static_cast<float>(surface_width);
  const float width = static_cast<float>(pip_width);
  const float height = static_cast<float>(pip_height);

  const float offset_x = std::max(0.0F, max_width - width - kPipMargin);
  const float offset_y = kPipMargin;

  return ViewPort { .top_left_x = offset_x,
    .top_left_y = offset_y,
    .width = width,
    .height = height,
    .min_depth = 0.0F,
    .max_depth = 1.0F };
}

} // namespace oxygen::examples::multiview
