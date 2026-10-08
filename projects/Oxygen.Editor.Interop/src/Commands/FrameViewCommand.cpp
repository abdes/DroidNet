//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma unmanaged

#include "pch.h"

#include <Oxygen/Scene/Scene.h>

#include <Commands/FrameViewCommand.h>
#include <EditorModule/ViewFraming.h>
#include <EditorModule/ViewManager.h>

namespace oxygen::interop::module {

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
      for (const auto& id : nodes_) {
        if (const auto handle = NodeRegistry::Lookup(id)) {
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

} // namespace oxygen::interop::module
