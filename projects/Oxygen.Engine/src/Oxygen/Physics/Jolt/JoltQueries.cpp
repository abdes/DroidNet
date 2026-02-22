//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cmath>
#include <ranges>

#include <Jolt/Jolt.h> // Must always be first (keep separate)

#include <Jolt/Math/DMat44.h>
#include <Jolt/Physics/Body/BodyFilter.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollideShape.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/NarrowPhaseQuery.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/ShapeCast.h>

#include <Oxygen/Core/Constants.h>
#include <Oxygen/Physics/Jolt/Converters.h>
#include <Oxygen/Physics/Jolt/JoltQueries.h>
#include <Oxygen/Physics/Jolt/JoltWorld.h>

namespace {

class IgnoreBodiesFilter final : public JPH::BodyFilter {
public:
  explicit IgnoreBodiesFilter(
    const std::span<const oxygen::physics::BodyId> ids)
    : ignored_ids_(ids)
  {
  }

  [[nodiscard]] auto ShouldCollide(const JPH::BodyID& in_body_id) const
    -> bool override
  {
    const auto id = oxygen::physics::BodyId {
      in_body_id.GetIndexAndSequenceNumber(),
    };
    return std::ranges::find(ignored_ids_, id) == ignored_ids_.end();
  }

private:
  std::span<const oxygen::physics::BodyId> ignored_ids_ {};
};

auto NormalizeOrFallback(
  const oxygen::Vec3& value, const oxygen::Vec3& fallback) -> oxygen::Vec3
{
  const auto len_sq = value.x * value.x + value.y * value.y + value.z * value.z;
  if (len_sq <= 1.0e-8F) {
    return fallback;
  }
  const auto inv_len = 1.0F / std::sqrt(len_sq);
  return oxygen::Vec3 { value.x * inv_len, value.y * inv_len,
    value.z * inv_len };
}

} // namespace

oxygen::physics::jolt::JoltQueries::JoltQueries(JoltWorld& world)
  : world_(&world)
{
}

auto oxygen::physics::jolt::JoltQueries::Raycast(
  const WorldId world_id, const query::RaycastDesc& desc) const
  -> PhysicsResult<query::OptionalRaycastHit>
{
  const auto* world = world_.get();
  if (world == nullptr) {
    return Err(PhysicsError::kNotInitialized);
  }
  const auto query = world->TryGetNarrowPhaseQuery(world_id);
  const auto body_interface = world->TryGetBodyInterface(world_id);
  if (query == nullptr || body_interface == nullptr) {
    return Err(PhysicsError::kWorldNotFound);
  }
  if (desc.max_distance <= 0.0F) {
    return Err(PhysicsError::kInvalidArgument);
  }

  IgnoreBodiesFilter body_filter(desc.ignore_bodies);
  JPH::RayCastResult hit {};
  const auto jolt_hit
    = query->CastRay(JPH::RRayCast(ToJoltRVec3(desc.origin),
                       ToJoltVec3(desc.direction * desc.max_distance)),
      hit, {}, {}, body_filter);
  if (!jolt_hit) {
    return Ok(query::OptionalRaycastHit {});
  }

  const auto hit_position
    = desc.origin + desc.direction * (desc.max_distance * hit.mFraction);
  const auto normal = NormalizeOrFallback(
    oxygen::Vec3 { -desc.direction.x, -desc.direction.y, -desc.direction.z },
    oxygen::space::move::Up);

  query::RaycastHit result {};
  result.body_id = BodyId { hit.mBodyID.GetIndexAndSequenceNumber() };
  result.user_data = body_interface->GetUserData(hit.mBodyID);
  result.position = hit_position;
  result.normal = normal;
  result.distance = desc.max_distance * hit.mFraction;
  return Ok(query::OptionalRaycastHit { result });
}

auto oxygen::physics::jolt::JoltQueries::Sweep(const WorldId world_id,
  const query::SweepDesc& desc, std::span<query::SweepHit> out_hits) const
  -> PhysicsResult<size_t>
{
  const auto* world = world_.get();
  if (world == nullptr) {
    return Err(PhysicsError::kNotInitialized);
  }
  const auto query = world->TryGetNarrowPhaseQuery(world_id);
  const auto body_interface = world->TryGetBodyInterface(world_id);
  if (query == nullptr || body_interface == nullptr) {
    return Err(PhysicsError::kWorldNotFound);
  }
  if (desc.max_distance <= 0.0F) {
    return Err(PhysicsError::kInvalidArgument);
  }

  const auto shape_result = MakeShape(desc.shape);
  if (shape_result.has_error()) {
    return Err(shape_result.error());
  }

  IgnoreBodiesFilter body_filter(desc.ignore_bodies);
  JPH::AllHitCollisionCollector<JPH::CastShapeCollector> collector {};
  query->CastShape(
    JPH::RShapeCast(shape_result.value().GetPtr(), JPH::Vec3::sReplicate(1.0F),
      JPH::RMat44::sTranslation(ToJoltRVec3(desc.origin)),
      ToJoltVec3(desc.direction * desc.max_distance)),
    JPH::ShapeCastSettings {}, JPH::RVec3::sZero(), collector, {}, {},
    body_filter);
  collector.Sort();

  const auto copy_count = std::min(out_hits.size(), collector.mHits.size());
  for (size_t i = 0; i < copy_count; ++i) {
    const auto& hit = collector.mHits[i];
    out_hits[i] = query::SweepHit {
      .body_id = BodyId { hit.mBodyID2.GetIndexAndSequenceNumber() },
      .user_data = body_interface->GetUserData(hit.mBodyID2),
      .position = ToOxygenVec3(hit.mContactPointOn2),
      .normal = -ToOxygenVec3(hit.mPenetrationAxis.Normalized()),
      .distance = desc.max_distance * hit.mFraction,
    };
  }

  return Ok(copy_count);
}

auto oxygen::physics::jolt::JoltQueries::Overlap(const WorldId world_id,
  const query::OverlapDesc& desc, std::span<uint64_t> out_user_data) const
  -> PhysicsResult<size_t>
{
  const auto* world = world_.get();
  if (world == nullptr) {
    return Err(PhysicsError::kNotInitialized);
  }
  const auto query = world->TryGetNarrowPhaseQuery(world_id);
  const auto body_interface = world->TryGetBodyInterface(world_id);
  if (query == nullptr || body_interface == nullptr) {
    return Err(PhysicsError::kWorldNotFound);
  }

  const auto shape_result = MakeShape(desc.shape);
  if (shape_result.has_error()) {
    return Err(shape_result.error());
  }

  IgnoreBodiesFilter body_filter(desc.ignore_bodies);
  JPH::AllHitCollisionCollector<JPH::CollideShapeCollector> collector {};
  query->CollideShape(shape_result.value().GetPtr(),
    JPH::Vec3::sReplicate(1.0F),
    JPH::RMat44::sTranslation(ToJoltRVec3(desc.center)),
    JPH::CollideShapeSettings {}, JPH::RVec3::sZero(), collector, {}, {},
    body_filter);

  const auto copy_count
    = std::min(out_user_data.size(), collector.mHits.size());
  for (size_t i = 0; i < copy_count; ++i) {
    out_user_data[i] = body_interface->GetUserData(collector.mHits[i].mBodyID2);
  }
  return Ok(copy_count);
}
