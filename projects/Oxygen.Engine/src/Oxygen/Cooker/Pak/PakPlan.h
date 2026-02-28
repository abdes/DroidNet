//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/SourceKey.h>
#include <Oxygen/Cooker/api_export.h>

namespace oxygen::content::pak {

enum class PakPatchAction : uint8_t {
  kCreate,
  kReplace,
  kDelete,
  kUnchanged,
};

OXGN_COOK_NDAPI auto to_string(PakPatchAction value) noexcept -> std::string_view;

struct PakHeaderPlan {
  uint64_t offset = 0;
  uint32_t size_bytes = 0;
  uint16_t content_version = 0;
  data::SourceKey source_key {};
};

struct PakRegionPlan {
  std::string region_name;
  uint64_t offset = 0;
  uint64_t size_bytes = 0;
  uint32_t alignment = 1;
};

struct PakTablePlan {
  std::string table_name;
  uint64_t offset = 0;
  uint64_t size_bytes = 0;
  uint32_t count = 0;
  uint32_t entry_size = 0;
  uint32_t expected_entry_size = 0;
  uint32_t alignment = 1;
  bool index_zero_required = false;
  bool index_zero_present = false;
  bool index_zero_forbidden = false;
};

struct PakAssetPlacementPlan {
  data::AssetKey asset_key {};
  data::AssetType asset_type = data::AssetType::kUnknown;
  uint64_t offset = 0;
  uint64_t size_bytes = 0;
  uint32_t alignment = 1;
  bool reserved_bytes_zeroed = true;
};

struct PakResourcePlacementPlan {
  std::string resource_kind;
  uint32_t resource_index = 0;
  std::string region_name;
  uint64_t offset = 0;
  uint64_t size_bytes = 0;
  uint32_t alignment = 1;
  bool reserved_bytes_zeroed = true;
};

struct PakAssetDirectoryEntryPlan {
  data::AssetKey asset_key {};
  data::AssetType asset_type = data::AssetType::kUnknown;
  uint64_t entry_offset = 0;
  uint64_t descriptor_offset = 0;
  uint32_t descriptor_size = 0;
};

struct PakDirectoryPlan {
  uint64_t offset = 0;
  uint64_t size_bytes = 0;
  std::vector<PakAssetDirectoryEntryPlan> entries;
};

struct PakBrowseEntryPlan {
  data::AssetKey asset_key {};
  std::string virtual_path;
};

struct PakBrowseIndexPlan {
  bool enabled = false;
  uint64_t offset = 0;
  uint64_t size_bytes = 0;
  std::vector<PakBrowseEntryPlan> entries;
};

struct PakFooterPlan {
  uint64_t offset = 0;
  uint32_t size_bytes = 0;
  uint64_t crc32_field_absolute_offset = 0;
};

struct PakPatchActionRecord {
  data::AssetKey asset_key {};
  data::AssetType asset_type = data::AssetType::kUnknown;
  PakPatchAction action = PakPatchAction::kUnchanged;
};

struct PakPatchClosureRecord {
  data::AssetKey asset_key {};
  std::string resource_kind;
  uint32_t resource_index = 0;
};

struct PakScriptParamRangePlan {
  uint32_t slot_index = 0;
  uint32_t params_array_offset = 0;
  uint32_t params_count = 0;
};

class PakPlan final {
public:
  struct Data {
    PakHeaderPlan header {};
    std::vector<PakRegionPlan> regions;
    std::vector<PakTablePlan> tables;
    std::vector<PakAssetPlacementPlan> assets;
    std::vector<PakResourcePlacementPlan> resources;
    PakDirectoryPlan directory {};
    PakBrowseIndexPlan browse_index {};
    PakFooterPlan footer {};
    std::vector<PakPatchActionRecord> patch_actions;
    std::vector<PakPatchClosureRecord> patch_closure;
    std::vector<PakScriptParamRangePlan> script_param_ranges;
    uint32_t script_param_record_count = 0;
    uint64_t planned_file_size = 0;
  };

  OXGN_COOK_API explicit PakPlan(Data data) noexcept;

  OXGN_COOK_NDAPI auto Header() const noexcept -> const PakHeaderPlan&;
  OXGN_COOK_NDAPI auto Regions() const noexcept -> std::span<const PakRegionPlan>;
  OXGN_COOK_NDAPI auto Tables() const noexcept -> std::span<const PakTablePlan>;
  OXGN_COOK_NDAPI auto Assets() const noexcept
    -> std::span<const PakAssetPlacementPlan>;
  OXGN_COOK_NDAPI auto Resources() const noexcept
    -> std::span<const PakResourcePlacementPlan>;
  OXGN_COOK_NDAPI auto Directory() const noexcept -> const PakDirectoryPlan&;
  OXGN_COOK_NDAPI auto BrowseIndex() const noexcept -> const PakBrowseIndexPlan&;
  OXGN_COOK_NDAPI auto Footer() const noexcept -> const PakFooterPlan&;
  OXGN_COOK_NDAPI auto PatchActions() const noexcept
    -> std::span<const PakPatchActionRecord>;
  OXGN_COOK_NDAPI auto PatchClosure() const noexcept
    -> std::span<const PakPatchClosureRecord>;
  OXGN_COOK_NDAPI auto ScriptParamRanges() const noexcept
    -> std::span<const PakScriptParamRangePlan>;
  OXGN_COOK_NDAPI auto ScriptParamRecordCount() const noexcept -> uint32_t;
  OXGN_COOK_NDAPI auto PlannedFileSize() const noexcept -> uint64_t;

private:
  Data data_;
};

} // namespace oxygen::content::pak
