//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma unmanaged

#include "pch.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/Constants.h>
#include <Oxygen/Scene/Light/DirectionalLight.h>
#include <Oxygen/Scene/Environment/LocalFogVolume.h>
#include <Oxygen/Scene/Light/PointLight.h>
#include <Oxygen/Scene/Light/SpotLight.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneNode.h>
#include <Oxygen/Vortex/SceneCameraViewResolver.h>

#include <EditorModule/EditorView.h>
#include <EditorModule/SceneHelperController.h>

namespace oxygen::interop::module {

  namespace {

    //! Exposes the renderer's camera resolution, so a camera's viewing volume
    //! is exactly what it renders.
    struct ViewResolver : vortex::FromNodeLookup {
      using FromNodeLookup::ResolveForNode;
    };

    auto ToPixel(const SubPixelPosition& position) -> glm::vec2
    {
      return { position.x, position.y };
    }

    auto ShowsIcons(const EditorView& view) -> bool
    {
      return view.GetConfig().render_options.show_icons;
    }

    //! Fills a camera node's viewing volume as it renders into `view`: an
    //! Auto-aspect camera takes the view's aspect, a Fixed one its own.
    void ResolveFrustum(scene::SceneNode camera_node, const EditorView& view,
      SceneHelperNode& helper)
    {
      const auto target = ViewPort {
        .top_left_x = 0.0F,
        .top_left_y = 0.0F,
        .width = std::max(view.GetWidth(), 1.0F),
        .height = std::max(view.GetHeight(), 1.0F),
        .min_depth = 0.0F,
        .max_depth = 1.0F,
      };
      const auto content = vortex::ResolveCameraContentRect(camera_node, target);
      const auto resolved = ViewResolver::ResolveForNode(camera_node, content);
      const auto view_matrix = resolved.ViewMatrix();
      const auto inverse = glm::inverse(resolved.ProjectionMatrix() * view_matrix);
      const auto inverse_view = glm::inverse(view_matrix);
      const auto eye = glm::vec3(inverse_view[3]);
      const auto forward = -glm::normalize(glm::vec3(inverse_view[2]));
      const auto corner = [&](const float x, const float y, const float z) {
        const auto p = inverse * glm::vec4(x, y, z, 1.0F);
        return glm::vec3(p) / p.w;
      };
      // Depth may run either way; the plane nearer the eye is the near one.
      auto near_z = 0.0F;
      auto far_z = 1.0F;
      if (glm::dot(corner(0.0F, 0.0F, 0.0F) - eye, forward)
        > glm::dot(corner(0.0F, 0.0F, 1.0F) - eye, forward)) {
        std::swap(near_z, far_z);
      }
      const std::array<glm::vec2, 4> ndc { {
        { -1.0F, -1.0F },
        { 1.0F, -1.0F },
        { 1.0F, 1.0F },
        { -1.0F, 1.0F },
      } };
      for (std::size_t i = 0; i < ndc.size(); ++i) {
        helper.frustum[i] = corner(ndc[i].x, ndc[i].y, near_z);
        helper.frustum[4 + i] = corner(ndc[i].x, ndc[i].y, far_z);
      }
      helper.position = eye;
      helper.direction = forward;
      helper.has_frustum = std::isfinite(helper.frustum[0].x)
        && std::isfinite(helper.frustum[7].x);
    }

  } // namespace

  void SceneHelperController::SetSettings(SceneHelperSettings settings)
  {
    std::lock_guard lock(mutex_);
    requested_ = std::move(settings);
    settings_changed_ = true;
  }

  void SceneHelperController::RequestCancel() noexcept
  {
    cancel_requested_.store(true, std::memory_order_relaxed);
  }

  void SceneHelperController::SetListener(Listener listener)
  {
    std::lock_guard lock(listener_mutex_);
    listener_ = std::move(listener);
  }

  void SceneHelperController::BeginFrame(scene::Scene* scene,
    const std::uint64_t scene_generation, const std::vector<UuidKey>& selected,
    const std::optional<UuidKey> active)
  {
    {
      std::lock_guard lock(mutex_);
      if (settings_changed_) {
        hidden_ = { requested_.hidden.begin(), requested_.hidden.end() };
        locked_ = { requested_.locked.begin(), requested_.locked.end() };
        display_scale_ = requested_.display_scale;
        settings_changed_ = false;
      }
    }
    if (scene != scene_ || scene_generation != scene_generation_) {
      scene_ = scene;
      scene_generation_ = scene_generation;
      cancel_requested_.store(true, std::memory_order_relaxed);
      SetHover(kInvalidViewId, std::nullopt, TriadAxis::kNone);
      pressed_triad_ = TriadAxis::kNone;
    }
    selected_ = { selected.begin(), selected.end() };
    active_ = active;
    CollectNodes();

    // A drag ends when its light is gone, deselected or locked.
    if (drag_.has_value()) {
      const auto& id = drag_->Node().id;
      const auto alive = std::ranges::any_of(nodes_,
        [&](const SceneHelperNode& node) { return node.id == id && node.editable; });
      if (!alive) {
        cancel_requested_.store(true, std::memory_order_relaxed);
      }
    }
    if (cancel_requested_.exchange(false, std::memory_order_relaxed)) {
      CancelDrag();
    }
  }

