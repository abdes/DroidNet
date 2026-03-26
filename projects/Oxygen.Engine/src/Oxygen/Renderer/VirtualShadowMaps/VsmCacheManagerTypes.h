//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Core/Types/Bool32.h>
#include <Oxygen/Graphics/Common/Forward.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmPhysicalPageAddressing.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmPhysicalPagePoolTypes.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmProjectionTypes.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmVirtualAddressSpaceTypes.h>
#include <Oxygen/Renderer/api_export.h>

namespace oxygen::renderer::vsm {

struct VsmCacheManagerConfig {
  bool allow_reuse { true };
  bool enable_strict_contract_checks { true };
  std::uint32_t retained_unreferenced_frame_count { 0 };
  std::string debug_name {};

  auto operator==(const VsmCacheManagerConfig&) const -> bool = default;
};

struct VsmCacheManagerFrameConfig {
  bool allow_reuse { true };
  bool force_invalidate_all { false };
  std::string debug_name {};

  auto operator==(const VsmCacheManagerFrameConfig&) const -> bool = default;
};

enum class VsmCacheDataState : std::uint8_t {
  kUnavailable = 0,
  kAvailable = 1,
  kInvalidated = 2,
};

OXGN_RNDR_NDAPI auto to_string(VsmCacheDataState value) noexcept
  -> std::string_view;

enum class VsmCacheBuildState : std::uint8_t {
  kIdle = 0,
  kFrameOpen = 1,
  kPlanned = 2,
  kReady = 3,
};

OXGN_RNDR_NDAPI auto to_string(VsmCacheBuildState value) noexcept
  -> std::string_view;

enum class VsmPageRequestFlags : std::uint8_t {
  kNone = 0,
  kRequired = OXYGEN_FLAG(0),
  kCoarse = OXYGEN_FLAG(1),
  kStaticOnly = OXYGEN_FLAG(2),
};

OXYGEN_DEFINE_FLAGS_OPERATORS(VsmPageRequestFlags)

OXGN_RNDR_API auto to_string(VsmPageRequestFlags value) -> std::string;

struct VsmPageRequest {
  VsmVirtualShadowMapId map_id { 0 };
  VsmVirtualPageCoord page {};
  VsmPageRequestFlags flags { VsmPageRequestFlags::kRequired };

  auto operator==(const VsmPageRequest&) const -> bool = default;
};

using VsmPageRequestSet = std::vector<VsmPageRequest>;
using VsmRemapKeyList = std::vector<std::string>;

enum class VsmPageRequestValidationResult : std::uint8_t {
  kValid = 0,
  kMissingMapId = 1,
  kMissingFlags = 2,
};

OXGN_RNDR_NDAPI auto to_string(VsmPageRequestValidationResult value) noexcept
  -> std::string_view;

enum class VsmRemapKeyListValidationResult : std::uint8_t {
  kValid = 0,
  kEmptyList = 1,
  kEmptyKey = 2,
  kDuplicateKey = 3,
};

OXGN_RNDR_NDAPI auto to_string(VsmRemapKeyListValidationResult value) noexcept
  -> std::string_view;

OXGN_RNDR_NDAPI auto Validate(const VsmPageRequest& request) noexcept
  -> VsmPageRequestValidationResult;
OXGN_RNDR_NDAPI auto IsValid(const VsmPageRequest& request) noexcept -> bool;
OXGN_RNDR_NDAPI auto Validate(const VsmRemapKeyList& remap_keys) noexcept
  -> VsmRemapKeyListValidationResult;
OXGN_RNDR_NDAPI auto IsValid(const VsmRemapKeyList& remap_keys) noexcept
  -> bool;

enum class VsmCacheInvalidationReason : std::uint8_t {
  kNone = 0,
  kExplicitReset = 1,
  kExplicitInvalidateAll = 2,
  kTargetedInvalidate = 3,
  kIncompatiblePool = 4,
  kIncompatibleFrameShape = 5,
};

OXGN_RNDR_NDAPI auto to_string(VsmCacheInvalidationReason value) noexcept
  -> std::string_view;

enum class VsmCacheInvalidationScope : std::uint8_t {
  kDynamicOnly = 0,
  kStaticOnly = 1,
  kStaticAndDynamic = 2,
};

OXGN_RNDR_NDAPI auto to_string(VsmCacheInvalidationScope value) noexcept
  -> std::string_view;

enum class VsmLightCacheKind : std::uint8_t {
  kLocal = 0,
  kDirectional = 1,
};

OXGN_RNDR_NDAPI auto to_string(VsmLightCacheKind value) noexcept
  -> std::string_view;

enum class VsmRenderedPageDirtyFlagBits : std::uint32_t {
  kNone = 0U,
  kDynamicRasterized = 1U << 0U,
  kStaticRasterized = 1U << 1U,
  kRevealForced = 1U << 2U,
};

struct VsmPrimitiveIdentity {
  std::uint32_t transform_index { 0U };
  std::uint32_t transform_generation { 0U };
  std::uint32_t submesh_index { 0U };
  std::uint32_t primitive_flags { 0U };

