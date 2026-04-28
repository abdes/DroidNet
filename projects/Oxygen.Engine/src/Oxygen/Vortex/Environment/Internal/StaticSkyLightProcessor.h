//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstdint>
#include <expected>
#include <span>
#include <vector>

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <Oxygen/Vortex/Environment/Types/SkyLightEnvironmentModel.h>
#include <Oxygen/Vortex/api_export.h>

namespace oxygen::data {
class TextureResource;
}

namespace oxygen::vortex::environment::internal {

inline constexpr std::uint32_t kStaticSkyLightDiffuseShElementCount = 8U;

enum class StaticSkyLightProcessFailure : std::uint8_t {
  kInvalidDimensions,
  kUnsupportedFormat,
  kInvalidPayloadLayout,
};

struct StaticSkyLightCpuProducts {
  std::array<glm::vec4, kStaticSkyLightDiffuseShElementCount>
    diffuse_irradiance_sh {};
  std::vector<glm::vec4> processed_rgba {};
  std::uint32_t output_face_size { 0U };
  float source_radiance_scale { 1.0F };
  float average_brightness { 0.0F };
};

[[nodiscard]] OXGN_VRTX_API auto ProcessStaticSkyLightCubemapCpu(
  const data::TextureResource& source_cubemap,
  const SkyLightEnvironmentModel& sky_light, std::uint32_t output_face_size)
  -> std::expected<StaticSkyLightCpuProducts, StaticSkyLightProcessFailure>;

[[nodiscard]] OXGN_VRTX_API auto EvaluatePackedStaticSkyLightDiffuseSh(
  std::span<const glm::vec4, kStaticSkyLightDiffuseShElementCount> sh,
  glm::vec3 normal_ws) -> glm::vec3;

} // namespace oxygen::vortex::environment::internal