  void SceneHelperController::CollectNodes()
  {
    nodes_.clear();
    if (scene_ == nullptr) {
      return;
    }
    std::vector<std::pair<scene::SceneNode, bool>> pending;
    for (auto& root : scene_->GetRootNodes()) {
      pending.emplace_back(root, false);
    }
    while (!pending.empty()) {
      auto [node, parent_hidden] = pending.back();
      pending.pop_back();
      if (!node.IsAlive()) {
        continue;
      }
      // Only the editor's own nodes have helpers; its viewport cameras are
      // not authored.
      const auto id = NodeRegistry::ReverseLookup(node.GetHandle());
      const auto hidden = parent_hidden || (id.has_value() && hidden_.contains(*id));
      for (auto child = node.GetFirstChild(); child.has_value();
        child = child->GetNextSibling()) {
        pending.emplace_back(*child, hidden);
      }
      if (!id.has_value() || hidden) {
        continue;
      }

      auto helper = SceneHelperNode { .id = *id };
      if (const auto point = node.GetLightAs<scene::PointLight>()) {
        helper.kind = SceneHelperKind::kPointLight;
        helper.color = point->get().Common().color_rgb;
        helper.range = point->get().GetRange();
      } else if (const auto spot = node.GetLightAs<scene::SpotLight>()) {
        helper.kind = SceneHelperKind::kSpotLight;
        helper.color = spot->get().Common().color_rgb;
        helper.range = spot->get().GetRange();
        helper.inner_cone = spot->get().GetInnerConeAngleRadians();
        helper.outer_cone = spot->get().GetOuterConeAngleRadians();
      } else if (const auto sun = node.GetLightAs<scene::DirectionalLight>()) {
        helper.kind = SceneHelperKind::kDirectionalLight;
        helper.color = sun->get().Common().color_rgb;
      } else if (node.HasCamera()) {
        helper.kind = SceneHelperKind::kCamera;
      } else if (const auto impl = node.GetImpl(); impl
        && impl->get().HasComponent<scene::environment::LocalFogVolume>()) {
        helper.kind = SceneHelperKind::kLocalFogVolume;
        // The renderer's sphere: the base radius scaled by the largest axis.
        const auto scale = glm::abs(
          node.GetTransform().GetWorldScale().value_or(glm::vec3 { 1.0F }));
        helper.range
          = scene::environment::LocalFogVolume::kBaseVolumeRadiusMeters
          * std::max({ scale.x, scale.y, scale.z });
      } else {
        continue;
      }
      auto transform = node.GetTransform();
      const auto position = transform.GetWorldPosition();
      if (!position.has_value()) {
        continue;
      }
      helper.position = *position;
      const auto rotation = transform.GetWorldRotation().value_or(
        glm::quat(1.0F, 0.0F, 0.0F, 0.0F));
      const auto direction = rotation * space::move::Forward;
      if (glm::length(direction) > 1.0e-4F) {
        helper.direction = glm::normalize(direction);
      }
      helper.selected = selected_.contains(*id);
      helper.active = helper.selected && active_ == id;
      helper.editable = helper.selected && !locked_.contains(*id);
      nodes_.push_back(helper);
    }
  }

  auto SceneHelperController::NodesFor(EditorView& view) const
    -> std::vector<SceneHelperNode>
  {
    auto viewed = view.GetRenderCameraNode();
    const auto viewed_id = viewed.IsAlive()
      ? NodeRegistry::ReverseLookup(viewed.GetHandle())
      : std::nullopt;
    std::vector<SceneHelperNode> nodes;
    nodes.reserve(nodes_.size());
    for (const auto& node : nodes_) {
      // The camera a view looks through is its eye.
      if (viewed_id == node.id) {
        continue;
      }
      nodes.push_back(node);
      if (node.kind != SceneHelperKind::kCamera || !node.selected
        || scene_ == nullptr) {
        continue;
      }
      if (const auto handle = NodeRegistry::Lookup(node.id)) {
        if (auto camera_node = scene_->GetNode(*handle);
          camera_node.has_value() && camera_node->IsAlive()) {
          ResolveFrustum(*camera_node, view, nodes.back());
        }
      }
    }
    return nodes;
  }

