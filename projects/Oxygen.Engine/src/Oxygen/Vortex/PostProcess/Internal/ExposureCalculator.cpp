//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>

#include <Oxygen/Vortex/PostProcess/Internal/ExposureCalculator.h>

namespace oxygen::vortex::postprocess::internal {

auto ExposureCalculator::ResolveExposure(
  const PostProcessConfig& config) const noexcept -> float
{
  return (std::max)(config.fixed_exposure, 0.0001F);
}

} // namespace oxygen::vortex::postprocess::internal
