//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cstddef>
#include <filesystem>
#include <limits>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Content/VirtualPath.h>
#include <Oxygen/Cooker/Pak/PakValidation.h>

namespace {
namespace pak = oxygen::content::pak;
namespace data = oxygen::data;

struct RangeRecord {
  uint64_t start = 0;
  uint64_t end = 0;
  std::string label;
};

auto AddDiagnosticWithContext(std::vector<pak::PakDiagnostic>& diagnostics,
  const pak::PakDiagnosticSeverity severity, const pak::PakBuildPhase phase,
  const std::string_view code, const std::string_view message,
  const std::optional<data::AssetKey>& asset_key = std::nullopt,
  const std::string_view resource_kind = {},
  const std::string_view table_name = {},
  const std::optional<uint64_t> offset = {},
  const std::filesystem::path& path = {}) -> void
{
  diagnostics.push_back(pak::PakDiagnostic {
    .severity = severity,
    .phase = phase,
    .code = std::string(code),
    .message = std::string(message),
    .asset_key
    = asset_key.has_value() ? data::to_string(*asset_key) : std::string {},
    .resource_kind = std::string(resource_kind),
    .table_name = std::string(table_name),
    .path = path,
    .offset = offset,
  });
}

auto AddDiagnostic(std::vector<pak::PakDiagnostic>& diagnostics,
  const pak::PakDiagnosticSeverity severity, const pak::PakBuildPhase phase,
  const std::string_view code, const std::string_view message,
  const std::string_view table_name = {},
  const std::optional<uint64_t> offset = {},
  const std::filesystem::path& path = {}) -> void
{
  AddDiagnosticWithContext(diagnostics, severity, phase, code, message,
    std::nullopt, {}, table_name, offset, path);
}

auto AddDiagnostic(std::vector<pak::PakDiagnostic>& diagnostics,
  const pak::PakDiagnosticSeverity severity, const pak::PakBuildPhase phase,
  const std::string_view code, const std::string_view message,
  const std::optional<data::AssetKey>& asset_key,
  const std::string_view resource_kind = {},
  const std::string_view table_name = {},
  const std::optional<uint64_t> offset = {},
  const std::filesystem::path& path = {}) -> void
{
  AddDiagnosticWithContext(diagnostics, severity, phase, code, message,
    asset_key, resource_kind, table_name, offset, path);
}

auto SafeAdd(const uint64_t lhs, const uint64_t rhs, uint64_t& out) -> bool
{
  if (rhs > (std::numeric_limits<uint64_t>::max)() - lhs) {
    return false;
  }
  out = lhs + rhs;
  return true;
}

auto SafeMul(const uint64_t lhs, const uint64_t rhs, uint64_t& out) -> bool
{
  if (lhs != 0 && rhs > (std::numeric_limits<uint64_t>::max)() / lhs) {
    return false;
  }
  out = lhs * rhs;
  return true;
}

auto IsAssetKeyLess(const data::AssetKey& lhs, const data::AssetKey& rhs)
  -> bool
{
  return lhs < rhs;
}

auto MakeAssetTypeName(const data::AssetType asset_type) -> std::string
{
  return std::to_string(static_cast<uint32_t>(asset_type));
}

auto IsCanonicalVirtualPath(const std::string_view path) -> bool
{
  return oxygen::content::IsCanonicalVirtualPath(path);
}

auto MakePatchActionSet(
  const pak::PakPlan& plan, const std::array<pak::PakPatchAction, 2>& actions)
  -> std::unordered_set<data::AssetKey>
{
  std::unordered_set<data::AssetKey> keys;
  for (const auto& action : plan.PatchActions()) {
    if (std::ranges::find(actions, action.action) != actions.end()) {
      keys.insert(action.asset_key);
    }
  }
  return keys;
}

auto IsKnownAssetType(const data::AssetType asset_type) -> bool
{
  return asset_type != data::AssetType::kUnknown;
}

struct ValidationState final {
  ValidationState(const pak::PakPlan& plan_in,
    const pak::PakPlanPolicy& policy_in, const pak::PakBuildRequest& request_in)
    : plan(oxygen::observer_ptr<const pak::PakPlan> { std::addressof(plan_in) })
    , policy(oxygen::observer_ptr<const pak::PakPlanPolicy> {
        std::addressof(policy_in) })
    , request(oxygen::observer_ptr<const pak::PakBuildRequest> {
        std::addressof(request_in) })
  {
  }

