//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once
#pragma managed(push, on)

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "World/ICommandFactory.h"
#include <glm/fwd.hpp>

// Forward declarations for types used in method signatures to avoid heavy includes
namespace oxygen::interop::module::commands{
  class CreateSceneNodeCommand;
  class RemoveSceneNodeCommand;
  class RenameSceneNodeCommand;
  class SetLocalTransformCommand;
  class CreateBasicMeshCommand;
  class SetVisibilityCommand;
  class ReparentSceneNodeCommand;
  class ReparentSceneNodesCommand;
  class UpdateTransformsForNodesCommand;
  class RemoveSceneNodesCommand;
} // namespace oxygen::interop::module::commands

namespace oxygen::scene {
  class NodeHandle;
}

namespace Oxygen::Interop::World {

  public ref class CommandFactory : public ICommandFactory {
  public:
    virtual oxygen::interop::module::commands::CreateSceneNodeCommand* CreateSceneNode(
      std::string name,
      oxygen::scene::NodeHandle parent,
      System::Action<Oxygen::Editor::Core::NodeHandle>^ onCreated,
      std::array<uint8_t, 16> regKey,
      bool initializeWorldAsRoot
    );

    virtual oxygen::interop::module::commands::RemoveSceneNodeCommand* CreateRemoveSceneNode(
      oxygen::scene::NodeHandle handle
    );

    virtual oxygen::interop::module::commands::RenameSceneNodeCommand* CreateRenameSceneNode(
      oxygen::scene::NodeHandle handle,
      std::string newName
    );

    virtual oxygen::interop::module::commands::SetLocalTransformCommand* CreateSetLocalTransform(
      oxygen::scene::NodeHandle handle,
      glm::vec3 position,
      glm::quat rotation,
      glm::vec3 scale
    );

    virtual oxygen::interop::module::commands::CreateBasicMeshCommand* CreateBasicMesh(
      oxygen::scene::NodeHandle handle,
      std::string meshType
    );

    virtual oxygen::interop::module::commands::SetVisibilityCommand* CreateSetVisibility(
      oxygen::scene::NodeHandle handle,
      bool visible
    );

    virtual oxygen::interop::module::commands::ReparentSceneNodeCommand* CreateReparentSceneNode(
      oxygen::scene::NodeHandle child,
      oxygen::scene::NodeHandle parent,
      bool preserveWorldTransform
    );

    virtual oxygen::interop::module::commands::ReparentSceneNodesCommand* CreateReparentSceneNodes(
      std::vector<oxygen::scene::NodeHandle> children,
      oxygen::scene::NodeHandle parent,
      bool preserveWorldTransform
    );

    virtual oxygen::interop::module::commands::UpdateTransformsForNodesCommand* CreateUpdateTransformsForNodes(
      std::vector<oxygen::scene::NodeHandle> nodes
    );

    virtual oxygen::interop::module::commands::RemoveSceneNodesCommand* CreateRemoveSceneNodes(
      std::vector<oxygen::scene::NodeHandle> nodes
    );
  };

} // namespace Oxygen::Interop::World

#pragma managed(pop)
