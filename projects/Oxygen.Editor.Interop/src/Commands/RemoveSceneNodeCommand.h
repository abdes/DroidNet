//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause.
//===----------------------------------------------------------------------===//

#pragma once
#pragma managed(push, off)

#include <string>

#include "EditorModule/EditorCommand.h"

namespace oxygen::interop::module::commands {

class RemoveSceneNodeCommand : public EditorCommand {
public:
  explicit RemoveSceneNodeCommand(oxygen::scene::NodeHandle node)
      : node_(node) {}

  void Execute(CommandContext &context) override {
    if (!context.Scene)
      return;

    auto sceneNode = context.Scene->GetNode(node_);
    if (sceneNode && sceneNode->IsAlive()) {
      if (sceneNode->HasChildren()) {
        context.Scene->DestroyNodeHierarchy(*sceneNode);
      } else {
        context.Scene->DestroyNode(*sceneNode);
      }
    }
  }

private:
  oxygen::scene::NodeHandle node_;
};

} // namespace oxygen::interop::module::commands

#pragma managed(pop)
