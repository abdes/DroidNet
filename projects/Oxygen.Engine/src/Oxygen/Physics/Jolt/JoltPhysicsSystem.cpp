//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Physics/Jolt/JoltPhysicsSystem.h>

oxygen::physics::jolt::JoltPhysicsSystem::JoltPhysicsSystem()
  : aggregates_(world_)
  , bodies_(world_, shapes_)
  , queries_(world_)
  , events_(world_)
  , characters_(world_)
  , areas_(world_, shapes_)
  , joints_(world_)
{
}

auto oxygen::physics::jolt::JoltPhysicsSystem::Worlds() noexcept
  -> system::IWorldApi&
{
  return world_;
}

auto oxygen::physics::jolt::JoltPhysicsSystem::Bodies() noexcept
  -> system::IBodyApi&
{
  return bodies_;
}

auto oxygen::physics::jolt::JoltPhysicsSystem::Queries() noexcept
  -> system::IQueryApi&
{
  return queries_;
}

auto oxygen::physics::jolt::JoltPhysicsSystem::Events() noexcept
  -> system::IEventApi&
{
  return events_;
}

auto oxygen::physics::jolt::JoltPhysicsSystem::Characters() noexcept
  -> system::ICharacterApi&
{
  return characters_;
}

auto oxygen::physics::jolt::JoltPhysicsSystem::Shapes() noexcept
  -> system::IShapeApi&
{
  return shapes_;
}

auto oxygen::physics::jolt::JoltPhysicsSystem::Areas() noexcept
  -> system::IAreaApi&
{
  return areas_;
}

auto oxygen::physics::jolt::JoltPhysicsSystem::Joints() noexcept
  -> system::IJointApi&
{
  return joints_;
}

auto oxygen::physics::jolt::JoltPhysicsSystem::Aggregates() noexcept
  -> system::IAggregateApi*
{
  return &aggregates_;
}

auto oxygen::physics::jolt::JoltPhysicsSystem::Worlds() const noexcept
  -> const system::IWorldApi&
{
  return world_;
}

auto oxygen::physics::jolt::JoltPhysicsSystem::Bodies() const noexcept
  -> const system::IBodyApi&
{
  return bodies_;
}

auto oxygen::physics::jolt::JoltPhysicsSystem::Queries() const noexcept
  -> const system::IQueryApi&
{
  return queries_;
}

auto oxygen::physics::jolt::JoltPhysicsSystem::Events() const noexcept
  -> const system::IEventApi&
{
  return events_;
}

auto oxygen::physics::jolt::JoltPhysicsSystem::Characters() const noexcept
  -> const system::ICharacterApi&
{
  return characters_;
}

auto oxygen::physics::jolt::JoltPhysicsSystem::Shapes() const noexcept
  -> const system::IShapeApi&
{
  return shapes_;
}

auto oxygen::physics::jolt::JoltPhysicsSystem::Areas() const noexcept
  -> const system::IAreaApi&
{
  return areas_;
}

auto oxygen::physics::jolt::JoltPhysicsSystem::Joints() const noexcept
  -> const system::IJointApi&
{
  return joints_;
}

auto oxygen::physics::jolt::JoltPhysicsSystem::Aggregates() const noexcept
  -> const system::IAggregateApi*
{
  return &aggregates_;
}
