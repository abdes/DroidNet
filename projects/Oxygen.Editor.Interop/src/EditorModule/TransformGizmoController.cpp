//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma unmanaged

#include "pch.h"

#include <cmath>
#include <utility>

#include <glm/gtc/quaternion.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneNode.h>
#include <Oxygen/Vortex/SceneCameraViewResolver.h>

#include <EditorModule/EditorView.h>
#include <EditorModule/TransformGizmoController.h>

namespace oxygen::interop::module {

  namespace {

    //! Exposes the renderer's camera resolution, so the gizmo projects
    //! exactly as the view renders.
    struct ViewResolver : vortex::FromNodeLookup {
      using FromNodeLookup::ResolveForNode;
    };

    auto ToPixel(const SubPixelPosition& position) -> glm::vec2
    {
      return { position.x, position.y };
    }

    auto SameTargets(const TransformGizmoSettings& a,
      const TransformGizmoSettings& b) -> bool
    {
      return a.tool == b.tool && a.targets == b.targets;
    }

  } // namespace

  void TransformGizmoController::SetSettings(TransformGizmoSettings settings)
  {
    std::lock_guard lock(mutex_);
    requested_ = std::move(settings);
    settings_changed_ = true;
  }

  void TransformGizmoController::RequestCancel() noexcept
  {
    cancel_requested_.store(true, std::memory_order_relaxed);
  }

  void TransformGizmoController::SetListener(Listener listener)
  {
    std::lock_guard lock(listener_mutex_);
    listener_ = std::move(listener);
  }

  void TransformGizmoController::BeginFrame(
    scene::Scene* scene, const std::uint64_t scene_generation)
  {
    {
      std::lock_guard lock(mutex_);
      if (settings_changed_) {
        if (drag_.has_value() && !SameTargets(settings_, requested_)) {
          cancel_requested_.store(true, std::memory_order_relaxed);
        }
        settings_ = requested_;
        settings_changed_ = false;
      }
    }
    if (scene != scene_ || scene_generation != scene_generation_) {
      scene_ = scene;
      scene_generation_ = scene_generation;
      cancel_requested_.store(true, std::memory_order_relaxed);
      SetHover(kInvalidViewId, GizmoHandle::kNone);
    }
    if (cancel_requested_.exchange(false, std::memory_order_relaxed)) {
      CancelDrag();
    }
    if (settings_.tool == TransformTool::kSelect || settings_.targets.empty()) {
      SetHover(kInvalidViewId, GizmoHandle::kNone);
    }
  }

