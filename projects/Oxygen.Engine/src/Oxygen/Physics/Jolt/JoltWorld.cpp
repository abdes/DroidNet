//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cmath>
#include <limits>
#include <mutex>
#include <unordered_set>
#include <utility>

#include <Jolt/Jolt.h> // must be first (keep separate)

#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/Memory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyManager.h>
#include <Jolt/Physics/Body/BodyType.h>
#include <Jolt/Physics/EPhysicsUpdateError.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>

#include <Oxygen/Physics/Jolt/JoltWorld.h>

namespace {

constexpr JPH::ObjectLayer kNonMovingObjectLayer = 0;
constexpr JPH::ObjectLayer kMovingObjectLayer = 1;
constexpr JPH::BroadPhaseLayer kNonMovingBroadPhaseLayer { 0 };
constexpr JPH::BroadPhaseLayer kMovingBroadPhaseLayer { 1 };
constexpr uint32_t kMaxBodies = 65536U;
constexpr uint32_t kMaxBodyPairs = 65536U;
constexpr uint32_t kMaxContactConstraints = 10240U;
constexpr uint32_t kMaxPhysicsJobs = 1024U;
constexpr uint32_t kMaxPhysicsBarriers = 64U;
constexpr size_t kTempAllocatorBytes = 10U * 1024U * 1024U;

class BroadPhaseLayerInterfaceImpl final
  : public JPH::BroadPhaseLayerInterface {
public:
  [[nodiscard]] auto GetNumBroadPhaseLayers() const -> JPH::uint override
  {
    return 2U;
  }

  [[nodiscard]] auto GetBroadPhaseLayer(const JPH::ObjectLayer layer) const
    -> JPH::BroadPhaseLayer override
  {
    return layer == kNonMovingObjectLayer ? kNonMovingBroadPhaseLayer
                                          : kMovingBroadPhaseLayer;
  }

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
  [[nodiscard]] auto GetBroadPhaseLayerName(
    const JPH::BroadPhaseLayer layer) const -> const char* override
  {
    if (layer == kNonMovingBroadPhaseLayer) {
      return "NonMoving";
    }
    if (layer == kMovingBroadPhaseLayer) {
      return "Moving";
    }
    return "Unknown";
  }
#endif
};

class ObjectLayerPairFilterImpl final : public JPH::ObjectLayerPairFilter {
public:
  [[nodiscard]] auto ShouldCollide(const JPH::ObjectLayer layer1,
    const JPH::ObjectLayer layer2) const -> bool override
  {
    if (layer1 == kNonMovingObjectLayer && layer2 == kNonMovingObjectLayer) {
      return false;
    }
    return true;
  }
};

class ObjectVsBroadPhaseLayerFilterImpl final
  : public JPH::ObjectVsBroadPhaseLayerFilter {
public:
  [[nodiscard]] auto ShouldCollide(const JPH::ObjectLayer layer1,
    const JPH::BroadPhaseLayer layer2) const -> bool override
  {
    if (layer1 == kNonMovingObjectLayer) {
      return layer2 == kMovingBroadPhaseLayer;
    }
    return true;
  }
};

auto ToJoltVec3(const oxygen::Vec3& value) -> JPH::Vec3
{
  return JPH::Vec3 { value.x, value.y, value.z };
}

auto ToOxygenVec3(const JPH::RVec3& value) -> oxygen::Vec3
{
  return oxygen::Vec3 { static_cast<float>(value.GetX()),
    static_cast<float>(value.GetY()), static_cast<float>(value.GetZ()) };
}

auto ToOxygenQuat(const JPH::Quat& value) -> oxygen::Quat
{
  return oxygen::Quat {
    value.GetW(),
    value.GetX(),
    value.GetY(),
    value.GetZ(),
  };
}

std::mutex g_jolt_runtime_mutex;
uint32_t g_jolt_runtime_ref_count = 0U;

auto AcquireJoltRuntime() -> bool
{
  std::scoped_lock lock(g_jolt_runtime_mutex);
  if (g_jolt_runtime_ref_count == 0U) {
    JPH::RegisterDefaultAllocator();
    JPH::Factory::sInstance = new JPH::Factory();
    JPH::RegisterTypes();
  }
  g_jolt_runtime_ref_count += 1U;
  return true;
}

auto ReleaseJoltRuntime() -> void
{
  std::scoped_lock lock(g_jolt_runtime_mutex);
  if (g_jolt_runtime_ref_count == 0U) {
    return;
  }
  g_jolt_runtime_ref_count -= 1U;
  if (g_jolt_runtime_ref_count == 0U) {
    JPH::UnregisterTypes();
    delete JPH::Factory::sInstance;
    JPH::Factory::sInstance = nullptr;
  }
}

} // namespace

