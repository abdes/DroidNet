//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once
#pragma managed(push, off)

#include <vector>

#include <Oxygen/Core/PhaseRegistry.h>
#include <Oxygen/Scene/Scene.h>

#include <EditorModule/EditorCommand.h>

namespace oxygen::interop::module {

  class RemoveSceneNodesCommand : public EditorCommand {
  public:
    explicit RemoveSceneNodesCommand(std::vector<oxygen::scene::NodeHandle> nodes)
      : EditorCommand(oxygen::core::PhaseId::kSceneMutation),
      nodes_(std::move(nodes)) {
    }

    void Execute(CommandContext& context) override {
      if (!context.Scene)
        return;

      for (const auto& handle : nodes_) {
        auto sceneNode = context.Scene->GetNode(handle);
        if (sceneNode && sceneNode->IsAlive()) {
          if (sceneNode->HasChildren()) {
            context.Scene->DestroyNodeHierarchy(*sceneNode);
          }
          else {
            context.Scene->DestroyNode(*sceneNode);
          }
        }
      }
    }

  private:
    std::vector<oxygen::scene::NodeHandle> nodes_;
  };

} // namespace oxygen::interop::module

#pragma managed(pop)
