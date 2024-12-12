//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include "oxygen/api_export.h"
#include "oxygen/base/resource.h"
#include "oxygen/base/resource_handle.h"
#include "oxygen/world/types.h"

namespace oxygen::world {

  namespace entity {

    /**
     * @brief The descriptor for creating a new game entity.
     *
     * Transforms are only created and removed with entities, and all game entities
     * must have a transform component. Therefore, it will always be true that for a
     * game entity id, the index and generation will be the same as for the
     * corresponding transform.
     */
    struct Descriptor
    {
      TransformDescriptor* transform;
    };

    /**
     * @brief Creates a new game entity. This factory function is the only way to
     * create an Entity.
     *
     * @param entity_desc The descriptor containing the properties of the entity to
     * be created.
     * @return The created Entity. If the creation fails, an invalid Entity is
     * returned.
     */
    OXYGEN_API auto CreateGameEntity(const GameEntityDescriptor& entity_desc)
      -> GameEntity;

    /**
     * @brief Removes a game entity and its associated transform component.  Upon
     * return, the entity is invalidated.
     *
     * @param entity The entity to be removed.
     * @return The number of entities removed (`1` if successful, `0` otherwise).
     */
    OXYGEN_API auto RemoveGameEntity(GameEntity& entity) -> size_t;

  }  // namespace entity

  /*!
   * @brief Represents a game entity in the world.
   *
   * The Entity class is a resource that represents a game entity. Each entity
   * must have a corresponding transform component. The entity and its transform
   * share the same index and generation in their resource handles.
   *
   * Entity objects can only be created through the factory function.
   *
   * @see CreateGameEntity()
   */
  class GameEntity : public Resource<resources::kGameEntity>
  {
    constexpr explicit GameEntity(const GameEntityId& entity_id)
      : Resource(entity_id) {
    }
    constexpr GameEntity() = default;  // Creates an invalid entity
    friend GameEntity entity::CreateGameEntity(const GameEntityDescriptor&);
    friend size_t entity::RemoveGameEntity(GameEntity&);

  public:
    [[nodiscard]] constexpr auto GetTransformId() const noexcept -> TransformId {
      // The transform handle is the same as the entity handle except for the
      // resource type.
      ResourceHandle transform_id(GetId());
      transform_id.SetResourceType(resources::kTransform);
      return transform_id;
    }

    OXYGEN_API [[nodiscard]] auto GetTransform() const noexcept -> Transform;

  private:
    [[nodiscard]] static auto CreateTransform(
      const TransformDescriptor& transform_desc, const GameEntityId& entity_id)
      -> Transform;
    static auto RemoveTransform(Transform& transform) -> size_t;
  };

}  // namespace oxygen::world
