//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause.
//===----------------------------------------------------------------------===//

#pragma once
#pragma managed(push, off)

#include <Oxygen/Scene/Types/NodeHandle.h>

#include <EditorModule/EditorCommand.h>

namespace oxygen::interop::module {

  class SetReceiveShadowsCommand : public EditorCommand {
  public:
    SetReceiveShadowsCommand(oxygen::scene::NodeHandle node, bool receivesShadows);

    void Execute(CommandContext& context) override;

  private:
    oxygen::scene::NodeHandle node_;
    bool receivesShadows_;
  };

  inline SetReceiveShadowsCommand::SetReceiveShadowsCommand(
    oxygen::scene::NodeHandle node, bool receivesShadows)
    : EditorCommand(oxygen::core::PhaseId::kSceneMutation), node_(node),
    receivesShadows_(receivesShadows) {
  }

  inline void SetReceiveShadowsCommand::Execute(CommandContext& context) {
    if (!context.Scene)
      return;

    auto sceneNode = context.Scene->GetNode(node_);
    if (sceneNode && sceneNode->IsAlive()) {
      auto flags = sceneNode->GetFlags();
      if (flags) {
        flags->get().SetLocalValue(oxygen::scene::SceneNodeFlags::kReceivesShadows,
          receivesShadows_);
        context.Scene->NotifyEnvironmentAuthoringChange();
      }
    }
  }

} // namespace oxygen::interop::module

#pragma managed(pop)
