//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Vortex/PostProcess/Types/PostProcessConfig.h>

namespace oxygen::vortex::postprocess::internal {

class ExposureCalculator {
public:
  [[nodiscard]] auto ResolveExposure(
    const PostProcessConfig& config) const noexcept -> float;
};

} // namespace oxygen::vortex::postprocess::internal
