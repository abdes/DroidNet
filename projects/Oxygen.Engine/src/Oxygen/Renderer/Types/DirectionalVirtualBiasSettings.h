//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

namespace oxygen::renderer {

struct DirectionalVirtualBiasSettings {
  float receiver_normal_bias_scale { 1.0F };
  float receiver_constant_bias_scale { 0.0F };
  float receiver_slope_bias_scale { 0.5F };
  float raster_constant_bias_scale { 0.1F };
  float raster_slope_bias_scale { 0.35F };
};

} // namespace oxygen::renderer