  void SceneHelperController::ProcessInput(
    EditorView& view, AccumulatedInput& input, const bool gizmo_has_pointer)
  {
    if (view.IsInset()) {
      return;
    }
    const auto view_id = view.GetViewId();
    const auto dragging_here = [&] {
      return drag_.has_value() && drag_view_ == view_id;
    };

    // Alt belongs to navigation: an Alt press never grabs a helper.
    std::erase_if(input.key_events, [&](const EditorKeyEvent& event) {
      switch (event.key) {
      case platform::Key::kLeftAlt:
      case platform::Key::kRightAlt:
        alt_down_ = event.pressed;
        return false;
      case platform::Key::kEscape:
        if (event.pressed && dragging_here()) {
          CancelDrag();
          return true;
        }
        return false;
      default:
        return false;
      }
    });

    auto camera = std::optional<GizmoCamera> {};
    auto nodes = std::optional<std::vector<SceneHelperNode>> {};
    const auto camera_of_view = [&]() -> const std::optional<GizmoCamera>& {
      if (!camera.has_value()) {
        camera = TransformGizmoController::CameraOf(view);
      }
      return camera;
    };
    const auto nodes_of_view = [&]() -> const std::vector<SceneHelperNode>& {
      if (!nodes.has_value()) {
        nodes = NodesFor(view);
      }
      return *nodes;
    };

    std::erase_if(input.button_events, [&](const EditorButtonEvent& event) {
      const auto position = ToPixel(event.position);
      if (event.button == platform::MouseButton::kLeft) {
        if (event.pressed) {
          if (drag_.has_value() || gizmo_has_pointer || alt_down_
            || !camera_of_view().has_value()) {
            return false;
          }
          if (const auto axis
            = HitTestTriad(*camera_of_view(), position, display_scale_);
            axis != TriadAxis::kNone) {
            pressed_triad_ = axis;
            pressed_view_ = view_id;
            return true;
          }
          const auto hit = HitTestHelperHandles(
            *camera_of_view(), nodes_of_view(), position, display_scale_);
          if (!hit.has_value()) {
            return false;
          }
          auto drag = HelperDrag::Begin(*camera_of_view(),
            nodes_of_view()[hit->node], hit->handle, position);
          if (!drag.has_value()) {
            return false;
          }
          drag_ = std::move(drag);
          drag_view_ = view_id;
          SetHover(kInvalidViewId, std::nullopt, TriadAxis::kNone);
          Emit(MakeDragEvent(TransformGizmoEventKind::kBegin));
          return true;
        }
        if (pressed_triad_ != TriadAxis::kNone && pressed_view_ == view_id) {
          // A click selects the axis it was released on.
          const auto pressed = std::exchange(pressed_triad_, TriadAxis::kNone);
          if (camera_of_view().has_value()
            && HitTestTriad(*camera_of_view(), position, display_scale_)
              == pressed) {
            Emit(TransformGizmoEvent {
              .kind = TransformGizmoEventKind::kViewAxis,
              .view = view_id,
              .handle = static_cast<GizmoHandle>(pressed),
              .pointer_pixel = position,
              .helper = true,
            });
          }
          return true;
        }
        if (!dragging_here()) {
          return false;
        }
        if (camera_of_view().has_value()) {
          drag_->Update(*camera_of_view(), position);
        }
        Emit(MakeDragEvent(TransformGizmoEventKind::kCommit));
        drag_.reset();
        drag_view_ = kInvalidViewId;
        return true;
      }
      if (event.button == platform::MouseButton::kRight) {
        if (event.pressed && dragging_here()) {
          CancelDrag();
          swallow_right_release_ = true;
          return true;
        }
        if (!event.pressed && swallow_right_release_) {
          swallow_right_release_ = false;
          return true;
        }
      }
      return false;
    });

    const auto moved
      = input.mouse_delta.dx != 0.0F || input.mouse_delta.dy != 0.0F;
    const auto pointer = ToPixel(input.last_position);
    if (dragging_here()) {
      // The drag owns the pointer: navigation must not move the camera.
      input.mouse_delta = {};
      if (moved && camera_of_view().has_value()
        && drag_->Update(*camera_of_view(), pointer)) {
        Emit(MakeDragEvent(TransformGizmoEventKind::kUpdate));
      }
      return;
    }
    if (!moved || drag_.has_value()) {
      return;
    }
    auto handle = std::optional<HelperHandleHit> {};
    auto triad = TriadAxis::kNone;
    if (!gizmo_has_pointer && camera_of_view().has_value()) {
      triad = HitTestTriad(*camera_of_view(), pointer, display_scale_);
      if (triad == TriadAxis::kNone) {
        handle = HitTestHelperHandles(
          *camera_of_view(), nodes_of_view(), pointer, display_scale_);
      }
    }
    SetHover(view_id, handle, triad);
  }

