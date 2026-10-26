//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "pch.h"

#include <Oxygen/Scene/Scene.h>

#include <Commands/CreateViewCommand.h>
#include <Commands/DestroyViewCommand.h>
#include <Commands/FrameViewCommand.h>
#include <Commands/HideViewCommand.h>
#include <Commands/QueryViewCameraPoseCommand.h>
#include <Commands/QueryViewEditorCameraCommand.h>
#include <Commands/SetViewCameraControlModeCommand.h>
#include <Commands/SetViewCameraMovementSpeedCommand.h>
#include <Commands/SetViewCameraPresetCommand.h>
#include <Commands/SetViewCameraSettingsCommand.h>
#include <Commands/SetViewRenderOptionsCommand.h>
#include <Commands/SetViewSceneCameraCommand.h>
#include <Commands/SetViewScenePilotCommand.h>
#include <Commands/ShowViewCommand.h>
#include <EditorModule/EditorCommand.h>
#include <EditorModule/EditorView.h>
#include <EditorModule/ViewFraming.h>
#include <EditorModule/ViewManager.h>

namespace oxygen::interop::module {

  void SetViewScenePilotCommand::Execute(CommandContext& /*context*/) {
    if (view_manager_ == nullptr) {
      return;
    }
    if (auto* view = view_manager_->GetView(view_id_); view != nullptr) {
      view->SetPilotSceneCamera(pilot_);
    }
  }

  QueryViewCameraPoseCommand::~QueryViewCameraPoseCommand() {
    // A command dropped without running still answers its caller.
    Complete(std::nullopt);
  }

  void QueryViewCameraPoseCommand::Execute(CommandContext& context) {
    if (view_manager_ == nullptr || !context.Scene) {
      Complete(std::nullopt);
      return;
    }
    auto* view = view_manager_->GetView(view_id_);
    const auto handle = NodeRegistry::Lookup(node_id_);
    if (view == nullptr || !handle.has_value()) {
      Complete(std::nullopt);
      return;
    }
    auto node = context.Scene->GetNode(*handle);
    if (!node.has_value() || !node->IsAlive()) {
      Complete(std::nullopt);
      return;
    }
    Complete(view->ResolveEditorCameraPose(*node));
  }

  void QueryViewCameraPoseCommand::Complete(
    std::optional<EditorCameraPose> pose) {
    if (!callback_) {
      return;
    }
    auto callback = std::move(callback_);
    callback_ = nullptr;
    try {
      callback(std::move(pose));
    } catch (...) {
      LOG_F(ERROR, "QueryViewCameraPoseCommand: callback failed");
    }
  }

  QueryViewEditorCameraCommand::~QueryViewEditorCameraCommand() {
    // A command dropped without running still answers its caller.
    Complete(std::nullopt);
  }

  void QueryViewEditorCameraCommand::Execute(CommandContext& /*context*/) {
    auto* view
      = view_manager_ != nullptr ? view_manager_->GetView(view_id_) : nullptr;
    Complete(view != nullptr ? view->GetEditorCameraState() : std::nullopt);
  }

  void QueryViewEditorCameraCommand::Complete(
    std::optional<EditorCameraState> state) {
    if (!callback_) {
      return;
    }
    auto callback = std::move(callback_);
    callback_ = nullptr;
    try {
      callback(std::move(state));
    } catch (...) {
      LOG_F(ERROR, "QueryViewEditorCameraCommand: callback failed");
    }
  }

  void CreateViewCommand::Execute(CommandContext& context) {
    DLOG_SCOPE_FUNCTION(4);

    if (view_manager_ == nullptr) {
      DLOG_F(WARNING, "CreateViewCommand: ViewManager null");
      if (cb_)
        cb_(false, ViewId{});
      return;
    }

    if (!context.Scene) {
      DLOG_F(WARNING, "CreateViewCommand: scene missing during frame start");
      if (cb_)
        cb_(false, ViewId{});
      return;
    }

    try {
      view_manager_->CreateViewNow(std::move(cfg_), std::move(cb_));
    }
    catch (const std::exception& ex) {
      LOG_F(ERROR, "CreateViewCommand failed: {}", ex.what());
      if (cb_)
        cb_(false, ViewId{});
    }
  }

  void DestroyViewCommand::Execute(CommandContext& /*context*/) {
    DLOG_SCOPE_FUNCTION(4);

    if (view_manager_ == nullptr) {
      DLOG_F(WARNING, "DestroyViewCommand: ViewManager null");
      return;
    }

    view_manager_->DestroyView(view_id_);
    LOG_F(INFO, "DestroyViewCommand: view {} destroyed", view_id_.get());
  }

  void ShowViewCommand::Execute(CommandContext& /*context*/) {
    DLOG_SCOPE_FUNCTION(4);

    if (view_manager_ == nullptr) {
      DLOG_F(WARNING, "ShowViewCommand: ViewManager null");
      return;
    }

    auto* view = view_manager_->GetView(view_id_);
    if (!view) {
      DLOG_F(WARNING, "ShowViewCommand: invalid view id {}", view_id_.get());
      return;
    }

    // Make visible and ensure registration so rendering resumes.
    view->Show();
    view_manager_->RegisterView(view_id_);
    LOG_F(INFO, "ShowViewCommand: view {} now visible and registered",
      view_id_.get());
  }

