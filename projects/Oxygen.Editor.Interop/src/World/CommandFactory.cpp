//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma unmanaged

#include "pch.h"

#include "World/CommandFactory.h"
#include <Commands/CreateBasicMeshCommand.h>
#include <Commands/CreateSceneNodeCommand.h>
#include <Commands/RemoveSceneNodeCommand.h>
#include <Commands/RenameSceneNodeCommand.h>
#include <Commands/ReparentSceneNodeCommand.h>
#include <Commands/ReparentSceneNodesCommand.h>
#include <Commands/SetLocalTransformCommand.h>
#include <Commands/SetVisibilityCommand.h>
#include <Commands/UpdateTransformsForNodesCommand.h>
#include <Commands/RemoveSceneNodesCommand.h>

using namespace oxygen::interop::module::commands;

namespace Oxygen::Interop::World {

  CreateSceneNodeCommand* CommandFactory::CreateSceneNode(
    std::string name,
    oxygen::scene::NodeHandle parent,
    System::Action<Oxygen::Editor::Core::NodeHandle>^ onCreated,
    std::array<uint8_t, 16> regKey,
    bool initializeWorldAsRoot
  ) {
    return new CreateSceneNodeCommand(name, parent, onCreated, regKey, initializeWorldAsRoot);
  }

  RemoveSceneNodeCommand* CommandFactory::CreateRemoveSceneNode(
    oxygen::scene::NodeHandle handle
  ) {
    return new RemoveSceneNodeCommand(handle);
  }

  RenameSceneNodeCommand* CommandFactory::CreateRenameSceneNode(
    oxygen::scene::NodeHandle handle,
    std::string newName
  ) {
    return new RenameSceneNodeCommand(handle, newName);
  }

  SetLocalTransformCommand* CommandFactory::CreateSetLocalTransform(
    oxygen::scene::NodeHandle handle,
    glm::vec3 position,
    glm::quat rotation,
    glm::vec3 scale
  ) {
    return new SetLocalTransformCommand(handle, position, rotation, scale);
  }

  CreateBasicMeshCommand* CommandFactory::CreateBasicMesh(
    oxygen::scene::NodeHandle handle,
    std::string meshType
  ) {
    return new CreateBasicMeshCommand(handle, meshType);
  }

  SetVisibilityCommand* CommandFactory::CreateSetVisibility(
    oxygen::scene::NodeHandle handle,
    bool visible
  ) {
    return new SetVisibilityCommand(handle, visible);
  }

  ReparentSceneNodeCommand* CommandFactory::CreateReparentSceneNode(
    oxygen::scene::NodeHandle child,
    oxygen::scene::NodeHandle parent,
    bool preserveWorldTransform
  ) {
    return new ReparentSceneNodeCommand(child, parent, preserveWorldTransform);
  }

  ReparentSceneNodesCommand* CommandFactory::CreateReparentSceneNodes(
    std::vector<oxygen::scene::NodeHandle> children,
    oxygen::scene::NodeHandle parent,
    bool preserveWorldTransform
  ) {
    return new ReparentSceneNodesCommand(std::move(children), parent, preserveWorldTransform);
  }

  UpdateTransformsForNodesCommand* CommandFactory::CreateUpdateTransformsForNodes(
    std::vector<oxygen::scene::NodeHandle> nodes
  ) {
    return new UpdateTransformsForNodesCommand(std::move(nodes));
  }

  RemoveSceneNodesCommand* CommandFactory::CreateRemoveSceneNodes(
    std::vector<oxygen::scene::NodeHandle> nodes
  ) {
    return new RemoveSceneNodesCommand(std::move(nodes));
  }

}
