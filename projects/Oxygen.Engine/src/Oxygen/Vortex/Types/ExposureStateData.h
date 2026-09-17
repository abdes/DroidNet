//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Constants.h>

namespace oxygen::vortex {

//! Immutable numerical-domain binding for one rendered frame.
struct alignas(16) FrameExposureData {
  float pre_exposure { 1.0F };
  float one_over_pre_exposure { 1.0F };
  ShaderVisibleIndex global_exposure_state_slot { kInvalidShaderVisibleIndex };
  std::uint32_t flags { 0U };
};

//! GPU qualification of reference products; this alone is not format admission.
struct alignas(16) HdrSuitabilityData {
  float candidate_pre_exposure { 1.0F };
  float maximum_scene_rgb { 0.0F };
  std::uint32_t checked_products { 0U };
  std::uint32_t failure_flags { 0U };
  std::uint32_t first_failure_product { 0U };
  std::uint32_t rejected_samples { 0U };
  std::uint32_t metering_failures { 0U };
  std::uint32_t image_failures { 0U };
  std::uint32_t overflow_failures { 0U };
  std::uint32_t checked_samples { 0U };
  std::uint32_t expected_products { 0U };
  std::uint32_t reserved { 0U };
};
static_assert(sizeof(HdrSuitabilityData) == 48U);

//! Shared GPU exposure-state ABI; uint64 identities use low/high uint32 words.
struct alignas(16) ExposureStateData {
  float displayed_scale { 1.0F };
  float target_scale { 1.0F };
  float latent_scale { 1.0F };
  float latent_target_scale { 1.0F };
  float raw_metered_luminance { 0.0F };
  float raw_metered_ev { 0.0F };
  std::uint32_t flags { 0U };
  std::uint32_t fallback_reason { 1U };
  std::array<std::uint32_t, 2> settings_revision {};
  std::array<std::uint32_t, 2> requested_generation {};
  std::array<std::uint32_t, 2> applied_generation {};
  std::array<std::uint32_t, 2> frame_sequence {};
  float fp16_candidate_pre_exposure { 1.0F };
  std::uint32_t fp16_eligible_streak { 0U };
  std::array<std::uint32_t, 2> product_layout_revision {};
};

//! Non-numerical asynchronous application/range status (low/high words).
struct alignas(16) ExposureCompletedStatus {
  std::array<std::uint32_t, 2> view_state_identity {};
  std::array<std::uint32_t, 2> frame_sequence {};
  std::array<std::uint32_t, 2> settings_revision {};
  std::array<std::uint32_t, 2> requested_generation {};
  std::array<std::uint32_t, 2> applied_generation {};
  std::array<std::uint32_t, 2> product_layout_revision {};
  std::uint32_t flags { 0U };
  std::uint32_t first_failure_product { 0U };
  std::uint32_t first_failure_kind { 0U };
  std::uint32_t fp16_eligible_streak { 0U };
  std::array<std::uint32_t, 2> candidate_state_generation {};
  std::uint32_t transition_rejection_reason { 0U };
  std::uint32_t reserved { 0U };
};
static_assert(sizeof(ExposureCompletedStatus) == 80U);
static_assert(offsetof(ExposureCompletedStatus, view_state_identity) == 0U);
static_assert(offsetof(ExposureCompletedStatus, frame_sequence) == 8U);
static_assert(offsetof(ExposureCompletedStatus, settings_revision) == 16U);
static_assert(offsetof(ExposureCompletedStatus, requested_generation) == 24U);
static_assert(offsetof(ExposureCompletedStatus, applied_generation) == 32U);
static_assert(
  offsetof(ExposureCompletedStatus, product_layout_revision) == 40U);
static_assert(offsetof(ExposureCompletedStatus, flags) == 48U);
static_assert(offsetof(ExposureCompletedStatus, first_failure_product) == 52U);
static_assert(offsetof(ExposureCompletedStatus, first_failure_kind) == 56U);
static_assert(offsetof(ExposureCompletedStatus, fp16_eligible_streak) == 60U);
static_assert(
  offsetof(ExposureCompletedStatus, candidate_state_generation) == 64U);
static_assert(
  offsetof(ExposureCompletedStatus, transition_rejection_reason) == 72U);
static_assert(offsetof(ExposureCompletedStatus, reserved) == 76U);

static_assert(std::is_standard_layout_v<FrameExposureData>);
static_assert(sizeof(FrameExposureData) == 16U);
static_assert(offsetof(FrameExposureData, pre_exposure) == 0U);
static_assert(offsetof(FrameExposureData, one_over_pre_exposure) == 4U);
static_assert(offsetof(FrameExposureData, global_exposure_state_slot) == 8U);
static_assert(offsetof(FrameExposureData, flags) == 12U);
static_assert(std::is_standard_layout_v<ExposureStateData>);
static_assert(sizeof(ExposureStateData) == 80U);
static_assert(offsetof(ExposureStateData, displayed_scale) == 0U);
static_assert(offsetof(ExposureStateData, target_scale) == 4U);
static_assert(offsetof(ExposureStateData, latent_scale) == 8U);
static_assert(offsetof(ExposureStateData, latent_target_scale) == 12U);
static_assert(offsetof(ExposureStateData, raw_metered_luminance) == 16U);
static_assert(offsetof(ExposureStateData, raw_metered_ev) == 20U);
static_assert(offsetof(ExposureStateData, flags) == 24U);
static_assert(offsetof(ExposureStateData, fallback_reason) == 28U);
static_assert(offsetof(ExposureStateData, settings_revision) == 32U);
static_assert(offsetof(ExposureStateData, requested_generation) == 40U);
static_assert(offsetof(ExposureStateData, applied_generation) == 48U);
static_assert(offsetof(ExposureStateData, frame_sequence) == 56U);
static_assert(offsetof(ExposureStateData, fp16_candidate_pre_exposure) == 64U);
static_assert(offsetof(ExposureStateData, fp16_eligible_streak) == 68U);
static_assert(offsetof(ExposureStateData, product_layout_revision) == 72U);

} // namespace oxygen::vortex
