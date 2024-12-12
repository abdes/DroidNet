//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include "glm/gtc/quaternion.hpp"

#include "api.h"
#include "oxygen/world/entity.h"
#include "oxygen/world/transform.h"

using oxygen::ResourceHandle;
using oxygen::world::EntityDescriptor;
using oxygen::world::TransformDescriptor;

auto CreateGameEntity(
    const OxygenGameEntityCreateInfo *pDescriptor) -> ResourceHandle::HandleT {
  assert(pDescriptor != nullptr);
  if (pDescriptor == nullptr) {
    return {};
  }
  assert(pDescriptor->transform != nullptr);
  if (pDescriptor->transform == nullptr) {
    return {};
  }

  assert(sizeof(pDescriptor->transform->position) == sizeof(glm::vec3));
  assert(sizeof(pDescriptor->transform->rotation) == sizeof(glm::vec3));
  assert(sizeof(pDescriptor->transform->scale) == sizeof(glm::vec3));

  TransformDescriptor transformDescriptor;
  transformDescriptor.position = pDescriptor->transform->position;
  // Convert rotation from Euler Angles to a Quaternion
  transformDescriptor.rotation = glm::quat(pDescriptor->transform->rotation);
  transformDescriptor.scale = pDescriptor->transform->scale;

  EntityDescriptor entityDescriptor;
  entityDescriptor.transform = &transformDescriptor;

  const auto entity = oxygen::world::entity::CreateGameEntity(entityDescriptor);
  return entity.GetEntityId().Handle();
}

void RemoveGameEntity(const ResourceHandle::HandleT entity_id) {
  const auto entity = oxygen::world::Entity(ResourceHandle(entity_id));
  oxygen::world::entity::RemoveGameEntity(entity);
}
