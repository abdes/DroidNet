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
#include <Oxygen/Renderer/VirtualShadowMaps/VsmProjectionTypes.h>
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
  std::uint32_t physical_page_index { 0U };
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
  offsetof(VsmShaderPageAllocationDecision, physical_page_index) == 4U);
static_assert(offsetof(VsmShaderPageAllocationDecision, page_flags) == 8U);
static_assert(offsetof(VsmShaderPageAllocationDecision, physical_meta) == 16U);

// Compact per-level propagation work item for stages 9 and 10.
//
// Oxygen stores each level in a fixed-size per-level page-table slice, but the
// propagation logic follows UE-style hierarchical reduction semantics: threads
// iterate the effective source level footprint, and parent addresses are
// derived by collapsing child coordinates through >> 1.
struct VsmShaderPageHierarchyDispatch {
  std::uint32_t first_page_table_entry { 0U };
  std::uint32_t level0_pages_x { 0U };
  std::uint32_t level0_pages_y { 0U };
  std::uint32_t pages_per_level { 0U };
  std::uint32_t source_level { 0U };
  std::uint32_t target_level { 0U };
  std::uint32_t source_pages_x { 0U };
  std::uint32_t source_pages_y { 0U };

  auto operator==(const VsmShaderPageHierarchyDispatch&) const -> bool
    = default;
};
static_assert(std::is_standard_layout_v<VsmShaderPageHierarchyDispatch>);
static_assert(sizeof(VsmShaderPageHierarchyDispatch) == 32U);
static_assert(
  offsetof(VsmShaderPageHierarchyDispatch, first_page_table_entry) == 0U);
static_assert(offsetof(VsmShaderPageHierarchyDispatch, level0_pages_x) == 4U);
static_assert(offsetof(VsmShaderPageHierarchyDispatch, level0_pages_y) == 8U);
static_assert(offsetof(VsmShaderPageHierarchyDispatch, pages_per_level) == 12U);
static_assert(offsetof(VsmShaderPageHierarchyDispatch, source_level) == 16U);
static_assert(offsetof(VsmShaderPageHierarchyDispatch, target_level) == 20U);
static_assert(offsetof(VsmShaderPageHierarchyDispatch, source_pages_x) == 24U);
static_assert(offsetof(VsmShaderPageHierarchyDispatch, source_pages_y) == 28U);

// Shader-facing invalidation work item for the Phase J dedicated GPU pass.
//
// The CPU cache manager stays responsible for mapping scene changes and static
// feedback to one previous-frame projection record. The GPU stage only
// receives the compact per-projection work item it needs to mark stale
// physical-page metadata.
struct VsmShaderInvalidationWorkItem {
  VsmPrimitiveIdentity primitive {};
  glm::vec4 world_bounding_sphere { 0.0F, 0.0F, 0.0F, 0.0F };
  std::uint32_t projection_index { 0U };
  std::uint32_t scope { 0U };
  std::uint32_t matched_static_feedback { 0U };
  std::uint32_t _pad0 { 0U };

  auto operator==(const VsmShaderInvalidationWorkItem&) const -> bool = default;
};
static_assert(std::is_standard_layout_v<VsmShaderInvalidationWorkItem>);
static_assert(sizeof(VsmShaderInvalidationWorkItem) == 48U);
static_assert(offsetof(VsmShaderInvalidationWorkItem, primitive) == 0U);
static_assert(
  offsetof(VsmShaderInvalidationWorkItem, world_bounding_sphere) == 16U);
static_assert(offsetof(VsmShaderInvalidationWorkItem, projection_index) == 32U);
static_assert(offsetof(VsmShaderInvalidationWorkItem, scope) == 36U);
static_assert(
  offsetof(VsmShaderInvalidationWorkItem, matched_static_feedback) == 40U);

// Shader-facing shadow-raster work item for one prepared virtual page.
//
// The rasterizer pass computes a page-local cropped view-projection matrix on
// the CPU so the GPU culling shader can test draw bounds directly against the
// exact page frustum it will later rasterize.
enum class VsmShaderRasterPageJobFlagBits : std::uint32_t {
  kNone = 0U,
  kStaticOnly = OXYGEN_FLAG(0),
};

OXYGEN_DEFINE_FLAGS_OPERATORS(VsmShaderRasterPageJobFlagBits)

struct VsmShaderRasterPageJob {
  glm::mat4 view_projection_matrix { 1.0F };
  std::uint32_t page_table_index { 0U };
  VsmVirtualShadowMapId map_id { 0U };
  std::uint32_t virtual_page_x { 0U };
  std::uint32_t virtual_page_y { 0U };
  std::uint32_t virtual_page_level { 0U };
  std::uint32_t physical_page_index { 0U };
  std::uint32_t job_flags { 0U };
  std::uint32_t _pad0 { 0U };

  auto operator==(const VsmShaderRasterPageJob&) const -> bool = default;
};
static_assert(std::is_standard_layout_v<VsmShaderRasterPageJob>);
static_assert(sizeof(VsmShaderRasterPageJob) == 96U);
static_assert(offsetof(VsmShaderRasterPageJob, view_projection_matrix) == 0U);
static_assert(offsetof(VsmShaderRasterPageJob, page_table_index) == 64U);
static_assert(offsetof(VsmShaderRasterPageJob, map_id) == 68U);
static_assert(offsetof(VsmShaderRasterPageJob, virtual_page_x) == 72U);
static_assert(offsetof(VsmShaderRasterPageJob, virtual_page_y) == 76U);
static_assert(offsetof(VsmShaderRasterPageJob, virtual_page_level) == 80U);
static_assert(offsetof(VsmShaderRasterPageJob, physical_page_index) == 84U);
static_assert(offsetof(VsmShaderRasterPageJob, job_flags) == 88U);

// Packed indirect command layout for ExecuteIndirect(kDrawWithRootConstant).
//
// DWORD0 is the per-draw root constant (`g_DrawIndex`), followed by the native
// D3D12 draw-arguments payload.
struct VsmShaderIndirectDrawCommand {
  std::uint32_t draw_index { 0U };
  std::uint32_t vertex_count_per_instance { 0U };
  std::uint32_t instance_count { 0U };
  std::uint32_t start_vertex_location { 0U };
  std::uint32_t start_instance_location { 0U };

  auto operator==(const VsmShaderIndirectDrawCommand&) const -> bool = default;
};
static_assert(std::is_standard_layout_v<VsmShaderIndirectDrawCommand>);
static_assert(sizeof(VsmShaderIndirectDrawCommand) == 20U);
static_assert(offsetof(VsmShaderIndirectDrawCommand, draw_index) == 0U);
static_assert(
  offsetof(VsmShaderIndirectDrawCommand, vertex_count_per_instance) == 4U);
static_assert(offsetof(VsmShaderIndirectDrawCommand, instance_count) == 8U);
static_assert(
  offsetof(VsmShaderIndirectDrawCommand, start_vertex_location) == 12U);
static_assert(
  offsetof(VsmShaderIndirectDrawCommand, start_instance_location) == 16U);

} // namespace oxygen::renderer::vsm
