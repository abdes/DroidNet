//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <string_view>

#include <Oxygen/Physics/PhysicsError.h>
#include <Oxygen/Physics/System/IPhysicsSystem.h>

namespace oxygen::physics::jolt {

auto GetBackendName() noexcept -> std::string_view;
auto CreatePhysicsSystem() -> PhysicsResult<std::unique_ptr<system::IPhysicsSystem>>;

} // namespace oxygen::physics::jolt
