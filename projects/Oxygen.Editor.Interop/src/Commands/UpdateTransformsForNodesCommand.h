//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause.
//===----------------------------------------------------------------------===//

#pragma once
#pragma managed(push, off)

#include <vector>

#include "EditorModule/EditorCommand.h"
#include <Oxygen/Scene/Types/NodeHandle.h>
#include <utility>

namespace oxygen::interop::module {

  class UpdateTransformsForNodesCommand : public EditorCommand {
  public:
    explicit UpdateTransformsForNodesCommand(
      std::vector<oxygen::scene::NodeHandle> nodes);

    void Execute(CommandContext& context) override;

  private:
    std::vector<oxygen::scene::NodeHandle> nodes_;
  };

  inline UpdateTransformsForNodesCommand::UpdateTransformsForNodesCommand(
    std::vector<oxygen::scene::NodeHandle> nodes)
    : EditorCommand(oxygen::core::PhaseId::kSceneMutation),
    nodes_(std::move(nodes)) {
  }

  inline void UpdateTransformsForNodesCommand::Execute(CommandContext& context) {
    if (!context.Scene)
      return;

    for (const auto& handle : nodes_) {
      auto sceneNode = context.Scene->GetNode(handle);
      if (!sceneNode || !sceneNode->IsAlive())
        continue;

      auto impl_opt = context.Scene->GetNodeImpl(*sceneNode);
      if (impl_opt.has_value()) {
        auto& impl = impl_opt->get();
        // Update transforms for this node's subtree (best-effort)
        impl.UpdateTransforms(*context.Scene);
      }
    }
  }

} // namespace oxygen::interop::module

#pragma managed(pop)
