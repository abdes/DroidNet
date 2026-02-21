//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Physics/System/IBodyApi.h>
#include <Oxygen/Physics/System/ICharacterApi.h>
#include <Oxygen/Physics/System/IEventApi.h>
#include <Oxygen/Physics/System/IQueryApi.h>
#include <Oxygen/Physics/System/IWorldApi.h>

namespace oxygen::physics::system {

//! Root physics service exposing domain APIs by composition.
/*!
 Responsibilities now:
 - Provide stable accessors to world, body, query, event, and character
   domains.
 - Keep domain boundaries explicit in one backend-agnostic entry point.

 ### Near Future

 - Add new domain accessors (for example joints, articulation, vehicles,
   deformables) without reshaping existing domain contracts.
*/
class IPhysicsSystem
{
public:
  IPhysicsSystem() = default;
  virtual ~IPhysicsSystem() = default;

  OXYGEN_MAKE_NON_COPYABLE(IPhysicsSystem)
  OXYGEN_MAKE_NON_MOVABLE(IPhysicsSystem)

  OXGN_PHYS_NDAPI virtual auto Worlds() noexcept -> IWorldApi& = 0;
  OXGN_PHYS_NDAPI virtual auto Bodies() noexcept -> IBodyApi& = 0;
  OXGN_PHYS_NDAPI virtual auto Queries() noexcept -> IQueryApi& = 0;
  OXGN_PHYS_NDAPI virtual auto Events() noexcept -> IEventApi& = 0;
  OXGN_PHYS_NDAPI virtual auto Characters() noexcept -> ICharacterApi& = 0;

  OXGN_PHYS_NDAPI virtual auto Worlds() const noexcept -> const IWorldApi& = 0;
  OXGN_PHYS_NDAPI virtual auto Bodies() const noexcept -> const IBodyApi& = 0;
  OXGN_PHYS_NDAPI virtual auto Queries() const noexcept -> const IQueryApi& = 0;
  OXGN_PHYS_NDAPI virtual auto Events() const noexcept -> const IEventApi& = 0;
  OXGN_PHYS_NDAPI virtual auto Characters() const noexcept
    -> const ICharacterApi& = 0;
};

} // namespace oxygen::physics::system
