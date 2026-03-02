//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <span>
#include <string>
#include <vector>

#include <Oxygen/Data/PakFormat.h>

namespace oxygen::content::import::internal {

struct ParsedPhysicsResourceDescriptorSidecar final {
  data::pak::core::ResourceIndexT resource_index
    = data::pak::core::kNoResourceIndex;
  data::pak::physics::PhysicsResourceDesc descriptor {};
};

[[nodiscard]] auto SerializePhysicsResourceDescriptorSidecar(
  data::pak::core::ResourceIndexT resource_index,
  const data::pak::physics::PhysicsResourceDesc& descriptor)
  -> std::vector<std::byte>;

[[nodiscard]] auto ParsePhysicsResourceDescriptorSidecar(
  std::span<const std::byte> bytes, ParsedPhysicsResourceDescriptorSidecar& out,
  std::string& error_message) -> bool;

} // namespace oxygen::content::import::internal
