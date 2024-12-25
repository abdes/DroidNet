//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "entity.h"

#include "oxygen/base/logging.h"
#include "oxygen/base/ResourceTable.h"
#include "transform.h"
#include <shared_mutex>

namespace {

  using oxygen::ResourceTable;
  using oxygen::world::GameEntity;
  using oxygen::world::Transform;

#define MAKE_RESOURCE_TABLE(name, type, itemType)                              \
  ResourceTable<type> name(oxygen::world::resources::k##itemType, 256)

  // TODO: table size needs to be moved to a constant

  MAKE_RESOURCE_TABLE(entities, GameEntity, GameEntity);
  MAKE_RESOURCE_TABLE(transforms, Transform, Transform);
  MAKE_RESOURCE_TABLE(positions, glm::vec3, Transform);
  MAKE_RESOURCE_TABLE(rotations, glm::quat, Transform);
  MAKE_RESOURCE_TABLE(scales, glm::vec3, Transform);

  std::shared_mutex entity_mutex;

}  // namespace

/**
 * @brief Creates a new transform component for the specified entity.
 *
 * This function inserts a new transform, position, rotation, and scale into
 * their respective resource tables. All this items share the same index with
 * the entity in its corresponding table in such a way that all of these items
 * can be batch processed efficiently.
 *
 * @param transform_desc The descriptor containing the initial position,
 * rotation, and scale for the transform.
 * @param entity_id The ID of the entity for which the transform is being
 * created.
 * @return The created Transform object.
 */
auto GameEntity::CreateTransform(
  const TransformDescriptor& transform_desc,
  const GameEntityId& entity_id) -> Transform
{
  const auto transform_id = transforms.Insert({});
  DCHECK_EQ_F(transform_id.Index(), entity_id.Index());

  const auto position_id = positions.Insert(transform_desc.position);
  DCHECK_EQ_F(position_id.Index(), entity_id.Index());

  const auto rotation_id = rotations.Insert(transform_desc.rotation);
  DCHECK_EQ_F(rotation_id.Index(), entity_id.Index());

  const auto scale_id = scales.Insert(transform_desc.scale);
  DCHECK_EQ_F(scale_id.Index(), entity_id.Index());

  return Transform(transform_id);
}

/**
 * @brief Removes the specified transform component. Upon return, the transform
 * is invalidated.
 *
 * This function erases the transform, position, rotation, and scale from their
 * respective resource tables.
 *
 * @param transform The Transform object to be removed.
 * @return The number of components removed (should be 1 if successful).
 */
auto GameEntity::RemoveTransform(Transform& transform) -> size_t
{
  if (!transform.IsValid()) {
    return 0;
  }

  auto removed = transforms.Erase(transform.GetId());
  CHECK_EQ_F(1, removed, "transform not in the resource table");
  removed = positions.Erase(transform.GetId());
  CHECK_EQ_F(1, removed, "transform-position not in the resource table");
  removed = rotations.Erase(transform.GetId());
  CHECK_EQ_F(1, removed, "transform-rotation not in the resource table");
  removed = scales.Erase(transform.GetId());
  CHECK_EQ_F(1, removed, "transform-scale not in the resource table");

  transform.Invalidate();
  return removed;
}

auto Transform::GetPosition() const noexcept -> glm::vec3 {
  CHECK_F(IsValid());
  std::unique_lock lock(entity_mutex);
  return positions.ItemAt(GetId());
}

auto Transform::GetRotation() const noexcept -> glm::quat {
  CHECK_F(IsValid());
  std::unique_lock lock(entity_mutex);
  return rotations.ItemAt(GetId());
}

auto Transform::GetScale() const noexcept -> glm::vec3 {
  CHECK_F(IsValid());
  std::unique_lock lock(entity_mutex);
  return scales.ItemAt(GetId());
}
void Transform::SetPosition(const glm::vec3 position) const noexcept {
  CHECK_F(IsValid());
  std::unique_lock lock(entity_mutex);
  positions.ItemAt(GetId()) = position;
}
void Transform::SetRotation(const glm::quat rotation) const noexcept {
  CHECK_F(IsValid());
  std::unique_lock lock(entity_mutex);
  rotations.ItemAt(GetId()) = rotation;
}
void Transform::SetScale(const glm::vec3 scale) const noexcept {
  CHECK_F(IsValid());
  std::unique_lock lock(entity_mutex);
  scales.ItemAt(GetId()) = scale;
}

auto oxygen::world::entity::CreateGameEntity(const GameEntityDescriptor& entity_desc) -> GameEntity
{
  // All game entities must have a transform component.
  CHECK_NOTNULL_F(entity_desc.transform, "all game entities must have a transform component!");

  std::unique_lock lock(entity_mutex);

  const auto entity_id = entities.Insert({});
  if (!entity_id.IsValid()) {
    return {};
  }

  // ReSharper disable once CppTooWideScopeInitStatement (we use it for DCHECK)
  const auto transform = GameEntity::CreateTransform(*entity_desc.transform, entity_id);
  if (!transform.IsValid()) {
    entities.Erase(entity_id);
    return {};
  }
  DCHECK_EQ_F(transform.GetId().Index(), entity_id.Index());
  DCHECK_EQ_F(transform.GetId().Generation(), entity_id.Generation());

  LOG_F(INFO, "Game entity created: {}", entity_id.ToString());
  return GameEntity(entity_id);
}

auto oxygen::world::entity::RemoveGameEntity(GameEntity& entity) -> size_t
{
  if (!entity.IsValid()) {
    return 0;
  }

  std::unique_lock lock(entity_mutex);

  const auto entity_id = entity.GetId().ToString(); // keep for logging later
  const auto entity_removed = entities.Erase(entity.GetId());
  if (entity_removed != 0) {
    auto transform = entity.GetTransform();
    CHECK_EQ_F(1, GameEntity::RemoveTransform(transform));
    entity.Invalidate();

    LOG_F(INFO, "Game entity removed: {}", entity_id);
  }
  return entity_removed;
}

auto GameEntity::GetTransform() const noexcept -> Transform {
  if (!IsValid()) {
    return {};
  }

  auto transform = Transform(GetTransformId());
  CHECK_F(transform.IsValid());
  return transform;
}