  void TransformGizmoController::ProcessInput(
    EditorView& view, AccumulatedInput& input)
  {
    if (view.IsInset()) {
      return;
    }
    const auto view_id = view.GetViewId();
    const auto pointer = ToPixel(input.last_position);
    const auto dragging_here = [&] {
      return drag_.has_value() && drag_view_ == view_id;
    };

    // Modifiers are tracked across views: Ctrl inverts snapping and Alt at
    // the press duplicates. Escape cancels a drag in this view.
    auto control_changed = false;
    std::erase_if(input.key_events, [&](const EditorKeyEvent& event) {
      switch (event.key) {
      case platform::Key::kLeftControl:
      case platform::Key::kRightControl:
        control_changed = control_changed || control_down_ != event.pressed;
        control_down_ = event.pressed;
        return false;
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
    const auto camera_of_view = [&]() -> const std::optional<GizmoCamera>& {
      if (!camera.has_value()) {
        camera = CameraOf(view);
      }
      return camera;
    };

    std::erase_if(input.button_events, [&](const EditorButtonEvent& event) {
      const auto position = ToPixel(event.position);
      if (event.button == platform::MouseButton::kLeft) {
        if (event.pressed) {
          if (drag_.has_value() || settings_.tool == TransformTool::kSelect
            || !camera_of_view().has_value()) {
            return false;
          }
          auto resolution = Resolve();
          if (!resolution.has_value()) {
            return false;
          }
          const auto handle = HitTestGizmo(*camera_of_view(), settings_.tool,
            resolution->frame, position, settings_.display_scale);
          if (handle == GizmoHandle::kNone) {
            return false;
          }
          auto drag = GizmoDrag::Begin(*camera_of_view(), settings_.tool,
            settings_.space, resolution->frame, handle, position,
            settings_.display_scale, std::move(resolution->targets));
          if (!drag.has_value()) {
            return false;
          }
          drag_ = std::move(drag);
          drag_view_ = view_id;
          drag_duplicate_ = alt_down_;
          swallow_left_release_ = true;
          SetHover(kInvalidViewId, GizmoHandle::kNone);
          Emit(MakeDragEvent(TransformGizmoEventKind::kBegin, *camera_of_view()));
          return true;
        }
        if (!swallow_left_release_) {
          return false;
        }
        swallow_left_release_ = false;
        if (dragging_here() && camera_of_view().has_value()) {
          drag_->Update(*camera_of_view(), position, settings_.snap,
            control_down_);
          Emit(
            MakeDragEvent(TransformGizmoEventKind::kCommit, *camera_of_view()));
          drag_.reset();
          drag_view_ = kInvalidViewId;
        } else if (dragging_here()) {
          CancelDrag();
        }
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
    if (dragging_here()) {
      // The drag owns the pointer: navigation must not move the camera.
      input.mouse_delta = {};
      if ((moved || control_changed) && camera_of_view().has_value()
        && drag_->Update(
          *camera_of_view(), pointer, settings_.snap, control_down_)) {
        Emit(MakeDragEvent(TransformGizmoEventKind::kUpdate, *camera_of_view()));
      }
      return;
    }
    if (!moved || drag_.has_value()) {
      return;
    }
    auto handle = GizmoHandle::kNone;
    if (settings_.tool != TransformTool::kSelect
      && camera_of_view().has_value()) {
      if (const auto resolution = Resolve(); resolution.has_value()) {
        handle = HitTestGizmo(*camera_of_view(), settings_.tool,
          resolution->frame, pointer, settings_.display_scale);
      }
    }
    SetHover(view_id, handle);
  }

  auto TransformGizmoController::BuildOverlay(EditorView& view)
    -> std::shared_ptr<const vortex::ViewOverlay>
  {
    if (view.IsInset() || settings_.tool == TransformTool::kSelect) {
      return nullptr;
    }
    const auto camera = CameraOf(view);
    if (!camera.has_value()) {
      return nullptr;
    }
    auto visual = GizmoVisual {
      .tool = settings_.tool,
      .frame = {},
      .hovered = view.GetViewId() == hover_view_ ? hovered_ : GizmoHandle::kNone,
      .drag = drag_.has_value() ? &*drag_ : nullptr,
      .display_scale = settings_.display_scale,
    };
    if (visual.drag == nullptr) {
      const auto resolution = Resolve();
      if (!resolution.has_value()) {
        return nullptr;
      }
      visual.frame = resolution->frame;
    }
    auto overlay = std::make_shared<vortex::ViewOverlay>();
    BuildGizmoOverlay(*camera, visual, *overlay);
    if (overlay->IsEmpty()) {
      return nullptr;
    }
    return overlay;
  }

  auto TransformGizmoController::Resolve() const -> std::optional<Resolution>
  {
    if (scene_ == nullptr || settings_.targets.empty()) {
      return std::nullopt;
    }
    auto resolution = Resolution {};
    std::optional<std::size_t> active;
    for (const auto& id : settings_.targets) {
      const auto handle = NodeRegistry::Lookup(id);
      if (!handle.has_value()) {
        continue;
      }
      auto node = scene_->GetNode(*handle);
      if (!node.has_value() || !node->IsAlive()) {
        continue;
      }
      auto transform = node->GetTransform();
      const auto world = transform.GetWorldMatrix();
      if (!world.has_value()) {
        continue;
      }
      auto parent_world = glm::mat4(1.0F);
      if (auto parent = node->GetParent(); parent.has_value()) {
        parent_world
          = parent->GetTransform().GetWorldMatrix().value_or(glm::mat4(1.0F));
      }
      if (settings_.active == id) {
        active = resolution.targets.size();
      }
      resolution.targets.push_back(GizmoTargetStart {
        .id = id,
        .world = *world,
        .parent_world = parent_world,
        .local_position
        = transform.GetLocalPosition().value_or(glm::vec3(0.0F)),
        .local_rotation = transform.GetLocalRotation().value_or(
          glm::quat(1.0F, 0.0F, 0.0F, 0.0F)),
        .local_scale = transform.GetLocalScale().value_or(glm::vec3(1.0F)),
      });
    }
    if (resolution.targets.empty()) {
      return std::nullopt;
    }
    const auto& pivot_target
      = resolution.targets[active.value_or(resolution.targets.size() - 1U)];
    resolution.frame.pivot = glm::vec3(pivot_target.world[3]);
    if (settings_.tool == TransformTool::kScale
      || settings_.space == TransformSpace::kLocal) {
      // The node's world axes, without its scale.
      const auto x = glm::normalize(glm::vec3(pivot_target.world[0]));
      const auto y = glm::normalize(glm::vec3(pivot_target.world[1]));
      const auto z = glm::normalize(glm::cross(x, y));
      const auto orthogonal_y = glm::cross(z, x);
      const auto orientation = glm::quat_cast(glm::mat3(x, orthogonal_y, z));
      resolution.frame.orientation = glm::normalize(orientation);
      if (!std::isfinite(resolution.frame.orientation.w)) {
        resolution.frame.orientation = glm::quat(1.0F, 0.0F, 0.0F, 0.0F);
      }
    }
    return resolution;
  }

  auto TransformGizmoController::CameraOf(EditorView& view)
    -> std::optional<GizmoCamera>
  {
    auto camera_node = view.GetRenderCameraNode();
    const auto width = view.GetWidth();
    const auto height = view.GetHeight();
    if (!camera_node.IsAlive() || width <= 0.0F || height <= 0.0F) {
      return std::nullopt;
    }
    const auto target = ViewPort {
      .top_left_x = 0.0F,
      .top_left_y = 0.0F,
      .width = width,
      .height = height,
      .min_depth = 0.0F,
      .max_depth = 1.0F,
    };
    // A Fixed-aspect scene camera renders into a centred rectangle.
    const auto content = vortex::ResolveCameraContentRect(camera_node, target);
    const auto resolved = ViewResolver::ResolveForNode(camera_node, content);
    return GizmoCamera::Create(resolved.ViewMatrix(),
      resolved.ProjectionMatrix(),
      glm::vec2 { content.top_left_x, content.top_left_y },
      glm::vec2 { content.width, content.height });
  }

  void TransformGizmoController::SetHover(
    const ViewId view, const GizmoHandle handle)
  {
    const auto hovering = handle != GizmoHandle::kNone;
    const auto was_hovering = hovered_ != GizmoHandle::kNone;
    if (view == hover_view_ && handle == hovered_) {
      return;
    }
    const auto previous_view = hover_view_;
    hover_view_ = view;
    hovered_ = handle;
    if (was_hovering && previous_view != view) {
      Emit(TransformGizmoEvent {
        .kind = TransformGizmoEventKind::kHover,
        .view = previous_view,
        .hovering = false,
      });
    }
    if (view != kInvalidViewId
      && (hovering != was_hovering || previous_view != view)) {
      Emit(TransformGizmoEvent {
        .kind = TransformGizmoEventKind::kHover,
        .view = view,
        .hovering = hovering,
      });
    }
  }

  void TransformGizmoController::CancelDrag()
  {
    if (!drag_.has_value()) {
      return;
    }
    auto event = TransformGizmoEvent {
      .kind = TransformGizmoEventKind::kCancel,
      .view = drag_view_,
      .tool = drag_->Tool(),
      .handle = drag_->Handle(),
      .duplicate = drag_duplicate_,
    };
    drag_.reset();
    drag_view_ = kInvalidViewId;
    Emit(std::move(event));
  }

  auto TransformGizmoController::MakeDragEvent(
    const TransformGizmoEventKind kind, const GizmoCamera& camera) const
    -> TransformGizmoEvent
  {
    return TransformGizmoEvent {
      .kind = kind,
      .view = drag_view_,
      .tool = drag_->Tool(),
      .handle = drag_->Handle(),
      .duplicate = drag_duplicate_,
      .hovering = false,
      .representable = drag_->IsRepresentable(),
      .targets = drag_->Results(),
      .readout = drag_->Readout(),
      .pivot_pixel = camera.Project(drag_->Frame().pivot),
      .pointer_pixel = drag_->Pointer(),
    };
  }

  void TransformGizmoController::Emit(TransformGizmoEvent event)
  {
    std::lock_guard lock(listener_mutex_);
    if (!listener_) {
      return;
    }
    try {
      listener_(event);
    } catch (...) {
      LOG_F(ERROR, "TransformGizmoController: event listener failed");
    }
  }

} // namespace oxygen::interop::module
