//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmCacheManagerTypes.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmPhysicalPageAddressing.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmVirtualAddressSpaceTypes.h>

namespace oxygen::renderer::vsm {

// These are narrow shader-facing ABI payloads. Keep the canonical CPU cache
// snapshots separate unless a record is already stable enough to share
// directly, like VsmPhysicalPageMeta.

inline constexpr std::uint32_t kVsmPageTableMappedBit = 0x80000000U;
inline constexpr std::uint32_t kVsmPageTablePhysicalPageMask = 0x7fffffffU;

enum class VsmShaderPageFlagBits : std::uint32_t {
  kNone = 0U,
  kAllocated = OXYGEN_FLAG(0),
  kDynamicUncached = OXYGEN_FLAG(1),
  kStaticUncached = OXYGEN_FLAG(2),
  kDetailGeometry = OXYGEN_FLAG(3),
  kMappedDescendant = OXYGEN_FLAG(4),
};

OXYGEN_DEFINE_FLAGS_OPERATORS(VsmShaderPageFlagBits)

struct VsmShaderPageTableEntry {
  std::uint32_t encoded { 0U };

  auto operator==(const VsmShaderPageTableEntry&) const -> bool = default;
};
static_assert(std::is_standard_layout_v<VsmShaderPageTableEntry>);
static_assert(sizeof(VsmShaderPageTableEntry) == 4U);

[[nodiscard]] constexpr auto MakeUnmappedShaderPageTableEntry() noexcept
  -> VsmShaderPageTableEntry
{
  return {};
}

[[nodiscard]] constexpr auto MakeMappedShaderPageTableEntry(
  const VsmPhysicalPageIndex physical_page) noexcept -> VsmShaderPageTableEntry
{
  return VsmShaderPageTableEntry {
    .encoded = kVsmPageTableMappedBit
      | (physical_page.value & kVsmPageTablePhysicalPageMask),
  };
}

[[nodiscard]] constexpr auto IsMapped(
  const VsmShaderPageTableEntry entry) noexcept -> bool
{
  return (entry.encoded & kVsmPageTableMappedBit) != 0U;
}

[[nodiscard]] constexpr auto DecodePhysicalPageIndex(
  const VsmShaderPageTableEntry entry) noexcept -> VsmPhysicalPageIndex
{
  return VsmPhysicalPageIndex {
    .value = entry.encoded & kVsmPageTablePhysicalPageMask,
  };
}

struct VsmShaderPageFlags {
  std::uint32_t bits { 0U };

  auto operator==(const VsmShaderPageFlags&) const -> bool = default;
};
static_assert(std::is_standard_layout_v<VsmShaderPageFlags>);
static_assert(sizeof(VsmShaderPageFlags) == 4U);

[[nodiscard]] constexpr auto HasAnyFlag(const VsmShaderPageFlags flags,
  const VsmShaderPageFlagBits bits) noexcept -> bool
{
  return (flags.bits & static_cast<std::uint32_t>(bits)) != 0U;
}

enum class VsmShaderPageRequestFlagBits : std::uint32_t {
  kNone = 0U,
  kRequired = OXYGEN_FLAG(0),
  kCoarse = OXYGEN_FLAG(1),
  kStaticOnly = OXYGEN_FLAG(2),
};

OXYGEN_DEFINE_FLAGS_OPERATORS(VsmShaderPageRequestFlagBits)

struct VsmShaderPageRequestFlags {
  std::uint32_t bits { 0U };

  auto operator==(const VsmShaderPageRequestFlags&) const -> bool = default;
};
static_assert(std::is_standard_layout_v<VsmShaderPageRequestFlags>);
static_assert(sizeof(VsmShaderPageRequestFlags) == 4U);

[[nodiscard]] constexpr auto HasAnyRequestFlag(
  const VsmShaderPageRequestFlags flags,
  const VsmShaderPageRequestFlagBits bits) noexcept -> bool
{
  return (flags.bits & static_cast<std::uint32_t>(bits)) != 0U;
}

// Narrow shader upload record for stage 6 reuse decisions.
//
// The CPU planner stays authoritative for reuse eligibility and final metadata.
// This record only carries the compact GPU application payload needed to
// rebuild the current-frame page table from scratch.
struct VsmShaderPageReuseDecision {
  std::uint32_t page_table_index { 0U };
  std::uint32_t physical_page_index { 0U };
  VsmShaderPageFlags page_flags {};
  std::uint32_t _pad0 { 0U };
  VsmPhysicalPageMeta physical_meta {};

  auto operator==(const VsmShaderPageReuseDecision&) const -> bool = default;
};
static_assert(std::is_standard_layout_v<VsmShaderPageReuseDecision>);
static_assert(sizeof(VsmShaderPageReuseDecision) == 72U);
static_assert(offsetof(VsmShaderPageReuseDecision, page_table_index) == 0U);
static_assert(offsetof(VsmShaderPageReuseDecision, physical_page_index) == 4U);
static_assert(offsetof(VsmShaderPageReuseDecision, page_flags) == 8U);
static_assert(offsetof(VsmShaderPageReuseDecision, physical_meta) == 16U);

// Narrow shader upload record for stage 8 allocation decisions.
//
// The packed available-page list produced by stage 7 stays the GPU-side source
// of truth for which physical page slot is consumed, while the CPU planner
// remains authoritative for the deterministic allocation order.
struct VsmShaderPageAllocationDecision {
  std::uint32_t page_table_index { 0U };
  std::uint32_t available_page_list_index { 0U };
  VsmShaderPageFlags page_flags {};
  std::uint32_t _pad0 { 0U };
  VsmPhysicalPageMeta physical_meta {};

