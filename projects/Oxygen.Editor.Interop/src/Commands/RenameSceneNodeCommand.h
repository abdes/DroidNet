//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause.
//===----------------------------------------------------------------------===//

#pragma once
#pragma managed(push, off)

#include <string>
#include <utility>

#include <Oxygen/Scene/SceneNode.h>
#include <Oxygen/Scene/Types/NodeHandle.h>

#include "EditorModule/EditorCommand.h"

namespace oxygen::interop::module::commands{

class RenameSceneNodeCommand : public EditorCommand {
public:
  RenameSceneNodeCommand(oxygen::scene::NodeHandle node, std::string newName);

  void Execute(CommandContext& context) override;

private:
  oxygen::scene::NodeHandle node_;
  std::string newName_;
};

inline RenameSceneNodeCommand::RenameSceneNodeCommand(
    oxygen::scene::NodeHandle node, std::string newName)
    : node_(node), newName_(std::move(newName)) {
}

inline void RenameSceneNodeCommand::Execute(CommandContext& context) {
  if (!context.Scene)
    return;

  auto sceneNode = context.Scene->GetNode(node_);
  if (sceneNode && sceneNode->IsAlive()) {
    (void)sceneNode->SetName(newName_);
  }
}

} // namespace oxygen::interop::module::commands

#pragma managed(pop)