  oxygen::observer_ptr<const pak::PakPlan> plan { nullptr };
  oxygen::observer_ptr<const pak::PakPlanPolicy> policy { nullptr };
  oxygen::observer_ptr<const pak::PakBuildRequest> request { nullptr };
  pak::PakValidation::Result output {};
  std::unordered_map<std::string, std::pair<uint64_t, uint64_t>> region_bounds;
  std::vector<RangeRecord> ranges;
};

auto ValidatePlannedFileSize(ValidationState& state) -> void
{
  using Severity = pak::PakDiagnosticSeverity;
  using Phase = pak::PakBuildPhase;

  if (state.plan->PlannedFileSize() == 0U) {
    AddDiagnostic(state.output.diagnostics, Severity::kError, Phase::kPlanning,
      "pak.plan.file_size_zero", "Planned file size must be non-zero.");
  }
}

auto ValidateTablesAndSchemas(ValidationState& state) -> void
{
  using Severity = pak::PakDiagnosticSeverity;
  using Phase = pak::PakBuildPhase;

  for (const auto& table : state.plan->Tables()) {
    if (table.table_name.empty()) {
      AddDiagnostic(state.output.diagnostics, Severity::kError,
        Phase::kPlanning, "pak.plan.table_name_empty",
        "Resource table_name must be non-empty.");
    }

    if (table.entry_size == 0U && table.count > 0U) {
      AddDiagnostic(state.output.diagnostics, Severity::kError,
        Phase::kPlanning, "pak.plan.table_entry_size_zero",
        "Resource table has count > 0 but entry_size is 0.", table.table_name,
        table.offset);
    }

    if (table.expected_entry_size != 0U && table.entry_size != 0U
      && table.entry_size != table.expected_entry_size) {
      AddDiagnostic(state.output.diagnostics, Severity::kError,
        Phase::kPlanning, "pak.plan.table_entry_size_mismatch",
        "Resource table entry_size does not match schema size.",
        table.table_name, table.offset);
    }

    uint64_t required_bytes = 0;
    if (!SafeMul(table.count, table.entry_size, required_bytes)) {
      AddDiagnostic(state.output.diagnostics, Severity::kError,
        Phase::kPlanning, "pak.plan.table_size_multiply_overflow",
        "Table count * entry_size overflowed uint64.", std::nullopt, {},
        table.table_name, table.offset);
    } else if (table.size_bytes != required_bytes) {
      AddDiagnostic(state.output.diagnostics, Severity::kError,
        Phase::kPlanning, "pak.plan.table_size_mismatch",
        "Table size_bytes does not equal count * entry_size.", std::nullopt, {},
        table.table_name, table.offset);
    }

    if (table.index_zero_required && table.count > 0U
      && !table.index_zero_present) {
      AddDiagnostic(state.output.diagnostics, Severity::kError,
        Phase::kPlanning, "pak.plan.index_zero_required_missing",
        "Table requires index 0 but it is not populated.", table.table_name,
        table.offset);
    }

    if (table.index_zero_forbidden && table.index_zero_present) {
      AddDiagnostic(state.output.diagnostics, Severity::kError,
        Phase::kPlanning, "pak.plan.index_zero_forbidden_present",
        "Table forbids index 0 but it is marked as present.", table.table_name,
        table.offset);
    }
  }
}

auto CollectRegionBoundsAndRanges(ValidationState& state) -> void
{
  using Severity = pak::PakDiagnosticSeverity;
  using Phase = pak::PakBuildPhase;
  auto region_ranges = std::vector<RangeRecord> {};
  region_ranges.reserve(state.plan->Regions().size());

  for (const auto& region : state.plan->Regions()) {
    if (region.region_name.empty()) {
      AddDiagnostic(state.output.diagnostics, Severity::kError,
        Phase::kPlanning, "pak.plan.region_name_empty",
        "Region name must be non-empty.");
    }

    uint64_t end = 0;
    if (!SafeAdd(region.offset, region.size_bytes, end)) {
      AddDiagnostic(state.output.diagnostics, Severity::kError,
        Phase::kPlanning, "pak.plan.region_overflow",
        "Region offset + size overflowed uint64.", region.region_name,
        region.offset);
      continue;
    }

    if (region.alignment == 0U || region.offset % region.alignment != 0U) {
      AddDiagnostic(state.output.diagnostics, Severity::kError,
        Phase::kPlanning, "pak.plan.region_alignment_mismatch",
        "Region offset does not satisfy alignment.", region.region_name,
        region.offset);
    }

    const auto [it, inserted] = state.region_bounds.emplace(
      region.region_name, std::make_pair(region.offset, end));
    if (!inserted) {
      AddDiagnostic(state.output.diagnostics, Severity::kError,
        Phase::kPlanning, "pak.plan.region_duplicate_name",
        "Region name appears more than once.", std::nullopt, {},
        region.region_name, region.offset);
      it->second.first = (std::min)(it->second.first, region.offset);
      it->second.second = (std::max)(it->second.second, end);
    }

    region_ranges.push_back(RangeRecord {
      .start = region.offset,
      .end = end,
      .label = region.region_name,
    });
  }

  std::ranges::sort(
    region_ranges, [](const RangeRecord& lhs, const RangeRecord& rhs) {
      if (lhs.start != rhs.start) {
        return lhs.start < rhs.start;
      }
      if (lhs.end != rhs.end) {
        return lhs.end < rhs.end;
      }
      return lhs.label < rhs.label;
    });

  for (size_t i = 0; i < region_ranges.size(); ++i) {
    const auto& range = region_ranges[i];
    if (range.end > state.plan->PlannedFileSize()) {
      AddDiagnostic(state.output.diagnostics, Severity::kError,
        Phase::kPlanning, "pak.plan.region_out_of_bounds",
        "Region end exceeds planned file size.", range.label, range.start);
    }

    if (i == 0U) {
      continue;
    }

    const auto& prev = region_ranges[i - 1U];
    if (range.start < prev.end) {
      AddDiagnostic(state.output.diagnostics, Severity::kError,
        Phase::kPlanning, "pak.plan.region_overlap", "Planned regions overlap.",
        range.label, range.start);
    }
  }
}

auto CollectTableRanges(ValidationState& state) -> void
{
  using Severity = pak::PakDiagnosticSeverity;
  using Phase = pak::PakBuildPhase;

  for (const auto& table : state.plan->Tables()) {
    uint64_t end = 0;
    if (!SafeAdd(table.offset, table.size_bytes, end)) {
      AddDiagnostic(state.output.diagnostics, Severity::kError,
        Phase::kPlanning, "pak.plan.table_overflow",
        "Table offset + size overflowed uint64.", table.table_name,
        table.offset);
      continue;
    }

    if (table.alignment == 0U || table.offset % table.alignment != 0U) {
      AddDiagnostic(state.output.diagnostics, Severity::kError,
        Phase::kPlanning, "pak.plan.table_alignment_mismatch",
        "Table offset does not satisfy alignment.", table.table_name,
        table.offset);
    }

    state.ranges.push_back(RangeRecord {
      .start = table.offset,
      .end = end,
      .label = table.table_name,
    });
  }
}

auto CollectAssetRangesAndValidateAssets(ValidationState& state) -> void
{
  using Severity = pak::PakDiagnosticSeverity;
  using Phase = pak::PakBuildPhase;

  for (const auto& asset : state.plan->Assets()) {
    if (!IsKnownAssetType(asset.asset_type)) {
      AddDiagnostic(state.output.diagnostics, Severity::kError,
        Phase::kPlanning, "pak.plan.asset_type_unknown",
        "Asset plan contains unknown asset_type.", asset.asset_key);
    }

    uint64_t end = 0;
    if (!SafeAdd(asset.offset, asset.size_bytes, end)) {
      AddDiagnostic(state.output.diagnostics, Severity::kError,
        Phase::kPlanning, "pak.plan.asset_overflow",
        "Asset offset + size overflowed uint64.", asset.asset_key, {}, {},
        asset.offset);
      continue;
    }

    if (asset.alignment == 0U || asset.offset % asset.alignment != 0U) {
      AddDiagnostic(state.output.diagnostics, Severity::kError,
        Phase::kPlanning, "pak.plan.asset_alignment_mismatch",
        "Asset offset does not satisfy alignment.", asset.asset_key, {}, {},
        asset.offset);
    }

    if (!asset.reserved_bytes_zeroed) {
      AddDiagnostic(state.output.diagnostics, Severity::kError,
        Phase::kPlanning, "pak.plan.asset_reserved_bytes_non_zero",
        "Asset descriptor reserved bytes must be zeroed.", asset.asset_key, {},
        {}, asset.offset);
    }

    state.ranges.push_back(
      RangeRecord { .start = asset.offset, .end = end, .label = "asset" });
  }
}

auto CollectResourceRangesAndValidateResources(ValidationState& state) -> void
{
  using Severity = pak::PakDiagnosticSeverity;
  using Phase = pak::PakBuildPhase;

  std::unordered_set<std::string> resource_keys;
  for (const auto& resource : state.plan->Resources()) {
    if (resource.resource_kind.empty()) {
      AddDiagnostic(state.output.diagnostics, Severity::kError,
        Phase::kPlanning, "pak.plan.resource_kind_empty",
        "Resource placement kind must be non-empty.");
    }
    if (resource.region_name.empty()) {
      AddDiagnostic(state.output.diagnostics, Severity::kError,
        Phase::kPlanning, "pak.plan.resource_region_empty",
        "Resource placement region_name must be non-empty.", std::nullopt,
        resource.resource_kind);
    }

    const auto resource_key
      = resource.resource_kind + ":" + std::to_string(resource.resource_index);
    const auto [_, inserted] = resource_keys.insert(resource_key);
    if (!inserted) {
      AddDiagnostic(state.output.diagnostics, Severity::kError,
        Phase::kPlanning, "pak.plan.resource_duplicate_key",
        "Resource kind/index pair must be unique.", std::nullopt,
        resource.resource_kind, resource.region_name, resource.offset);
    }

    uint64_t end = 0;
    if (!SafeAdd(resource.offset, resource.size_bytes, end)) {
      AddDiagnostic(state.output.diagnostics, Severity::kError,
        Phase::kPlanning, "pak.plan.resource_overflow",
        "Resource offset + size overflowed uint64.", std::nullopt,
        resource.resource_kind, resource.region_name, resource.offset);
      continue;
    }

    if (resource.alignment == 0U
      || resource.offset % resource.alignment != 0U) {
      AddDiagnostic(state.output.diagnostics, Severity::kError,
        Phase::kPlanning, "pak.plan.resource_alignment_mismatch",
        "Resource offset does not satisfy alignment.", std::nullopt,
        resource.resource_kind, resource.region_name, resource.offset);
    }

    if (!resource.reserved_bytes_zeroed) {
      AddDiagnostic(state.output.diagnostics, Severity::kError,
        Phase::kPlanning, "pak.plan.resource_reserved_bytes_non_zero",
        "Resource descriptor reserved bytes must be zeroed.", std::nullopt,
        resource.resource_kind, resource.region_name, resource.offset);
    }

    const auto region_it = state.region_bounds.find(resource.region_name);
    if (region_it == state.region_bounds.end()) {
      AddDiagnostic(state.output.diagnostics, Severity::kError,
        Phase::kPlanning, "pak.plan.resource_region_missing",
        "Resource references a region_name not present in plan.", std::nullopt,
        resource.resource_kind, resource.region_name, resource.offset);
    } else if (resource.offset < region_it->second.first
      || end > region_it->second.second) {
      AddDiagnostic(state.output.diagnostics, Severity::kError,
        Phase::kPlanning, "pak.plan.resource_out_of_region",
        "Resource data range must be fully contained in its declared region.",
        std::nullopt, resource.resource_kind, resource.region_name,
        resource.offset);
    }

    state.ranges.push_back(RangeRecord {
      .start = resource.offset, .end = end, .label = resource_key });
  }
}

auto CollectDirectoryRange(ValidationState& state) -> void
{
  using Severity = pak::PakDiagnosticSeverity;
  using Phase = pak::PakBuildPhase;

  const auto& directory = state.plan->Directory();
  uint64_t end = 0;
  if (!SafeAdd(directory.offset, directory.size_bytes, end)) {
    AddDiagnostic(state.output.diagnostics, Severity::kError, Phase::kPlanning,
      "pak.plan.directory_overflow",
      "Directory offset + size overflowed uint64.", std::string_view {},
      directory.offset);
    return;
  }

  state.ranges.push_back(RangeRecord {
    .start = directory.offset,
    .end = end,
    .label = "directory",
  });
}

auto CollectBrowseRangeAndValidatePaths(ValidationState& state) -> void
{
  using Severity = pak::PakDiagnosticSeverity;
  using Phase = pak::PakBuildPhase;

  const auto& browse = state.plan->BrowseIndex();
  if (browse.enabled) {
    uint64_t end = 0;
    if (!SafeAdd(browse.offset, browse.size_bytes, end)) {
      AddDiagnostic(state.output.diagnostics, Severity::kError,
        Phase::kPlanning, "pak.plan.browse_index_overflow",
        "Browse index offset + size overflowed uint64.", std::string_view {},
        browse.offset);
    } else {
      state.ranges.push_back(RangeRecord {
        .start = browse.offset,
        .end = end,
        .label = "browse_index",
      });
    }

    std::unordered_set<std::string> seen_paths;
    for (const auto& browse_entry : browse.entries) {
      const auto& path = browse_entry.virtual_path;
      if (!IsCanonicalVirtualPath(path)) {
        AddDiagnostic(state.output.diagnostics, Severity::kError,
          Phase::kPlanning, "pak.plan.browse_path_not_canonical",
          "Browse index virtual path must be canonical.", std::nullopt, {},
          "browse_index", {}, path);
      }

      const auto [_, inserted] = seen_paths.insert(path);
      if (!inserted) {
        AddDiagnostic(state.output.diagnostics, Severity::kError,
          Phase::kPlanning, "pak.plan.browse_path_duplicate",
          "Browse index virtual paths must be duplicate-free.", std::nullopt,
          {}, "browse_index", {}, path);
      }
    }
  } else if (!browse.entries.empty()) {
    AddDiagnostic(state.output.diagnostics, Severity::kError, Phase::kPlanning,
      "pak.plan.browse_path_without_index",
      "Browse virtual paths are populated but browse index is disabled.",
      std::nullopt, {}, "browse_index");
  }
}

auto CollectFooterRangeAndValidateCrcOffset(ValidationState& state) -> void
{
  using Severity = pak::PakDiagnosticSeverity;
  using Phase = pak::PakBuildPhase;

  const auto& footer = state.plan->Footer();
  uint64_t end = 0;
  if (!SafeAdd(footer.offset, footer.size_bytes, end)) {
    AddDiagnostic(state.output.diagnostics, Severity::kError, Phase::kPlanning,
      "pak.plan.footer_overflow", "Footer offset + size overflowed uint64.",
      std::string_view {}, footer.offset);
  } else {
    state.ranges.push_back(
      RangeRecord { .start = footer.offset, .end = end, .label = "footer" });
  }

  uint64_t crc32_end = 0;
  const auto crc32_end_ok
    = SafeAdd(footer.crc32_field_absolute_offset, sizeof(uint32_t), crc32_end);
  if (!crc32_end_ok || footer.crc32_field_absolute_offset < footer.offset
    || crc32_end > end) {
    AddDiagnostic(state.output.diagnostics, Severity::kError, Phase::kPlanning,
      "pak.plan.crc_offset_invalid",
      "CRC32 field offset is outside footer bounds.", std::string_view {},
      footer.crc32_field_absolute_offset);
  }
}

auto ValidateSectionBoundsAndOverlaps(ValidationState& state) -> void
{
  using Severity = pak::PakDiagnosticSeverity;
  using Phase = pak::PakBuildPhase;

  std::ranges::sort(
    state.ranges, [](const RangeRecord& lhs, const RangeRecord& rhs) {
      if (lhs.start != rhs.start) {
        return lhs.start < rhs.start;
      }
      if (lhs.end != rhs.end) {
        return lhs.end < rhs.end;
      }
      return lhs.label < rhs.label;
    });

  for (size_t i = 0; i < state.ranges.size(); ++i) {
    const auto& range = state.ranges[i];

    if (range.end > state.plan->PlannedFileSize()) {
      AddDiagnostic(state.output.diagnostics, Severity::kError,
        Phase::kPlanning, "pak.plan.section_out_of_bounds",
        "Planned section end exceeds planned file size.", range.label,
        range.start);
    }

    if (i == 0U) {
      continue;
    }

    const auto& prev = state.ranges[i - 1U];
    if (range.start < prev.end) {
      AddDiagnostic(state.output.diagnostics, Severity::kError,
        Phase::kPlanning, "pak.plan.section_overlap",
        "Planned sections overlap.", range.label, range.start);
    }
  }
}

auto ValidatePlannedAssetKeysUnique(ValidationState& state) -> void
{
  using Severity = pak::PakDiagnosticSeverity;
  using Phase = pak::PakBuildPhase;

  std::vector<data::AssetKey> keys;
  keys.reserve(state.plan->Assets().size());
  for (const auto& asset : state.plan->Assets()) {
    keys.push_back(asset.asset_key);
  }

  std::ranges::sort(keys, IsAssetKeyLess);
  for (size_t i = 1; i < keys.size(); ++i) {
    if (keys[i] == keys[i - 1U]) {
      AddDiagnostic(state.output.diagnostics, Severity::kError,
        Phase::kPlanning, "pak.plan.duplicate_asset_key",
        "Duplicate AssetKey detected in planned assets.");
    }
  }
}

auto ValidateDirectoryKeysAndAssetTypes(ValidationState& state) -> void
{
  using Severity = pak::PakDiagnosticSeverity;
  using Phase = pak::PakBuildPhase;

  std::vector<data::AssetKey> directory_keys;
  directory_keys.reserve(state.plan->Directory().entries.size());
  for (const auto& entry : state.plan->Directory().entries) {
    directory_keys.push_back(entry.asset_key);

    if (!IsKnownAssetType(entry.asset_type)) {
      AddDiagnostic(state.output.diagnostics, Severity::kError,
        Phase::kPlanning, "pak.plan.directory_asset_type_unknown",
        "Directory entry contains unknown asset_type.", entry.asset_key);
    }
  }

  std::ranges::sort(directory_keys, IsAssetKeyLess);
  for (size_t i = 1; i < directory_keys.size(); ++i) {
    if (directory_keys[i] == directory_keys[i - 1U]) {
      AddDiagnostic(state.output.diagnostics, Severity::kError,
        Phase::kPlanning, "pak.plan.directory_duplicate_asset_key",
        "Directory contains duplicate AssetKey entries.");
    }
  }
}

auto ValidateDirectoryEntriesAgainstAssets(ValidationState& state) -> void
{
  using Severity = pak::PakDiagnosticSeverity;
  using Phase = pak::PakBuildPhase;

  for (const auto& entry : state.plan->Directory().entries) {
    const auto it = std::ranges::find_if(
      state.plan->Assets(), [&entry](const pak::PakAssetPlacementPlan& asset) {
        return asset.asset_key == entry.asset_key;
      });

    if (it == state.plan->Assets().end()) {
      AddDiagnostic(state.output.diagnostics, Severity::kError,
        Phase::kPlanning, "pak.plan.directory_entry_missing_asset",
        "Directory entry references a missing asset placement.",
        std::string_view {}, entry.descriptor_offset);
      continue;
    }

    if (it->asset_type != entry.asset_type) {
      AddDiagnostic(state.output.diagnostics, Severity::kError,
        Phase::kPlanning, "pak.plan.asset_type_mismatch",
        "Directory entry asset_type mismatches planned descriptor type.");
    }

    uint64_t descriptor_end = 0;
    if (!SafeAdd(
          entry.descriptor_offset, entry.descriptor_size, descriptor_end)) {
      AddDiagnostic(state.output.diagnostics, Severity::kError,
        Phase::kPlanning, "pak.plan.descriptor_offset_overflow",
        "Directory descriptor_offset + descriptor_size overflowed uint64.",
        entry.asset_key, {}, "asset_directory", entry.descriptor_offset);
      continue;
    }

    uint64_t asset_end = 0;
    if (!SafeAdd(it->offset, it->size_bytes, asset_end)) {
      AddDiagnostic(state.output.diagnostics, Severity::kError,
        Phase::kPlanning, "pak.plan.asset_overflow",
        "Asset offset + size overflowed uint64.", entry.asset_key, {},
        "asset_directory", it->offset);
      continue;
    }

    if (entry.descriptor_offset < it->offset || descriptor_end > asset_end) {
      AddDiagnostic(state.output.diagnostics, Severity::kError,
        Phase::kPlanning, "pak.plan.descriptor_out_of_asset_bounds",
        "Descriptor range must be contained in the planned asset range.",
        entry.asset_key, {}, "asset_directory", entry.descriptor_offset);
    }
  }
}

auto ValidateScriptParamRanges(ValidationState& state) -> void
{
  using Severity = pak::PakDiagnosticSeverity;
  using Phase = pak::PakBuildPhase;

  std::vector<RangeRecord> script_ranges;
  script_ranges.reserve(state.plan->ScriptSlots().size());
  for (const auto& slot : state.plan->ScriptSlots()) {
    uint64_t end = 0;
    if (!SafeAdd(slot.params_array_index, slot.params_count, end)) {
      AddDiagnostic(state.output.diagnostics, Severity::kError,
        Phase::kPlanning, "pak.plan.script_param_overflow",
        "Script param range overflow.", std::nullopt, {}, "script_slot_table");
      continue;
    }

    if (end > state.plan->ScriptParamRecordCount()) {
      AddDiagnostic(state.output.diagnostics, Severity::kError,
        Phase::kPlanning, "pak.plan.script_param_out_of_bounds",
        "Script param range exceeds script param record array.", std::nullopt,
        {}, "script_slot_table");
    }

    script_ranges.push_back(RangeRecord {
      .start = slot.params_array_index,
      .end = end,
      .label = std::to_string(slot.slot_index),
    });
  }

  std::ranges::sort(
    script_ranges, [](const RangeRecord& lhs, const RangeRecord& rhs) {
      if (lhs.start != rhs.start) {
        return lhs.start < rhs.start;
      }
      return lhs.end < rhs.end;
    });

  for (size_t i = 1; i < script_ranges.size(); ++i) {
    if (script_ranges[i].start < script_ranges[i - 1U].end) {
      AddDiagnostic(state.output.diagnostics, Severity::kError,
        Phase::kPlanning, "pak.plan.script_param_overlap",
        "Script param ranges must not overlap.", std::nullopt, {},
        "script_slot_table", script_ranges[i].start);
    }
  }
}

auto ValidatePatchActionsAgainstBaseCatalogs(ValidationState& state) -> void
{
  using Severity = pak::PakDiagnosticSeverity;
  using Phase = pak::PakBuildPhase;

  std::unordered_map<data::AssetKey, data::AssetType> base_asset_types;
  for (const auto& base : state.request->base_catalogs) {
    for (const auto& entry : base.entries) {
      const auto [it, inserted]
        = base_asset_types.emplace(entry.asset_key, entry.asset_type);
      if (!inserted && it->second != entry.asset_type) {
        AddDiagnostic(state.output.diagnostics, Severity::kError,
          Phase::kPlanning, "pak.plan.base_catalog_type_mismatch",
          "Same AssetKey appears in base catalogs with different asset_type.",
          entry.asset_key);
      }
    }
  }

  std::vector<data::AssetKey> patch_keys;
  patch_keys.reserve(state.plan->PatchActions().size());
  for (const auto& action : state.plan->PatchActions()) {
    if (!IsKnownAssetType(action.asset_type)) {
      AddDiagnostic(state.output.diagnostics, Severity::kError,
        Phase::kPlanning, "pak.plan.patch_action_asset_type_unknown",
        "Patch action contains unknown asset_type.", action.asset_key);
    }

    patch_keys.push_back(action.asset_key);

    const auto base_it = base_asset_types.find(action.asset_key);
    const auto exists_in_base = base_it != base_asset_types.end();
    if (state.policy->mode == pak::PakPlanMode::kPatch) {
      if (action.action == pak::PakPatchAction::kReplace
        || action.action == pak::PakPatchAction::kDelete
        || action.action == pak::PakPatchAction::kUnchanged) {
        if (!exists_in_base) {
          AddDiagnostic(state.output.diagnostics, Severity::kError,
            Phase::kPlanning, "pak.plan.patch_action_missing_base",
            "Patch action requires key to exist in base catalogs.",
            action.asset_key);
        }
      }

      if (action.action == pak::PakPatchAction::kCreate && exists_in_base) {
        AddDiagnostic(state.output.diagnostics, Severity::kError,
          Phase::kPlanning, "pak.plan.create_conflicts_with_base",
          "Create action requires key absent from base catalogs.",
          action.asset_key);
      }

      if (action.action == pak::PakPatchAction::kReplace && exists_in_base
        && base_it->second != action.asset_type) {
        AddDiagnostic(state.output.diagnostics, Severity::kError,
          Phase::kPlanning, "pak.plan.replace_type_mismatch",
          "Replace operation must preserve base asset_type.", action.asset_key,
          "asset_type", MakeAssetTypeName(action.asset_type));
      }
    }
  }

  std::ranges::sort(patch_keys, IsAssetKeyLess);
  for (size_t i = 1; i < patch_keys.size(); ++i) {
    if (patch_keys[i] == patch_keys[i - 1U]) {
      AddDiagnostic(state.output.diagnostics, Severity::kError,
        Phase::kPlanning, "pak.plan.patch_action_duplicate_key",
        "Patch action map contains duplicate AssetKey records.");
    }
  }
}

auto ValidatePatchClosureConsistency(ValidationState& state) -> void
{
  using Severity = pak::PakDiagnosticSeverity;
  using Phase = pak::PakBuildPhase;

  if (state.policy->mode != pak::PakPlanMode::kPatch) {
    return;
  }

  const auto emitted_asset_keys = MakePatchActionSet(*state.plan,
    { pak::PakPatchAction::kCreate, pak::PakPatchAction::kReplace });
  std::unordered_set<std::string> closure_entries;

  for (const auto& closure : state.plan->PatchClosure()) {
    if (!emitted_asset_keys.contains(closure.asset_key)) {
      AddDiagnostic(state.output.diagnostics, Severity::kError,
        Phase::kPlanning, "pak.plan.patch_closure_for_non_emitted_asset",
        "Patch closure entry must target Create/Replace asset keys.",
        closure.asset_key, closure.resource_kind);
    }

    const auto key = data::to_string(closure.asset_key) + "|"
      + closure.resource_kind + "|" + std::to_string(closure.resource_index);
    const auto [_, inserted] = closure_entries.insert(key);
    if (!inserted) {
      AddDiagnostic(state.output.diagnostics, Severity::kError,
        Phase::kPlanning, "pak.plan.patch_closure_duplicate_entry",
        "Patch closure contains duplicate asset/resource entries.",
        closure.asset_key, closure.resource_kind);
    }

    const auto resource_it = std::ranges::find_if(state.plan->Resources(),
      [&closure](const pak::PakResourcePlacementPlan& resource) {
        return resource.resource_kind == closure.resource_kind
          && resource.resource_index == closure.resource_index;
      });
    if (resource_it == state.plan->Resources().end()) {
      AddDiagnostic(state.output.diagnostics, Severity::kError,
        Phase::kPlanning, "pak.plan.patch_closure_resource_missing",
        "Patch closure references a resource that is not planned.",
        closure.asset_key, closure.resource_kind);
    }
  }
}

auto FinalizeValidation(ValidationState& state) -> pak::PakValidation::Result
{
  using Severity = pak::PakDiagnosticSeverity;

  state.output.success = std::ranges::none_of(
    state.output.diagnostics, [](const pak::PakDiagnostic& diagnostic) {
      return diagnostic.severity == Severity::kError;
    });
  return std::move(state.output);
}

} // namespace

namespace oxygen::content::pak {

auto PakValidation::Validate(const PakPlan& plan, const PakPlanPolicy& policy,
  const PakBuildRequest& request) -> PakValidation::Result
{
  ValidationState state(plan, policy, request);

  ValidatePlannedFileSize(state);
  ValidateTablesAndSchemas(state);
  CollectRegionBoundsAndRanges(state);
  CollectTableRanges(state);
  CollectAssetRangesAndValidateAssets(state);
  CollectResourceRangesAndValidateResources(state);
  CollectDirectoryRange(state);
  CollectBrowseRangeAndValidatePaths(state);
  CollectFooterRangeAndValidateCrcOffset(state);
  ValidateSectionBoundsAndOverlaps(state);
  ValidatePlannedAssetKeysUnique(state);
  ValidateDirectoryKeysAndAssetTypes(state);
  ValidateDirectoryEntriesAgainstAssets(state);
  ValidateScriptParamRanges(state);
  ValidatePatchActionsAgainstBaseCatalogs(state);
  ValidatePatchClosureConsistency(state);

  return FinalizeValidation(state);
}

} // namespace oxygen::content::pak
