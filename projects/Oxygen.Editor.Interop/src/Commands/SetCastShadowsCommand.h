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

  class SetCastShadowsCommand : public EditorCommand {
  public:
    SetCastShadowsCommand(oxygen::scene::NodeHandle node, bool castsShadows);

    void Execute(CommandContext& context) override;

  private:
    oxygen::scene::NodeHandle node_;
    bool castsShadows_;
  };

  inline SetCastShadowsCommand::SetCastShadowsCommand(
    oxygen::scene::NodeHandle node, bool castsShadows)
    : EditorCommand(oxygen::core::PhaseId::kSceneMutation), node_(node),
    castsShadows_(castsShadows) {
  }

  inline void SetCastShadowsCommand::Execute(CommandContext& context) {
    if (!context.Scene)
      return;

    auto sceneNode = context.Scene->GetNode(node_);
    if (sceneNode && sceneNode->IsAlive()) {
      auto flags = sceneNode->GetFlags();
      if (flags) {
        flags->get().SetLocalValue(oxygen::scene::SceneNodeFlags::kCastsShadows,
          castsShadows_);
        context.Scene->NotifyEnvironmentAuthoringChange();
      }
    }
  }

} // namespace oxygen::interop::module

#pragma managed(pop)
