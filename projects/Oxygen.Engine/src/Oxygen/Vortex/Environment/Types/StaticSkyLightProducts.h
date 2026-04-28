//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>

#include <glm/vec3.hpp>

#include <Oxygen/Content/ResourceKey.h>
#include <Oxygen/Core/Bindless/Types.h>

namespace oxygen::vortex::environment {

struct StaticSkyLightProductKey {
  content::ResourceKey source_cubemap {};
  std::uint64_t source_revision { 0U };
  std::uint32_t output_face_size { 0U };
  std::uint32_t source_format_class { 0U };
  float source_rotation_radians { 0.0F };
  bool lower_hemisphere_solid_color { true };
  glm::vec3 lower_hemisphere_color { 0.0F, 0.0F, 0.0F };
  float lower_hemisphere_blend_alpha { 1.0F };

  auto operator==(const StaticSkyLightProductKey&) const -> bool = default;
};

enum class StaticSkyLightProductStatus : std::uint8_t {
  kDisabled,
  kUnavailable,
  kRegeneratingCurrentKey,
  kValidCurrentKey,
  kStaleWrongKeyRejected,
};

enum class StaticSkyLightUnavailableReason : std::uint8_t {
  kNone,
  kCapturedSceneDeferred,
  kRealTimeCaptureDeferred,
  kMissingCubemap,
  kResourceResolveFailed,
  kNotTextureCube,
  kUnsupportedFormat,
  kProcessingFailed,
  kShaderConsumerMigrationIncomplete,
};

struct StaticSkyLightProducts {
  StaticSkyLightProductKey key {};
  ShaderVisibleIndex source_cubemap_srv { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex processed_cubemap_srv { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex diffuse_irradiance_sh_srv { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex prefiltered_cubemap_srv { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex brdf_lut_srv { kInvalidShaderVisibleIndex };
  std::uint32_t processed_cubemap_max_mip { 0U };
  std::uint32_t prefiltered_cubemap_max_mip { 0U };
  std::uint32_t product_revision { 0U };
  float source_radiance_scale { 1.0F };
  float average_brightness { 0.0F };
  StaticSkyLightProductStatus status { StaticSkyLightProductStatus::kDisabled };
  StaticSkyLightUnavailableReason unavailable_reason {
    StaticSkyLightUnavailableReason::kNone
  };
};

} // namespace oxygen::vortex::environment
