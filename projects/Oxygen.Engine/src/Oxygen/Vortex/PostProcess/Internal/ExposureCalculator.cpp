//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Vortex/PostProcess/Internal/ExposureCalculator.h>
#include <Oxygen/Vortex/PostProcess/Types/PostProcessConfig.h>

namespace oxygen::vortex::postprocess::internal {

auto ExposureCalculator::ResolveExposure(
  const ResolvedPostProcessConfig& config) const noexcept -> float
{
  return config.Exposure().fixed_scale;
}

} // namespace oxygen::vortex::postprocess::internal
