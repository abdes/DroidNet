//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Cooker/Pak/PakValidation.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <filesystem>
#include <limits>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

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
  return std::lexicographical_compare(
    lhs.guid.begin(), lhs.guid.end(), rhs.guid.begin(), rhs.guid.end());
}

auto MakeAssetTypeName(const data::AssetType asset_type) -> std::string
{
  return std::to_string(static_cast<uint32_t>(asset_type));
}

auto IsCanonicalVirtualPath(const std::string_view path) -> bool
{
  if (path.empty() || path.front() != '/') {
    return false;
  }
  if (path.find('\\') != std::string_view::npos
    || path.find("//") != std::string_view::npos) {
    return false;
  }

  size_t segment_start = 1;
  while (segment_start <= path.size()) {
    const auto slash = path.find('/', segment_start);
    const auto segment_end
      = slash == std::string_view::npos ? path.size() : slash;
    const auto segment
      = path.substr(segment_start, segment_end - segment_start);
    if (segment == "." || segment == "..") {
      return false;
    }

    if (slash == std::string_view::npos) {
      break;
    }
    segment_start = slash + 1;
  }

  return true;
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

} // namespace

namespace oxygen::content::pak {

auto PakValidation::Validate(const PakPlan& plan, const PakPlanPolicy& policy,
  const PakBuildRequest& request) -> PakValidation::Result
{
  Result output {};
  auto& diagnostics = output.diagnostics;

  if (plan.PlannedFileSize() == 0) {
    AddDiagnostic(diagnostics, PakDiagnosticSeverity::kError,
      PakBuildPhase::kPlanning, "pak.plan.file_size_zero",
      "Planned file size must be non-zero.");
  }

  for (const auto& table : plan.Tables()) {
    if (table.table_name.empty()) {
      AddDiagnostic(diagnostics, PakDiagnosticSeverity::kError,
        PakBuildPhase::kPlanning, "pak.plan.table_name_empty",
        "Resource table_name must be non-empty.");
    }

    if (table.entry_size == 0 && table.count > 0) {
      AddDiagnostic(diagnostics, PakDiagnosticSeverity::kError,
        PakBuildPhase::kPlanning, "pak.plan.table_entry_size_zero",
        "Resource table has count > 0 but entry_size is 0.", table.table_name,
        table.offset);
    }

    if (table.expected_entry_size != 0 && table.entry_size != 0
      && table.entry_size != table.expected_entry_size) {
      AddDiagnostic(diagnostics, PakDiagnosticSeverity::kError,
        PakBuildPhase::kPlanning, "pak.plan.table_entry_size_mismatch",
        "Resource table entry_size does not match schema size.",
        table.table_name, table.offset);
    }

    uint64_t required_bytes = 0;
    if (!SafeMul(table.count, table.entry_size, required_bytes)) {
      AddDiagnostic(diagnostics, PakDiagnosticSeverity::kError,
        PakBuildPhase::kPlanning, "pak.plan.table_size_multiply_overflow",
        "Table count * entry_size overflowed uint64.", std::nullopt, {},
        table.table_name, table.offset);
    } else if (table.size_bytes != required_bytes) {
      AddDiagnostic(diagnostics, PakDiagnosticSeverity::kError,
        PakBuildPhase::kPlanning, "pak.plan.table_size_mismatch",
        "Table size_bytes does not equal count * entry_size.", std::nullopt, {},
        table.table_name, table.offset);
    }

    if (table.index_zero_required && table.count > 0
      && !table.index_zero_present) {
      AddDiagnostic(diagnostics, PakDiagnosticSeverity::kError,
        PakBuildPhase::kPlanning, "pak.plan.index_zero_required_missing",
        "Table requires index 0 but it is not populated.", table.table_name,
        table.offset);
    }

    if (table.index_zero_forbidden && table.index_zero_present) {
      AddDiagnostic(diagnostics, PakDiagnosticSeverity::kError,
        PakBuildPhase::kPlanning, "pak.plan.index_zero_forbidden_present",
        "Table forbids index 0 but it is marked as present.", table.table_name,
        table.offset);
    }
  }

  std::unordered_map<std::string, std::pair<uint64_t, uint64_t>> region_bounds;
  std::vector<RangeRecord> ranges;

  for (const auto& region : plan.Regions()) {
    if (region.region_name.empty()) {
      AddDiagnostic(diagnostics, PakDiagnosticSeverity::kError,
        PakBuildPhase::kPlanning, "pak.plan.region_name_empty",
        "Region name must be non-empty.");
    }

    uint64_t end = 0;
    if (!SafeAdd(region.offset, region.size_bytes, end)) {
      AddDiagnostic(diagnostics, PakDiagnosticSeverity::kError,
        PakBuildPhase::kPlanning, "pak.plan.region_overflow",
        "Region offset + size overflowed uint64.", region.region_name,
        region.offset);
      continue;
    }

    if (region.alignment == 0 || region.offset % region.alignment != 0) {
      AddDiagnostic(diagnostics, PakDiagnosticSeverity::kError,
        PakBuildPhase::kPlanning, "pak.plan.region_alignment_mismatch",
        "Region offset does not satisfy alignment.", region.region_name,
        region.offset);
    }

    const auto [it, inserted] = region_bounds.emplace(
      region.region_name, std::make_pair(region.offset, end));
    if (!inserted) {
      AddDiagnostic(diagnostics, PakDiagnosticSeverity::kError,
        PakBuildPhase::kPlanning, "pak.plan.region_duplicate_name",
        "Region name appears more than once.", std::nullopt, {},
        region.region_name, region.offset);
      it->second.first = (std::min)(it->second.first, region.offset);
      it->second.second = (std::max)(it->second.second, end);
    }

    ranges.push_back(RangeRecord {
      .start = region.offset, .end = end, .label = region.region_name });
  }

  for (const auto& table : plan.Tables()) {
    uint64_t end = 0;
    if (!SafeAdd(table.offset, table.size_bytes, end)) {
      AddDiagnostic(diagnostics, PakDiagnosticSeverity::kError,
        PakBuildPhase::kPlanning, "pak.plan.table_overflow",
        "Table offset + size overflowed uint64.", table.table_name,
        table.offset);
      continue;
    }

    if (table.alignment == 0 || table.offset % table.alignment != 0) {
      AddDiagnostic(diagnostics, PakDiagnosticSeverity::kError,
        PakBuildPhase::kPlanning, "pak.plan.table_alignment_mismatch",
        "Table offset does not satisfy alignment.", table.table_name,
        table.offset);
    }

    ranges.push_back(RangeRecord {
      .start = table.offset, .end = end, .label = table.table_name });
  }

  for (const auto& asset : plan.Assets()) {
    if (!IsKnownAssetType(asset.asset_type)) {
      AddDiagnostic(diagnostics, PakDiagnosticSeverity::kError,
        PakBuildPhase::kPlanning, "pak.plan.asset_type_unknown",
        "Asset plan contains unknown asset_type.", asset.asset_key);
    }

    uint64_t end = 0;
    if (!SafeAdd(asset.offset, asset.size_bytes, end)) {
      AddDiagnostic(diagnostics, PakDiagnosticSeverity::kError,
        PakBuildPhase::kPlanning, "pak.plan.asset_overflow",
        "Asset offset + size overflowed uint64.", asset.asset_key, {}, {},
        asset.offset);
      continue;
    }

    if (asset.alignment == 0 || asset.offset % asset.alignment != 0) {
      AddDiagnostic(diagnostics, PakDiagnosticSeverity::kError,
        PakBuildPhase::kPlanning, "pak.plan.asset_alignment_mismatch",
        "Asset offset does not satisfy alignment.", asset.asset_key, {}, {},
        asset.offset);
    }

    if (!asset.reserved_bytes_zeroed) {
      AddDiagnostic(diagnostics, PakDiagnosticSeverity::kError,
        PakBuildPhase::kPlanning, "pak.plan.asset_reserved_bytes_non_zero",
        "Asset descriptor reserved bytes must be zeroed.", asset.asset_key, {},
        {}, asset.offset);
    }

    ranges.push_back(
      RangeRecord { .start = asset.offset, .end = end, .label = "asset" });
  }

  {
    std::unordered_set<std::string> resource_keys;
    for (const auto& resource : plan.Resources()) {
      if (resource.resource_kind.empty()) {
        AddDiagnostic(diagnostics, PakDiagnosticSeverity::kError,
          PakBuildPhase::kPlanning, "pak.plan.resource_kind_empty",
          "Resource placement kind must be non-empty.");
      }
      if (resource.region_name.empty()) {
        AddDiagnostic(diagnostics, PakDiagnosticSeverity::kError,
          PakBuildPhase::kPlanning, "pak.plan.resource_region_empty",
          "Resource placement region_name must be non-empty.", std::nullopt,
          resource.resource_kind);
      }

      const auto resource_key = resource.resource_kind + ":"
        + std::to_string(resource.resource_index);
      const auto [_, inserted] = resource_keys.insert(resource_key);
      if (!inserted) {
        AddDiagnostic(diagnostics, PakDiagnosticSeverity::kError,
          PakBuildPhase::kPlanning, "pak.plan.resource_duplicate_key",
          "Resource kind/index pair must be unique.", std::nullopt,
          resource.resource_kind, resource.region_name, resource.offset);
      }

      uint64_t end = 0;
      if (!SafeAdd(resource.offset, resource.size_bytes, end)) {
        AddDiagnostic(diagnostics, PakDiagnosticSeverity::kError,
          PakBuildPhase::kPlanning, "pak.plan.resource_overflow",
          "Resource offset + size overflowed uint64.", std::nullopt,
          resource.resource_kind, resource.region_name, resource.offset);
        continue;
      }

      if (resource.alignment == 0
        || resource.offset % resource.alignment != 0) {
        AddDiagnostic(diagnostics, PakDiagnosticSeverity::kError,
          PakBuildPhase::kPlanning, "pak.plan.resource_alignment_mismatch",
          "Resource offset does not satisfy alignment.", std::nullopt,
          resource.resource_kind, resource.region_name, resource.offset);
      }

      if (!resource.reserved_bytes_zeroed) {
        AddDiagnostic(diagnostics, PakDiagnosticSeverity::kError,
          PakBuildPhase::kPlanning, "pak.plan.resource_reserved_bytes_non_zero",
          "Resource descriptor reserved bytes must be zeroed.", std::nullopt,
          resource.resource_kind, resource.region_name, resource.offset);
      }

      const auto region_it = region_bounds.find(resource.region_name);
      if (region_it == region_bounds.end()) {
        AddDiagnostic(diagnostics, PakDiagnosticSeverity::kError,
          PakBuildPhase::kPlanning, "pak.plan.resource_region_missing",
          "Resource references a region_name not present in plan.",
          std::nullopt, resource.resource_kind, resource.region_name,
          resource.offset);
      } else {
        if (resource.offset < region_it->second.first
          || end > region_it->second.second) {
          AddDiagnostic(diagnostics, PakDiagnosticSeverity::kError,
            PakBuildPhase::kPlanning, "pak.plan.resource_out_of_region",
            "Resource data range must be fully contained in its declared "
            "region.",
            std::nullopt, resource.resource_kind, resource.region_name,
            resource.offset);
        }
      }

      ranges.push_back(RangeRecord {
        .start = resource.offset, .end = end, .label = resource_key });
    }
  }

  {
    const auto& directory = plan.Directory();
    uint64_t end = 0;
    if (!SafeAdd(directory.offset, directory.size_bytes, end)) {
      AddDiagnostic(diagnostics, PakDiagnosticSeverity::kError,
        PakBuildPhase::kPlanning, "pak.plan.directory_overflow",
        "Directory offset + size overflowed uint64.", std::string_view {},
        directory.offset);
    } else {
      ranges.push_back(RangeRecord {
        .start = directory.offset, .end = end, .label = "directory" });
    }
  }

  {
    const auto& browse = plan.BrowseIndex();
    if (browse.enabled) {
      uint64_t end = 0;
      if (!SafeAdd(browse.offset, browse.size_bytes, end)) {
        AddDiagnostic(diagnostics, PakDiagnosticSeverity::kError,
          PakBuildPhase::kPlanning, "pak.plan.browse_index_overflow",
          "Browse index offset + size overflowed uint64.", std::string_view {},
          browse.offset);
      } else {
        ranges.push_back(RangeRecord {
          .start = browse.offset,
          .end = end,
          .label = "browse_index",
        });
      }

      std::unordered_set<std::string> seen_paths;
      for (const auto& path : browse.virtual_paths) {
        if (!IsCanonicalVirtualPath(path)) {
          AddDiagnostic(diagnostics, PakDiagnosticSeverity::kError,
            PakBuildPhase::kPlanning, "pak.plan.browse_path_not_canonical",
            "Browse index virtual path must be canonical.", std::nullopt, {},
            "browse_index", {}, path);
        }

        const auto [_, inserted] = seen_paths.insert(path);
        if (!inserted) {
          AddDiagnostic(diagnostics, PakDiagnosticSeverity::kError,
            PakBuildPhase::kPlanning, "pak.plan.browse_path_duplicate",
            "Browse index virtual paths must be duplicate-free.", std::nullopt,
            {}, "browse_index", {}, path);
        }
      }
    } else if (!browse.virtual_paths.empty()) {
      AddDiagnostic(diagnostics, PakDiagnosticSeverity::kError,
        PakBuildPhase::kPlanning, "pak.plan.browse_path_without_index",
        "Browse virtual paths are populated but browse index is disabled.",
        std::nullopt, {}, "browse_index");
    }
  }

  {
    const auto& footer = plan.Footer();
    uint64_t end = 0;
    if (!SafeAdd(footer.offset, footer.size_bytes, end)) {
      AddDiagnostic(diagnostics, PakDiagnosticSeverity::kError,
        PakBuildPhase::kPlanning, "pak.plan.footer_overflow",
        "Footer offset + size overflowed uint64.", std::string_view {},
        footer.offset);
    } else {
      ranges.push_back(
        RangeRecord { .start = footer.offset, .end = end, .label = "footer" });
    }

    uint64_t crc32_end = 0;
    const auto crc32_end_ok = SafeAdd(
      footer.crc32_field_absolute_offset, sizeof(uint32_t), crc32_end);
    if (!crc32_end_ok || footer.crc32_field_absolute_offset < footer.offset
      || crc32_end > end) {
      AddDiagnostic(diagnostics, PakDiagnosticSeverity::kError,
        PakBuildPhase::kPlanning, "pak.plan.crc_offset_invalid",
        "CRC32 field offset is outside footer bounds.", std::string_view {},
        footer.crc32_field_absolute_offset);
    }
  }

  std::ranges::sort(ranges, [](const RangeRecord& lhs, const RangeRecord& rhs) {
    if (lhs.start != rhs.start) {
      return lhs.start < rhs.start;
    }
    if (lhs.end != rhs.end) {
      return lhs.end < rhs.end;
    }
    return lhs.label < rhs.label;
  });

  for (size_t i = 0; i < ranges.size(); ++i) {
    const auto& range = ranges[i];

    if (range.end > plan.PlannedFileSize()) {
      AddDiagnostic(diagnostics, PakDiagnosticSeverity::kError,
        PakBuildPhase::kPlanning, "pak.plan.section_out_of_bounds",
        "Planned section end exceeds planned file size.", range.label,
        range.start);
    }

    if (i == 0) {
      continue;
    }

    const auto& prev = ranges[i - 1];
    if (range.start < prev.end) {
      AddDiagnostic(diagnostics, PakDiagnosticSeverity::kError,
        PakBuildPhase::kPlanning, "pak.plan.section_overlap",
        "Planned sections overlap.", range.label, range.start);
    }
  }

  {
    std::vector<data::AssetKey> keys;
    keys.reserve(plan.Assets().size());
    for (const auto& asset : plan.Assets()) {
      keys.push_back(asset.asset_key);
    }

    std::ranges::sort(keys, IsAssetKeyLess);
    for (size_t i = 1; i < keys.size(); ++i) {
      if (keys[i] == keys[i - 1]) {
        AddDiagnostic(diagnostics, PakDiagnosticSeverity::kError,
          PakBuildPhase::kPlanning, "pak.plan.duplicate_asset_key",
          "Duplicate AssetKey detected in planned assets.");
      }
    }
  }

  {
    std::vector<data::AssetKey> directory_keys;
    directory_keys.reserve(plan.Directory().entries.size());
    for (const auto& entry : plan.Directory().entries) {
      directory_keys.push_back(entry.asset_key);

      if (!IsKnownAssetType(entry.asset_type)) {
        AddDiagnostic(diagnostics, PakDiagnosticSeverity::kError,
          PakBuildPhase::kPlanning, "pak.plan.directory_asset_type_unknown",
          "Directory entry contains unknown asset_type.", entry.asset_key);
      }
    }
    std::ranges::sort(directory_keys, IsAssetKeyLess);
    for (size_t i = 1; i < directory_keys.size(); ++i) {
      if (directory_keys[i] == directory_keys[i - 1]) {
        AddDiagnostic(diagnostics, PakDiagnosticSeverity::kError,
          PakBuildPhase::kPlanning, "pak.plan.directory_duplicate_asset_key",
          "Directory contains duplicate AssetKey entries.");
      }
    }
  }

  for (const auto& entry : plan.Directory().entries) {
    const auto it = std::ranges::find_if(
      plan.Assets(), [&entry](const PakAssetPlacementPlan& asset) {
        return asset.asset_key == entry.asset_key;
      });

    if (it == plan.Assets().end()) {
      AddDiagnostic(diagnostics, PakDiagnosticSeverity::kError,
        PakBuildPhase::kPlanning, "pak.plan.directory_entry_missing_asset",
        "Directory entry references a missing asset placement.",
        std::string_view {}, entry.descriptor_offset);
      continue;
    }

    if (it->asset_type != entry.asset_type) {
      AddDiagnostic(diagnostics, PakDiagnosticSeverity::kError,
        PakBuildPhase::kPlanning, "pak.plan.asset_type_mismatch",
        "Directory entry asset_type mismatches planned descriptor type.");
    }

    uint64_t descriptor_end = 0;
    if (!SafeAdd(
          entry.descriptor_offset, entry.descriptor_size, descriptor_end)) {
      AddDiagnostic(diagnostics, PakDiagnosticSeverity::kError,
        PakBuildPhase::kPlanning, "pak.plan.descriptor_offset_overflow",
        "Directory descriptor_offset + descriptor_size overflowed uint64.",
        entry.asset_key, {}, "asset_directory", entry.descriptor_offset);
      continue;
    }

    uint64_t asset_end = 0;
    if (!SafeAdd(it->offset, it->size_bytes, asset_end)) {
      AddDiagnostic(diagnostics, PakDiagnosticSeverity::kError,
        PakBuildPhase::kPlanning, "pak.plan.asset_overflow",
        "Asset offset + size overflowed uint64.", entry.asset_key, {},
        "asset_directory", it->offset);
      continue;
    }

    if (entry.descriptor_offset < it->offset || descriptor_end > asset_end) {
      AddDiagnostic(diagnostics, PakDiagnosticSeverity::kError,
        PakBuildPhase::kPlanning, "pak.plan.descriptor_out_of_asset_bounds",
        "Descriptor range must be contained in the planned asset range.",
        entry.asset_key, {}, "asset_directory", entry.descriptor_offset);
    }
  }

  {
    std::vector<RangeRecord> script_ranges;
    script_ranges.reserve(plan.ScriptParamRanges().size());
    for (const auto& range : plan.ScriptParamRanges()) {
      uint64_t end = 0;
      if (!SafeAdd(range.params_array_offset, range.params_count, end)) {
        AddDiagnostic(diagnostics, PakDiagnosticSeverity::kError,
          PakBuildPhase::kPlanning, "pak.plan.script_param_overflow",
          "Script param range overflow.", std::nullopt, {},
          "script_slot_table");
        continue;
      }
      if (end > plan.ScriptParamRecordCount()) {
        AddDiagnostic(diagnostics, PakDiagnosticSeverity::kError,
          PakBuildPhase::kPlanning, "pak.plan.script_param_out_of_bounds",
          "Script param range exceeds script param record array.", std::nullopt,
          {}, "script_slot_table");
      }
      script_ranges.push_back(RangeRecord {
        .start = range.params_array_offset,
        .end = end,
        .label = std::to_string(range.slot_index),
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
      if (script_ranges[i].start < script_ranges[i - 1].end) {
        AddDiagnostic(diagnostics, PakDiagnosticSeverity::kError,
          PakBuildPhase::kPlanning, "pak.plan.script_param_overlap",
          "Script param ranges must not overlap.", std::nullopt, {},
          "script_slot_table", script_ranges[i].start);
      }
    }
  }

  {
    std::unordered_map<data::AssetKey, data::AssetType> base_asset_types;
    for (const auto& base : request.base_catalogs) {
      for (const auto& entry : base.entries) {
        const auto [it, inserted]
          = base_asset_types.emplace(entry.asset_key, entry.asset_type);
        if (!inserted && it->second != entry.asset_type) {
          AddDiagnostic(diagnostics, PakDiagnosticSeverity::kError,
            PakBuildPhase::kPlanning, "pak.plan.base_catalog_type_mismatch",
            "Same AssetKey appears in base catalogs with different asset_type.",
            entry.asset_key);
        }
      }
    }

    std::vector<data::AssetKey> patch_keys;
    patch_keys.reserve(plan.PatchActions().size());

    for (const auto& action : plan.PatchActions()) {
      if (!IsKnownAssetType(action.asset_type)) {
        AddDiagnostic(diagnostics, PakDiagnosticSeverity::kError,
          PakBuildPhase::kPlanning, "pak.plan.patch_action_asset_type_unknown",
          "Patch action contains unknown asset_type.", action.asset_key);
      }

      patch_keys.push_back(action.asset_key);

      const auto base_it = base_asset_types.find(action.asset_key);
      const auto exists_in_base = base_it != base_asset_types.end();

      if (policy.mode == PakPlanMode::kPatch) {
        if (action.action == PakPatchAction::kReplace
          || action.action == PakPatchAction::kDelete
          || action.action == PakPatchAction::kUnchanged) {
          if (!exists_in_base) {
            AddDiagnostic(diagnostics, PakDiagnosticSeverity::kError,
              PakBuildPhase::kPlanning, "pak.plan.patch_action_missing_base",
              "Patch action requires key to exist in base catalogs.",
              action.asset_key);
          }
        }

        if (action.action == PakPatchAction::kCreate && exists_in_base) {
          AddDiagnostic(diagnostics, PakDiagnosticSeverity::kError,
            PakBuildPhase::kPlanning, "pak.plan.create_conflicts_with_base",
            "Create action requires key absent from base catalogs.",
            action.asset_key);
        }

        if (action.action == PakPatchAction::kReplace && exists_in_base
          && base_it->second != action.asset_type) {
          AddDiagnostic(diagnostics, PakDiagnosticSeverity::kError,
            PakBuildPhase::kPlanning, "pak.plan.replace_type_mismatch",
            "Replace operation must preserve base asset_type.",
            action.asset_key, "asset_type",
            MakeAssetTypeName(action.asset_type));
        }
      }
    }

    std::ranges::sort(patch_keys, IsAssetKeyLess);
    for (size_t i = 1; i < patch_keys.size(); ++i) {
      if (patch_keys[i] == patch_keys[i - 1]) {
        AddDiagnostic(diagnostics, PakDiagnosticSeverity::kError,
          PakBuildPhase::kPlanning, "pak.plan.patch_action_duplicate_key",
          "Patch action map contains duplicate AssetKey records.");
      }
    }
  }

  if (policy.mode == PakPlanMode::kPatch) {
    const auto emitted_asset_keys = MakePatchActionSet(
      plan, { PakPatchAction::kCreate, PakPatchAction::kReplace });
    std::unordered_set<std::string> closure_entries;
    for (const auto& closure : plan.PatchClosure()) {
      if (!emitted_asset_keys.contains(closure.asset_key)) {
        AddDiagnostic(diagnostics, PakDiagnosticSeverity::kError,
          PakBuildPhase::kPlanning,
          "pak.plan.patch_closure_for_non_emitted_asset",
          "Patch closure entry must target Create/Replace asset keys.",
          closure.asset_key, closure.resource_kind);
      }

      const auto key = data::to_string(closure.asset_key) + "|"
        + closure.resource_kind + "|" + std::to_string(closure.resource_index);
      const auto [_, inserted] = closure_entries.insert(key);
      if (!inserted) {
        AddDiagnostic(diagnostics, PakDiagnosticSeverity::kError,
          PakBuildPhase::kPlanning, "pak.plan.patch_closure_duplicate_entry",
          "Patch closure contains duplicate asset/resource entries.",
          closure.asset_key, closure.resource_kind);
      }

      const auto resource_it = std::ranges::find_if(
        plan.Resources(), [&closure](const PakResourcePlacementPlan& resource) {
          return resource.resource_kind == closure.resource_kind
            && resource.resource_index == closure.resource_index;
        });
      if (resource_it == plan.Resources().end()) {
        AddDiagnostic(diagnostics, PakDiagnosticSeverity::kError,
          PakBuildPhase::kPlanning, "pak.plan.patch_closure_resource_missing",
          "Patch closure references a resource that is not planned.",
          closure.asset_key, closure.resource_kind);
      }
    }
  }

  output.success
    = std::ranges::none_of(diagnostics, [](const PakDiagnostic& diagnostic) {
        return diagnostic.severity == PakDiagnosticSeverity::kError;
      });

  return output;
}

} // namespace oxygen::content::pak
