//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Jolt/Jolt.h> // IWYU pragma: keep (must always be first)

#include <array>
#include <limits>
#include <mutex>
#include <optional>
#include <utility>
#include <variant>

#include <Jolt/Core/Memory.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/Shape.h>
#include <glm/common.hpp>
#include <glm/ext/quaternion_geometric.hpp>
#include <glm/ext/quaternion_trigonometric.hpp>

#include <Oxygen/Core/Constants.h>
#include <Oxygen/Data/ProceduralMeshes.h>
#include <Oxygen/Physics/Body/BodyDesc.h>
#include <Oxygen/Physics/Character/CharacterController.h>
#include <Oxygen/Physics/Jolt/Converters.h>
#include <Oxygen/Physics/PhysicsError.h>
#include <Oxygen/Physics/Query/Raycast.h>
#include <Oxygen/Physics/Shape.h>
#include <Oxygen/Physics/World/WorldDesc.h>
#include <Oxygen/Physics/test/Jolt/Jolt_test_fixture.h>
#include <Oxygen/Testing/GTest.h>

namespace oxygen::physics::test::jolt {
namespace {

  namespace backend = oxygen::physics::jolt;

  class JoltShapeAxisTest : public JoltTestFixture {
  protected:
    auto SetUp() -> void override
    {
      // Direct shape tests use the executable's static Jolt copy, separately
      // from the allocator initialized inside the Physics shared library.
      static std::once_flag allocator_once;
      std::call_once(
        allocator_once, [] -> void { JPH::RegisterDefaultAllocator(); });
      JoltTestFixture::SetUp();
    }
  };

  constexpr auto kTolerance = 2.0e-5F;
  constexpr std::array kAxes {
    space::move::Right,
    space::move::Back,
    space::move::Up,
  };

  auto ExpectNear(const Vec3& actual, const Vec3& expected) -> void
  {
    EXPECT_NEAR(actual.x, expected.x, kTolerance);
    EXPECT_NEAR(actual.y, expected.y, kTolerance);
    EXPECT_NEAR(actual.z, expected.z, kTolerance);
  }

