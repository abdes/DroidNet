//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <string>
#include <array>
#include <vector>
#include <memory>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <Oxygen/Scene/Scene.h>

namespace oxygen::interop::module::commands{
  class CreateSceneNodeCommand;
  class RemoveSceneNodeCommand;
  class RenameSceneNodeCommand;
  class SetLocalTransformCommand;
  class CreateBasicMeshCommand;
  class SetVisibilityCommand;
  class ReparentSceneNodeCommand;
  class UpdateTransformsForNodesCommand;
} // namespace oxygen::interop::module::commands

namespace Oxygen::Interop::World {

  public interface class ICommandFactory {
    oxygen::interop::module::commands::CreateSceneNodeCommand* CreateSceneNode(
      std::string name,
      oxygen::scene::NodeHandle parent,
      System::Action<Oxygen::Editor::Core::NodeHandle>^ onCreated,
      std::array<uint8_t, 16> regKey,
      bool initializeWorldAsRoot
    );

    oxygen::interop::module::commands::RemoveSceneNodeCommand* CreateRemoveSceneNode(
      oxygen::scene::NodeHandle handle
    );

    oxygen::interop::module::commands::RenameSceneNodeCommand* CreateRenameSceneNode(
      oxygen::scene::NodeHandle handle,
      std::string newName
    );

    oxygen::interop::module::commands::SetLocalTransformCommand* CreateSetLocalTransform(
      oxygen::scene::NodeHandle handle,
      glm::vec3 position,
      glm::quat rotation,
      glm::vec3 scale
    );

    oxygen::interop::module::commands::CreateBasicMeshCommand* CreateBasicMesh(
      oxygen::scene::NodeHandle handle,
      std::string meshType
    );

    oxygen::interop::module::commands::SetVisibilityCommand* CreateSetVisibility(
      oxygen::scene::NodeHandle handle,
      bool visible
    );

    oxygen::interop::module::commands::ReparentSceneNodeCommand* CreateReparentSceneNode(
      oxygen::scene::NodeHandle child,
      oxygen::scene::NodeHandle parent,
      bool preserveWorldTransform
    );

    oxygen::interop::module::commands::UpdateTransformsForNodesCommand* CreateUpdateTransformsForNodes(
      std::vector<oxygen::scene::NodeHandle> nodes
    );
  };

} // namespace Oxygen::Interop::World
