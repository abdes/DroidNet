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

#include <glm/common.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include "EditorModule/ViewRenderer.h"

#include <Oxygen/Core/Constants.h>
#include <Oxygen/Scene/Camera/Orthographic.h>
#include <Oxygen/Scene/Camera/Perspective.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneTraversal.h>
#include <Oxygen/Scene/Types/Traversal.h>

namespace oxygen::interop::module {

namespace {

constexpr float kDefaultPerspectiveFovY = 60.0F * oxygen::math::DegToRad;
constexpr float kDefaultPerspectiveNearPlane = 0.05F;
constexpr float kDefaultPerspectiveFarPlane = 1000.0F;
constexpr float kDefaultSceneRadius = 12.0F;
constexpr float kFrameMargin = 1.35F;
constexpr glm::vec3 kDefaultFocusPoint { 0.0F, 0.0F, 0.0F };
constexpr glm::vec3 kDefaultPerspectiveViewDirection { 0.75F, -1.35F, 0.65F };

struct SceneFrameBounds {
  glm::vec3 center { kDefaultFocusPoint };
  float radius { kDefaultSceneRadius };
  std::size_t renderable_count { 0 };
};

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

[[nodiscard]] auto IsUsableSphere(const Vec4& sphere) noexcept -> bool {
  return std::isfinite(sphere.x) && std::isfinite(sphere.y)
    && std::isfinite(sphere.z) && std::isfinite(sphere.w)
    && sphere.w > 0.0F;
}

[[nodiscard]] auto ResolveSceneFrameBounds(scene::Scene& scene) noexcept
  -> std::optional<SceneFrameBounds> {
  scene.Update();

  glm::vec3 min_bounds { std::numeric_limits<float>::max() };
  glm::vec3 max_bounds { std::numeric_limits<float>::lowest() };
  std::size_t renderable_count = 0;
  const std::weak_ptr<const scene::Scene> scene_weak { scene.weak_from_this() };

  (void)scene.Traverse().Traverse(
    [&](const scene::MutableVisitedNode& visited,
      [[maybe_unused]] const bool dry_run) -> scene::VisitResult {
      DCHECK_F(!dry_run, "pre-order scene framing traversal should not dry-run");
      scene::SceneNode node { scene_weak, visited.handle };
      const auto renderable = node.GetRenderable();
      if (!renderable.HasGeometry()) {
        return scene::VisitResult::kContinue;
      }

      const auto sphere = renderable.GetWorldBoundingSphere();
      if (!IsUsableSphere(sphere)) {
        return scene::VisitResult::kContinue;
      }

      const glm::vec3 center { sphere.x, sphere.y, sphere.z };
      const glm::vec3 extent { sphere.w };
      min_bounds = glm::min(min_bounds, center - extent);
      max_bounds = glm::max(max_bounds, center + extent);
      ++renderable_count;
      return scene::VisitResult::kContinue;
    });

  if (renderable_count == 0 || !IsFinite(min_bounds) || !IsFinite(max_bounds)) {
    return std::nullopt;
  }

  const glm::vec3 center = 0.5F * (min_bounds + max_bounds);
  const float radius = 0.5F * glm::length(max_bounds - min_bounds);
  if (!IsFinite(center) || !std::isfinite(radius) || radius <= 0.0F) {
    return std::nullopt;
  }

  return SceneFrameBounds {
    .center = center,
    .radius = std::max(radius, 1.0F),
    .renderable_count = renderable_count,
  };
}

[[nodiscard]] auto ResolveDefaultCameraDistance(const float radius) noexcept
  -> float {
  const float half_fov = kDefaultPerspectiveFovY * 0.5F;
  const float fit_distance = radius / std::sin(half_fov);
  return std::max(8.0F, fit_distance * kFrameMargin);
}

auto ApplyPerspectiveProjectionDefaults(scene::PerspectiveCamera& camera,
  const float aspect,
  const ViewPort& viewport,
  const float scene_radius,
  const float camera_distance) noexcept -> void {
  camera.SetFieldOfView(kDefaultPerspectiveFovY);
  camera.SetAspectRatio(std::max(0.001F, aspect));
  camera.SetNearPlane(kDefaultPerspectiveNearPlane);
  camera.SetFarPlane(std::max(kDefaultPerspectiveFarPlane,
    camera_distance + scene_radius * 4.0F));
  camera.SetViewport(viewport);
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

void EditorView::RetargetScene(scene::Scene& scene) {
  if (state_ == ViewState::kDestroyed) {
    return;
  }

  scene_ = scene.weak_from_this();
  camera_node_ = {};
  initial_orientation_set_ = false;
  initial_scene_frame_applied_ = false;
  if (state_ == ViewState::kReleasing) {
    state_ = ViewState::kReady;
  }

  LOG_F(INFO, "EditorView '{}' retargeted to replacement scene.", config_.name);
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

  LOG_F(2, "EditorView '{}' OnSceneMutation: updated viewport camera for {}x{}",
    config_.name, width_, height_);
}

auto EditorView::OnPreRender(vortex::Renderer &renderer) -> oxygen::co::Co<> {
  LOG_SCOPE_F(4, "EditorView::OnPreRender");
  if (state_ != ViewState::kReady || !visible_) {
    co_return;
  }

  if (width_ <= 0.0f || height_ <= 0.0f) {
    co_return;
  }

  if (auto gfx = graphics_.lock()) {
    EnsureRenderTarget(*gfx);
  }

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

  // Runtime view publication is owned by EditorModule::OnPublishViews. Keeping
  // PreRender free of FrameContext view-output mutation prevents the legacy
  // editor intent view from competing with the Vortex-published scene view.
  (void)renderer;

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

void EditorView::EnsureRenderTarget(Graphics& graphics) {
  ResizeIfNeeded(graphics);
}

void EditorView::ResizeIfNeeded() {
  if (auto gfx = graphics_.lock()) {
    ResizeIfNeeded(*gfx);
  }
}

void EditorView::ResizeIfNeeded(Graphics& gfx) {
  bool need_resize = false;
  if (!color_texture_ ||
      static_cast<float>(color_texture_->GetDescriptor().width) != width_ ||
      static_cast<float>(color_texture_->GetDescriptor().height) != height_) {
    need_resize = true;
  }

  if (need_resize) {
      auto &reclaimer = gfx.GetDeferredReclaimer();
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
          oxygen::graphics::ResourceStates::kCommon;

      // Assign a helpful debug name so runtime diagnostics show which
      // EditorView owns this texture (helps avoid generic "Texture" labels).
      std::string dbg_base = config_.name.empty()
                                 ? std::string("EditorView:Unnamed")
                     : fmt::format(fmt::runtime("EditorView:{}"),
                       config_.name);
      color_desc.debug_name = dbg_base + ".Color";
      color_texture_ = gfx.CreateTexture(color_desc);

      graphics::TextureDesc depth_desc = color_desc;
      depth_desc.format = oxygen::Format::kDepth32;
      depth_desc.is_shader_resource = false;
      depth_desc.use_clear_value = true;
      depth_desc.clear_value = {1.0f, 0.0f, 0.0f, 0.0f};
      depth_desc.initial_state = oxygen::graphics::ResourceStates::kDepthWrite;

      depth_desc.debug_name = dbg_base + ".Depth";
      depth_texture_ = gfx.CreateTexture(depth_desc);

      graphics::FramebufferDesc fb_desc;
      fb_desc.AddColorAttachment(color_texture_);
      fb_desc.SetDepthAttachment(depth_texture_);

      framebuffer_ = gfx.CreateFramebuffer(fb_desc);

      LOG_F(INFO,
            "EditorView '{}' resized resources to {}x{} "
            "(color.use_clear_value={}, color.clear_value=({}, {}, {}, {}))",
            config_.name, width_, height_, color_desc.use_clear_value,
            color_desc.clear_value.r, color_desc.clear_value.g,
            color_desc.clear_value.b, color_desc.clear_value.a);
  }
}

void EditorView::CreateCamera(scene::Scene &scene) {
  // Create camera node in the scene
  camera_node_ = scene.CreateNode(config_.name + "_Camera");

  auto camera = std::make_unique<scene::PerspectiveCamera>();
  const float aspect =
    (width_ > 0.0F && height_ > 0.0F) ? (width_ / height_) : 1.0F;
  const ViewPort vp{
      .top_left_x = 0.0f,
      .top_left_y = 0.0f,
      .width = width_,
      .height = height_,
      .min_depth = 0.0f,
      .max_depth = 1.0f,
  };
  const float default_distance = ResolveDefaultCameraDistance(kDefaultSceneRadius);
  ApplyPerspectiveProjectionDefaults(
    *camera, aspect, vp, kDefaultSceneRadius, default_distance);
  camera_node_.AttachCamera(std::move(camera));

  focus_point_ = kDefaultFocusPoint;
  const glm::vec3 view_direction = NormalizeSafe(
    kDefaultPerspectiveViewDirection, glm::vec3(1.0F, -1.0F, 0.5F));
  const glm::vec3 position = focus_point_ + view_direction * default_distance;
  auto transform = camera_node_.GetTransform();
  (void)transform.SetLocalPosition(position);
  (void)transform.SetLocalRotation(LookRotationFromPositionToTarget(
    position, focus_point_, oxygen::space::move::Up));
  initial_orientation_set_ = true;

  LOG_F(INFO,
    "EditorView '{}' created camera node pos=({}, {}, {}) focus=({}, {}, {}) "
    "fov_y_deg={} near={} far={}",
    config_.name, position.x, position.y, position.z, focus_point_.x,
    focus_point_.y, focus_point_.z,
    kDefaultPerspectiveFovY / oxygen::math::DegToRad,
    kDefaultPerspectiveNearPlane, kDefaultPerspectiveFarPlane);
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
    float scene_radius = kDefaultSceneRadius;
    float camera_distance = ResolveDefaultCameraDistance(scene_radius);

    if (!initial_scene_frame_applied_) {
      if (auto scn = scene_.lock()) {
        if (const auto frame_bounds = ResolveSceneFrameBounds(*scn)) {
          focus_point_ = frame_bounds->center;
          scene_radius = frame_bounds->radius;
          camera_distance = ResolveDefaultCameraDistance(scene_radius);

          const glm::vec3 view_direction = NormalizeSafe(
            kDefaultPerspectiveViewDirection, glm::vec3(1.0F, -1.0F, 0.5F));
          const glm::vec3 position = focus_point_ + view_direction * camera_distance;
          auto transform = camera_node_.GetTransform();
          (void)transform.SetLocalPosition(position);
          (void)transform.SetLocalRotation(LookRotationFromPositionToTarget(
            position, focus_point_, oxygen::space::move::Up));

          ortho_half_height_ = std::max(1.0F, scene_radius * kFrameMargin);
          initial_orientation_set_ = true;
          initial_scene_frame_applied_ = true;

          LOG_F(INFO,
            "EditorView '{}' framed scene: renderables={} center=({}, {}, {}) "
            "radius={} camera_pos=({}, {}, {}) distance={}",
            config_.name, frame_bounds->renderable_count, focus_point_.x,
            focus_point_.y, focus_point_.z, scene_radius, position.x,
            position.y, position.z, camera_distance);
        }
      }
    } else if (auto transform = camera_node_.GetTransform(); true) {
      const glm::vec3 position =
        transform.GetLocalPosition().value_or(focus_point_);
      const glm::vec3 offset = position - focus_point_;
      const float distance = std::sqrt(glm::dot(offset, offset));
      if (std::isfinite(distance)) {
        camera_distance = std::max(camera_distance, distance);
      }
      scene_radius = std::max(scene_radius, ortho_half_height_);
    }

    ApplyPerspectiveProjectionDefaults(
      cam, aspect, vp, scene_radius, camera_distance);
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

void EditorView::RegisterWithRenderer(vortex::Renderer &renderer) {
  if (view_id_ == kInvalidViewId) {
    return;
  }

  // Store renderer for cleanup
  renderer_module_ = oxygen::observer_ptr<vortex::Renderer>(&renderer);

  if (renderer_) {
    scene::SceneNode node = camera_node_;
    std::optional<ViewPort> viewport_override;
    if (current_context_) {
      viewport_override =
        current_context_->frame_context.GetViewContext(view_id_).view.viewport;
    }
    vortex::SceneCameraViewResolver scene_resolver(
        [node](const ViewId &) { return node; }, viewport_override);
    auto resolved_view = scene_resolver(view_id_);

    renderer_->RegisterWithEngine(renderer, view_id_, std::move(resolved_view));
  }
}

void EditorView::UnregisterFromRenderer(vortex::Renderer &renderer) {
  if (renderer_) {
    renderer_->UnregisterFromEngine(renderer);
  }
}

void EditorView::SetRenderGraph(
    std::shared_ptr<vortex::Renderer::RenderGraphFactory> factory) {
  render_graph_factory_ = std::move(factory);
}

} // namespace oxygen::interop::module