  auto operator==(const VsmPrimitiveIdentity&) const -> bool = default;
};
static_assert(std::is_standard_layout_v<VsmPrimitiveIdentity>);
static_assert(sizeof(VsmPrimitiveIdentity) == 16U);

struct VsmStaticPrimitivePageFeedbackRecord {
  VsmPrimitiveIdentity primitive {};
  std::uint32_t page_table_index { 0U };
  VsmPhysicalPageIndex physical_page {};
  VsmVirtualShadowMapId map_id { 0U };
  VsmVirtualPageCoord virtual_page {};
  std::uint32_t valid { 0U };

  auto operator==(const VsmStaticPrimitivePageFeedbackRecord&) const -> bool
    = default;
};
static_assert(std::is_standard_layout_v<VsmStaticPrimitivePageFeedbackRecord>);

struct VsmCacheEntryFrameState {
  VsmVirtualShadowMapId virtual_map_id { 0 };
  std::uint32_t first_page_table_entry { 0 };
  std::uint32_t page_table_entry_count { 0 };
  std::uint64_t rendered_frame { 0 };
  std::uint64_t scheduled_frame { 0 };
  bool is_uncached { false };
  bool is_distant { false };
  bool is_retained_unreferenced { false };

  auto operator==(const VsmCacheEntryFrameState&) const -> bool = default;
};

struct VsmLightCacheEntryState {
  std::string remap_key {};
  VsmLightCacheKind kind { VsmLightCacheKind::kLocal };
  VsmCacheEntryFrameState previous_frame_state {};
  VsmCacheEntryFrameState current_frame_state {};

  auto operator==(const VsmLightCacheEntryState&) const -> bool = default;
};

struct VsmPageTableEntry {
  bool is_mapped { false };
  VsmPhysicalPageIndex physical_page {};

  auto operator==(const VsmPageTableEntry&) const -> bool = default;
};

// Shared CPU/GPU metadata record for one physical page.
//
// Unlike the richer cache snapshots, this leaf record is intentionally ABI-safe
// so it can be uploaded directly to a StructuredBuffer without a second mirror
// type. The 32-bit flag-like fields stay scalar on purpose to match HLSL bool
// semantics and avoid layout drift.
struct VsmPhysicalPageMeta {
  Bool32 is_allocated { false };
  Bool32 is_dirty { false };
  Bool32 used_this_frame { false };
  Bool32 view_uncached { false };
  Bool32 static_invalidated { false };
  Bool32 dynamic_invalidated { false };
  std::uint32_t age { 0 };
  VsmVirtualShadowMapId owner_id { 0 };
  std::uint32_t owner_mip_level { 0 };
  VsmVirtualPageCoord owner_page {};
  std::uint64_t last_touched_frame { 0 };

