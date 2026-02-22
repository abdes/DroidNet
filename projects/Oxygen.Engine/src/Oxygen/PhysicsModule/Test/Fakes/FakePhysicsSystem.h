//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Physics/System/IPhysicsSystem.h>
#include <Oxygen/PhysicsModule/Test/Fakes/BackendState.h>
#include <Oxygen/PhysicsModule/Test/Fakes/FakeAggregateApi.h>
#include <Oxygen/PhysicsModule/Test/Fakes/FakeAreaApi.h>
#include <Oxygen/PhysicsModule/Test/Fakes/FakeBodyApi.h>
#include <Oxygen/PhysicsModule/Test/Fakes/FakeCharacterApi.h>
#include <Oxygen/PhysicsModule/Test/Fakes/FakeEventApi.h>
#include <Oxygen/PhysicsModule/Test/Fakes/FakeJointApi.h>
#include <Oxygen/PhysicsModule/Test/Fakes/FakeQueryApi.h>
#include <Oxygen/PhysicsModule/Test/Fakes/FakeShapeApi.h>
#include <Oxygen/PhysicsModule/Test/Fakes/FakeVehicleApi.h>
#include <Oxygen/PhysicsModule/Test/Fakes/FakeWorldApi.h>

namespace oxygen::physics::test::detail {

class FakePhysicsSystem final : public system::IPhysicsSystem {
public:
  FakePhysicsSystem()
    : worlds_(state_)
    , bodies_(state_)
    , events_(state_)
    , characters_(state_)
    , shapes_(state_)
    , areas_(state_)
    , joints_(state_)
    , aggregates_(state_)
    , vehicles_(state_)
  {
  }

  [[nodiscard]] auto State() noexcept -> BackendState& { return state_; }
  [[nodiscard]] auto State() const noexcept -> const BackendState&
  {
    return state_;
  }

  auto Worlds() noexcept -> system::IWorldApi& override { return worlds_; }
  auto Bodies() noexcept -> system::IBodyApi& override { return bodies_; }
  auto Queries() noexcept -> system::IQueryApi& override { return queries_; }
  auto Events() noexcept -> system::IEventApi& override { return events_; }
  auto Characters() noexcept -> system::ICharacterApi& override
  {
    return characters_;
  }
  auto Shapes() noexcept -> system::IShapeApi& override { return shapes_; }
  auto Areas() noexcept -> system::IAreaApi& override { return areas_; }
  auto Joints() noexcept -> system::IJointApi& override { return joints_; }
  auto Aggregates() noexcept -> system::IAggregateApi* override
  {
    return &aggregates_;
  }
  auto Vehicles() noexcept -> system::IVehicleApi* override
  {
    return &vehicles_;
  }

  auto Worlds() const noexcept -> const system::IWorldApi& override
  {
    return worlds_;
  }
  auto Bodies() const noexcept -> const system::IBodyApi& override
  {
    return bodies_;
  }
  auto Queries() const noexcept -> const system::IQueryApi& override
  {
    return queries_;
  }
  auto Events() const noexcept -> const system::IEventApi& override
  {
    return events_;
  }
  auto Characters() const noexcept -> const system::ICharacterApi& override
  {
    return characters_;
  }
  auto Shapes() const noexcept -> const system::IShapeApi& override
  {
    return shapes_;
  }
  auto Areas() const noexcept -> const system::IAreaApi& override
  {
    return areas_;
  }
  auto Joints() const noexcept -> const system::IJointApi& override
  {
    return joints_;
  }
  auto Aggregates() const noexcept -> const system::IAggregateApi* override
  {
    return &aggregates_;
  }
  auto Vehicles() const noexcept -> const system::IVehicleApi* override
  {
    return &vehicles_;
  }

private:
  BackendState state_ {};
  FakeWorldApi worlds_;
  FakeBodyApi bodies_;
  FakeQueryApi queries_ {};
  FakeEventApi events_;
  FakeCharacterApi characters_;
  FakeShapeApi shapes_;
  FakeAreaApi areas_;
  FakeJointApi joints_;
  FakeAggregateApi aggregates_;
  FakeVehicleApi vehicles_;
};

} // namespace oxygen::physics::test::detail
