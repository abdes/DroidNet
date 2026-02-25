//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Physics/PhysicsError.h>
#include <Oxygen/PhysicsModule/PhysicsModule.h>
#include <Oxygen/PhysicsModule/ScenePhysics.h>

namespace oxygen::physics {

namespace internal {
  auto ScenePhysicsTagFactory::Get() noexcept -> ScenePhysicsTag
  {
    return ScenePhysicsTag {};
  }
} // namespace internal

namespace {

  auto SeedCharacterDescFromNodeWorldPose(scene::SceneNode& node,
    const character::CharacterDesc& input_desc) -> character::CharacterDesc
  {
    auto seeded_desc = input_desc;
    if (const auto world_position = node.GetTransform().GetWorldPosition();
      world_position.has_value()) {
      seeded_desc.initial_position = *world_position;
    }
    if (const auto world_rotation = node.GetTransform().GetWorldRotation();
      world_rotation.has_value()) {
      seeded_desc.initial_rotation = *world_rotation;
    }
    return seeded_desc;
  }

} // namespace

auto ScenePhysics::CharacterFacade::Move(
  const character::CharacterMoveInput& input, float delta_time) const
  -> PhysicsResult<character::CharacterMoveResult>
{
  if (character_api_ == nullptr) {
    return Err(PhysicsError::kNotInitialized);
  }
  auto result = character_api_->MoveCharacter(
    world_id_, character_id_, input, delta_time);
  if (!result.has_value()) {
    return result;
  }
  if (physics_module_ != nullptr) {
    const auto& state = result.value().state;
    const auto scene_tag = internal::ScenePhysicsTagFactory::Get();
    [[maybe_unused]] const auto synced = physics_module_->ApplyWorldPoseToNode(
      scene_tag, node_, state.position, state.rotation);
    DCHECK_F(synced,
      "Character authority contract violated: character move result "
      "must map back to a valid observed scene node.");
  }
  return result;
}

auto ScenePhysics::AttachRigidBody(observer_ptr<PhysicsModule> physics_module,
  scene::SceneNode& node, const body::BodyDesc& desc)
  -> std::optional<RigidBodyFacade>
{
  const auto result = AttachRigidBodyDetailed(physics_module, node, desc);
  if (!result.has_value()) {
    LOG_F(ERROR, "ScenePhysics::AttachRigidBody CreateBody failed: {}",
      to_string(result.error()));
    return std::nullopt;
  }
  return result.value();
}

auto ScenePhysics::AttachRigidBodyDetailed(
  observer_ptr<PhysicsModule> physics_module, scene::SceneNode& node,
  const body::BodyDesc& desc) -> PhysicsResult<RigidBodyFacade>
{
  if (!physics_module || !node.IsValid()) {
    return Err(PhysicsError::kInvalidArgument);
  }
  const auto scene_tag = internal::ScenePhysicsTagFactory::Get();
  DCHECK_F(node.IsAlive(),
    "AttachRigidBody contract violated: node handle must be alive.");
  DCHECK_F(physics_module->IsNodeInObservedScene(scene_tag, node.GetHandle()),
    "AttachRigidBody contract violated: node must belong to currently "
    "observed scene.");
  if (!physics_module->IsNodeInObservedScene(scene_tag, node.GetHandle())) {
    return Err(PhysicsError::kInvalidArgument);
  }
  DCHECK_F(physics_module->GetCharacterIdForNode(scene_tag, node.GetHandle())
      == kInvalidCharacterId,
    "AttachRigidBody contract violated: node already has a character.");
  if (physics_module->GetCharacterIdForNode(scene_tag, node.GetHandle())
    != kInvalidCharacterId) {
    return Err(PhysicsError::kAlreadyExists);
  }
  DCHECK_F(physics_module->GetAggregateIdForNode(scene_tag, node.GetHandle())
      == kInvalidAggregateId,
    "AttachRigidBody contract violated: node already has an aggregate.");
  if (physics_module->GetAggregateIdForNode(scene_tag, node.GetHandle())
    != kInvalidAggregateId) {
    return Err(PhysicsError::kAlreadyExists);
  }

  auto& body_api = physics_module->Bodies();
  const auto world_id = physics_module->GetWorldId();
  DCHECK_F(world_id != kInvalidWorldId,
    "AttachRigidBody contract violated: physics world must be valid.");
  if (world_id == kInvalidWorldId) {
    return Err(PhysicsError::kNotInitialized);
  }

  // Create the body
  const auto result = body_api.CreateBody(world_id, desc);
  if (!result.has_value()) {
    return Err(result.error());
  }

  const BodyId body_id = result.value();

  physics_module->RegisterNodeBodyMapping(
    scene_tag, node.GetHandle(), body_id, desc.type);

  return PhysicsResult<RigidBodyFacade>::Ok(RigidBodyFacade(node.GetHandle(),
    world_id, body_id, observer_ptr<system::IBodyApi> { &body_api }));
}

auto ScenePhysics::GetRigidBody(observer_ptr<PhysicsModule> physics_module,
  const scene::NodeHandle& node) -> std::optional<RigidBodyFacade>
{
  if (!physics_module || !node.IsValid()) {
    return std::nullopt;
  }
  const auto scene_tag = internal::ScenePhysicsTagFactory::Get();

  const auto body_id = physics_module->GetBodyIdForNode(scene_tag, node);
  if (body_id == kInvalidBodyId) {
    return std::nullopt;
  }

  return RigidBodyFacade(node, physics_module->GetWorldId(), body_id,
    observer_ptr<system::IBodyApi> { &physics_module->Bodies() });
}

auto ScenePhysics::AttachCharacter(observer_ptr<PhysicsModule> physics_module,
  scene::SceneNode& node, const character::CharacterDesc& desc)
  -> std::optional<CharacterFacade>
{
  const auto result = AttachCharacterDetailed(physics_module, node, desc);
  if (!result.has_value()) {
    LOG_F(ERROR, "ScenePhysics::AttachCharacter CreateCharacter failed: {}",
      to_string(result.error()));
    return std::nullopt;
  }
  return result.value();
}

auto ScenePhysics::AttachCharacterDetailed(
  observer_ptr<PhysicsModule> physics_module, scene::SceneNode& node,
  const character::CharacterDesc& desc) -> PhysicsResult<CharacterFacade>
{
  if (!physics_module || !node.IsValid()) {
    return Err(PhysicsError::kInvalidArgument);
  }
  const auto scene_tag = internal::ScenePhysicsTagFactory::Get();
  DCHECK_F(node.IsAlive(),
    "AttachCharacter contract violated: node handle must be alive.");
  DCHECK_F(physics_module->IsNodeInObservedScene(scene_tag, node.GetHandle()),
    "AttachCharacter contract violated: node must belong to currently "
    "observed scene.");
  if (!physics_module->IsNodeInObservedScene(scene_tag, node.GetHandle())) {
    return Err(PhysicsError::kInvalidArgument);
  }
  DCHECK_F(physics_module->GetBodyIdForNode(scene_tag, node.GetHandle())
      == kInvalidBodyId,
    "AttachCharacter contract violated: node already has a rigid body.");
  if (physics_module->GetBodyIdForNode(scene_tag, node.GetHandle())
    != kInvalidBodyId) {
    return Err(PhysicsError::kAlreadyExists);
  }
  DCHECK_F(physics_module->GetAggregateIdForNode(scene_tag, node.GetHandle())
      == kInvalidAggregateId,
    "AttachCharacter contract violated: node already has an aggregate.");
  if (physics_module->GetAggregateIdForNode(scene_tag, node.GetHandle())
    != kInvalidAggregateId) {
    return Err(PhysicsError::kAlreadyExists);
  }

  auto& character_api = physics_module->Characters();
  const auto world_id = physics_module->GetWorldId();
  DCHECK_F(world_id != kInvalidWorldId,
    "AttachCharacter contract violated: physics world must be valid.");
  if (world_id == kInvalidWorldId) {
    return Err(PhysicsError::kNotInitialized);
  }

  const auto seeded_desc = SeedCharacterDescFromNodeWorldPose(node, desc);
  const auto result = character_api.CreateCharacter(world_id, seeded_desc);
  if (!result.has_value()) {
    return Err(result.error());
  }

  const CharacterId character_id = result.value();
  physics_module->RegisterNodeCharacterMapping(
    scene_tag, node.GetHandle(), character_id);
  return PhysicsResult<CharacterFacade>::Ok(
    CharacterFacade(node.GetHandle(), world_id, character_id,
      observer_ptr<system::ICharacterApi> { &character_api }, physics_module));
}

auto ScenePhysics::GetCharacter(observer_ptr<PhysicsModule> physics_module,
  const scene::NodeHandle& node) -> std::optional<CharacterFacade>
{
  if (!physics_module || !node.IsValid()) {
    return std::nullopt;
  }
  const auto scene_tag = internal::ScenePhysicsTagFactory::Get();

  const auto character_id
    = physics_module->GetCharacterIdForNode(scene_tag, node);
  if (character_id == kInvalidCharacterId) {
    return std::nullopt;
  }

  return CharacterFacade(node, physics_module->GetWorldId(), character_id,
    observer_ptr<system::ICharacterApi> { &physics_module->Characters() },
    physics_module);
}

auto ScenePhysics::CastRay(observer_ptr<PhysicsModule> physics_module,
  const query::RaycastDesc& ray) -> std::optional<SceneRayCastHit>
{
  if (!physics_module) {
    return std::nullopt;
  }
  const auto scene_tag = internal::ScenePhysicsTagFactory::Get();

  auto& query_api = physics_module->Queries();
  const auto world_id = physics_module->GetWorldId();
  if (world_id == kInvalidWorldId) {
    return std::nullopt;
  }

  const auto result = query_api.Raycast(world_id, ray);
  if (!result.has_value() || !result.value().has_value()) {
    return std::nullopt;
  }

  const auto& hit = result.value().value();
  const auto node_handle
    = physics_module->GetNodeForBodyId(scene_tag, hit.body_id);
  if (!node_handle.has_value()) {
    return std::nullopt;
  }

  return SceneRayCastHit {
    .node = *node_handle,
    .position = hit.position,
    .normal = hit.normal,
    .distance = hit.distance,
  };
}

} // namespace oxygen::physics
