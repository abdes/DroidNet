//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Physics/Physics.h>

#if defined(OXGN_PHYS_BACKEND_JOLT)
#  include <Oxygen/Physics/Jolt/JoltBackend.h>
#endif
#if defined(OXGN_PHYS_BACKEND_PHYSX)
#  include <Oxygen/Physics/PhysX/PhysXBackend.h>
#endif

auto oxygen::physics::GetSelectedBackend() noexcept -> PhysicsBackend
{
#if defined(OXGN_PHYS_BACKEND_JOLT) && defined(OXGN_PHYS_BACKEND_NONE)
#  error "Multiple physics backends selected. Select exactly one backend."
#elif defined(OXGN_PHYS_BACKEND_JOLT)
  return PhysicsBackend::kJolt;
#elif defined(OXGN_PHYS_BACKEND_NONE)
  return PhysicsBackend::kNone;
#else
#  error "No physics backend selected. Set OXYGEN_PHYSICS_BACKEND in CMake."
#endif
}

auto oxygen::physics::GetBackendName() noexcept -> std::string_view
{
  return nostd::to_string(GetSelectedBackend());
}

auto oxygen::physics::CreatePhysicsSystem(const PhysicsBackend backend)
  -> PhysicsResult<std::unique_ptr<system::IPhysicsSystem>>
{
  switch (backend) {
  case PhysicsBackend::kNone:
    return Err(PhysicsError::kBackendUnavailable);
  case PhysicsBackend::kJolt:
#if defined(OXGN_PHYS_BACKEND_JOLT)
    return oxygen::physics::jolt::CreatePhysicsSystem();
#else
    return Err(PhysicsError::kBackendUnavailable);
#endif
  case PhysicsBackend::kPhysX:
#if defined(OXGN_PHYS_BACKEND_PHYSX)
    return oxygen::physics::physx::CreatePhysicsSystem();
#else
    return Err(PhysicsError::kBackendUnavailable);
#endif
  }
  return Err(PhysicsError::kInvalidArgument);
}
