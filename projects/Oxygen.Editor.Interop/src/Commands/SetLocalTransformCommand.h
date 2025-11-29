//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause.
//===----------------------------------------------------------------------===//

#pragma once
#pragma managed(push, off)

#include <glm/vec3.hpp>
#include <glm/gtc/quaternion.hpp>

#include "EditorModule/EditorCommand.h"

namespace oxygen::interop::module::commands {

class SetLocalTransformCommand : public EditorCommand {
public:
  SetLocalTransformCommand(oxygen::scene::NodeHandle node,
                           const glm::vec3 &position, const glm::quat &rotation,
                           const glm::vec3 &scale)
      : node_(node), position_(position), rotation_(rotation), scale_(scale) {}

  void Execute(CommandContext &context) override {
    if (!context.Scene)
      return;

    auto sceneNode = context.Scene->GetNode(node_);
    if (sceneNode && sceneNode->IsAlive()) {
      auto transform = sceneNode->GetTransform();
      transform.SetLocalTransform(position_, rotation_, scale_);
    }
  }

private:
  oxygen::scene::NodeHandle node_;
  glm::vec3 position_;
  glm::quat rotation_;
  glm::vec3 scale_;
};

} // namespace oxygen::interop::module::commands

#pragma managed(pop)
