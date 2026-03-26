//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <type_traits>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmCacheManagerTypes.h>

#include "VirtualShadowTestFixtures.h"

namespace {

using oxygen::renderer::vsm::IsValid;
using oxygen::renderer::vsm::Validate;
using oxygen::renderer::vsm::VsmAllocationAction;
using oxygen::renderer::vsm::VsmAllocationFailureReason;
using oxygen::renderer::vsm::VsmCacheBuildState;
using oxygen::renderer::vsm::VsmCacheDataState;
using oxygen::renderer::vsm::VsmCacheEntryFrameState;
using oxygen::renderer::vsm::VsmCacheInvalidationReason;
using oxygen::renderer::vsm::VsmCacheInvalidationScope;
using oxygen::renderer::vsm::VsmCacheManagerConfig;
using oxygen::renderer::vsm::VsmCacheManagerFrameConfig;
using oxygen::renderer::vsm::VsmExtractedCacheFrame;
using oxygen::renderer::vsm::VsmLightCacheEntryState;
using oxygen::renderer::vsm::VsmLightCacheKind;
using oxygen::renderer::vsm::VsmPageAllocationDecision;
using oxygen::renderer::vsm::VsmPageAllocationFrame;
using oxygen::renderer::vsm::VsmPageAllocationPlan;
using oxygen::renderer::vsm::VsmPageAllocationSnapshot;
using oxygen::renderer::vsm::VsmPageInitializationAction;
using oxygen::renderer::vsm::VsmPageInitializationWorkItem;
using oxygen::renderer::vsm::VsmPageRequest;
using oxygen::renderer::vsm::VsmPageRequestFlags;
using oxygen::renderer::vsm::VsmPageRequestValidationResult;
using oxygen::renderer::vsm::VsmPageTableEntry;
using oxygen::renderer::vsm::VsmPhysicalPageIndex;
using oxygen::renderer::vsm::VsmPhysicalPageMeta;
using oxygen::renderer::vsm::VsmRemapKeyList;
using oxygen::renderer::vsm::VsmRemapKeyListValidationResult;
using oxygen::renderer::vsm::VsmVirtualAddressSpaceFrame;
using oxygen::renderer::vsm::VsmVirtualMapLayout;
using oxygen::renderer::vsm::VsmVirtualPageCoord;
using oxygen::renderer::vsm::testing::VirtualShadowTest;
using ::testing::Eq;
using ::testing::IsEmpty;
using ::testing::Not;

template <typename T>
concept HasNoStdToString = requires(const T& value) {
  { nostd::to_string(value) } -> std::convertible_to<std::string_view>;
};

template <typename T>
concept HasPoolIdentityField = requires(T value) { value.pool_identity; };

template <typename T>
concept HasCurrentFrameField = requires(T value) { value.current_frame; };

template <typename T>
concept HasPhysicalPoolField = requires(T value) { value.physical_pool; };

template <typename T>
concept HasPreviousToCurrentRemapField
  = requires(T value) { value.previous_to_current_remap; };

template <typename T>
concept HasVirtualFrameField = requires(T value) { value.virtual_frame; };

template <typename T> auto ExpectStringSurface(const T value) -> void
{
  const auto first = std::string(nostd::to_string(value));
  const auto second = std::string(nostd::to_string(value));
  EXPECT_THAT(first, Not(IsEmpty()));
  EXPECT_THAT(first, Eq(second));
}

class VsmCacheManagerTypesTest : public VirtualShadowTest { };

NOLINT_TEST_F(VsmCacheManagerTypesTest,
  CacheManagerTypesExposeStringSurfaceWithoutMirroringTheSeam)
{
  static_assert(std::is_copy_constructible_v<VsmCacheManagerConfig>);
  static_assert(std::is_copy_constructible_v<VsmCacheManagerFrameConfig>);
  static_assert(std::is_copy_constructible_v<VsmPageAllocationSnapshot>);
  static_assert(std::is_copy_constructible_v<VsmPageAllocationFrame>);
  static_assert(
    std::is_same_v<VsmExtractedCacheFrame, VsmPageAllocationSnapshot>);

  static_assert(HasNoStdToString<VsmCacheDataState>);
  static_assert(HasNoStdToString<VsmCacheBuildState>);
  static_assert(HasNoStdToString<VsmPageRequestFlags>);
  static_assert(HasNoStdToString<VsmPageRequestValidationResult>);
  static_assert(HasNoStdToString<VsmRemapKeyListValidationResult>);
  static_assert(HasNoStdToString<VsmCacheInvalidationReason>);
  static_assert(HasNoStdToString<VsmCacheInvalidationScope>);
  static_assert(HasNoStdToString<VsmLightCacheKind>);
  static_assert(HasNoStdToString<VsmAllocationAction>);
  static_assert(HasNoStdToString<VsmAllocationFailureReason>);
  static_assert(HasNoStdToString<VsmPageInitializationAction>);

  static_assert(!HasPoolIdentityField<VsmCacheManagerConfig>);
  static_assert(!HasPoolIdentityField<VsmCacheManagerFrameConfig>);
  static_assert(!HasCurrentFrameField<VsmCacheManagerConfig>);
  static_assert(!HasCurrentFrameField<VsmCacheManagerFrameConfig>);
  static_assert(!HasPhysicalPoolField<VsmCacheManagerConfig>);
  static_assert(!HasPhysicalPoolField<VsmCacheManagerFrameConfig>);
  static_assert(!HasPreviousToCurrentRemapField<VsmCacheManagerConfig>);
  static_assert(!HasPreviousToCurrentRemapField<VsmCacheManagerFrameConfig>);
  static_assert(!HasVirtualFrameField<VsmCacheManagerConfig>);
  static_assert(!HasVirtualFrameField<VsmCacheManagerFrameConfig>);

  const auto config = VsmCacheManagerConfig {
    .allow_reuse = false,
    .enable_strict_contract_checks = false,
    .debug_name = "phase1-cache-config",
  };
  const auto frame_config = VsmCacheManagerFrameConfig {
    .allow_reuse = false,
    .force_invalidate_all = true,
    .debug_name = "phase1-frame-config",
  };

  EXPECT_FALSE(config.allow_reuse);
  EXPECT_FALSE(config.enable_strict_contract_checks);
  EXPECT_EQ(config.debug_name, "phase1-cache-config");

  EXPECT_FALSE(frame_config.allow_reuse);
  EXPECT_TRUE(frame_config.force_invalidate_all);
  EXPECT_EQ(frame_config.debug_name, "phase1-frame-config");

  ExpectStringSurface(VsmCacheDataState::kUnavailable);
  ExpectStringSurface(VsmCacheBuildState::kFrameOpen);
  ExpectStringSurface(
    VsmPageRequestFlags::kRequired | VsmPageRequestFlags::kStaticOnly);
  ExpectStringSurface(VsmPageRequestValidationResult::kMissingMapId);
  ExpectStringSurface(VsmRemapKeyListValidationResult::kDuplicateKey);
  ExpectStringSurface(VsmCacheInvalidationReason::kIncompatiblePool);
  ExpectStringSurface(VsmCacheInvalidationScope::kStaticAndDynamic);
  ExpectStringSurface(VsmLightCacheKind::kDirectional);
  ExpectStringSurface(VsmAllocationAction::kAllocateNew);
  ExpectStringSurface(VsmAllocationFailureReason::kNoAvailablePhysicalPages);
  ExpectStringSurface(VsmPageInitializationAction::kCopyStaticSlice);
}

NOLINT_TEST_F(VsmCacheManagerTypesTest,
  CacheManagerTypeValidationRejectsDefaultRequestsAndMalformedRemapLists)
{
  const auto default_request = VsmPageRequest {};
  EXPECT_FALSE(IsValid(default_request));
  EXPECT_EQ(
    Validate(default_request), VsmPageRequestValidationResult::kMissingMapId);

  const auto missing_flags_request = VsmPageRequest {
    .map_id = 7,
    .page = { .level = 1, .page_x = 2, .page_y = 3 },
    .flags = VsmPageRequestFlags::kNone,
  };
  EXPECT_FALSE(IsValid(missing_flags_request));
  EXPECT_EQ(Validate(missing_flags_request),
    VsmPageRequestValidationResult::kMissingFlags);

  const auto valid_request = VsmPageRequest {
    .map_id = 9,
    .page = { .level = 0, .page_x = 4, .page_y = 5 },
    .flags = VsmPageRequestFlags::kRequired | VsmPageRequestFlags::kCoarse,
  };
  EXPECT_TRUE(IsValid(valid_request));
  EXPECT_EQ(Validate(valid_request), VsmPageRequestValidationResult::kValid);

  const auto empty_keys = VsmRemapKeyList {};
  EXPECT_FALSE(IsValid(empty_keys));
  EXPECT_EQ(Validate(empty_keys), VsmRemapKeyListValidationResult::kEmptyList);

  const auto empty_key = VsmRemapKeyList { "light-a", "" };
  EXPECT_FALSE(IsValid(empty_key));
  EXPECT_EQ(Validate(empty_key), VsmRemapKeyListValidationResult::kEmptyKey);

  const auto duplicate_keys
    = VsmRemapKeyList { "light-a", "light-b", "light-a" };
  EXPECT_FALSE(IsValid(duplicate_keys));
  EXPECT_EQ(
    Validate(duplicate_keys), VsmRemapKeyListValidationResult::kDuplicateKey);

  const auto valid_keys = VsmRemapKeyList { "light-a", "sun-main" };
  EXPECT_TRUE(IsValid(valid_keys));
  EXPECT_EQ(Validate(valid_keys), VsmRemapKeyListValidationResult::kValid);
}

NOLINT_TEST_F(VsmCacheManagerTypesTest,
  CacheManagerValueTypesPreserveConfiguredStateExactly)
{
  const auto request = VsmPageRequest {
    .map_id = 15,
    .page = { .level = 2, .page_x = 3, .page_y = 4 },
    .flags = VsmPageRequestFlags::kRequired | VsmPageRequestFlags::kStaticOnly,
  };
  const auto manager_config = oxygen::renderer::vsm::VsmCacheManagerConfig {
    .allow_reuse = true,
    .enable_strict_contract_checks = true,
    .retained_unreferenced_frame_count = 2,
    .debug_name = "phase1-cache-manager",
  };
  const auto previous_state = VsmCacheEntryFrameState {
    .virtual_map_id = 7,
    .first_page_table_entry = 8,
    .page_table_entry_count = 9,
    .rendered_frame = 11,
    .scheduled_frame = 12,
    .is_uncached = true,
    .is_distant = false,
    .is_retained_unreferenced = false,
  };
  const auto current_state = VsmCacheEntryFrameState {
    .virtual_map_id = 17,
    .first_page_table_entry = 18,
    .page_table_entry_count = 19,
    .rendered_frame = 13,
    .scheduled_frame = 14,
    .is_uncached = false,
    .is_distant = true,
    .is_retained_unreferenced = true,
  };
  const auto light_entry = VsmLightCacheEntryState {
    .remap_key = "sun-main",
    .kind = VsmLightCacheKind::kDirectional,
    .previous_frame_state = previous_state,
    .current_frame_state = current_state,
  };
  const auto page_table_entry = VsmPageTableEntry {
    .is_mapped = true,
    .physical_page = VsmPhysicalPageIndex { .value = 42 },
  };
  const auto page_meta = VsmPhysicalPageMeta {
    .is_allocated = true,
    .is_dirty = true,
    .used_this_frame = true,
    .view_uncached = false,
    .static_invalidated = true,
    .dynamic_invalidated = false,
    .age = 3,
    .owner_id = 15,
    .owner_mip_level = 2,
    .owner_page = VsmVirtualPageCoord { .level = 2, .page_x = 3, .page_y = 4 },
    .last_touched_frame = 77,
  };
  const auto decision = VsmPageAllocationDecision {
    .request = request,
    .action = VsmAllocationAction::kAllocateNew,
    .failure_reason = VsmAllocationFailureReason::kNone,
    .previous_physical_page = VsmPhysicalPageIndex { .value = 0 },
    .current_physical_page = VsmPhysicalPageIndex { .value = 42 },
  };
  const auto initialization_work = VsmPageInitializationWorkItem {
    .physical_page = VsmPhysicalPageIndex { .value = 42 },
    .action = VsmPageInitializationAction::kCopyStaticSlice,
  };
  const auto plan = VsmPageAllocationPlan {
    .decisions = { decision },
    .initialization_work = { initialization_work },
    .reused_page_count = 0,
    .allocated_page_count = 1,
    .initialized_page_count = 1,
    .evicted_page_count = 0,
    .rejected_page_count = 0,
  };
  const auto virtual_frame = VsmVirtualAddressSpaceFrame {
    .frame_generation = 21,
    .config = { .first_virtual_id = 64, .debug_name = "phase1-virtual-frame" },
    .total_page_table_entry_count = 8,
    .local_light_layouts = {
      VsmVirtualMapLayout {
        .id = 15,
        .remap_key = "sun-main",
        .level_count = 3,
        .pages_per_level_x = 2,
        .pages_per_level_y = 2,
        .total_page_count = 12,
        .first_page_table_entry = 1,
      },
    },
  };
  const auto snapshot = VsmPageAllocationSnapshot {
    .frame_generation = 21,
    .pool_identity = 99,
    .is_hzb_data_available = true,
    .virtual_frame = virtual_frame,
    .light_cache_entries = { light_entry },
    .retained_local_light_layouts = {
      VsmVirtualMapLayout {
        .id = 27,
        .remap_key = "retained-local",
        .level_count = 1,
        .pages_per_level_x = 1,
        .pages_per_level_y = 1,
        .total_page_count = 1,
        .first_page_table_entry = 8,
      },
    },
    .page_table = { page_table_entry },
    .physical_pages = { page_meta },
  };
  const auto frame = VsmPageAllocationFrame {
    .snapshot = snapshot,
    .plan = plan,
    .is_ready = true,
  };

  const VsmExtractedCacheFrame extracted = snapshot;

  EXPECT_EQ(request.map_id, 15U);
  EXPECT_EQ(request.page.level, 2U);
  EXPECT_EQ(request.page.page_x, 3U);
  EXPECT_EQ(request.page.page_y, 4U);
  EXPECT_EQ(manager_config.retained_unreferenced_frame_count, 2U);
  EXPECT_EQ(light_entry.previous_frame_state, previous_state);
  EXPECT_EQ(light_entry.current_frame_state, current_state);
  EXPECT_EQ(page_table_entry.physical_page.value, 42U);
  EXPECT_EQ(page_meta.owner_id, 15U);
  EXPECT_EQ(page_meta.owner_page.page_x, 3U);
  EXPECT_EQ(decision.current_physical_page.value, 42U);
  EXPECT_EQ(plan.allocated_page_count, 1U);
  EXPECT_EQ(plan.initialization_work.front(), initialization_work);
  EXPECT_EQ(snapshot.virtual_frame, virtual_frame);
  EXPECT_EQ(snapshot.light_cache_entries.front(), light_entry);
  EXPECT_EQ(snapshot.retained_local_light_layouts.front().id, 27U);
  EXPECT_EQ(snapshot.page_table.front(), page_table_entry);
  EXPECT_EQ(snapshot.physical_pages.front(), page_meta);
  EXPECT_EQ(frame.snapshot, snapshot);
  EXPECT_EQ(frame.plan, plan);
  EXPECT_TRUE(frame.is_ready);
  EXPECT_EQ(extracted, snapshot);
}

} // namespace
