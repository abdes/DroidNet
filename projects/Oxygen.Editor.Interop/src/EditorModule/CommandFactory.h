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
#include <EditorModule/ICommandFactory.h>

// Forward declarations for types used in method signatures to avoid heavy
// includes
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
class AttachPerspectiveCameraCommand;
class DetachCameraCommand;
} // namespace oxygen::interop::module

namespace oxygen::scene {
  class NodeHandle;
}

namespace Oxygen::Interop::World {

  public
  ref class CommandFactory : public ICommandFactory {
  public:
    virtual oxygen::interop::module::CreateSceneNodeCommand*
      CreateSceneNode(std::string name, oxygen::scene::NodeHandle parent,
        System::Action<Oxygen::Core::NodeHandle>^ onCreated,
        std::array<uint8_t, 16> regKey, bool initializeWorldAsRoot);

    virtual oxygen::interop::module::RemoveSceneNodeCommand*
      CreateRemoveSceneNode(oxygen::scene::NodeHandle handle);

    virtual oxygen::interop::module::RenameSceneNodeCommand*
      CreateRenameSceneNode(oxygen::scene::NodeHandle handle, std::string newName);

    virtual oxygen::interop::module::SetLocalTransformCommand*
      CreateSetLocalTransform(oxygen::scene::NodeHandle handle, glm::vec3 position,
        glm::quat rotation, glm::vec3 scale);

    virtual oxygen::interop::module::SetPropertiesCommand*
      CreateSetProperties(oxygen::scene::NodeHandle handle,
        std::vector<oxygen::interop::module::PropertyEntry> entries);

    virtual oxygen::interop::module::SetGeometryCommand*
      CreateSetGeometry(oxygen::scene::NodeHandle handle, std::string assetUri);

    virtual oxygen::interop::module::SetMaterialOverrideCommand*
      CreateSetMaterialOverride(oxygen::scene::NodeHandle handle,
        std::size_t slotIndex, std::string materialUri);

    virtual oxygen::interop::module::SetEnvironmentCommand*
      CreateSetEnvironment(
        oxygen::interop::module::SkyAtmosphereParams atmosphere,
        oxygen::interop::module::PostProcessParams postProcess);

    virtual oxygen::interop::module::DetachGeometryCommand*
      CreateDetachGeometry(oxygen::scene::NodeHandle handle);

    virtual oxygen::interop::module::AttachPerspectiveCameraCommand*
      CreateAttachPerspectiveCamera(oxygen::scene::NodeHandle handle,
        float fieldOfViewYRadians, float aspectRatio, float nearPlane,
        float farPlane);

    virtual oxygen::interop::module::DetachCameraCommand*
      CreateDetachCamera(oxygen::scene::NodeHandle handle);

    virtual oxygen::interop::module::SetVisibilityCommand*
      CreateSetVisibility(oxygen::scene::NodeHandle handle, bool visible);

    virtual oxygen::interop::module::ReparentSceneNodeCommand*
      CreateReparentSceneNode(oxygen::scene::NodeHandle child,
        oxygen::scene::NodeHandle parent,
        bool preserveWorldTransform);

    virtual oxygen::interop::module::ReparentSceneNodesCommand*
      CreateReparentSceneNodes(std::vector<oxygen::scene::NodeHandle> children,
        oxygen::scene::NodeHandle parent,
        bool preserveWorldTransform);

    virtual oxygen::interop::module::UpdateTransformsForNodesCommand*
      CreateUpdateTransformsForNodes(std::vector<oxygen::scene::NodeHandle> nodes);

    virtual oxygen::interop::module::RemoveSceneNodesCommand*
      CreateRemoveSceneNodes(std::vector<oxygen::scene::NodeHandle> nodes);
  };

} // namespace Oxygen::Interop::World

#pragma managed(pop)
