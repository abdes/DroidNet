//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <vector>

#include <glm/vec4.hpp>

#include <Oxygen/Vortex/api_export.h>

namespace oxygen::vortex::lighting::internal {

OXGN_VRTX_NDAPI auto GeneratePointLightProxySphereVertices()
  -> std::vector<glm::vec4>;

OXGN_VRTX_NDAPI auto GenerateSpotLightProxyConeVertices()
  -> std::vector<glm::vec4>;

} // namespace oxygen::vortex::lighting::internal
