//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once
#pragma managed(push, off)

#include <utility>
#include <vector>

#include <Oxygen/Core/PhaseRegistry.h>
#include <Oxygen/Scene/SceneNode.h>
#include <Oxygen/Scene/Types/NodeHandle.h>

#include <EditorModule/EditorCommand.h>

namespace oxygen::interop::module {

  class ReparentSceneNodesCommand : public EditorCommand {
  public:
    ReparentSceneNodesCommand(std::vector<oxygen::scene::NodeHandle> children,
      oxygen::scene::NodeHandle parent,
      bool preserveWorldTransform)
      : EditorCommand(oxygen::core::PhaseId::kSceneMutation),
      children_(std::move(children)),
      parent_(parent),
      preserve_(preserveWorldTransform) {
    }

    void Execute(CommandContext& context) override {
      if (!context.Scene)
        return;

      if (parent_.IsValid()) {
        auto parentNode = context.Scene->GetNode(parent_);
        if (!parentNode || !parentNode->IsAlive()) {
          // If parent is invalid, we cannot reparent to it.
          return;
        }

        for (const auto& childHandle : children_) {
          auto sceneNode = context.Scene->GetNode(childHandle);
          if (!sceneNode || !sceneNode->IsAlive())
            continue;

          (void)context.Scene->ReparentNode(*sceneNode, *parentNode, preserve_);
        }
      }
      else {
        for (const auto& childHandle : children_) {
          auto sceneNode = context.Scene->GetNode(childHandle);
          if (!sceneNode || !sceneNode->IsAlive())
            continue;

          (void)context.Scene->MakeNodeRoot(*sceneNode, preserve_);
        }
      }
    }

  private:
    std::vector<oxygen::scene::NodeHandle> children_;
    oxygen::scene::NodeHandle parent_;
    bool preserve_;
  };

} // namespace oxygen::interop::module

#pragma managed(pop)
