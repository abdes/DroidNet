//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>

#include <Oxygen/Core/Bindless/Types.h>

#include <Oxygen/Vortex/Environment/Types/AtmosphereModel.h>
#include <Oxygen/Vortex/Environment/Types/HeightFogModel.h>
#include <Oxygen/Vortex/Environment/Types/SkyLightEnvironmentModel.h>
#include <Oxygen/Vortex/Environment/Types/VolumetricFogModel.h>

namespace oxygen::vortex::environment {

struct EnvironmentViewProducts {
  AtmosphereModel atmosphere {};
  HeightFogModel height_fog {};
  SkyLightEnvironmentModel sky_light {};
  VolumetricFogModel volumetric_fog {};

  ShaderVisibleIndex sky_view_lut_srv { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex camera_aerial_perspective_srv { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex distant_sky_light_lut_srv { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex integrated_light_scattering_srv {
    kInvalidShaderVisibleIndex
  };

  std::uint32_t flags { 0U };
};

} // namespace oxygen::vortex::environment