  auto operator==(const VsmShaderPageAllocationDecision&) const -> bool
    = default;
};
static_assert(std::is_standard_layout_v<VsmShaderPageAllocationDecision>);
static_assert(sizeof(VsmShaderPageAllocationDecision) == 72U);
static_assert(
  offsetof(VsmShaderPageAllocationDecision, page_table_index) == 0U);
static_assert(
  offsetof(VsmShaderPageAllocationDecision, available_page_list_index) == 4U);
static_assert(offsetof(VsmShaderPageAllocationDecision, page_flags) == 8U);
static_assert(offsetof(VsmShaderPageAllocationDecision, physical_meta) == 16U);

// Compact per-level propagation work item for stages 9 and 10.
//
// VSM level propagation in Oxygen follows the published virtual-map layout:
// each coarser level reuses the same page-grid footprint as the next finer
// level, so propagation operates on matching (x, y) coordinates across levels.
struct VsmShaderPageHierarchyDispatch {
  std::uint32_t first_page_table_entry { 0U };
  std::uint32_t pages_x { 0U };
  std::uint32_t pages_y { 0U };
  std::uint32_t pages_per_level { 0U };
  std::uint32_t source_level { 0U };
  std::uint32_t target_level { 0U };
  std::uint32_t _pad0 { 0U };
  std::uint32_t _pad1 { 0U };

  auto operator==(const VsmShaderPageHierarchyDispatch&) const -> bool
    = default;
};
static_assert(std::is_standard_layout_v<VsmShaderPageHierarchyDispatch>);
static_assert(sizeof(VsmShaderPageHierarchyDispatch) == 32U);
static_assert(
  offsetof(VsmShaderPageHierarchyDispatch, first_page_table_entry) == 0U);
static_assert(offsetof(VsmShaderPageHierarchyDispatch, pages_x) == 4U);
static_assert(offsetof(VsmShaderPageHierarchyDispatch, pages_y) == 8U);
static_assert(offsetof(VsmShaderPageHierarchyDispatch, pages_per_level) == 12U);
static_assert(offsetof(VsmShaderPageHierarchyDispatch, source_level) == 16U);
static_assert(offsetof(VsmShaderPageHierarchyDispatch, target_level) == 20U);

enum class VsmProjectionLightType : std::uint32_t {
  kLocal = 0U,
  kDirectional = 1U,
};

struct VsmProjectionData {
  glm::mat4 view_matrix { 1.0F };
  glm::mat4 projection_matrix { 1.0F };
  glm::vec4 view_origin_ws_pad { 0.0F, 0.0F, 0.0F, 0.0F };
  glm::ivec2 clipmap_corner_offset { 0, 0 };
  std::uint32_t clipmap_level { 0U };
  std::uint32_t light_type { static_cast<std::uint32_t>(
    VsmProjectionLightType::kLocal) };

  auto operator==(const VsmProjectionData&) const -> bool = default;
};
static_assert(std::is_standard_layout_v<VsmProjectionData>);
static_assert(sizeof(VsmProjectionData) == 160U);
static_assert(offsetof(VsmProjectionData, view_matrix) == 0U);
static_assert(offsetof(VsmProjectionData, projection_matrix) == 64U);
static_assert(offsetof(VsmProjectionData, view_origin_ws_pad) == 128U);
static_assert(offsetof(VsmProjectionData, clipmap_corner_offset) == 144U);
static_assert(offsetof(VsmProjectionData, clipmap_level) == 152U);
static_assert(offsetof(VsmProjectionData, light_type) == 156U);

inline constexpr std::uint32_t kVsmInvalidLightIndex = 0xffffffffU;

// Shader-facing routing record for page-request generation.
//
// The generator needs more than the compact projection payload alone: it also
// needs the target virtual map id, the page-grid dimensions, the page-table
// base slot, and the clustered-light index used to prune local-light demand.
struct VsmPageRequestProjection {
  VsmProjectionData projection {};
  VsmVirtualShadowMapId map_id { 0U };
  std::uint32_t first_page_table_entry { 0U };
  std::uint32_t pages_x { 0U };
  std::uint32_t pages_y { 0U };
  std::uint32_t level_count { 1U };
  std::uint32_t coarse_level { 0U };
  std::uint32_t light_index { kVsmInvalidLightIndex };
  std::uint32_t _pad0 { 0U };

  auto operator==(const VsmPageRequestProjection&) const -> bool = default;
};
static_assert(std::is_standard_layout_v<VsmPageRequestProjection>);
static_assert(sizeof(VsmPageRequestProjection) == 192U);
static_assert(offsetof(VsmPageRequestProjection, projection) == 0U);
static_assert(offsetof(VsmPageRequestProjection, map_id) == 160U);
static_assert(
  offsetof(VsmPageRequestProjection, first_page_table_entry) == 164U);
static_assert(offsetof(VsmPageRequestProjection, pages_x) == 168U);
static_assert(offsetof(VsmPageRequestProjection, pages_y) == 172U);
static_assert(offsetof(VsmPageRequestProjection, level_count) == 176U);
static_assert(offsetof(VsmPageRequestProjection, coarse_level) == 180U);
static_assert(offsetof(VsmPageRequestProjection, light_index) == 184U);

} // namespace oxygen::renderer::vsm
