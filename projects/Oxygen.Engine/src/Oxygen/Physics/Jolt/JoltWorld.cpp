//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cmath>
#include <deque>
#include <functional>
#include <limits>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <Jolt/Jolt.h> // must be first (keep separate)

#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/Memory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyManager.h>
#include <Jolt/Physics/Body/BodyType.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/EPhysicsUpdateError.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Vehicle/VehicleConstraint.h>
#include <Jolt/RegisterTypes.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Physics/Jolt/Converters.h>
#include <Oxygen/Physics/Jolt/JoltWorld.h>

namespace {

constexpr JPH::ObjectLayer kNonMovingObjectLayer = 0;
constexpr JPH::ObjectLayer kMovingObjectLayer = 1;
constexpr oxygen::physics::BroadPhaseLayer kNonMovingBroadPhaseLayer = 0U;
constexpr oxygen::physics::BroadPhaseLayer kMovingBroadPhaseLayer = 1U;
// Jolt asserts layer_count < cBroadPhaseLayerInvalid (255), so max count is
// 254.
constexpr JPH::uint kBroadPhaseLayerCount = static_cast<JPH::uint>(
  std::numeric_limits<JPH::BroadPhaseLayer::Type>::max() - 1U);
constexpr uint32_t kCollisionLayerBitCount = sizeof(uint32_t) * 8U;
constexpr uint32_t kMaxBodies = 65536U;
constexpr uint32_t kMaxBodyPairs = 65536U;
constexpr uint32_t kMaxContactConstraints = 10240U;
constexpr uint32_t kMaxPhysicsJobs = 1024U;
constexpr uint32_t kMaxPhysicsBarriers = 64U;
constexpr size_t kTempAllocatorBytes = 10U * 1024U * 1024U;

[[nodiscard]] auto RemoveInvalidVehicleConstraintsBeforeStep(
  JPH::PhysicsSystem& physics_system,
  const std::unordered_set<oxygen::physics::BodyId>& known_body_ids) -> bool
{
  const auto constraints = physics_system.GetConstraints();
  std::vector<JPH::Constraint*> invalid_constraints {};
  invalid_constraints.reserve(constraints.size());
  std::vector<JPH::VehicleConstraint*> invalid_vehicle_listeners {};

  for (const auto& constraint_ref : constraints) {
    if (constraint_ref == nullptr) {
      continue;
    }
    auto* constraint = constraint_ref.GetPtr();
    if (constraint == nullptr
      || constraint->GetSubType() != JPH::EConstraintSubType::Vehicle) {
      continue;
    }

    auto* vehicle_constraint = static_cast<JPH::VehicleConstraint*>(constraint);
    const auto* vehicle_body = vehicle_constraint->GetVehicleBody();
    const auto has_body = vehicle_body != nullptr;
    const auto body_id = has_body
      ? oxygen::physics::BodyId {
          vehicle_body->GetID().GetIndexAndSequenceNumber(),
        }
      : oxygen::physics::kInvalidBodyId;
    const auto body_known = has_body && known_body_ids.contains(body_id);
    const auto is_rigid = has_body && vehicle_body->IsRigidBody();
    const auto is_soft = has_body && vehicle_body->IsSoftBody();
    const auto is_dynamic = has_body && vehicle_body->IsDynamic();
    const auto is_static = has_body && vehicle_body->IsStatic();
    const auto is_kinematic = has_body && vehicle_body->IsKinematic();
    if (has_body && body_known && is_rigid && is_dynamic) {
      continue;
    }

    LOG_F(ERROR,
      "JoltWorld: invalid vehicle constraint before step "
      "(constraint_ptr={} body_ptr={} body_id={} known_body={} rigid={} "
      "soft={} dynamic={} static={} kinematic={}).",
      static_cast<const void*>(vehicle_constraint),
      static_cast<const void*>(vehicle_body), has_body ? body_id.get() : 0U,
      body_known, is_rigid, is_soft, is_dynamic, is_static, is_kinematic);
    invalid_constraints.push_back(vehicle_constraint);
    invalid_vehicle_listeners.push_back(vehicle_constraint);
  }

  if (invalid_constraints.empty()) {
    return false;
  }

  for (auto* listener : invalid_vehicle_listeners) {
    physics_system.RemoveStepListener(listener);
  }
  physics_system.RemoveConstraints(
    invalid_constraints.data(), static_cast<int>(invalid_constraints.size()));
  LOG_F(ERROR,
    "JoltWorld: removed {} invalid vehicle constraint(s) before step.",
    invalid_constraints.size());
  return true;
}

struct ObjectLayerMetadata final {
  oxygen::physics::CollisionLayer collision_layer {
    oxygen::physics::kCollisionLayerDefault,
  };
  oxygen::physics::CollisionMask collision_mask {
    oxygen::physics::kCollisionMaskAll,
  };
  oxygen::physics::BroadPhaseLayer broad_phase_layer {
    kMovingBroadPhaseLayer,
  };
  bool is_non_moving { false };
};

struct ObjectLayerKey final {
  bool is_non_moving { false };
  uint32_t collision_layer { 0U };
  uint32_t collision_mask { 0U };

