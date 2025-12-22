//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once
#pragma managed(push, on)

#include <array>
#include <string>
#include <vector>

#include <glm/fwd.hpp>

#include <Oxygen/Scene/Types/NodeHandle.h>

namespace oxygen::interop::module{
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
class DetachGeometryCommand;
} // namespace oxygen::interop::module

namespace Oxygen::Interop::World {

  public
    interface class ICommandFactory {
    oxygen::interop::module::CreateSceneNodeCommand*
      CreateSceneNode(std::string name, oxygen::scene::NodeHandle parent,
        System::Action<Oxygen::Editor::Core::NodeHandle>^ onCreated,
        std::array<uint8_t, 16> regKey, bool initializeWorldAsRoot);

    oxygen::interop::module::RemoveSceneNodeCommand*
      CreateRemoveSceneNode(oxygen::scene::NodeHandle handle);

    oxygen::interop::module::RenameSceneNodeCommand*
      CreateRenameSceneNode(oxygen::scene::NodeHandle handle, std::string newName);

    oxygen::interop::module::SetLocalTransformCommand*
      CreateSetLocalTransform(oxygen::scene::NodeHandle handle, glm::vec3 position,
        glm::quat rotation, glm::vec3 scale);

    oxygen::interop::module::CreateBasicMeshCommand*
      CreateBasicMesh(oxygen::scene::NodeHandle handle, std::string meshType);

    oxygen::interop::module::DetachGeometryCommand*
      CreateDetachGeometry(oxygen::scene::NodeHandle handle);

    oxygen::interop::module::SetVisibilityCommand*
      CreateSetVisibility(oxygen::scene::NodeHandle handle, bool visible);

    oxygen::interop::module::ReparentSceneNodeCommand*
      CreateReparentSceneNode(oxygen::scene::NodeHandle child,
        oxygen::scene::NodeHandle parent,
        bool preserveWorldTransform);

    oxygen::interop::module::ReparentSceneNodesCommand*
      CreateReparentSceneNodes(std::vector<oxygen::scene::NodeHandle> children,
        oxygen::scene::NodeHandle parent,
        bool preserveWorldTransform);

    oxygen::interop::module::UpdateTransformsForNodesCommand*
      CreateUpdateTransformsForNodes(std::vector<oxygen::scene::NodeHandle> nodes);

    oxygen::interop::module::RemoveSceneNodesCommand*
      CreateRemoveSceneNodes(std::vector<oxygen::scene::NodeHandle> nodes);
  };

} // namespace Oxygen::Interop::World

#pragma managed(pop)
