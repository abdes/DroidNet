//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "oxygen/world/entity.h"
#include "oxygen/world/transform.h"
#include "gtest/gtest.h"

using oxygen::world::GameEntity;
using oxygen::world::GameEntityDescriptor;
using oxygen::world::GameEntityId;
using oxygen::world::Transform;
using oxygen::world::TransformDescriptor;
using oxygen::world::TransformId;
using oxygen::world::entity::CreateGameEntity;
using oxygen::world::entity::RemoveGameEntity;

// -- Entity tests ---------------------------------------------------------- //

// NOLINTNEXTLINE
TEST(EntityTest, CanCreateAndRemoveEntity)
{
    TransformDescriptor transform_desc {};
    const GameEntityDescriptor entity_desc {
        .transform = &transform_desc,
    };

    auto entity = CreateGameEntity(entity_desc);

    ASSERT_TRUE(entity.IsValid());

    const auto removed = RemoveGameEntity(entity);
    ASSERT_EQ(removed, 1);
}

// NOLINTNEXTLINE
TEST(EntityTest, CreateEntityCreatesTransform)
{
    TransformDescriptor transform_desc { .position = { 1.0f, 2.0f, 3.0f },
        .rotation = { 1.0f, 0.0f, 0.0f, 0.0f },
        .scale = { 1.0f, 1.0f, 1.0f } };
    const GameEntityDescriptor entity_desc {
        .transform = &transform_desc,
    };

    const auto entity = CreateGameEntity(entity_desc);
    ASSERT_TRUE(entity.IsValid());

    const auto transform = entity.GetTransform();
    ASSERT_TRUE(transform.IsValid());
    ASSERT_EQ(transform.GetPosition(), transform_desc.position);
    ASSERT_EQ(transform.GetRotation(), transform_desc.rotation);
    ASSERT_EQ(transform.GetScale(), transform_desc.scale);
}

// NOLINTNEXTLINE
TEST(EntityTest, RemoveEntityRemovesTransform)
{
    TransformDescriptor transform_desc {};
    const GameEntityDescriptor entity_desc {
        .transform = &transform_desc,
    };

    auto entity = CreateGameEntity(entity_desc);
    ASSERT_TRUE(entity.IsValid());
    ASSERT_TRUE(entity.GetTransform().IsValid());

    const auto removed = RemoveGameEntity(entity);
    ASSERT_EQ(removed, 1);
    ASSERT_FALSE(entity.GetTransform().IsValid());
}

// NOLINTNEXTLINE
TEST(EntityTest, AbortWhenCreateEntityWithNullTransform)
{
    constexpr GameEntityDescriptor entity_desc {
        .transform = nullptr,
    };

    EXPECT_DEATH(
        { CreateGameEntity(entity_desc); },
        "all game entities must have a transform component!");
}

// NOLINTNEXTLINE
TEST(EntityTest, CreateMultipleEntities)
{
    TransformDescriptor transform_desc1 {};
    TransformDescriptor transform_desc2 {};
    GameEntityDescriptor entity_desc1 {
        .transform = &transform_desc1,
    };
    GameEntityDescriptor entity_desc2 {
        .transform = &transform_desc2,
    };

    auto entity1 = CreateGameEntity(entity_desc1);
    auto entity2 = CreateGameEntity(entity_desc2);

    ASSERT_TRUE(entity1.IsValid());
    ASSERT_TRUE(entity2.IsValid());
    ASSERT_NE(entity1.GetId(), entity2.GetId());

    RemoveGameEntity(entity1);
    RemoveGameEntity(entity2);
}

// NOLINTNEXTLINE
TEST(EntityTest, RemoveInvalidEntityDoesNothing)
{
    TransformDescriptor transform_desc {};
    GameEntity entity = CreateGameEntity(GameEntityDescriptor { &transform_desc });

    ASSERT_EQ(RemoveGameEntity(entity), 1);
    ASSERT_FALSE(entity.IsValid());
    ASSERT_EQ(RemoveGameEntity(entity), 0);
}

// NOLINTNEXTLINE
TEST(EntityTest, GetTransformId)
{
    TransformDescriptor transform_desc {};
    const GameEntityDescriptor entity_desc {
        .transform = &transform_desc,
    };

    auto entity = CreateGameEntity(entity_desc);

    ASSERT_TRUE(entity.IsValid());
    const auto transform_id = entity.GetTransformId();
    ASSERT_EQ(transform_id.ResourceType(), oxygen::world::resources::kTransform);

    RemoveGameEntity(entity);
}

// -- Transform tests ------------------------------------------------------- //

// NOLINTNEXTLINE
TEST(TransformTest, GetPosition)
{
    TransformDescriptor transform_desc { .position = { 1.0f, 2.0f, 3.0f },
        .rotation = { 1.0f, 0.0f, 0.0f, 0.0f },
        .scale = { 1.0f, 1.0f, 1.0f } };
    const GameEntityDescriptor entity_desc {
        .transform = &transform_desc,
    };

    auto entity = CreateGameEntity(entity_desc);
    ASSERT_TRUE(entity.IsValid());

    auto transform = entity.GetTransform();
    ASSERT_TRUE(transform.IsValid());

    auto position = transform.GetPosition();
    ASSERT_EQ(position, transform_desc.position);

    RemoveGameEntity(entity);
}

// NOLINTNEXTLINE
TEST(TransformTest, GetRotation)
{
    TransformDescriptor transform_desc { .position = { 1.0f, 2.0f, 3.0f },
        .rotation = { 1.0f, 0.0f, 0.0f, 0.0f },
        .scale = { 1.0f, 1.0f, 1.0f } };
    const GameEntityDescriptor entity_desc {
        .transform = &transform_desc,
    };

    auto entity = CreateGameEntity(entity_desc);
    ASSERT_TRUE(entity.IsValid());

    auto transform = entity.GetTransform();
    ASSERT_TRUE(transform.IsValid());

    auto rotation = transform.GetRotation();
    ASSERT_EQ(rotation, transform_desc.rotation);

    RemoveGameEntity(entity);
}

// NOLINTNEXTLINE
TEST(TransformTest, GetScale)
{
    TransformDescriptor transform_desc { .position = { 1.0f, 2.0f, 3.0f },
        .rotation = { 1.0f, 0.0f, 0.0f, 0.0f },
        .scale = { 1.0f, 1.0f, 1.0f } };
    const GameEntityDescriptor entity_desc {
        .transform = &transform_desc,
    };

    auto entity = CreateGameEntity(entity_desc);
    ASSERT_TRUE(entity.IsValid());

    auto transform = entity.GetTransform();
    ASSERT_TRUE(transform.IsValid());

    auto scale = transform.GetScale();
    ASSERT_EQ(scale, transform_desc.scale);

    RemoveGameEntity(entity);
}