  auto ExpectLocalSurface(
    const JPH::Shape& shape, const Vec3& position, const Vec3& normal) -> void
  {
    const auto origin = backend::ToJoltVec3(position + normal);
    const auto direction = backend::ToJoltVec3(-2.0F * normal);
    JPH::RayCastResult hit;
    ASSERT_TRUE(shape.CastRay(
      JPH::RayCast { origin, direction }, JPH::SubShapeIDCreator {}, hit));
    EXPECT_NEAR(hit.mFraction, 0.5F, kTolerance);
    const auto hit_position = origin + hit.mFraction * direction;
    ExpectNear(backend::ToOxygenVec3(hit_position), position);
    ExpectNear(backend::ToOxygenVec3(
                 shape.GetSurfaceNormal(hit.mSubShapeID2, hit_position)),
      normal);
  }

} // namespace

NOLINT_TEST_F(JoltShapeAxisTest, CapsuleMatchesRenderMeshBoundsAndSurface)
{
  AssertBackendAvailabilityContract();
  // The first case is the shared default recipe; custom and sphere-limit
  // dimensions catch assumptions about fixed height or a nonzero cylinder.
  for (const auto dimensions : std::array {
         Vec2 { 2.0F, 0.5F },
         Vec2 { 3.5F, 0.375F },
         Vec2 { 1.0F, 0.5F },
       }) {
    SCOPED_TRACE(::testing::Message()
      << "height=" << dimensions.x << " radius=" << dimensions.y);
    const auto height = dimensions.x;
    const auto radius = dimensions.y;
    const auto half_height = height / 2.0F;
    const auto mesh = data::MakeCapsuleMeshAsset(8, 32, height, radius);
    if (!mesh.has_value()) {
      FAIL() << "The capsule recipe must generate a render mesh.";
    }
    const auto shape = backend::MakeShape(
      CapsuleShape { .radius = radius, .half_height = half_height - radius });
    ASSERT_TRUE(shape.has_value());

    auto mesh_min = Vec3 { std::numeric_limits<float>::max() };
    auto mesh_max = Vec3 { std::numeric_limits<float>::lowest() };
    for (const auto& vertex : mesh->first) {
      mesh_min = glm::min(mesh_min, vertex.position);
      mesh_max = glm::max(mesh_max, vertex.position);
      // Every tessellated vertex lies on the analytic collision surface.
      // This also checks normals on both hemispheres and the cylinder.
      ExpectLocalSurface(*shape.value(), vertex.position, vertex.normal);
    }
    const auto bounds = shape.value()->GetLocalBounds();
    ExpectNear(backend::ToOxygenVec3(bounds.mMin), mesh_min);
    ExpectNear(backend::ToOxygenVec3(bounds.mMax), mesh_max);
    ExpectNear(mesh_max, Vec3 { radius, radius, half_height });
  }
}

NOLINT_TEST_F(JoltShapeAxisTest, CylinderUsesOxygenZAxis)
{
  AssertBackendAvailabilityContract();
  constexpr auto radius = 0.375F;
  constexpr auto half_height = 1.25F;
  const auto shape = backend::MakeShape(
    CylinderShape { .radius = radius, .half_height = half_height });
  ASSERT_TRUE(shape.has_value());
  const auto bounds = shape.value()->GetLocalBounds();
  const Vec3 extent { radius, radius, half_height };
  ExpectNear(backend::ToOxygenVec3(bounds.mMin), -extent);
  ExpectNear(backend::ToOxygenVec3(bounds.mMax), extent);
  for (const auto& axis : kAxes) {
    for (const auto sign : { -1.0F, 1.0F }) {
      const auto normal = sign * axis;
      ExpectLocalSurface(*shape.value(), normal * extent, normal);
    }
  }
}

namespace {
  //! The bounds of `shape` in its own space: Jolt reports them around the
  //! center of mass.
  auto ShapeSpaceBounds(const JPH::Shape& shape) -> std::pair<Vec3, Vec3>
  {
    const auto bounds = shape.GetLocalBounds();
    const auto com = shape.GetCenterOfMass();
    return {
      backend::ToOxygenVec3(bounds.mMin + com),
      backend::ToOxygenVec3(bounds.mMax + com),
    };
  }