struct oxygen::physics::jolt::JoltWorld::WorldState final {
  WorldState(const world::WorldDesc& desc)
    : temp_allocator(kTempAllocatorBytes)
    , job_system(kMaxPhysicsJobs, kMaxPhysicsBarriers)
  {
    physics_system.Init(kMaxBodies, 0U, kMaxBodyPairs, kMaxContactConstraints,
      broad_phase_layer_interface, object_vs_broad_phase_layer_filter,
      object_layer_pair_filter);
    physics_system.SetGravity(ToJoltVec3(desc.gravity));
    collision_filter = desc.collision_filter;
  }

  JPH::PhysicsSystem physics_system {};
  JPH::TempAllocatorImpl temp_allocator;
  JPH::JobSystemThreadPool job_system;
  BroadPhaseLayerInterfaceImpl broad_phase_layer_interface {};
  ObjectLayerPairFilterImpl object_layer_pair_filter {};
  ObjectVsBroadPhaseLayerFilterImpl object_vs_broad_phase_layer_filter {};
  std::shared_ptr<ICollisionFilter> collision_filter {};
  std::unordered_set<BodyId> body_ids {};
};

oxygen::physics::jolt::JoltWorld::JoltWorld()
{
  runtime_ready_ = AcquireJoltRuntime();
}

oxygen::physics::jolt::JoltWorld::~JoltWorld()
{
  worlds_.clear();
  if (runtime_ready_) {
    ReleaseJoltRuntime();
  }
}

auto oxygen::physics::jolt::JoltWorld::CreateWorld(const world::WorldDesc& desc)
  -> PhysicsResult<WorldId>
{
  if (!runtime_ready_) {
    return Err(PhysicsError::kBackendInitFailed);
  }
  if (next_world_id_ == std::numeric_limits<uint32_t>::max()) {
    return Err(PhysicsError::kBackendInitFailed);
  }

  try {
    const auto world_id = WorldId { next_world_id_++ };
    worlds_.emplace(world_id, std::make_unique<WorldState>(desc));
    return Ok(world_id);
  } catch (...) {
    return Err(PhysicsError::kBackendInitFailed);
  }
}

auto oxygen::physics::jolt::JoltWorld::DestroyWorld(const WorldId world_id)
  -> PhysicsResult<void>
{
  if (worlds_.erase(world_id) == 0U) {
    return Err(PhysicsError::kWorldNotFound);
  }
  return PhysicsResult<void>::Ok();
}

auto oxygen::physics::jolt::JoltWorld::Step(const WorldId world_id,
  const float delta_time, const int max_sub_steps, const float fixed_dt_seconds)
  -> PhysicsResult<void>
{
  auto world = TryGetWorld(world_id);
  if (world == nullptr) {
    return Err(PhysicsError::kWorldNotFound);
  }
  if (delta_time <= 0.0F || fixed_dt_seconds <= 0.0F || max_sub_steps <= 0) {
    return Err(PhysicsError::kInvalidArgument);
  }

  const auto required_sub_steps = static_cast<int>(std::ceil(delta_time
    / std::max(fixed_dt_seconds, std::numeric_limits<float>::epsilon())));
  const auto collision_steps = std::clamp(required_sub_steps, 1, max_sub_steps);
  const auto update_error = world->physics_system.Update(
    delta_time, collision_steps, &world->temp_allocator, &world->job_system);
  if (update_error != JPH::EPhysicsUpdateError::None) {
    return Err(PhysicsError::kBackendInitFailed);
  }
  return PhysicsResult<void>::Ok();
}

