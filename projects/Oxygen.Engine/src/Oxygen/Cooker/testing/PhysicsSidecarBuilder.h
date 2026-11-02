//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include <Oxygen/Cooker/testing/SceneBuilder.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/PakFormat_physics.h>

namespace oxygen::cooker::testing {

//! The bindings of a scene's physics sidecar, by node of its scene.
/*!
 Every binding names nodes of one `SceneBuilder`; the sidecar is paired with
 that scene by `LooseCookedRoot::PhysicsSidecar`, which records what a cook
 records of it: its key, its node count and the SHA-256 of its bytes. Each
 record a test passes keeps its values; the builder sets its nodes and keys.

 ```cpp
 auto bindings = PhysicsSidecarBuilder {};
 bindings.RigidBody(crate, shape, material);
 game.PhysicsSidecar("Scenes/Main.opscene", scene, bindings);
 ```
*/
class PhysicsSidecarBuilder final {
public:
  auto RigidBody(SceneNode node, const data::AssetKey& shape,
    const data::AssetKey& material,
    data::pak::physics::RigidBodyBindingRecord record = {})
    -> PhysicsSidecarBuilder&;

  auto Collider(SceneNode node, const data::AssetKey& shape,
    const data::AssetKey& material,
    data::pak::physics::ColliderBindingRecord record = {})
    -> PhysicsSidecarBuilder&;

  auto Character(SceneNode node, const data::AssetKey& shape,
    data::pak::physics::CharacterBindingRecord record = {})
    -> PhysicsSidecarBuilder&;

  //! A soft body of `node` with the topology resource `topology` and no
  //! pinned or kinematic vertices.
  auto SoftBody(SceneNode node, const data::AssetKey& topology,
    data::pak::physics::SoftBodyBindingRecord record = {})
    -> PhysicsSidecarBuilder&;

  //! A joint between `a` and `b`, or the world without `b`.
  auto Joint(SceneNode a, std::optional<SceneNode> b,
    const data::AssetKey& constraint,
    data::pak::physics::JointBindingRecord record = {})
    -> PhysicsSidecarBuilder&;

  //! A vehicle rooted at `node`, without wheels.
  auto Vehicle(SceneNode node, const data::AssetKey& constraint,
    data::pak::physics::VehicleBindingRecord record = {})
    -> PhysicsSidecarBuilder&;

  auto Aggregate(
    SceneNode node, data::pak::physics::AggregateBindingRecord record = {})
    -> PhysicsSidecarBuilder&;

  //! The backend the sidecar is cooked for, whose arm of each backend-scalar
  //! union its records hold; Jolt by default, as an import's.
  auto Backend(data::pak::physics::PhysicsBackend backend)
    -> PhysicsSidecarBuilder&;

  //! The builder whose nodes the bindings name; none without bindings.
  [[nodiscard]] auto Owner() const noexcept -> std::optional<SceneBuilderId>
  {
    return owner_;
  }

  //! The descriptor `name` of the sidecar of the scene `scene_key`, whose
  //! descriptor is `scene_bytes`.
  /*!
   @throw std::invalid_argument When a binding names a node the scene does not
   hold.
   @throw std::logic_error When the product reader refuses the sidecar.
  */
  [[nodiscard]] auto Bytes(const data::AssetKey& scene_key,
    std::span<const std::byte> scene_bytes, std::string_view name) const
    -> std::vector<std::byte>;

  //! The references a cook records of the sidecar of `scene_key`.
  [[nodiscard]] auto References(const data::AssetKey& scene_key) const
    -> data::AssetReferences;

private:
  auto Claim(SceneNode node) -> data::pak::world::SceneNodeIndexT;

  std::optional<SceneBuilderId> owner_;
  data::pak::world::SceneNodeIndexT highest_node_ = 0;
  data::pak::physics::PhysicsBackend backend_
    = data::pak::physics::PhysicsBackend::kJolt;
  std::vector<data::pak::physics::RigidBodyBindingRecord> rigid_bodies_;
  std::vector<data::pak::physics::ColliderBindingRecord> colliders_;
  std::vector<data::pak::physics::CharacterBindingRecord> characters_;
  std::vector<data::pak::physics::SoftBodyBindingRecord> soft_bodies_;
  std::vector<data::pak::physics::JointBindingRecord> joints_;
  std::vector<data::pak::physics::VehicleBindingRecord> vehicles_;
  std::vector<data::pak::physics::AggregateBindingRecord> aggregates_;
};

} // namespace oxygen::cooker::testing
