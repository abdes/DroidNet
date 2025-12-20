//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause.
//===----------------------------------------------------------------------===//

#pragma once
#pragma managed(push, off)

#include <Oxygen/Core/PhaseRegistry.h>
#include <Oxygen/Scene/Types/NodeHandle.h>

#include <EditorModule/EditorCommand.h>

namespace oxygen::interop::module {

  class RemoveSceneNodeCommand : public EditorCommand {
  public:
    explicit RemoveSceneNodeCommand(oxygen::scene::NodeHandle node);

    void Execute(CommandContext& context) override;

  private:
    oxygen::scene::NodeHandle node_;
  };

  inline RemoveSceneNodeCommand::RemoveSceneNodeCommand(
    oxygen::scene::NodeHandle node)
    : EditorCommand(oxygen::core::PhaseId::kSceneMutation), node_(node) {
  }

  inline void RemoveSceneNodeCommand::Execute(CommandContext& context) {
    if (!context.Scene)
      return;

    auto sceneNode = context.Scene->GetNode(node_);
    if (sceneNode && sceneNode->IsAlive()) {
      if (sceneNode->HasChildren()) {
        context.Scene->DestroyNodeHierarchy(*sceneNode);
        return;
      }
      context.Scene->DestroyNode(*sceneNode);
    }
  }

} // namespace oxygen::interop::module

#pragma managed(pop)
