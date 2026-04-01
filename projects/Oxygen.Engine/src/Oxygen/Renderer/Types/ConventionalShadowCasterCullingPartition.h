//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

#include <Oxygen/Renderer/Types/PassMask.h>

namespace oxygen::renderer {

//! Shader upload record for one conventional raster partition.
/*!
 `CSM-4` builds counted-indirect command/count buffers per partition while the
 CPU-side raster path remains unchanged. This record binds one partition's draw
 range and output UAVs for the compute culling pass.
*/
struct alignas(16) ConventionalShadowCasterCullingPartition {
  std::uint32_t record_begin { 0U };
  std::uint32_t record_count { 0U };
  std::uint32_t command_uav_index { 0U };
  std::uint32_t count_uav_index { 0U };
  std::uint32_t max_commands_per_job { 0U };
  std::uint32_t partition_index { 0U };
  engine::PassMask pass_mask {};
  std::uint32_t _pad0 { 0U };

  auto operator==(const ConventionalShadowCasterCullingPartition&) const -> bool
    = default;
};

static_assert(
  std::is_standard_layout_v<ConventionalShadowCasterCullingPartition>);
static_assert(sizeof(ConventionalShadowCasterCullingPartition) == 32U);
static_assert(
  offsetof(ConventionalShadowCasterCullingPartition, record_begin) == 0U);
static_assert(
  offsetof(ConventionalShadowCasterCullingPartition, record_count) == 4U);
static_assert(
  offsetof(ConventionalShadowCasterCullingPartition, command_uav_index) == 8U);
static_assert(
  offsetof(ConventionalShadowCasterCullingPartition, count_uav_index) == 12U);
static_assert(
  offsetof(ConventionalShadowCasterCullingPartition, max_commands_per_job)
  == 16U);
static_assert(
  offsetof(ConventionalShadowCasterCullingPartition, partition_index) == 20U);
static_assert(
  offsetof(ConventionalShadowCasterCullingPartition, pass_mask) == 24U);

} // namespace oxygen::renderer