  void SceneHelperController::BuildOverlay(
    EditorView& view, vortex::ViewOverlay& overlay)
  {
    if (view.IsInset()) {
      return;
    }
    const auto camera = TransformGizmoController::CameraOf(view);
    if (!camera.has_value()) {
      return;
    }
    const auto nodes = NodesFor(view);
    if (ShowsIcons(view)) {
      BuildHelperIcons(*camera, nodes, display_scale_, overlay);
    }
    const auto hovered_here = view.GetViewId() == hover_view_;
    auto visual = HelperVisual {
      .drag = drag_.has_value() ? &*drag_ : nullptr,
      .display_scale = display_scale_,
    };
    if (hovered_here && hovered_handle_.has_value()) {
      // Hover indices refer to the view's own node list, built the same way.
      visual.hovered = *hovered_handle_;
      visual.has_hover = visual.hovered.node < nodes.size();
    }
    BuildSelectedHelpers(*camera, nodes, visual, overlay);
    BuildTriad(*camera, hovered_here ? hovered_triad_ : TriadAxis::kNone,
      display_scale_, overlay);
  }

  auto SceneHelperController::PickIcons(EditorView& view,
    const vortex::ViewPickRect& rect) -> std::vector<HelperIconHit>
  {
    if (view.IsInset() || !ShowsIcons(view)) {
      return {};
    }
    const auto camera = TransformGizmoController::CameraOf(view);
    if (!camera.has_value()) {
      return {};
    }
    const auto min = glm::vec2 { static_cast<float>(rect.x),
      static_cast<float>(rect.y) };
    const auto max = min
      + glm::vec2 { static_cast<float>(rect.width),
          static_cast<float>(rect.height) };
    return PickHelperIcons(*camera, NodesFor(view), min, max, display_scale_);
  }

  void SceneHelperController::SetHover(const ViewId view,
    const std::optional<HelperHandleHit> handle, const TriadAxis triad)
  {
    const auto hovering = handle.has_value() || triad != TriadAxis::kNone;
    const auto was_hovering
      = hovered_handle_.has_value() || hovered_triad_ != TriadAxis::kNone;
    const auto previous_view = hover_view_;
    hover_view_ = view;
    hovered_handle_ = handle;
    hovered_triad_ = triad;
    if (was_hovering && previous_view != view) {
      Emit(TransformGizmoEvent {
        .kind = TransformGizmoEventKind::kHover,
        .view = previous_view,
        .hovering = false,
        .helper = true,
      });
    }
    if (view != kInvalidViewId
      && (hovering != was_hovering || previous_view != view)) {
      Emit(TransformGizmoEvent {
        .kind = TransformGizmoEventKind::kHover,
        .view = view,
        .hovering = hovering,
        .helper = true,
      });
    }
  }

  void SceneHelperController::CancelDrag()
  {
    if (!drag_.has_value()) {
      return;
    }
    auto event = MakeDragEvent(TransformGizmoEventKind::kCancel);
    event.value = drag_->StartValue();
    drag_.reset();
    drag_view_ = kInvalidViewId;
    Emit(std::move(event));
  }

  auto SceneHelperController::MakeDragEvent(
    const TransformGizmoEventKind kind) const -> TransformGizmoEvent
  {
    return TransformGizmoEvent {
      .kind = kind,
      .view = drag_view_,
      .tool = TransformTool::kSelect,
      .hovering = false,
      .pointer_pixel = drag_->Pointer(),
      .helper = true,
      .node = drag_->Node().id,
      .helper_handle = drag_->Handle(),
      .value = drag_->Value(),
    };
  }

  void SceneHelperController::Emit(TransformGizmoEvent event)
  {
    std::lock_guard lock(listener_mutex_);
    if (!listener_) {
      return;
    }
    try {
      listener_(event);
    } catch (...) {
      LOG_F(ERROR, "SceneHelperController: event listener failed");
    }
  }

} // namespace oxygen::interop::module
