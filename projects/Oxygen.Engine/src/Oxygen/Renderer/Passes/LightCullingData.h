//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>

namespace oxygen::engine {

//! Simple POD to aggregate light-culling related binding/configuration data
//! passed from the culling pass into the environment dynamic data manager.
struct LightCullingData {
  uint32_t bindless_cluster_grid_slot { 0 };
  uint32_t bindless_cluster_index_list_slot { 0 };
  uint32_t cluster_dim_x { 0 };
  uint32_t cluster_dim_y { 0 };
  uint32_t cluster_dim_z { 0 };
  uint32_t tile_size_px { 16 };
};

} // namespace oxygen::engine
