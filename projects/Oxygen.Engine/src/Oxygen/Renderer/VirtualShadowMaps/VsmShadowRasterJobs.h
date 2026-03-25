// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <span>
#include <vector>

#include <Oxygen/Core/Types/Scissors.h>
#include <Oxygen/Core/Types/ViewPort.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmCacheManagerTypes.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmPhysicalPagePoolTypes.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmShaderTypes.h>
#include <Oxygen/Renderer/api_export.h>

namespace oxygen::renderer::vsm {

// Prepared CPU-side raster job for one virtual page mapped into the physical
// VSM pool. This is intentionally a narrow staging record for the shadow
// rasterizer pass, not a new long-lived cache product.
struct VsmShadowRasterPageJob {
  std::uint32_t page_table_index { 0U };
  VsmVirtualShadowMapId map_id { 0U };
  VsmVirtualPageCoord virtual_page {};
  VsmPhysicalPageIndex physical_page {};
  VsmPhysicalPageCoord physical_coord {};
  VsmPageRequestProjection projection {};
  oxygen::ViewPort viewport {};
  oxygen::Scissors scissors {};
  bool static_only { false };
};

OXGN_RNDR_API auto BuildShadowRasterPageJobs(
  const VsmPageAllocationFrame& frame,
  const VsmPhysicalPoolSnapshot& physical_pool,
  std::span<const VsmPageRequestProjection> projections)
  -> std::vector<VsmShadowRasterPageJob>;

} // namespace oxygen::renderer::vsm
