//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <memory>

#include <Oxygen/Vortex/PostProcess/Internal/ExposureCalculator.h>
#include <Oxygen/Vortex/PostProcess/Passes/ExposurePass.h>

namespace oxygen::vortex::postprocess {

ExposurePass::ExposurePass(Renderer& renderer)
  : renderer_(renderer)
  , calculator_(std::make_unique<internal::ExposureCalculator>())
{
}

ExposurePass::~ExposurePass() = default;

auto ExposurePass::Execute(const PostProcessConfig& config) const -> Result
{
  static_cast<void>(renderer_);
  return {
    .requested = true,
    .executed = config.enable_auto_exposure,
    .used_fixed_exposure = !config.enable_auto_exposure,
    .exposure_value = calculator_->ResolveExposure(config),
  };
}

} // namespace oxygen::vortex::postprocess
