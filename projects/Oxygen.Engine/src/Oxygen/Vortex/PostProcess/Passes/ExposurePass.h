//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Vortex/PostProcess/Types/PostProcessConfig.h>
#include <Oxygen/Vortex/api_export.h>

namespace oxygen::vortex {

class Renderer;

namespace postprocess::internal {
class ExposureCalculator;
} // namespace postprocess::internal

namespace postprocess {

class ExposurePass {
public:
  struct Result {
    bool requested { false };
    bool executed { false };
    bool used_fixed_exposure { false };
    float exposure_value { 1.0F };
  };

  OXGN_VRTX_API explicit ExposurePass(Renderer& renderer);
  OXGN_VRTX_API ~ExposurePass();

  ExposurePass(const ExposurePass&) = delete;
  auto operator=(const ExposurePass&) -> ExposurePass& = delete;
  ExposurePass(ExposurePass&&) = delete;
  auto operator=(ExposurePass&&) -> ExposurePass& = delete;

  [[nodiscard]] OXGN_VRTX_API auto Execute(
    const PostProcessConfig& config) const -> Result;

private:
  Renderer& renderer_;
  std::unique_ptr<internal::ExposureCalculator> calculator_;
};

} // namespace postprocess

} // namespace oxygen::vortex