  void HideViewCommand::Execute(CommandContext& /*context*/) {
    DLOG_SCOPE_FUNCTION(4);

    if (view_manager_ == nullptr) {
      DLOG_F(WARNING, "HideViewCommand: ViewManager null");
      return;
    }

    auto* view = view_manager_->GetView(view_id_);
    if (!view) {
      DLOG_F(WARNING, "HideViewCommand: invalid view id {}", view_id_.get());
      return;
    }

    // Mark hidden and unregister from FrameContext so it stops rendering.
    view->Hide();
    view_manager_->UnregisterView(view_id_);
    LOG_F(INFO, "HideViewCommand: view {} hidden and unregistered",
      view_id_.get());
  }

  FrameViewCommand::~FrameViewCommand() {
    // A command dropped without running still answers its caller.
    Complete(EditorFramingOutcome::kNoView);
  }

  void FrameViewCommand::Execute(CommandContext& context) {
    auto* view
      = view_manager_ != nullptr ? view_manager_->GetView(view_id_) : nullptr;
    if (view == nullptr || context.Scene == nullptr) {
      Complete(EditorFramingOutcome::kNoView);
      return;
    }

    std::optional<FrameSphere> bounds;
    if (nodes_.empty()) {
      bounds = ResolveSceneFrameSphere(*context.Scene);
    } else {
      std::vector<scene::NodeHandle> handles;
      handles.reserve(nodes_.size());
      for (const auto& reference : nodes_) {
        if (const auto handle
          = NodeRegistry::Lookup(*context.Scene, reference)) {
          handles.push_back(*handle);
        }
      }
      bounds = ResolveNodesFrameSphere(*context.Scene, handles);
      // Finite transforms are an authoring invariant: no bounds means none
      // of the nodes exist.
      if (!bounds.has_value()) {
        Complete(EditorFramingOutcome::kNothingToFrame);
        return;
      }
    }
    if (!bounds.has_value()) {
      LOG_F(WARNING, "FrameViewCommand: view '{}' has no finite bounds to frame",
        view->GetName());
      Complete(EditorFramingOutcome::kInvalidBounds);
      return;
    }
    Complete(view->BeginFraming(bounds->center, bounds->radius));
  }

  void FrameViewCommand::Complete(const EditorFramingOutcome outcome) {
    if (!callback_) {
      return;
    }
    auto callback = std::move(callback_);
    callback_ = nullptr;
    try {
      callback(outcome);
    } catch (...) {
      LOG_F(ERROR, "FrameViewCommand: callback failed");
    }
  }

  void SetViewCameraControlModeCommand::Execute(CommandContext& /*context*/) {
    DLOG_SCOPE_FUNCTION(4);

    if (view_manager_ == nullptr) {
      DLOG_F(WARNING, "SetViewCameraControlModeCommand: ViewManager null");
      return;
    }

    view_manager_->SetCameraControlMode(view_id_, mode_);
  }

  void SetViewCameraMovementSpeedCommand::Execute(CommandContext& /*context*/) {
    DLOG_SCOPE_FUNCTION(4);

    if (view_manager_ == nullptr) {
      DLOG_F(WARNING, "SetViewCameraMovementSpeedCommand: ViewManager null");
      return;
    }

    view_manager_->SetCameraMovementSpeed(
      view_id_, speed_units_per_second_);
  }

  void SetViewCameraPresetCommand::Execute(CommandContext& /*context*/) {
    DLOG_SCOPE_FUNCTION(4);

    if (view_manager_ == nullptr) {
      DLOG_F(WARNING, "SetViewCameraPresetCommand: ViewManager null");
      return;
    }

    view_manager_->SetCameraViewPreset(view_id_, preset_);
  }

  void SetViewCameraSettingsCommand::Execute(CommandContext& /*context*/) {
    DLOG_SCOPE_FUNCTION(4);

    if (view_manager_ == nullptr) {
      DLOG_F(WARNING, "SetViewCameraSettingsCommand: ViewManager null");
      return;
    }

    view_manager_->SetCameraViewSettings(view_id_, field_of_view_y_radians_,
      near_plane_, far_plane_);
  }

  void SetViewRenderOptionsCommand::Execute(CommandContext& /*context*/) {
    DLOG_SCOPE_FUNCTION(4);

    if (view_manager_ == nullptr) {
      DLOG_F(WARNING, "SetViewRenderOptionsCommand: ViewManager null");
      return;
    }

    view_manager_->SetRenderOptions(view_id_, options_);
  }

  void SetViewSceneCameraCommand::Execute(CommandContext& /*context*/) {
    DLOG_SCOPE_FUNCTION(4);

    if (view_manager_ == nullptr) {
      DLOG_F(WARNING, "SetViewSceneCameraCommand: ViewManager null");
      return;
    }

    view_manager_->SetSceneCamera(view_id_, camera_node_id_);
  }

} // namespace oxygen::interop::module
