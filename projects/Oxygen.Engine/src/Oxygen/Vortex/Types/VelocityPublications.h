//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstdint>
#include <limits>

#include <Oxygen/Base/Compilers.h>
#include <Oxygen/Core/Constants.h>

namespace oxygen::vortex {

enum class VelocityProducerFamily : std::uint32_t {
  kSkinnedPose = 0U,
  kMorphDeformation = 1U,
  kMaterialWpo = 2U,
  kMotionVectorStatus = 3U,
};

enum class MotionPublicationCapabilityBits : std::uint32_t {
  kUsesWorldPositionOffset = 1U << 0U,
  kUsesMotionVectorWorldOffset = 1U << 1U,
  kUsesTemporalResponsiveness = 1U << 2U,
  kHasPixelAnimation = 1U << 3U,
  kHasRuntimePayload = 1U << 4U,
};

[[nodiscard]] constexpr auto HasAnyMotionPublicationCapability(
  const std::uint32_t flags, const MotionPublicationCapabilityBits bits) noexcept
  -> bool
{
  return (flags & static_cast<std::uint32_t>(bits)) != 0U;
}

constexpr std::uint32_t kInvalidVelocityPublicationIndex {
  (std::numeric_limits<std::uint32_t>::max)()
};

enum class VelocityDrawPublicationFlagBits : std::uint32_t {
  kCurrentSkinnedPoseValid = 1U << 0U,
  kPreviousSkinnedPoseValid = 1U << 1U,
  kCurrentMorphValid = 1U << 2U,
  kPreviousMorphValid = 1U << 3U,
  kCurrentMaterialWpoValid = 1U << 4U,
  kPreviousMaterialWpoValid = 1U << 5U,
  kMaterialWpoHistoryValid = 1U << 6U,
  kCurrentMotionVectorStatusValid = 1U << 7U,
  kPreviousMotionVectorStatusValid = 1U << 8U,
  kMotionVectorStatusHistoryValid = 1U << 9U,
};

[[nodiscard]] constexpr auto HasAnyVelocityDrawPublicationFlag(
  const std::uint32_t flags, const VelocityDrawPublicationFlagBits bits) noexcept
  -> bool
{
  return (flags & static_cast<std::uint32_t>(bits)) != 0U;
}

struct alignas(packing::kShaderDataFieldAlignment) SkinnedPosePublication {
  std::uint64_t contract_hash { 0U };
  std::uint32_t capability_flags { 0U };
  std::uint32_t reserved0 { 0U };
  std::array<float, 4U> reserved_payload0 {};
};
static_assert(sizeof(SkinnedPosePublication) == 32U);

struct alignas(packing::kShaderDataFieldAlignment) MorphPublication {
  std::uint64_t contract_hash { 0U };
  std::uint32_t capability_flags { 0U };
  std::uint32_t reserved0 { 0U };
  std::array<float, 4U> reserved_payload0 {};
};
static_assert(sizeof(MorphPublication) == 32U);

struct alignas(packing::kShaderDataFieldAlignment) MaterialWpoPublication {
  std::uint64_t contract_hash { 0U };
  std::uint32_t capability_flags { 0U };
  std::uint32_t reserved0 { 0U };
  std::array<float, 4U> parameter_block0 {};
};
static_assert(sizeof(MaterialWpoPublication) == 32U);

struct alignas(packing::kShaderDataFieldAlignment)
MotionVectorStatusPublication {
  std::uint64_t contract_hash { 0U };
  std::uint32_t capability_flags { 0U };
  std::uint32_t reserved0 { 0U };
  std::array<float, 4U> parameter_block0 {};
};
static_assert(sizeof(MotionVectorStatusPublication) == 32U);

struct alignas(packing::kShaderDataFieldAlignment) VelocityDrawMetadata {
  std::uint32_t current_skinned_pose_index { kInvalidVelocityPublicationIndex };
  std::uint32_t previous_skinned_pose_index { kInvalidVelocityPublicationIndex };
  std::uint32_t current_morph_index { kInvalidVelocityPublicationIndex };
  std::uint32_t previous_morph_index { kInvalidVelocityPublicationIndex };
  std::uint32_t current_material_wpo_index { kInvalidVelocityPublicationIndex };
  std::uint32_t previous_material_wpo_index { kInvalidVelocityPublicationIndex };
  std::uint32_t current_motion_vector_status_index {
    kInvalidVelocityPublicationIndex
  };
  std::uint32_t previous_motion_vector_status_index {
    kInvalidVelocityPublicationIndex
  };
  std::uint32_t publication_flags { 0U };
  std::array<std::uint32_t, 3U> _pad_to_16 {};
};
static_assert(sizeof(VelocityDrawMetadata) == 48U);

} // namespace oxygen::vortex
