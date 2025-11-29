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

// TODO: properly design this API and implement all needed features.

namespace Oxygen::Interop::World {

  public
  ref class OxygenWorld {
  public:
    OxygenWorld(Oxygen::Editor::EngineInterface::EngineContext^ context);

    // Scene management
    void CreateScene(String^ name);

    // Node management
    void CreateSceneNode(String^ name, String^ parentName);
    void RemoveSceneNode(String^ name);

    // Transform management
    void SetLocalTransform(String^ nodeName,
        System::Numerics::Vector3 position,
        System::Numerics::Quaternion rotation,
        System::Numerics::Vector3 scale);

    // Geometry management
    void CreateBasicMesh(String^ nodeName, String^ meshType);

  private:
    Oxygen::Editor::EngineInterface::EngineContext^ context_;
  };

} // namespace Oxygen::Interop::World
