//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once
#pragma managed(push, on)

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include <glm/fwd.hpp>

#include <Commands/SetEnvironmentCommand.h>

#include <Oxygen/Scene/Types/NodeHandle.h>

namespace oxygen::interop::module{
class CreateSceneNodeCommand;
class RemoveSceneNodeCommand;
class RenameSceneNodeCommand;
class SetLocalTransformCommand;
class SetPropertiesCommand;
struct PropertyEntry;
enum class ComponentId : std::uint16_t;
class SetGeometryCommand;
class SetMaterialOverrideCommand;
class SetVisibilityCommand;
class ReparentSceneNodeCommand;
class ReparentSceneNodesCommand;
class UpdateTransformsForNodesCommand;
class RemoveSceneNodesCommand;
class DetachGeometryCommand;
class AttachPerspectiveCameraCommand;
class DetachCameraCommand;
} // namespace oxygen::interop::module

namespace Oxygen::Interop::World {

  public
    interface class ICommandFactory {
    oxygen::interop::module::CreateSceneNodeCommand*
      CreateSceneNode(std::string name, oxygen::scene::NodeHandle parent,
        System::Action<Oxygen::Core::NodeHandle>^ onCreated,
        std::array<uint8_t, 16> regKey, bool initializeWorldAsRoot);

    oxygen::interop::module::RemoveSceneNodeCommand*
      CreateRemoveSceneNode(oxygen::scene::NodeHandle handle);

    oxygen::interop::module::RenameSceneNodeCommand*
      CreateRenameSceneNode(oxygen::scene::NodeHandle handle, std::string newName);

    oxygen::interop::module::SetLocalTransformCommand*
      CreateSetLocalTransform(oxygen::scene::NodeHandle handle, glm::vec3 position,
        glm::quat rotation, glm::vec3 scale);

    //! Property pipeline §5.3 — generic partial-update transport.
    oxygen::interop::module::SetPropertiesCommand*
      CreateSetProperties(oxygen::scene::NodeHandle handle,
        std::vector<oxygen::interop::module::PropertyEntry> entries);

    oxygen::interop::module::SetGeometryCommand*
      CreateSetGeometry(oxygen::scene::NodeHandle handle, std::string assetUri);

    oxygen::interop::module::SetMaterialOverrideCommand*
      CreateSetMaterialOverride(oxygen::scene::NodeHandle handle,
        std::size_t slotIndex, std::string materialUri);

    oxygen::interop::module::SetEnvironmentCommand*
      CreateSetEnvironment(
        oxygen::interop::module::SkyAtmosphereParams atmosphere,
        oxygen::interop::module::PostProcessParams postProcess);

    oxygen::interop::module::DetachGeometryCommand*
      CreateDetachGeometry(oxygen::scene::NodeHandle handle);

    oxygen::interop::module::AttachPerspectiveCameraCommand*
      CreateAttachPerspectiveCamera(oxygen::scene::NodeHandle handle,
        float fieldOfViewYRadians, float aspectRatio, float nearPlane,
        float farPlane);

    oxygen::interop::module::DetachCameraCommand*
      CreateDetachCamera(oxygen::scene::NodeHandle handle);

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
