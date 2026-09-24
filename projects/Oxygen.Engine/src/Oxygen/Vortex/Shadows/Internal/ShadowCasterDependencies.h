//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <vector>

#include <Oxygen/Vortex/PreparedSceneFrame.h>

namespace oxygen::vortex::shadows::internal {

auto BuildShadowCasterDependencies(const PreparedSceneFrame& scene)
  -> std::vector<ShadowCasterDependency>;

} // namespace oxygen::vortex::shadows::internal