  [[nodiscard]] auto operator==(const ObjectLayerKey&) const -> bool = default;
};

struct ObjectLayerKeyHash final {
  [[nodiscard]] auto operator()(const ObjectLayerKey& key) const noexcept
    -> size_t
  {
    size_t seed = std::hash<uint32_t> {}(key.collision_layer);
    seed ^= std::hash<uint32_t> {}(key.collision_mask) + 0x9e3779b9U
      + (seed << 6U) + (seed >> 2U);
    seed ^= std::hash<bool> {}(key.is_non_moving) + 0x9e3779b9U + (seed << 6U)
      + (seed >> 2U);
    return seed;
  }
};

[[nodiscard]] auto IsCollisionLayerBitAddressable(
  const oxygen::physics::CollisionLayer layer) noexcept -> bool
{
  return layer.get() < kCollisionLayerBitCount;
}

[[nodiscard]] auto CollisionLayerBit(
  const oxygen::physics::CollisionLayer layer) noexcept -> uint32_t
{
  if (!IsCollisionLayerBitAddressable(layer)) {
    return 0U;
  }
  return 1U << layer.get();
}

[[nodiscard]] auto ShouldMaskAllowLayer(
  const oxygen::physics::CollisionMask mask,
  const oxygen::physics::CollisionLayer layer) noexcept -> bool
{
  const auto layer_bit = CollisionLayerBit(layer);
  return layer_bit != 0U && (mask.get() & layer_bit) != 0U;
}

[[nodiscard]] auto IsNonMovingBodyType(
  const oxygen::physics::body::BodyType body_type) noexcept -> bool
{
  return body_type == oxygen::physics::body::BodyType::kStatic;
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
  class BroadPhaseLayerInterfaceImpl final
    : public JPH::BroadPhaseLayerInterface {
  public:
    explicit BroadPhaseLayerInterfaceImpl(WorldState& world_state)
      : world_state_(world_state)
    {
    }

    [[nodiscard]] auto GetNumBroadPhaseLayers() const -> JPH::uint override
    {
      return kBroadPhaseLayerCount;
    }

    [[nodiscard]] auto GetBroadPhaseLayer(const JPH::ObjectLayer layer) const
      -> JPH::BroadPhaseLayer override
    {
      return JPH::BroadPhaseLayer {
        world_state_.GetBroadPhaseLayerForObjectLayer(layer),
      };
    }

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    [[nodiscard]] auto GetBroadPhaseLayerName(
      const JPH::BroadPhaseLayer layer) const -> const char* override
    {
      if (layer.GetValue() == kNonMovingBroadPhaseLayer) {
        return "NonMoving";
      }
      if (layer.GetValue() == kMovingBroadPhaseLayer) {
        return "Moving";
      }
      return "Authored";
    }
#endif

  private:
    WorldState& world_state_;
  };

  class ObjectLayerPairFilterImpl final : public JPH::ObjectLayerPairFilter {
  public:
    explicit ObjectLayerPairFilterImpl(WorldState& world_state)
      : world_state_(world_state)
    {
    }

    [[nodiscard]] auto ShouldCollide(const JPH::ObjectLayer layer1,
      const JPH::ObjectLayer layer2) const -> bool override
    {
      return world_state_.ShouldObjectLayersCollide(layer1, layer2);
    }

  private:
    WorldState& world_state_;
  };

  class ObjectVsBroadPhaseLayerFilterImpl final
    : public JPH::ObjectVsBroadPhaseLayerFilter {
  public:
    explicit ObjectVsBroadPhaseLayerFilterImpl(WorldState& world_state)
      : world_state_(world_state)
    {
    }

    [[nodiscard]] auto ShouldCollide(const JPH::ObjectLayer layer1,
      const JPH::BroadPhaseLayer layer2) const -> bool override
    {
      return world_state_.ShouldObjectVsBroadPhaseLayerCollide(layer1, layer2);
    }

  private:
    WorldState& world_state_;
  };

  class ContactListenerImpl final : public JPH::ContactListener {
  public:
    explicit ContactListenerImpl(WorldState& world_state)
      : world_state_(world_state)
    {
    }

    auto OnContactAdded(const JPH::Body& in_body1, const JPH::Body& in_body2,
      const JPH::ContactManifold& in_manifold,
      JPH::ContactSettings& io_settings) -> void override
    {
      const auto event_type = io_settings.mIsSensor
        ? events::PhysicsEventType::kTriggerBegin
        : events::PhysicsEventType::kContactBegin;
      PushEvent(event_type, in_body1, in_body2, in_manifold);
    }

    auto OnContactRemoved(const JPH::SubShapeIDPair& in_sub_shape_pair)
      -> void override
    {
      std::scoped_lock lock(world_state_.event_mutex);
      const auto it
        = world_state_.active_contact_events.find(in_sub_shape_pair);
      if (it == world_state_.active_contact_events.end()) {
        return;
      }
      events::PhysicsEvent removed = it->second;
      removed.type = removed.type == events::PhysicsEventType::kTriggerBegin
        ? events::PhysicsEventType::kTriggerEnd
        : events::PhysicsEventType::kContactEnd;
      removed.contact_normal = Vec3 { 0.0F, 0.0F, 0.0F };
      removed.contact_position = Vec3 { 0.0F, 0.0F, 0.0F };
      removed.penetration_depth = 0.0F;
      removed.applied_impulse = Vec3 { 0.0F, 0.0F, 0.0F };
      world_state_.pending_events.push_back(removed);
      world_state_.active_contact_events.erase(it);
    }

  private:
    auto PushEvent(const events::PhysicsEventType type, const JPH::Body& body1,
      const JPH::Body& body2, const JPH::ContactManifold& manifold) -> void
    {
      events::PhysicsEvent event {};
      event.type = type;
      event.body_a = BodyId { body1.GetID().GetIndexAndSequenceNumber() };
      event.body_b = BodyId { body2.GetID().GetIndexAndSequenceNumber() };
      event.user_data_a = body1.GetUserData();
      event.user_data_b = body2.GetUserData();
      event.contact_normal = ToOxygenVec3(manifold.mWorldSpaceNormal);
      if (!manifold.mRelativeContactPointsOn1.empty()) {
        event.contact_position
          = ToOxygenVec3(manifold.GetWorldSpaceContactPointOn1(0));
      } else {
        event.contact_position = Vec3 { 0.0F, 0.0F, 0.0F };
      }
      event.penetration_depth = manifold.mPenetrationDepth;
      event.applied_impulse = Vec3 { 0.0F, 0.0F, 0.0F };

      const auto key = JPH::SubShapeIDPair { body1.GetID(),
        manifold.mSubShapeID1, body2.GetID(), manifold.mSubShapeID2 };

      std::scoped_lock lock(world_state_.event_mutex);
      world_state_.pending_events.push_back(event);
      world_state_.active_contact_events.insert_or_assign(key, event);
    }

    WorldState& world_state_;
  };

  WorldState(const world::WorldDesc& desc)
    : temp_allocator(kTempAllocatorBytes)
    , job_system(kMaxPhysicsJobs, kMaxPhysicsBarriers)
    , collision_filter(desc.collision_filter)
    , broad_phase_layer_interface(*this)
    , object_layer_pair_filter(*this)
    , object_vs_broad_phase_layer_filter(*this)
    , contact_listener(*this)
  {
    InitializeDefaultObjectLayers();
    physics_system.Init(kMaxBodies, 0U, kMaxBodyPairs, kMaxContactConstraints,
      broad_phase_layer_interface, object_vs_broad_phase_layer_filter,
      object_layer_pair_filter);
    physics_system.SetGravity(ToJoltVec3(desc.gravity));
    physics_system.SetContactListener(&contact_listener);
  }

  [[nodiscard]] auto ResolveObjectLayer(const body::BodyType body_type,
    const CollisionLayer collision_layer, const CollisionMask collision_mask)
    -> PhysicsResult<JPH::ObjectLayer>
  {
    if (!IsCollisionLayerBitAddressable(collision_layer)) {
      return Err(PhysicsError::kInvalidCollisionMask);
    }

    const auto is_non_moving = IsNonMovingBodyType(body_type);
    const auto key = ObjectLayerKey {
      .is_non_moving = is_non_moving,
      .collision_layer = collision_layer.get(),
      .collision_mask = collision_mask.get(),
    };

    std::scoped_lock lock(collision_layer_mutex);
    const auto existing = object_layers.find(key);
    if (existing != object_layers.end()) {
      return Ok(existing->second);
    }

    constexpr auto kMaxObjectLayerValue
      = static_cast<size_t>(std::numeric_limits<JPH::ObjectLayer>::max());
    constexpr auto kObjectLayerCapacity = kMaxObjectLayerValue + 1U;
    if (object_layer_metadata.size() >= kObjectLayerCapacity) {
      return Err(PhysicsError::kBackendInitFailed);
    }

    const auto object_layer
      = static_cast<JPH::ObjectLayer>(object_layer_metadata.size());
    object_layer_metadata.push_back(ObjectLayerMetadata {
      .collision_layer = collision_layer,
      .collision_mask = collision_mask,
      .broad_phase_layer
      = ResolveBroadPhaseLayer(collision_layer, is_non_moving),
      .is_non_moving = is_non_moving,
    });
    object_layers.insert_or_assign(key, object_layer);
    return Ok(object_layer);
  }

  [[nodiscard]] auto QueryMaskAllowsObjectLayer(const CollisionMask query_mask,
    const JPH::ObjectLayer object_layer) const noexcept -> bool
  {
    const auto metadata = GetObjectLayerMetadata(object_layer);
    return ShouldMaskAllowLayer(query_mask, metadata.collision_layer);
  }

  JPH::PhysicsSystem physics_system {};
  JPH::TempAllocatorImpl temp_allocator;
  JPH::JobSystemThreadPool job_system;
  std::shared_ptr<ICollisionFilter> collision_filter {};
  mutable std::mutex collision_layer_mutex {};
  std::vector<ObjectLayerMetadata> object_layer_metadata {};
  std::unordered_map<ObjectLayerKey, JPH::ObjectLayer, ObjectLayerKeyHash>
    object_layers {};
  BroadPhaseLayerInterfaceImpl broad_phase_layer_interface;
  ObjectLayerPairFilterImpl object_layer_pair_filter;
  ObjectVsBroadPhaseLayerFilterImpl object_vs_broad_phase_layer_filter;
  std::unordered_set<BodyId> body_ids {};
  mutable std::mutex event_mutex {};
  std::deque<events::PhysicsEvent> pending_events {};
  mutable std::mutex active_body_ids_mutex {};
  mutable JPH::BodyIDVector temp_active_body_ids {};
  std::unordered_map<JPH::SubShapeIDPair, events::PhysicsEvent>
    active_contact_events {};
  ContactListenerImpl contact_listener;

private:
  auto InitializeDefaultObjectLayers() -> void
  {
    const auto default_collision_layer = kCollisionLayerDefault;
    const auto default_collision_mask = kCollisionMaskAll;

    object_layer_metadata.clear();
    object_layers.clear();
    object_layer_metadata.reserve(8U);

    object_layer_metadata.push_back(ObjectLayerMetadata {
      .collision_layer = default_collision_layer,
      .collision_mask = default_collision_mask,
      .broad_phase_layer
      = ResolveBroadPhaseLayer(default_collision_layer, true),
      .is_non_moving = true,
    });
    object_layer_metadata.push_back(ObjectLayerMetadata {
      .collision_layer = default_collision_layer,
      .collision_mask = default_collision_mask,
      .broad_phase_layer
      = ResolveBroadPhaseLayer(default_collision_layer, false),
      .is_non_moving = false,
    });

    object_layers.insert_or_assign(
      ObjectLayerKey {
        .is_non_moving = true,
        .collision_layer = default_collision_layer.get(),
        .collision_mask = default_collision_mask.get(),
      },
      kNonMovingObjectLayer);
    object_layers.insert_or_assign(
      ObjectLayerKey {
        .is_non_moving = false,
        .collision_layer = default_collision_layer.get(),
        .collision_mask = default_collision_mask.get(),
      },
      kMovingObjectLayer);
  }

  [[nodiscard]] auto ResolveBroadPhaseLayer(
    const CollisionLayer collision_layer, const bool is_non_moving) const
    -> BroadPhaseLayer
  {
    if (collision_filter != nullptr) {
      const auto configured_layer
        = collision_filter->GetBroadPhaseLayer(collision_layer);
      if (configured_layer < kBroadPhaseLayerCount) {
        return configured_layer;
      }
    }
    return is_non_moving ? kNonMovingBroadPhaseLayer : kMovingBroadPhaseLayer;
  }

  [[nodiscard]] auto GetObjectLayerMetadata(
    const JPH::ObjectLayer object_layer) const -> ObjectLayerMetadata
  {
    std::scoped_lock lock(collision_layer_mutex);
    const auto index = static_cast<size_t>(object_layer);
    if (index < object_layer_metadata.size()) {
      return object_layer_metadata[index];
    }

    return object_layer == kNonMovingObjectLayer
      ? ObjectLayerMetadata {
          .collision_layer = kCollisionLayerDefault,
          .collision_mask = kCollisionMaskAll,
          .broad_phase_layer = kNonMovingBroadPhaseLayer,
          .is_non_moving = true,
        }
      : ObjectLayerMetadata {
          .collision_layer = kCollisionLayerDefault,
          .collision_mask = kCollisionMaskAll,
          .broad_phase_layer = kMovingBroadPhaseLayer,
          .is_non_moving = false,
        };
  }

  [[nodiscard]] auto GetBroadPhaseLayerForObjectLayer(
    const JPH::ObjectLayer object_layer) const -> BroadPhaseLayer
  {
    return GetObjectLayerMetadata(object_layer).broad_phase_layer;
  }

  [[nodiscard]] auto ShouldObjectLayersCollide(
    const JPH::ObjectLayer layer1, const JPH::ObjectLayer layer2) const -> bool
  {
    const auto metadata1 = GetObjectLayerMetadata(layer1);
    const auto metadata2 = GetObjectLayerMetadata(layer2);

    if (!ShouldMaskAllowLayer(
          metadata1.collision_mask, metadata2.collision_layer)
      || !ShouldMaskAllowLayer(
        metadata2.collision_mask, metadata1.collision_layer)) {
      return false;
    }

    if (collision_filter != nullptr) {
      return collision_filter->ShouldCollide(
        metadata1.collision_layer, metadata2.collision_layer);
    }

    if (metadata1.is_non_moving && metadata2.is_non_moving) {
      return false;
    }
    return true;
  }

  [[nodiscard]] auto ShouldObjectVsBroadPhaseLayerCollide(
    const JPH::ObjectLayer layer1, const JPH::BroadPhaseLayer layer2) const
    -> bool
  {
    const auto metadata = GetObjectLayerMetadata(layer1);

    if (collision_filter != nullptr) {
      return collision_filter->ShouldCollide(
        metadata.collision_layer, layer2.GetValue());
    }

    if (metadata.is_non_moving) {
      return layer2.GetValue() == kMovingBroadPhaseLayer;
    }
    return true;
  }
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
  const auto simulation_lock = LockSimulationApi();
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
  const auto simulation_lock = LockSimulationApi();
  if (worlds_.erase(world_id) == 0U) {
    return Err(PhysicsError::kWorldNotFound);
  }
  return PhysicsResult<void>::Ok();
}

auto oxygen::physics::jolt::JoltWorld::Step(const WorldId world_id,
  const float delta_time, const int max_sub_steps, const float fixed_dt_seconds)
  -> PhysicsResult<void>
{
  const auto simulation_lock = LockSimulationApi();
  auto world = TryGetWorld(world_id);
  if (world == nullptr) {
    return Err(PhysicsError::kWorldNotFound);
  }
  if (delta_time <= 0.0F || fixed_dt_seconds <= 0.0F || max_sub_steps <= 0) {
    return Err(PhysicsError::kInvalidArgument);
  }
  if (RemoveInvalidVehicleConstraintsBeforeStep(
        world->physics_system, world->body_ids)) {
    return Err(PhysicsError::kBackendInitFailed);
  }

  const auto required_sub_steps = static_cast<int>(std::ceil(delta_time
    / std::max(fixed_dt_seconds, std::numeric_limits<float>::epsilon())));
  const auto collision_steps = std::clamp(required_sub_steps, 1, max_sub_steps);
  simulation_step_in_progress_.store(true, std::memory_order_release);
  const auto update_error = world->physics_system.Update(
    delta_time, collision_steps, &world->temp_allocator, &world->job_system);
  simulation_step_in_progress_.store(false, std::memory_order_release);
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
  const auto simulation_lock = LockSimulationApi();
  const auto world = TryGetWorld(world_id);
  if (world == nullptr) {
    return Err(PhysicsError::kWorldNotFound);
  }

  std::scoped_lock lock(world->active_body_ids_mutex);
  world->temp_active_body_ids.clear();
  world->physics_system.GetActiveBodies(
    JPH::EBodyType::RigidBody, world->temp_active_body_ids);

  const auto copy_count
    = std::min(out_transforms.size(), world->temp_active_body_ids.size());
  const auto& body_interface = world->physics_system.GetBodyInterface();
  for (size_t i = 0; i < copy_count; ++i) {
    const auto jolt_body_id = world->temp_active_body_ids[i];
    JPH::RVec3 position {};
    JPH::Quat rotation {};
    body_interface.GetPositionAndRotation(jolt_body_id, position, rotation);
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
  const auto simulation_lock = LockSimulationApi();
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
  const auto simulation_lock = LockSimulationApi();
  auto world = TryGetWorld(world_id);
  if (world == nullptr) {
    return Err(PhysicsError::kWorldNotFound);
  }
  world->physics_system.SetGravity(ToJoltVec3(gravity));
  return PhysicsResult<void>::Ok();
}

auto oxygen::physics::jolt::JoltWorld::GetPendingEventCount(
  const WorldId world_id) const -> PhysicsResult<size_t>
{
  const auto world = TryGetWorld(world_id);
  if (world == nullptr) {
    return Err(PhysicsError::kWorldNotFound);
  }
  std::scoped_lock lock(world->event_mutex);
  return Ok(world->pending_events.size());
}

auto oxygen::physics::jolt::JoltWorld::DrainEvents(const WorldId world_id,
  std::span<events::PhysicsEvent> out_events) -> PhysicsResult<size_t>
{
  auto world = TryGetWorld(world_id);
  if (world == nullptr) {
    return Err(PhysicsError::kWorldNotFound);
  }
  std::scoped_lock lock(world->event_mutex);
  const auto drain_count
    = std::min(out_events.size(), world->pending_events.size());
  for (size_t i = 0; i < drain_count; ++i) {
    out_events[i] = world->pending_events.front();
    world->pending_events.pop_front();
  }
  return Ok(drain_count);
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

auto oxygen::physics::jolt::JoltWorld::TryGetBodyLockInterface(
  const WorldId world_id) const noexcept
  -> observer_ptr<const JPH::BodyLockInterface>
{
  const auto world = TryGetWorld(world_id);
  if (world == nullptr) {
    return observer_ptr<const JPH::BodyLockInterface> {};
  }
  return observer_ptr<const JPH::BodyLockInterface> {
    &world->physics_system.GetBodyLockInterface(),
  };
}

auto oxygen::physics::jolt::JoltWorld::TryGetNarrowPhaseQuery(
  const WorldId world_id) const noexcept
  -> observer_ptr<const JPH::NarrowPhaseQuery>
{
  const auto world = TryGetWorld(world_id);
  if (world == nullptr) {
    return observer_ptr<const JPH::NarrowPhaseQuery> {};
  }
  return observer_ptr<const JPH::NarrowPhaseQuery> {
    &world->physics_system.GetNarrowPhaseQuery(),
  };
}

auto oxygen::physics::jolt::JoltWorld::TryGetPhysicsSystem(
  const WorldId world_id) noexcept -> observer_ptr<JPH::PhysicsSystem>
{
  auto world = TryGetWorld(world_id);
  if (world == nullptr) {
    return observer_ptr<JPH::PhysicsSystem> {};
  }
  return observer_ptr<JPH::PhysicsSystem> { &world->physics_system };
}

auto oxygen::physics::jolt::JoltWorld::TryGetPhysicsSystem(
  const WorldId world_id) const noexcept
  -> observer_ptr<const JPH::PhysicsSystem>
{
  const auto world = TryGetWorld(world_id);
  if (world == nullptr) {
    return observer_ptr<const JPH::PhysicsSystem> {};
  }
  return observer_ptr<const JPH::PhysicsSystem> { &world->physics_system };
}

auto oxygen::physics::jolt::JoltWorld::ResolveBodyObjectLayer(
  const WorldId world_id, const body::BodyType body_type,
  const CollisionLayer collision_layer, const CollisionMask collision_mask)
  -> PhysicsResult<uint16_t>
{
  auto world = TryGetWorld(world_id);
  if (world == nullptr) {
    return Err(PhysicsError::kWorldNotFound);
  }

  const auto object_layer_result
    = world->ResolveObjectLayer(body_type, collision_layer, collision_mask);
  if (object_layer_result.has_error()) {
    return Err(object_layer_result.error());
  }
  return Ok(static_cast<uint16_t>(object_layer_result.value()));
}

auto oxygen::physics::jolt::JoltWorld::QueryMaskAllowsObjectLayer(
  const WorldId world_id, const CollisionMask query_mask,
  const uint16_t object_layer) const noexcept -> bool
{
  const auto world = TryGetWorld(world_id);
  if (world == nullptr) {
    return false;
  }
  return world->QueryMaskAllowsObjectLayer(
    query_mask, static_cast<JPH::ObjectLayer>(object_layer));
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

auto oxygen::physics::jolt::JoltWorld::LockSimulationApi() const
  -> std::unique_lock<std::mutex>
{
  if (simulation_api_mutex_.try_lock()) {
    return std::unique_lock<std::mutex>(simulation_api_mutex_, std::adopt_lock);
  }

  if (simulation_step_in_progress_.load(std::memory_order_acquire)
    && !simulation_overlap_log_emitted_.exchange(
      true, std::memory_order_acq_rel)) {
    LOG_F(WARNING,
      "JoltWorld: physics API call attempted while simulation step was "
      "in progress; serializing via simulation API lock.");
  }

  simulation_api_mutex_.lock();
  return std::unique_lock<std::mutex>(simulation_api_mutex_, std::adopt_lock);
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