  //! Where a ray from `origin` along `direction`, both in the shape's own
  //! space, first hits `shape`; nothing when it misses.
  auto FirstHit(const JPH::Shape& shape, const Vec3& origin,
    const Vec3& direction) -> std::optional<Vec3>
  {
    const auto com = backend::ToOxygenVec3(shape.GetCenterOfMass());
    JPH::RayCastResult hit;
    if (!shape.CastRay(JPH::RayCast { backend::ToJoltVec3(origin - com),
                         backend::ToJoltVec3(direction) },
          JPH::SubShapeIDCreator {}, hit)) {
      return std::nullopt;
    }
    return origin + (hit.mFraction * direction);
  }
} // namespace

//! A cone's base is at -half_height and its apex at +half_height, along
//! Oxygen +Z.
NOLINT_TEST_F(JoltShapeAxisTest, ConeUsesOxygenZAxisWithApexUp)
{
  AssertBackendAvailabilityContract();
  constexpr auto radius = 0.5F;
  constexpr auto half_height = 1.5F;
  const auto shape = backend::MakeShape(
    ConeShape { .radius = radius, .half_height = half_height });
  ASSERT_HAS_VALUE(shape);

  const auto [low, high] = ShapeSpaceBounds(*shape.value());
  ExpectNear(low, Vec3 { -radius, -radius, -half_height });
  ExpectNear(high, Vec3 { radius, radius, half_height });
  const auto apex = FirstHit(
    *shape.value(), Vec3 { 0.0F, 0.0F, 5.0F }, Vec3 { 0.0F, 0.0F, -10.0F });
  ASSERT_HAS_VALUE(apex);
  ExpectNear(*apex, Vec3 { 0.0F, 0.0F, half_height });
  // Halfway up, the cone is half as wide as its base.
  const auto side = FirstHit(
    *shape.value(), Vec3 { 2.0F, 0.0F, 0.0F }, Vec3 { -2.0F, 0.0F, 0.0F });
  ASSERT_HAS_VALUE(side);
  ExpectNear(*side, Vec3 { radius / 2.0F, 0.0F, 0.0F });
}

//! Shapes smaller than Jolt's default convex radius still build.
NOLINT_TEST_F(JoltShapeAxisTest, SmallBoxAndCylinderBuild)
{
  AssertBackendAvailabilityContract();
  const auto box
    = backend::MakeShape(BoxShape { .extents = Vec3 { 0.01F, 1.0F, 1.0F } });
  ASSERT_HAS_VALUE(box);
  ExpectNear(ShapeSpaceBounds(*box.value()).second, Vec3 { 0.01F, 1.0F, 1.0F });
  const auto cylinder = backend::MakeShape(
    CylinderShape { .radius = 0.01F, .half_height = 0.02F });
  ASSERT_HAS_VALUE(cylinder);
  ExpectNear(
    ShapeSpaceBounds(*cylinder.value()).second, Vec3 { 0.01F, 0.01F, 0.02F });
}

//! A compound places each child at its local transform.
NOLINT_TEST_F(JoltShapeAxisTest, CompoundPlacesEachChild)
{
  AssertBackendAvailabilityContract();
  const auto compound = CompoundShape {
    .children = {
      CompoundShapeChild {
        .shape = SphereShape { .radius = 0.5F },
        .local_position = Vec3 { 2.0F, 0.0F, 0.0F },
      },
      CompoundShapeChild {
        .shape = BoxShape { .extents = Vec3 { 0.5F, 0.5F, 0.5F } },
        .local_position = Vec3 { -2.0F, 0.0F, 0.0F },
        .local_scale = Vec3 { 1.0F, 2.0F, 1.0F },
      },
    },
  };

  const auto shape = backend::MakeShape(compound);

  ASSERT_HAS_VALUE(shape);
  EXPECT_EQ(shape.value()->GetType(), JPH::EShapeType::Compound);
  const auto [low, high] = ShapeSpaceBounds(*shape.value());
  ExpectNear(low, Vec3 { -2.5F, -1.0F, -0.5F });
  ExpectNear(high, Vec3 { 2.5F, 1.0F, 0.5F });
  const auto sphere_top = FirstHit(
    *shape.value(), Vec3 { 2.0F, 0.0F, 5.0F }, Vec3 { 0.0F, 0.0F, -10.0F });
  ASSERT_HAS_VALUE(sphere_top);
  ExpectNear(*sphere_top, Vec3 { 2.0F, 0.0F, 0.5F });
  EXPECT_FALSE(FirstHit(
    *shape.value(), Vec3 { 0.0F, 0.0F, 5.0F }, Vec3 { 0.0F, 0.0F, -10.0F })
      .has_value());
}

//! A compound of one child is that child at its transform; one of none, or
//! with a child the runtime cannot scale, is refused.
NOLINT_TEST_F(JoltShapeAxisTest, CompoundOfOneChildIsThatChildPlaced)
{
  AssertBackendAvailabilityContract();
  const auto single = backend::MakeShape(CompoundShape {
    .children = {
      CompoundShapeChild {
        .shape = SphereShape { .radius = 0.5F },
        .local_position = Vec3 { 0.0F, 0.0F, 3.0F },
      },
    },
  });
  ASSERT_HAS_VALUE(single);
  ExpectNear(
    ShapeSpaceBounds(*single.value()).first, Vec3 { -0.5F, -0.5F, 2.5F });

  const auto empty = backend::MakeShape(CompoundShape {});
  ASSERT_TRUE(empty.has_error());
  EXPECT_EQ(empty.error(), PhysicsError::kShapeCompoundZeroChildren);

  const auto non_uniform_sphere = backend::MakeShape(CompoundShape {
    .children = {
      CompoundShapeChild {
        .shape = SphereShape { .radius = 0.5F },
        .local_scale = Vec3 { 1.0F, 2.0F, 1.0F },
      },
      CompoundShapeChild { .shape = SphereShape { .radius = 0.5F } },
    },
  });
  EXPECT_TRUE(non_uniform_sphere.has_error());
}

//! Only a static body takes a compound that holds a static-only shape.
NOLINT_TEST_F(JoltShapeAxisTest, CompoundWithAPlaneMustBeStatic)
{
  AssertBackendAvailabilityContract();
  EXPECT_TRUE(backend::MustBeStatic(CompoundShape {
    .children = {
      CompoundShapeChild { .shape = PlaneShape {} },
      CompoundShapeChild { .shape = SphereShape {} },
    },
  }));
  EXPECT_FALSE(backend::MustBeStatic(CompoundShape {
    .children = {
      CompoundShapeChild { .shape = BoxShape {} },
      CompoundShapeChild { .shape = SphereShape {} },
    },
  }));
  EXPECT_TRUE(backend::MustBeStatic(PlaneShape {}));
  EXPECT_FALSE(backend::MustBeStatic(ConeShape {}));
}

NOLINT_TEST_F(JoltShapeAxisTest, AxialPrimitiveDimensionsRejectInvalidValues)
{
  AssertBackendAvailabilityContract();
  for (const auto invalid : {
         -1.0F,
         std::numeric_limits<float>::quiet_NaN(),
         std::numeric_limits<float>::infinity(),
         -std::numeric_limits<float>::infinity(),
       }) {
    EXPECT_TRUE(backend::MakeShape(CapsuleShape { invalid, 0.5F }).has_error());
    EXPECT_TRUE(backend::MakeShape(CapsuleShape { 0.5F, invalid }).has_error());
    EXPECT_TRUE(
      backend::MakeShape(CylinderShape { invalid, 0.5F }).has_error());
    EXPECT_TRUE(
      backend::MakeShape(CylinderShape { 0.5F, invalid }).has_error());
  }
  EXPECT_TRUE(backend::MakeShape(CapsuleShape { 0.0F, 0.5F }).has_error());
  EXPECT_TRUE(backend::MakeShape(CylinderShape { 0.0F, 0.5F }).has_error());
  EXPECT_TRUE(backend::MakeShape(CylinderShape { 0.5F, 0.0F }).has_error());
  EXPECT_TRUE(backend::MakeShape(CapsuleShape { 0.5F, 0.0F }).has_value());
}

NOLINT_TEST_F(
  JoltShapeAxisTest, BodyTransformsApplyAfterPrimitiveAxisAdaptation)
{
  AssertBackendAvailabilityContract();
  const auto world = System().Worlds().CreateWorld(world::WorldDesc {});
  ASSERT_TRUE(world.has_value());
  constexpr auto radius = 0.375F;
  constexpr auto half_height = 0.875F;
  constexpr Vec3 kBodyPosition { 4.0F, -3.0F, 2.0F };
  constexpr Vec3 kShapePosition { 0.75F, -0.5F, 1.0F };
  constexpr auto kBodyAngle = 0.4F;
  constexpr auto kShapeAngle = 0.6F;
  constexpr auto kShapeScale = 1.5F;
  constexpr auto kRayLength = 2.0F;
  const std::array<CollisionShape, 2> shapes {
    CapsuleShape { .radius = radius, .half_height = half_height },
    CylinderShape { .radius = radius, .half_height = half_height },
  };
  for (const auto& shape : shapes) {
    for (const auto transform_case : { 0, 1, 2 }) {
      body::BodyDesc desc {};
      desc.type = body::BodyType::kStatic;
      desc.shape = shape;
      if (transform_case == 1) {
        desc.initial_position = kBodyPosition;
        desc.initial_rotation = glm::angleAxis(kBodyAngle, space::move::Up);
        desc.shape_local_position = kShapePosition;
        desc.shape_local_rotation
          = glm::angleAxis(kShapeAngle, space::move::Right);
        desc.shape_local_scale = Vec3 { kShapeScale };
      } else if (transform_case == 2) {
        // A collider fitted to a Y-long imported mesh: shape-local -90X
        // aligns canonical Z to model Y; the authored body +90X stands the
        // model upright. The backend primitive basis precedes both.
        desc.initial_rotation
          = glm::angleAxis(math::HalfPi, space::move::Right);
        desc.shape_local_rotation
          = glm::angleAxis(-math::HalfPi, space::move::Right);
      }
      const auto body = System().Bodies().CreateBody(world.value(), desc);
      ASSERT_TRUE(body.has_value());
      const auto height_extent = half_height
        + (std::holds_alternative<CapsuleShape>(shape) ? radius : 0.0F);
      const Vec3 extent { radius, radius, height_extent };
      for (const auto& axis : kAxes) {
        for (const auto sign : { -1.0F, 1.0F }) {
          const auto normal = sign * axis;
          const auto world_normal
            = desc.initial_rotation * (desc.shape_local_rotation * normal);
          const auto world_position = desc.initial_position
            + desc.initial_rotation
              * (desc.shape_local_position
                + desc.shape_local_rotation
                  * (desc.shape_local_scale * normal * extent));
          query::RaycastDesc ray {};
          ray.origin = world_position + world_normal;
          ray.direction = -world_normal;
          ray.max_distance = kRayLength;
          const auto hit = System().Queries().Raycast(world.value(), ray);
          ASSERT_HAS_VALUE(hit);
          const auto& found = hit.value();
          ASSERT_HAS_VALUE(found);
          EXPECT_EQ(found->body_id, body.value());
          EXPECT_NEAR(found->distance, 1.0F, kTolerance);
          ExpectNear(found->position, world_position);
          ExpectNear(found->normal, world_normal);
        }
      }
      const auto rotation
        = System().Bodies().GetBodyRotation(world.value(), body.value());
      ASSERT_TRUE(rotation.has_value());
      EXPECT_NEAR(
        glm::dot(rotation.value(), desc.initial_rotation), 1.0F, kTolerance);
      EXPECT_TRUE(
        System().Bodies().DestroyBody(world.value(), body.value()).has_value());
    }
  }
  EXPECT_TRUE(System().Worlds().DestroyWorld(world.value()).has_value());
}

NOLINT_TEST_F(JoltShapeAxisTest, CharacterCapsuleUsesOxygenZAxis)
{
  AssertBackendAvailabilityContract();
  const auto world = System().Worlds().CreateWorld(world::WorldDesc {});
  ASSERT_TRUE(world.has_value());
  character::CharacterDesc desc {};
  constexpr auto kRadius = 0.5F;
  constexpr auto kHalfCylinder = 1.0F;
  constexpr auto kRayStart = 3.0F;
  constexpr auto kRayLength = 4.0F;
  desc.shape = CapsuleShape { .radius = kRadius, .half_height = kHalfCylinder };
  const auto character
    = System().Characters().CreateCharacter(world.value(), desc);
  ASSERT_TRUE(character.has_value());
  for (const auto& axis : kAxes) {
    query::RaycastDesc ray {};
    ray.origin = kRayStart * axis;
    ray.direction = -axis;
    ray.max_distance = kRayLength;
    const auto hit = System().Queries().Raycast(world.value(), ray);
    ASSERT_TRUE(hit.has_value());
    if (!hit.value().has_value()) {
      FAIL() << "The character capsule must intersect each axis ray.";
    }
    ExpectNear(hit.value()->position,
      axis * Vec3 { kRadius, kRadius, kHalfCylinder + kRadius });
  }
  EXPECT_TRUE(System()
      .Characters()
      .DestroyCharacter(world.value(), character.value())
      .has_value());
  EXPECT_TRUE(System().Worlds().DestroyWorld(world.value()).has_value());
}

} // namespace oxygen::physics::test::jolt
