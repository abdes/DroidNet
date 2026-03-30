//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Renderer/VirtualShadowMaps/VsmVirtualAddressSpaceTypes.h>
#include <Oxygen/Renderer/api_export.h>

namespace oxygen::renderer::vsm {

OXGN_RNDR_NDAPI auto BuildVirtualRemapTable(
  const VsmVirtualAddressSpaceFrame& previous_frame,
  const VsmVirtualAddressSpaceFrame& current_frame) -> VsmVirtualRemapTable;

} // namespace oxygen::renderer::vsm
