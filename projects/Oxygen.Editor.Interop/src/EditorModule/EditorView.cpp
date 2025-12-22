//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma unmanaged

#include "pch.h"

#include "EditorModule/EditorView.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include <glm/gtc/quaternion.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include "EditorModule/ViewRenderer.h"

#include <Oxygen/Core/Constants.h>
#include <Oxygen/Scene/Camera/Orthographic.h>
#include <Oxygen/Scene/Camera/Perspective.h>

namespace oxygen::interop::module {

namespace {

[[nodiscard]] auto IsFinite(const glm::vec3& v) noexcept -> bool {
  return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

[[nodiscard]] auto NormalizeSafe(const glm::vec3 v,
  const glm::vec3 fallback) noexcept -> glm::vec3 {
  const float len2 = glm::dot(v, v);
  if (len2 <= std::numeric_limits<float>::epsilon()) {
    return fallback;
  }
  return v / std::sqrt(len2);
}

[[nodiscard]] auto LookRotationFromPositionToTarget(
  const glm::vec3& position,
  const glm::vec3& target_position,
  const glm::vec3& up_direction) noexcept -> glm::quat {
  const glm::vec3 forward =
    NormalizeSafe(target_position - position, glm::vec3(0.0f, 0.0f, -1.0f));
  const glm::vec3 right =
    NormalizeSafe(glm::cross(forward, up_direction), glm::vec3(1.0f, 0.0f, 0.0f));
  const glm::vec3 up = glm::cross(right, forward);

  // Build a rotation matrix with -Z as forward.
  glm::mat4 look_matrix(1.0f);
  look_matrix[0] = glm::vec4(right, 0.0f);
  look_matrix[1] = glm::vec4(up, 0.0f);
  look_matrix[2] = glm::vec4(-forward, 0.0f);

  return glm::quat_cast(look_matrix);
}

[[nodiscard]] auto ResolvePresetForward(CameraViewPreset preset) noexcept
  -> glm::vec3 {
  // These are world-space directions from the focus point towards the camera.
  // Engine conventions (see Oxygen/Core/Constants.h):
  // - Right-handed
  // - Z-up
  // - World forward = -Y
  switch (preset) {
  case CameraViewPreset::kTop:
    return oxygen::space::move::Up;
  case CameraViewPreset::kBottom:
    return oxygen::space::move::Down;
  case CameraViewPreset::kLeft:
    return oxygen::space::move::Left;
  case CameraViewPreset::kRight:
    return oxygen::space::move::Right;
  case CameraViewPreset::kFront:
    // "Front" view: camera is in +Y looking toward -Y.
    return oxygen::space::move::Back;
  case CameraViewPreset::kBack:
    // "Back" view: camera is in -Y looking toward +Y.
    return oxygen::space::move::Forward;
  case CameraViewPreset::kPerspective:
  default:
    return oxygen::space::move::Back;
  }
}

[[nodiscard]] auto ResolvePresetUp(CameraViewPreset preset) noexcept
  -> glm::vec3 {
  // Choose up vectors that keep screen orientation stable for each preset.
  // For Top/Bottom, use +/-Y so right stays +X.
  switch (preset) {
  case CameraViewPreset::kTop:
    return oxygen::space::move::Back;
  case CameraViewPreset::kBottom:
    return oxygen::space::move::Forward;
  default:
    return oxygen::space::move::Up;
  }
}

} // namespace

auto EditorView::Config::ResolveExtent() const -> SubPixelExtent {
  if (compositing_target.has_value() && compositing_target.value()) {
    auto *surface = compositing_target.value();
    float w = static_cast<float>(surface->Width());
    float h = static_cast<float>(surface->Height());

    // Try to get more accurate dimensions from backbuffer if available
    auto back = surface->GetCurrentBackBuffer();
    if (back) {
      const auto &desc = back->GetDescriptor();
      if (desc.width > 0 && desc.height > 0) {
        w = static_cast<float>(desc.width);
        h = static_cast<float>(desc.height);
      }
    }
    return {.width = w, .height = h};
  }

  // Warn about misconfigured views using default 1x1 dimensions
  if (width == 1 && height == 1) {
    LOG_F(WARNING,
          "View '{}' has no compositing target and is using default 1x1 "
          "dimensions. This likely indicates a misconfigured view.",
          name);
  }

  return {.width = static_cast<float>(width),
          .height = static_cast<float>(height)};
}

EditorView::EditorView(Config config) : config_(std::move(config)) {
  renderer_ = std::make_unique<ViewRenderer>();

  // Initialize dimensions from config
  auto extent = config_.ResolveExtent();
  width_ = extent.width;
  height_ = extent.height;
}

EditorView::~EditorView() {
  if (state_ != ViewState::kDestroyed) {
    ReleaseResources();
  }
}

void EditorView::Resize(uint32_t width, uint32_t height) {
  if (static_cast<float>(width) != width_ ||
      static_cast<float>(height) != height_) {
    LOG_F(INFO, "EditorView '{}' Resize: {}x{} -> {}x{}", config_.name, width_,
          height_, width, height);
  }
  width_ = static_cast<float>(width);
  height_ = static_cast<float>(height);
}

void EditorView::SetRenderingContext(const EditorViewContext &ctx) {
  current_context_ = &ctx;
  // Avoid assigning an invalid weak_ptr into the member if the Graphics
  // instance isn't currently owned by a shared_ptr. This helps prevent
  // surprising control-block corruption in rare shutdown/race cases.
  auto tmp = ctx.graphics.weak_from_this();
  if (tmp.expired()) {
    DLOG_F(WARNING, "EditorView::SetRenderingContext - graphics weak pointer is expired");
    graphics_.reset();
  } else {
    graphics_ = std::move(tmp);
  }
}

void EditorView::ClearPhaseRecorder() { current_context_ = nullptr; }

void EditorView::Initialize(scene::Scene &scene) {
  if (state_ != ViewState::kCreating) {
    return;
  }

  // Store scene reference for later use
  scene_ = scene.weak_from_this();

  state_ = ViewState::kReady;
  LOG_F(INFO, "EditorView '{}' initialized.", config_.name);
}

void EditorView::OnSceneMutation() {
  LOG_SCOPE_F(4, "EditorView::OnSceneMutation");
  if (!current_context_ || state_ == ViewState::kDestroyed) {
    return;
  }

  if (!visible_) {
    return;
  }

  // Get scene for mutations
  auto scn = scene_.lock();
  if (!scn) {
    return;
  }

  // Create camera if this is the first time
  if (!camera_node_.IsAlive()) {
    CreateCamera(*scn);
  }

  // Update camera for this frame
  UpdateCameraForFrame();

  LOG_F(2,
        "EditorView '{}' OnSceneMutation: Updating ViewContext with size {}x{}",
        config_.name, width_, height_);

  View view;
  view.viewport = ViewPort{
      .top_left_x = 0.0f,
      .top_left_y = 0.0f,
      .width = width_,
      .height = height_,
      .min_depth = 0.0f,
      .max_depth = 1.0f,
  };
  view.scissor = Scissors{
      .left = 0,
      .top = 0,
      .right = static_cast<int32_t>(width_),
      .bottom = static_cast<int32_t>(height_),
  };

  // Register with FrameContext
  engine::ViewContext vc{
      .view = view,
      .metadata = engine::ViewMetadata{.name = config_.name,
                                       .purpose = config_.purpose},
      .output = nullptr // Set later by Renderer or used internally
  };

  // We must never register views from EditorView.
  // The owning manager (ViewManager or higher-level module) is responsible
  // for registering views with FrameContext and assigning the engine ViewId.
  // If we don't have an assigned id yet it indicates a lifecycle error; log
  // and skip updating/registration here.
  if (view_id_ != kInvalidViewId) {
    current_context_->frame_context.UpdateView(view_id_, std::move(vc));
  } else {
    LOG_F(WARNING, "EditorView::OnSceneMutation invoked but EditorView has no "
                   "engine-assigned ViewId. Owner must register the view "
                   "before scenes are mutated.");
  }
}

auto EditorView::OnPreRender(engine::Renderer &renderer) -> oxygen::co::Co<> {
  LOG_SCOPE_F(4, "EditorView::OnPreRender");
  if (state_ != ViewState::kReady || !visible_) {
    co_return;
  }

  if (width_ <= 0.0f || height_ <= 0.0f) {
    co_return;
  }

  ResizeIfNeeded();

  // Set initial camera orientation (only once, after transform propagation)
  if (!initial_orientation_set_ && camera_node_.IsAlive()) {
    auto transform = camera_node_.GetTransform();
    const glm::vec3 position =
      transform.GetLocalPosition().value_or(glm::vec3(0.0f, 0.0f, 0.0f));

    const glm::vec3 forward =
      NormalizeSafe(focus_point_ - position, oxygen::space::look::Forward);
    const glm::vec3 up_dir = oxygen::space::move::Up;
    const glm::vec3 right =
      NormalizeSafe(glm::cross(forward, up_dir), oxygen::space::look::Right);
    const glm::vec3 up = glm::cross(right, forward);

    glm::mat4 look_matrix(1.0f);
    look_matrix[0] = glm::vec4(right, 0.0f);
    look_matrix[1] = glm::vec4(up, 0.0f);
    look_matrix[2] = glm::vec4(-forward, 0.0f);

    (void)transform.SetLocalRotation(glm::quat_cast(look_matrix));
    initial_orientation_set_ = true;
  }

  // After resizing (or if resources previously existed) ensure the
  // FrameContext gets a single SetViewOutput call so Renderer can find
  // our framebuffer. This centralizes the SetViewOutput call — preventing
  // duplicate updates from both the creation and the renderer-path.
  if (framebuffer_ && view_id_ != kInvalidViewId && current_context_) {
    current_context_->frame_context.SetViewOutput(
        view_id_,
        oxygen::observer_ptr<graphics::Framebuffer>(framebuffer_.get()));
    // Ensure FrameContext now has the output populated
    DCHECK_NOTNULL_F(
        current_context_->frame_context.GetViewContext(view_id_).output,
        "EditorView::OnPreRender - framebuffer did not populate FrameContext "
        "output for view {}",
        view_id_);
  }

  // Update ViewRenderer with new framebuffer
  if (renderer_) {
    renderer_->SetFramebuffer(framebuffer_);

    // Ensure registered with renderer
    RegisterWithRenderer(renderer);
  }

  co_return;
}

void EditorView::Show() { visible_ = true; }

void EditorView::Hide() { visible_ = false; }

void EditorView::ReleaseResources() {
  if (state_ == ViewState::kDestroyed) {
    return;
  }

  state_ = ViewState::kReleasing;

  // Schedule GPU resources for deferred destruction
  if (auto gfx = graphics_.lock()) {
    auto &reclaimer = gfx->GetDeferredReclaimer();
    if (color_texture_)
      graphics::DeferredObjectRelease(color_texture_, reclaimer);
    if (depth_texture_)
      graphics::DeferredObjectRelease(depth_texture_, reclaimer);
    if (framebuffer_)
      graphics::DeferredObjectRelease(framebuffer_, reclaimer);
  }

  // Unregister from engine if we have the renderer
  if (renderer_ && renderer_module_) {
    renderer_->UnregisterFromEngine(*renderer_module_);
  }
  renderer_module_ = nullptr;

  // Detach camera and destroy node
  if (camera_node_.IsAlive()) {
    camera_node_.DetachCamera();
    if (auto scn = scene_.lock()) {
      scn->DestroyNode(camera_node_);
    }
  }

  view_id_ = ViewId{};
  state_ = ViewState::kDestroyed;
}

void EditorView::ResizeIfNeeded() {
  bool need_resize = false;
  if (!color_texture_ ||
      static_cast<float>(color_texture_->GetDescriptor().width) != width_ ||
      static_cast<float>(color_texture_->GetDescriptor().height) != height_) {
    need_resize = true;
  }

  if (need_resize) {
    if (auto gfx = graphics_.lock()) {
      auto &reclaimer = gfx->GetDeferredReclaimer();
      if (color_texture_)
        graphics::DeferredObjectRelease(color_texture_, reclaimer);
      if (depth_texture_)
        graphics::DeferredObjectRelease(depth_texture_, reclaimer);
      if (framebuffer_)
        graphics::DeferredObjectRelease(framebuffer_, reclaimer);

      graphics::TextureDesc color_desc;
      color_desc.width = static_cast<uint32_t>(width_);
      color_desc.height = static_cast<uint32_t>(height_);
      color_desc.format = oxygen::Format::kRGBA8UNorm;
      color_desc.texture_type = oxygen::TextureType::kTexture2D;
      color_desc.is_render_target = true;
      color_desc.is_shader_resource = true;
      color_desc.use_clear_value = true; // better for performance
      color_desc.clear_value = config_.clear_color;
      color_desc.initial_state =
          oxygen::graphics::ResourceStates::kShaderResource;

      // Assign a helpful debug name so runtime diagnostics show which
      // EditorView owns this texture (helps avoid generic "Texture" labels).
      std::string dbg_base = config_.name.empty()
                                 ? std::string("EditorView:Unnamed")
                     : fmt::format(fmt::runtime("EditorView:{}"),
                       config_.name);
      color_desc.debug_name = dbg_base + ".Color";
      color_texture_ = gfx->CreateTexture(color_desc);

      graphics::TextureDesc depth_desc = color_desc;
      depth_desc.format = oxygen::Format::kDepth32;
      depth_desc.is_shader_resource = false;
      depth_desc.use_clear_value = true;
      depth_desc.clear_value = {1.0f, 0.0f, 0.0f, 0.0f};
      depth_desc.initial_state = oxygen::graphics::ResourceStates::kDepthWrite;

      depth_desc.debug_name = dbg_base + ".Depth";
      depth_texture_ = gfx->CreateTexture(depth_desc);

      graphics::FramebufferDesc fb_desc;
      fb_desc.AddColorAttachment(color_texture_);
      fb_desc.SetDepthAttachment(depth_texture_);

      framebuffer_ = gfx->CreateFramebuffer(fb_desc);

      LOG_F(INFO,
            "EditorView '{}' resized resources to {}x{} "
            "(color.use_clear_value={}, color.clear_value=({}, {}, {}, {}))",
            config_.name, width_, height_, color_desc.use_clear_value,
            color_desc.clear_value.r, color_desc.clear_value.g,
            color_desc.clear_value.b, color_desc.clear_value.a);
    }
  }
}

void EditorView::CreateCamera(scene::Scene &scene) {
  // Create camera node in the scene
  camera_node_ = scene.CreateNode(config_.name + "_Camera");

  auto camera = std::make_unique<scene::PerspectiveCamera>();
  camera_node_.AttachCamera(std::move(camera));

  // Set initial position (orientation setup happens in OnPreRender)
  camera_node_.GetTransform().SetLocalPosition(glm::vec3(10.0F, -10.0F, +7.0F));

  LOG_F(INFO, "EditorView '{}' created camera node", config_.name);
}

void EditorView::UpdateCameraForFrame() {
  if (!current_context_) {
    return;
  }

  if (!camera_node_.IsAlive()) {
    return;
  }

  const float aspect =
    (width_ > 0.0f && height_ > 0.0f) ? (width_ / height_) : 1.0f;

  const ViewPort vp{
      .top_left_x = 0.0f,
      .top_left_y = 0.0f,
      .width = width_,
      .height = height_,
      .min_depth = 0.0f,
      .max_depth = 1.0f,
  };

  if (auto cam_ref = camera_node_.GetCameraAs<scene::PerspectiveCamera>(); cam_ref) {
    auto& cam = cam_ref->get();
    cam.SetAspectRatio(aspect);
    cam.SetViewport(vp);
    return;
  }

  if (auto cam_ref = camera_node_.GetCameraAs<scene::OrthographicCamera>(); cam_ref) {
    auto& cam = cam_ref->get();
    cam.SetViewport(vp);

    // Keep ortho extents stable in screen-space by deriving width from aspect.
    const float half_h = std::max(0.001f, ortho_half_height_);
    const float half_w = half_h * std::max(0.001f, aspect);

    // Keep near/far in a sane range.
    // Using fixed planes can make orthographic presets appear empty if the
    // camera is far from the focus point (everything gets clipped).
    constexpr float kNear = 0.1f;
    float far_plane = 1000.0f;
    if (auto transform = camera_node_.GetTransform(); true) {
      const glm::vec3 pos =
        transform.GetLocalPosition().value_or(glm::vec3(0.0f, 0.0f, 0.0f));
      const glm::vec3 focus = focus_point_;
      const glm::vec3 d = pos - focus;
      const float dist = std::sqrt(glm::dot(d, d));
      if (std::isfinite(dist)) {
        far_plane = std::max(far_plane, dist * 4.0f);
      }
    }
    cam.SetExtents(-half_w, half_w, -half_h, half_h, kNear, far_plane);
  }
}

void EditorView::SetCameraViewPreset(CameraViewPreset preset) {
  camera_view_preset_ = preset;

  if (!camera_node_.IsAlive()) {
    return;
  }

  auto transform = camera_node_.GetTransform();

  glm::vec3 focus = focus_point_;
  if (!IsFinite(focus)) {
    focus = glm::vec3(0.0f, 0.0f, 0.0f);
    focus_point_ = focus;
  }

  const glm::vec3 position =
    transform.GetLocalPosition().value_or(glm::vec3(0.0f, 0.0f, 5.0f));

  const glm::vec3 offset = position - focus;
  float radius = std::sqrt(glm::dot(offset, offset));
  if (!std::isfinite(radius) || radius <= 0.001f) {
    radius = 10.0f;
  }

  if (preset == CameraViewPreset::kPerspective) {
    // Ensure the camera component is perspective.
    if (!camera_node_.GetCameraAs<scene::PerspectiveCamera>()) {
      camera_node_.ReplaceCamera(std::make_unique<scene::PerspectiveCamera>());
    }
    // Do not override the existing transform for perspective.
    return;
  }

  // If we are switching from a perspective camera to an orthographic preset,
  // initialize the orthographic size to approximately match the current view.
  // This avoids surprising "empty" frames when the default ortho size is too
  // small or too large for the current focus/radius.
  if (auto cam_ref = camera_node_.GetCameraAs<scene::PerspectiveCamera>(); cam_ref) {
    const float fov_y = cam_ref->get().GetFieldOfView();
    if (std::isfinite(fov_y) && fov_y > 0.001f && std::isfinite(radius)) {
      const float half_h = std::tan(fov_y * 0.5f) * radius;
      if (std::isfinite(half_h)) {
        ortho_half_height_ = std::max(0.001f, half_h);
      }
    }
  }

  // Orthographic presets: ensure the camera component is orthographic.
  if (!camera_node_.GetCameraAs<scene::OrthographicCamera>()) {
    camera_node_.ReplaceCamera(std::make_unique<scene::OrthographicCamera>());
  }

  // Align transform to the preset.
  const glm::vec3 view_dir = NormalizeSafe(ResolvePresetForward(preset),
    glm::vec3(0.0f, 0.0f, 1.0f));
  const glm::vec3 new_position = focus + view_dir * radius;

  const glm::vec3 up = ResolvePresetUp(preset);
  const glm::quat rot = LookRotationFromPositionToTarget(new_position, focus, up);

  (void)transform.SetLocalPosition(new_position);
  (void)transform.SetLocalRotation(rot);

  // Mark initial orientation as set so OnPreRender doesn't re-orient using
  // the old (pre-preset) data.
  initial_orientation_set_ = true;
}

auto EditorView::GetViewId() const -> ViewId { return view_id_; }

auto EditorView::GetState() const -> ViewState { return state_; }

auto EditorView::IsVisible() const -> bool { return visible_; }

auto EditorView::GetCameraNode() const -> scene::SceneNode {
  return camera_node_;
}

auto EditorView::GetOrthoHalfHeight() const noexcept -> float {
  return ortho_half_height_;
}

auto EditorView::SetOrthoHalfHeight(const float half_height) noexcept -> void {
  if (!std::isfinite(half_height)) {
    return;
  }
  ortho_half_height_ = std::max(0.001f, half_height);
}

void EditorView::RegisterWithRenderer(engine::Renderer &renderer) {
  if (view_id_ == kInvalidViewId) {
    return;
  }

  // Store renderer for cleanup
  renderer_module_ = oxygen::observer_ptr<engine::Renderer>(&renderer);

  if (renderer_) {
    // Create resolver
    scene::SceneNode node = camera_node_;
    oxygen::engine::ViewResolver resolver =
        [node](const oxygen::engine::ViewContext &ctx) -> oxygen::ResolvedView {
      renderer::SceneCameraViewResolver scene_resolver(
          [node](const ViewId &) { return node; });
      return scene_resolver(ctx.id);
    };

    renderer_->RegisterWithEngine(renderer, view_id_, std::move(resolver));
  }
}

void EditorView::UnregisterFromRenderer(engine::Renderer &renderer) {
  if (renderer_) {
    renderer_->UnregisterFromEngine(renderer);
  }
}

void EditorView::SetRenderGraph(
    std::shared_ptr<engine::Renderer::RenderGraphFactory> factory) {
  render_graph_factory_ = std::move(factory);
}

} // namespace oxygen::interop::module
