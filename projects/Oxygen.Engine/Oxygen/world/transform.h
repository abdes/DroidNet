//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include "glm/glm.hpp"
#include "glm/gtc/quaternion.hpp"

#include "Oxygen/api_export.h"
#include "Oxygen/Base/Resource.h"
#include "Oxygen/World/Types.h"

namespace oxygen::world {

  namespace transform {

    /**
     * @brief The descriptor for creating a new transform component.
     *
     * Transforms are only created and removed with entities, and all game entities
     * must have a transform component. Therefore, it will always be true that for a
     * transform component id, the index and generation will be the same as for the
     * corresponding game entity.
     */
    struct Descriptor
    {
      glm::vec3 position{};
      glm::quat rotation{};
      glm::vec3 scale{ 1.F, 1.F, 1.F };
    };

  }  // namespace transform

  class Transform final : public Resource<resources::kTransform>
  {
    constexpr explicit Transform(const TransformId& transform_id)
      : Resource(transform_id) {
    }
    constexpr Transform() = default;  // Creates an invalid entity
    friend class GameEntity;

  public:
    [[nodiscard]] constexpr auto GetEntityId() const noexcept -> GameEntityId {
      // The transform handle is the same as the entity handle except for the
      // resource type.
      ResourceHandle entity_id(GetId());
      entity_id.SetResourceType(resources::kGameEntity);
      return entity_id;
    }

    OXYGEN_API [[nodiscard]] auto GetPosition() const noexcept -> glm::vec3;
    OXYGEN_API [[nodiscard]] auto GetRotation() const noexcept -> glm::quat;
    OXYGEN_API [[nodiscard]] auto GetScale() const noexcept -> glm::vec3;

    OXYGEN_API void SetPosition(glm::vec3 position) const noexcept;
    OXYGEN_API void SetRotation(glm::quat rotation) const noexcept;
    OXYGEN_API void SetScale(glm::vec3 scale) const noexcept;
  };

}  // namespace oxygen::world
