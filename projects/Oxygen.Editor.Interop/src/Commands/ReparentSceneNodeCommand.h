//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause.
//===----------------------------------------------------------------------===//

#pragma once
#pragma managed(push, off)

#include <Oxygen/Core/PhaseRegistry.h>
#include <Oxygen/Scene/SceneNode.h>
#include <Oxygen/Scene/Types/NodeHandle.h>

#include <EditorModule/EditorCommand.h>

namespace oxygen::interop::module {

  class ReparentSceneNodeCommand : public EditorCommand {
  public:
    ReparentSceneNodeCommand(oxygen::scene::NodeHandle child,
      oxygen::scene::NodeHandle parent,
      bool preserve_world_transform = true);

    void Execute(CommandContext& context) override;

  private:
    oxygen::scene::NodeHandle child_;
    oxygen::scene::NodeHandle parent_;
    bool preserve_;
  };

  inline ReparentSceneNodeCommand::ReparentSceneNodeCommand(
    oxygen::scene::NodeHandle child, oxygen::scene::NodeHandle parent,
    bool preserve_world_transform)
    : EditorCommand(oxygen::core::PhaseId::kSceneMutation), child_(child),
    parent_(parent), preserve_(preserve_world_transform) {
  }

  inline void ReparentSceneNodeCommand::Execute(CommandContext& context) {
    if (!context.Scene) {
      return;
    }

    auto sceneNode = context.Scene->GetNode(child_);
    if (!sceneNode || !sceneNode->IsAlive()) {
      return;
    }

    if (parent_.IsValid()) {
      auto parentNode = context.Scene->GetNode(parent_);
      if (parentNode && parentNode->IsAlive()) {
        (void)context.Scene->ReparentNode(*sceneNode, *parentNode, preserve_);
      }
      return;
    }

    // Reparent to root
    (void)context.Scene->MakeNodeRoot(*sceneNode, preserve_);
  }

} // namespace oxygen::interop::module

#pragma managed(pop)
