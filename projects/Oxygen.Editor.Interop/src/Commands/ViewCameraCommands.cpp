//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma unmanaged

#include "pch.h"

#include <Commands/QueryViewCameraPoseCommand.h>
#include <Commands/SetViewScenePilotCommand.h>
#include <EditorModule/ViewManager.h>

#include <Oxygen/Scene/Scene.h>

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

} // namespace oxygen::interop::module
