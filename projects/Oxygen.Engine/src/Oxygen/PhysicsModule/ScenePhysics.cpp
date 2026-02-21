//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Base/Logging.h>
#include <Oxygen/PhysicsModule/PhysicsModule.h>
#include <Oxygen/PhysicsModule/ScenePhysics.h>

namespace oxygen::physics {

auto ScenePhysics::AttachRigidBody(observer_ptr<PhysicsModule> physics_module,
  scene::SceneNode& node, const body::BodyDesc& desc)
  -> std::optional<RigidBodyFacade>
{
  if (!physics_module || !node.IsValid()) {
    return std::nullopt;
  }
  DCHECK_F(node.IsAlive(),
    "AttachRigidBody contract violated: node handle must be alive.");
  DCHECK_F(physics_module->IsNodeInObservedScene(node.GetHandle()),
    "AttachRigidBody contract violated: node must belong to currently "
    "observed scene.");
  if (!physics_module->IsNodeInObservedScene(node.GetHandle())) {
    return std::nullopt;
  }

  auto& body_api = physics_module->GetBodyApi();
  const auto world_id = physics_module->GetWorldId();
  DCHECK_F(world_id != kInvalidWorldId,
    "AttachRigidBody contract violated: physics world must be valid.");
  if (world_id == kInvalidWorldId) {
    return std::nullopt;
  }

  const auto pos = node.GetTransform().GetWorldPosition().value_or(Vec3(0.0F));
  const auto rot = node.GetTransform().GetWorldRotation().value_or(
    Quat(1.0F, 0.0F, 0.0F, 0.0F));

  body::BodyDesc modified_desc = desc;
  modified_desc.initial_position = pos;
  modified_desc.initial_rotation = rot;

  // Create the body
  const auto result = body_api.CreateBody(world_id, modified_desc);
  if (!result.has_value()) {
    return std::nullopt;
  }

  const BodyId body_id = result.value();

  physics_module->RegisterNodeBodyMapping(node.GetHandle(), body_id, desc.type);

  return RigidBodyFacade(node.GetHandle(), world_id, body_id,
    observer_ptr<system::IBodyApi> { &body_api });
}

auto ScenePhysics::GetRigidBody(observer_ptr<PhysicsModule> physics_module,
  const scene::NodeHandle& node) -> std::optional<RigidBodyFacade>
{
  if (!physics_module || !node.IsValid()) {
    return std::nullopt;
  }

  const auto body_id = physics_module->GetBodyIdForNode(node);
  if (body_id == kInvalidBodyId) {
    return std::nullopt;
  }

  return RigidBodyFacade(node, physics_module->GetWorldId(), body_id,
    observer_ptr<system::IBodyApi> { &physics_module->GetBodyApi() });
}

auto ScenePhysics::CastRay(observer_ptr<PhysicsModule> physics_module,
  const query::RaycastDesc& ray) -> std::optional<SceneRayCastHit>
{
  if (!physics_module) {
    return std::nullopt;
  }

  auto& query_api = physics_module->GetQueryApi();
  const auto world_id = physics_module->GetWorldId();
  if (world_id == kInvalidWorldId) {
    return std::nullopt;
  }

  const auto result = query_api.Raycast(world_id, ray);
  if (!result.has_value() || !result.value().has_value()) {
    return std::nullopt;
  }

  const auto& hit = result.value().value();
  const auto node_handle = physics_module->GetNodeForBodyId(hit.body_id);
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
