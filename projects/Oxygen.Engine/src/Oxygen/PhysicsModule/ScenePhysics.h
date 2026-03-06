//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <optional>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Base/Result.h>
#include <Oxygen/Physics/Body/BodyDesc.h>
#include <Oxygen/Physics/Character/CharacterController.h>
#include <Oxygen/Physics/Handles.h>
#include <Oxygen/Physics/PhysicsError.h>
#include <Oxygen/Physics/System/IQueryApi.h>
#include <Oxygen/PhysicsModule/api_export.h>
#include <Oxygen/Scene/Scene.h>

namespace oxygen::physics {

class PhysicsModule;

class ScenePhysics {
public:
  struct SceneRayCastHit {
    scene::NodeHandle node;
    Vec3 position;
    Vec3 normal;
    float distance;
  };

  /*!
   Attach a rigid body to a scene node and register ownership in PhysicsModule.

   Contract:
   - Caller must invoke this from allowed scene-mutation phases
     (`kGameplay` or `kSceneMutation`).
   - Scene node must be valid and owned by the currently observed scene.
   - On success, PhysicsModule side tables are updated
     (`NodeHandle <-> BodyId`) and subsequent sync phases manage this node as
     part of the physics subset.

   Timing:
   - Attachment is visible to scene/physics reconciliation in the same frame.
   - Simulation effects are allowed to appear on the next fixed-simulation step
     (one-frame latency by design).
   - Motion authority is selected by `body::BodyDesc::type` and enforced by
     `PhysicsModule` (`kKinematic` push, `kDynamic` pull).

   Failure:
   - Returns `std::nullopt` if module/world/node preconditions are not met,
     ownership contracts are violated (character/aggregate already mapped), or
     body creation fails.
  */
  OXGN_PHSYNC_API static auto AttachRigidBody(
    observer_ptr<PhysicsModule> physics_module, scene::SceneNode& node,
    const body::BodyDesc& desc) -> std::optional<BodyId>;
  OXGN_PHSYNC_API static auto AttachRigidBodyDetailed(
    observer_ptr<PhysicsModule> physics_module, scene::SceneNode& node,
    const body::BodyDesc& desc) -> PhysicsResult<BodyId>;

  OXGN_PHSYNC_API static auto GetRigidBody(
    observer_ptr<PhysicsModule> physics_module, const scene::NodeHandle& node)
    -> std::optional<BodyId>;

  /*!
   Attach a character controller to a scene node and register ownership in
   PhysicsModule.

   Contract:
   - Scene node must be valid and owned by the currently observed scene.
   - Node must not already be mapped as a rigid body.
   - Node must not already be mapped as an aggregate.
   - Character motion is command-authoritative: use
     `ScenePhysics::MoveCharacter` for movement intent.
   - Successful `ScenePhysics::MoveCharacter` calls apply
     returned world pose back to the scene node as local transform using
     parent-aware conversion.
   - Scene-authored transform writes to a character-owned node are contract
     violations (debug-asserted by PhysicsModule observer path).
   - Character attachment does not participate in rigid-body transform
     push/pull sync.

   Near Future:

   - Aggregate attachment/query facades are intentionally not exposed here yet.
     Aggregate ownership is currently integrated via `PhysicsModule` mapping
     APIs while higher-level scene-facing APIs are being designed.

  */
  OXGN_PHSYNC_API static auto AttachCharacter(
    observer_ptr<PhysicsModule> physics_module, scene::SceneNode& node,
    const character::CharacterDesc& desc) -> std::optional<CharacterId>;
  OXGN_PHSYNC_API static auto AttachCharacterDetailed(
    observer_ptr<PhysicsModule> physics_module, scene::SceneNode& node,
    const character::CharacterDesc& desc) -> PhysicsResult<CharacterId>;

  OXGN_PHSYNC_API static auto GetCharacter(
    observer_ptr<PhysicsModule> physics_module, const scene::NodeHandle& node)
    -> std::optional<CharacterId>;

  OXGN_PHSYNC_API static auto MoveCharacter(
    observer_ptr<PhysicsModule> physics_module, CharacterId character_id,
    const character::CharacterMoveInput& input, float delta_time)
    -> PhysicsResult<character::CharacterMoveResult>;

  OXGN_PHSYNC_API static auto CastRay(
    observer_ptr<PhysicsModule> physics_module, const query::RaycastDesc& ray)
    -> std::optional<SceneRayCastHit>;
};

} // namespace oxygen::physics
