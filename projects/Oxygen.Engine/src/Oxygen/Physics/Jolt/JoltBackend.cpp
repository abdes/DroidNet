//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Physics/Jolt/JoltBackend.h>
#include <Oxygen/Physics/Jolt/JoltPhysicsSystem.h>

auto oxygen::physics::jolt::GetBackendName() noexcept -> std::string_view
{
  return "jolt";
}

auto oxygen::physics::jolt::CreatePhysicsSystem()
  -> PhysicsResult<std::unique_ptr<system::IPhysicsSystem>>
{
  return Ok(std::make_unique<JoltPhysicsSystem>());
}
