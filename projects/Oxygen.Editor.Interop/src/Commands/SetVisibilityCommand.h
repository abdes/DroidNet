//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause.
//===----------------------------------------------------------------------===//

#pragma once
#pragma managed(push, off)

#include "EditorModule/EditorCommand.h"

namespace oxygen::interop::module::commands {

class SetVisibilityCommand : public EditorCommand {
public:
  SetVisibilityCommand(oxygen::scene::NodeHandle node, bool visible)
      : node_(node), visible_(visible) {}

  void Execute(CommandContext &context) override {
    if (!context.Scene)
      return;

    auto sceneNode = context.Scene->GetNode(node_);
    if (sceneNode && sceneNode->IsAlive()) {
      auto flags = sceneNode->GetFlags();
      if (flags) {
        flags->get().SetLocalValue(oxygen::scene::SceneNodeFlags::kVisible,
                                   visible_);
      }
    }
  }

private:
  oxygen::scene::NodeHandle node_;
  bool visible_;
};

} // namespace oxygen::interop::module::commands

#pragma managed(pop)
