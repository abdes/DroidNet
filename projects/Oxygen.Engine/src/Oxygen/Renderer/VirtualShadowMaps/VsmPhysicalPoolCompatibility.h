//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Renderer/VirtualShadowMaps/VsmPhysicalPagePoolTypes.h>

namespace oxygen::renderer::vsm {

OXGN_RNDR_NDAPI auto ComputePhysicalPoolCompatibility(
  const VsmPhysicalPoolConfig& current,
  const VsmPhysicalPoolConfig& requested) noexcept
  -> VsmPhysicalPoolCompatibilityResult;

} // namespace oxygen::renderer::vsm