auto oxygen::physics::jolt::JoltWorld::GetActiveBodyTransforms(
  const WorldId world_id,
  std::span<system::ActiveBodyTransform> out_transforms) const
  -> PhysicsResult<size_t>
{
  const auto world = TryGetWorld(world_id);
  if (world == nullptr) {
    return Err(PhysicsError::kWorldNotFound);
  }

  JPH::BodyIDVector active_body_ids {};
  world->physics_system.GetActiveBodies(
    JPH::EBodyType::RigidBody, active_body_ids);

  const auto copy_count
    = std::min(out_transforms.size(), active_body_ids.size());
  const auto& body_interface = world->physics_system.GetBodyInterface();
  for (size_t i = 0; i < copy_count; ++i) {
    const auto jolt_body_id = active_body_ids[i];
    const auto position = body_interface.GetPosition(jolt_body_id);
    const auto rotation = body_interface.GetRotation(jolt_body_id);
    out_transforms[i] = system::ActiveBodyTransform {
      .body_id = BodyId { jolt_body_id.GetIndexAndSequenceNumber() },
      .user_data = body_interface.GetUserData(jolt_body_id),
      .position = ToOxygenVec3(position),
      .rotation = ToOxygenQuat(rotation),
    };
  }

  return Ok(copy_count);
}

auto oxygen::physics::jolt::JoltWorld::GetGravity(const WorldId world_id) const
  -> PhysicsResult<Vec3>
{
  const auto world = TryGetWorld(world_id);
  if (world == nullptr) {
    return Err(PhysicsError::kWorldNotFound);
  }
  const auto gravity = world->physics_system.GetGravity();
  return Ok(Vec3 { gravity.GetX(), gravity.GetY(), gravity.GetZ() });
}

auto oxygen::physics::jolt::JoltWorld::SetGravity(
  const WorldId world_id, const Vec3& gravity) -> PhysicsResult<void>
{
  auto world = TryGetWorld(world_id);
  if (world == nullptr) {
    return Err(PhysicsError::kWorldNotFound);
  }
  world->physics_system.SetGravity(ToJoltVec3(gravity));
  return PhysicsResult<void>::Ok();
}

auto oxygen::physics::jolt::JoltWorld::TryGetBodyInterface(
  const WorldId world_id) noexcept -> observer_ptr<JPH::BodyInterface>
{
  auto world = TryGetWorld(world_id);
  if (world == nullptr) {
    return observer_ptr<JPH::BodyInterface> {};
  }
  return observer_ptr<JPH::BodyInterface> {
    &world->physics_system.GetBodyInterface(),
  };
}

auto oxygen::physics::jolt::JoltWorld::TryGetBodyInterface(
  const WorldId world_id) const noexcept
  -> observer_ptr<const JPH::BodyInterface>
{
  const auto world = TryGetWorld(world_id);
  if (world == nullptr) {
    return observer_ptr<const JPH::BodyInterface> {};
  }
  return observer_ptr<const JPH::BodyInterface> {
    &world->physics_system.GetBodyInterface(),
  };
}

auto oxygen::physics::jolt::JoltWorld::HasBody(
  const WorldId world_id, const BodyId body_id) const noexcept -> bool
{
  const auto world = TryGetWorld(world_id);
  if (world == nullptr) {
    return false;
  }
  return world->body_ids.contains(body_id);
}

auto oxygen::physics::jolt::JoltWorld::RegisterBody(
  const WorldId world_id, const BodyId body_id) -> PhysicsResult<void>
{
  auto world = TryGetWorld(world_id);
  if (world == nullptr) {
    return Err(PhysicsError::kWorldNotFound);
  }
  if (!world->body_ids.insert(body_id).second) {
    return Err(PhysicsError::kAlreadyExists);
  }
  return PhysicsResult<void>::Ok();
}

auto oxygen::physics::jolt::JoltWorld::UnregisterBody(
  const WorldId world_id, const BodyId body_id) -> PhysicsResult<void>
{
  auto world = TryGetWorld(world_id);
  if (world == nullptr) {
    return Err(PhysicsError::kWorldNotFound);
  }
  if (world->body_ids.erase(body_id) == 0U) {
    return Err(PhysicsError::kBodyNotFound);
  }
  return PhysicsResult<void>::Ok();
}

auto oxygen::physics::jolt::JoltWorld::TryGetWorld(
  const WorldId world_id) noexcept -> observer_ptr<WorldState>
{
  const auto it = worlds_.find(world_id);
  if (it == worlds_.end()) {
    return observer_ptr<WorldState> {};
  }
  return observer_ptr<WorldState> { it->second.get() };
}

auto oxygen::physics::jolt::JoltWorld::TryGetWorld(
  const WorldId world_id) const noexcept -> observer_ptr<const WorldState>
{
  const auto it = worlds_.find(world_id);
  if (it == worlds_.end()) {
    return observer_ptr<const WorldState> {};
  }
  return observer_ptr<const WorldState> { it->second.get() };
}
