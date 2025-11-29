//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Scene/Scene.h>

#include "EngineContext.h"

using namespace System;
using namespace System::Numerics;

namespace Oxygen::Interop::World {

    // Future Enhancements:
    // - Implement Two-Way Synchronization (Engine -> Editor events for physics/scripts).
    // - Implement Read-Replica for thread-safe immediate property access.

    public ref class OxygenWorld {
    public:
        OxygenWorld(EngineContext^ context);

        // Scene management
        // Scenes are identified by name on the native engine side; keep CreateScene(String) as before.
        void CreateScene(String^ name);

        // Node management (GUID-based): all node APIs accept node or parent GUIDs
        // Creates a node and invokes the callback with the new node GUID on the engine thread.
        void CreateSceneNode(String^ name, System::Guid nodeId, Nullable<System::Guid> parentGuid, Action<System::Guid>^ onCreated);
        void CreateSceneNode(String^ name, System::Guid nodeId, Nullable<System::Guid> parentGuid, Action<System::Guid>^ onCreated, bool initializeWorldAsRoot);
        void RemoveSceneNode(System::Guid nodeId);
        void RenameSceneNode(System::Guid nodeId, String^ newName);

        // Transform management
        void SetLocalTransform(System::Guid nodeId,
            System::Numerics::Vector3 position,
            System::Numerics::Quaternion rotation,
            System::Numerics::Vector3 scale);

        // Geometry management
        void CreateBasicMesh(System::Guid nodeId, String^ meshType);
        void SetVisibility(System::Guid nodeId, bool visible);

        // Selection (Editor-side state)
        void SelectNode(System::Guid nodeId);
        void DeselectNode(System::Guid nodeId);

        // Reparent a node to a new parent (or make root when parent is null)
        void ReparentSceneNode(System::Guid child, Nullable<System::Guid> parent, bool preserveWorldTransform);
        void UpdateTransformsForNodes(array<System::Guid>^ nodes);

        // (All APIs are GUID-based; no scene names are accepted)

    private:
        EngineContext^ context_;
    };

} // namespace Oxygen::Interop::World
