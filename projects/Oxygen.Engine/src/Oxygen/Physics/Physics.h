//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <string_view>

#include <Oxygen/Physics/Backend.h>
#include <Oxygen/Physics/PhysicsError.h>
#include <Oxygen/Physics/System/IPhysicsSystem.h>
#include <Oxygen/Physics/api_export.h>

namespace oxygen::physics {

OXGN_PHYS_NDAPI auto GetSelectedBackend() noexcept -> PhysicsBackend;
OXGN_PHYS_NDAPI auto GetBackendName() noexcept -> std::string_view;
OXGN_PHYS_NDAPI auto CreatePhysicsSystem()
  -> PhysicsResult<std::unique_ptr<system::IPhysicsSystem>>;

} // namespace oxygen::physics
