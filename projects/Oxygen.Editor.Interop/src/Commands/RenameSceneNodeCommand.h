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

class RenameSceneNodeCommand : public EditorCommand {
public:
  RenameSceneNodeCommand(oxygen::scene::NodeHandle node, std::string newName)
      : node_(node), newName_(std::move(newName)) {}

  void Execute(CommandContext &context) override {
    if (!context.Scene)
      return;

    auto sceneNode = context.Scene->GetNode(node_);
    if (sceneNode && sceneNode->IsAlive()) {
      [[maybe_unused]] auto result = sceneNode->SetName(newName_);
    }
  }

private:
  oxygen::scene::NodeHandle node_;
  std::string newName_;
};

} // namespace oxygen::interop::module::commands

#pragma managed(pop)
