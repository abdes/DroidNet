//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include "glm/glm.hpp"

#include "oxygen/base/resource_handle.h"
#include "oxygen/editor-api//api_export.h"

#include <glm/vec3.hpp>

struct OxygenTransformCreateInfo {
  glm::vec3 position;
  glm::vec3 rotation;
  glm::vec3 scale;
};

struct OxygenGameEntityCreateInfo {
  OxygenTransformCreateInfo *transform;
};

OXYGEN_ENGINE_API auto CreateGameEntity(
    const OxygenGameEntityCreateInfo *pDescriptor)
    -> oxygen::ResourceHandle::HandleT;

OXYGEN_ENGINE_API void RemoveGameEntity(oxygen::ResourceHandle::HandleT);