  auto operator==(const VsmPhysicalPageMeta&) const -> bool = default;
};
static_assert(std::is_standard_layout_v<VsmPhysicalPageMeta>);
static_assert(sizeof(VsmPhysicalPageMeta) == 56U);
static_assert(offsetof(VsmPhysicalPageMeta, is_allocated) == 0U);
static_assert(offsetof(VsmPhysicalPageMeta, age) == 24U);
static_assert(offsetof(VsmPhysicalPageMeta, owner_id) == 28U);
static_assert(offsetof(VsmPhysicalPageMeta, owner_mip_level) == 32U);
static_assert(offsetof(VsmPhysicalPageMeta, owner_page) == 36U);
static_assert(offsetof(VsmPhysicalPageMeta, last_touched_frame) == 48U);

enum class VsmAllocationAction : std::uint8_t {
  kReuseExisting = 0,
  kAllocateNew = 1,
  kInitializeOnly = 2,
  kEvict = 3,
  kReject = 4,
};

OXGN_RNDR_NDAPI auto to_string(VsmAllocationAction value) noexcept
  -> std::string_view;

enum class VsmAllocationFailureReason : std::uint8_t {
  kNone = 0,
  kInvalidRequest = 1,
  kNoAvailablePhysicalPages = 2,
  kEntryInvalidated = 3,
  kRemapRejected = 4,
  kCacheUnavailable = 5,
};

OXGN_RNDR_NDAPI auto to_string(VsmAllocationFailureReason value) noexcept
  -> std::string_view;

enum class VsmPageInitializationAction : std::uint8_t {
  kClearDepth = 0,
  kCopyStaticSlice = 1,
};

OXGN_RNDR_NDAPI auto to_string(VsmPageInitializationAction value) noexcept
  -> std::string_view;

struct VsmPageInitializationWorkItem {
  VsmPhysicalPageIndex physical_page {};
  VsmPageInitializationAction action {
    VsmPageInitializationAction::kClearDepth
  };

  auto operator==(const VsmPageInitializationWorkItem&) const -> bool = default;
};

struct VsmPageAllocationDecision {
  VsmPageRequest request {};
  VsmAllocationAction action { VsmAllocationAction::kReject };
  VsmAllocationFailureReason failure_reason {
    VsmAllocationFailureReason::kNone
  };
  VsmPhysicalPageIndex previous_physical_page {};
  VsmPhysicalPageIndex current_physical_page {};

  auto operator==(const VsmPageAllocationDecision&) const -> bool = default;
};

struct VsmPageAllocationPlan {
  std::vector<VsmPageAllocationDecision> decisions {};
  std::vector<VsmPageInitializationWorkItem> initialization_work {};
  std::uint32_t reused_page_count { 0 };
  std::uint32_t allocated_page_count { 0 };
  std::uint32_t initialized_page_count { 0 };
  std::uint32_t evicted_page_count { 0 };
  std::uint32_t rejected_page_count { 0 };

  auto operator==(const VsmPageAllocationPlan&) const -> bool = default;
};

struct VsmPageAllocationSnapshot {
  std::uint64_t frame_generation { 0 };
  std::uint64_t pool_identity { 0 };
  std::uint32_t pool_page_size_texels { 0 };
  std::uint32_t pool_tile_capacity { 0 };
  std::uint32_t pool_slice_count { 0 };
  Format pool_depth_format { Format::kUnknown };
  VsmPhysicalPoolSliceRoleList pool_slice_roles {};
  bool is_hzb_data_available { false };
  VsmVirtualAddressSpaceFrame virtual_frame {};
  std::vector<VsmLightCacheEntryState> light_cache_entries {};
  std::vector<VsmVirtualMapLayout> retained_local_light_layouts {};
  std::vector<VsmClipmapLayout> retained_directional_layouts {};
  std::vector<VsmPrimitiveIdentity> visible_shadow_primitives {};
  std::vector<VsmStaticPrimitivePageFeedbackRecord>
    static_primitive_page_feedback {};
  std::vector<VsmPageRequestProjection> projection_records {};
  std::vector<VsmPageTableEntry> page_table {};
  std::vector<VsmPhysicalPageMeta> physical_pages {};

  auto operator==(const VsmPageAllocationSnapshot&) const -> bool = default;
};

struct VsmPageAllocationFrame {
  VsmPageAllocationSnapshot snapshot {};
  VsmPageAllocationPlan plan {};
  std::shared_ptr<const graphics::Buffer> physical_page_meta_buffer {};
  std::shared_ptr<const graphics::Buffer> page_table_buffer {};
  std::shared_ptr<const graphics::Buffer> page_flags_buffer {};
  std::shared_ptr<const graphics::Buffer> dirty_flags_buffer {};
  std::shared_ptr<const graphics::Buffer> physical_page_list_buffer {};
  std::shared_ptr<const graphics::Buffer> page_rect_bounds_buffer {};
  bool is_ready { false };

  auto operator==(const VsmPageAllocationFrame&) const -> bool = default;
};

using VsmExtractedCacheFrame = VsmPageAllocationSnapshot;

} // namespace oxygen::renderer::vsm
