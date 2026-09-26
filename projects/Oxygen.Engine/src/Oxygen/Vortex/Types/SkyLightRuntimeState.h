//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>

#include <Oxygen/Content/ResourceKey.h>
#include <Oxygen/Vortex/Environment/Types/StaticSkyLightProducts.h>

namespace oxygen::vortex {

enum class SkyLightGpuValidation : std::uint8_t {
  kNotRequested,
  kPending,
  kValid,
  kInvalid,
  kUnavailable,
};

//! Last scene-specific sky-light decision; contains no GPU ownership.
struct SkyLightRuntimeState {
  bool observed { false };
  bool enabled { false };
  //! Complete products accepted for ordered consumption, not GPU readback.
  bool usable { false };
  bool empty_capture { false };
  std::uint32_t source { 0U };
  content::ResourceKey source_cubemap {};
  std::uint64_t scene_lifetime { 0U };
  std::uint64_t frame_sequence { 0U };
  std::uint64_t published_source_revision { 0U };
  std::uint64_t desired_source_revision { 0U };
  std::uint32_t published_revision { 0U };
  std::uint32_t building_revision { 0U };
  std::uint32_t face_size { 0U };
  //! Snapshot age while behind the desired source; zero when current.
  std::uint64_t source_age_frames { 0U };
  double cpu_update_ms { 0.0 };
  environment::StaticSkyLightProductStatus status {
    environment::StaticSkyLightProductStatus::kDisabled
  };
  environment::StaticSkyLightUnavailableReason unavailable_reason {
    environment::StaticSkyLightUnavailableReason::kNone
  };
  SkyLightGpuValidation gpu_validation { SkyLightGpuValidation::kNotRequested };
  //! Matching generation checked on the GPU; zero until a readback succeeds.
  std::uint32_t validated_revision { 0U };
  //! Most recent rejected generation, including one superseded before readback.
  std::uint32_t last_failed_gpu_revision { 0U };
  //! Meaningful only for kValid; the shader consumes its own GPU metadata.
  float source_radiance_scale { 1.0F };
  float average_brightness { 0.0F };
};

} // namespace oxygen::vortex
