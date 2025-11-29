//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause.
//===----------------------------------------------------------------------===//

#pragma once
#pragma managed(push, off)

#include "EditorModule/EditorCommand.h"

namespace oxygen::interop::module::commands {

class ReparentSceneNodeCommand : public EditorCommand {
public:
  ReparentSceneNodeCommand(oxygen::scene::NodeHandle child,
                           oxygen::scene::NodeHandle parent,
                           bool preserve_world_transform = true)
      : child_(child), parent_(parent), preserve_(preserve_world_transform) {}

  void Execute(CommandContext &context) override {
    if (!context.Scene)
      return;

    auto sceneNode = context.Scene->GetNode(child_);
    if (!sceneNode || !sceneNode->IsAlive())
      return;

    if (parent_.IsValid()) {
      auto parentNode = context.Scene->GetNode(parent_);
      if (parentNode && parentNode->IsAlive()) {
        [[maybe_unused]] auto result =
            context.Scene->ReparentNode(*sceneNode, *parentNode, preserve_);
      }
    } else {
      [[maybe_unused]] auto result =
          context.Scene->MakeNodeRoot(*sceneNode, preserve_);
    }
  }

private:
  oxygen::scene::NodeHandle child_;
  oxygen::scene::NodeHandle parent_;
  bool preserve_;
};

} // namespace oxygen::interop::module::commands

#pragma managed(pop)
