//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Physics/Jolt/JoltAreas.h>
#include <Oxygen/Physics/Jolt/JoltBodies.h>
#include <Oxygen/Physics/Jolt/JoltCharacters.h>
#include <Oxygen/Physics/Jolt/JoltEvents.h>
#include <Oxygen/Physics/Jolt/JoltJoints.h>
#include <Oxygen/Physics/Jolt/JoltQueries.h>
#include <Oxygen/Physics/Jolt/JoltShapes.h>
#include <Oxygen/Physics/Jolt/JoltWorld.h>
#include <Oxygen/Physics/System/IPhysicsSystem.h>

namespace oxygen::physics::jolt {

//! Jolt-backed root physics service composed from domain implementations.
class JoltPhysicsSystem final : public system::IPhysicsSystem {
public:
  JoltPhysicsSystem();
  ~JoltPhysicsSystem() override = default;

  OXYGEN_MAKE_NON_COPYABLE(JoltPhysicsSystem)
  OXYGEN_MAKE_NON_MOVABLE(JoltPhysicsSystem)

  auto Worlds() noexcept -> system::IWorldApi& override;
  auto Bodies() noexcept -> system::IBodyApi& override;
  auto Queries() noexcept -> system::IQueryApi& override;
  auto Events() noexcept -> system::IEventApi& override;
  auto Characters() noexcept -> system::ICharacterApi& override;
  auto Shapes() noexcept -> system::IShapeApi& override;
  auto Areas() noexcept -> system::IAreaApi& override;
  auto Joints() noexcept -> system::IJointApi& override;

  auto Worlds() const noexcept -> const system::IWorldApi& override;
  auto Bodies() const noexcept -> const system::IBodyApi& override;
  auto Queries() const noexcept -> const system::IQueryApi& override;
  auto Events() const noexcept -> const system::IEventApi& override;
  auto Characters() const noexcept -> const system::ICharacterApi& override;
  auto Shapes() const noexcept -> const system::IShapeApi& override;
  auto Areas() const noexcept -> const system::IAreaApi& override;
  auto Joints() const noexcept -> const system::IJointApi& override;

private:
  JoltWorld world_ {};
  JoltShapes shapes_ {};
  JoltBodies bodies_;
  JoltQueries queries_;
  JoltEvents events_;
  JoltCharacters characters_ {};
  JoltAreas areas_ {};
  JoltJoints joints_ {};
};

} // namespace oxygen::physics::jolt
