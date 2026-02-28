//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <filesystem>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Content/PakFile.h>
#include <Oxygen/Cooker/Loose/Inspection.h>
#include <Oxygen/Cooker/Pak/PakPlanBuilder.h>
#include <Oxygen/Cooker/Pak/PakPlanPolicy.h>
#include <Oxygen/Cooker/Pak/PakValidation.h>
#include <Oxygen/Data/LooseCookedIndexFormat.h>
#include <Oxygen/Data/PakFormat_audio.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Data/PakFormat_physics.h>
#include <Oxygen/Data/PakFormat_render.h>
#include <Oxygen/Data/PakFormat_scripting.h>
#include <Oxygen/Serio/FileStream.h>
#include <Oxygen/Serio/Reader.h>

namespace {
namespace pak = oxygen::content::pak;
namespace content = oxygen::content;
namespace lc = oxygen::content::lc;
namespace data = oxygen::data;
namespace serio = oxygen::serio;
namespace core = oxygen::data::pak::core;
namespace audio = oxygen::data::pak::audio;
namespace render = oxygen::data::pak::render;
namespace script = oxygen::data::pak::scripting;
namespace physics = oxygen::data::pak::physics;

constexpr std::array<uint8_t, 16> kZeroSourceKeyBytes {};
constexpr uint32_t kRegionAlignment = 256U;
constexpr uint32_t kTableAlignment = 16U;
constexpr uint32_t kAssetAlignment = 16U;
constexpr uint32_t kDirectoryAlignment = 16U;
constexpr uint32_t kBrowseAlignment = 16U;
constexpr uint32_t kFooterAlignment = 16U;
constexpr uint64_t kMaxCountAsUint64 = (std::numeric_limits<uint32_t>::max)();
constexpr int kUnknownRegionOrder = (std::numeric_limits<int>::max)();

struct AlignmentBytes final {
  uint32_t value = 1U;
};

struct AggregatedAsset {
  data::AssetKey key {};
  data::AssetType asset_type = data::AssetType::kUnknown;
  std::filesystem::path descriptor_path;
  uint64_t descriptor_size = 0;
  oxygen::base::Sha256Digest descriptor_digest {};
  oxygen::base::Sha256Digest transitive_resource_digest {};
  std::string virtual_path;
  size_t source_order = 0;
};

struct PendingResource {
  std::string region_name;
  std::string resource_kind;
  uint64_t size_bytes = 0;
  uint32_t alignment = kRegionAlignment;
  size_t source_order = 0;
  std::filesystem::path path;
};

struct TableCounts {
  uint64_t texture_count = 0;
  uint64_t buffer_count = 0;
  uint64_t audio_count = 0;
  uint64_t script_resource_count = 0;
  uint64_t script_slot_count = 0;
  uint64_t physics_count = 0;
};

auto AddDiagnostic(std::vector<pak::PakDiagnostic>& diagnostics,
  const pak::PakDiagnosticSeverity severity, const pak::PakBuildPhase phase,
  const std::string_view code, const std::string_view message,
  const std::filesystem::path& path = {}) -> void
{
  diagnostics.push_back(pak::PakDiagnostic {
    .severity = severity,
    .phase = phase,
    .code = std::string(code),
    .message = std::string(message),
    .asset_key = {},
    .resource_kind = {},
    .table_name = {},
    .path = path,
    .offset = {},
  });
}

auto IsAssetKeyLess(const data::AssetKey& lhs, const data::AssetKey& rhs)
  -> bool
{
  return std::ranges::lexicographical_compare(lhs.guid, rhs.guid);
}

auto IsKnownAssetType(const data::AssetType asset_type) -> bool
{
  return asset_type >= data::AssetType::kMaterial
    && asset_type <= data::AssetType::kPhysicsScene;
}

auto AlignUp(const uint64_t value, const AlignmentBytes alignment) -> uint64_t
{
  if (alignment.value <= 1U) {
    return value;
  }

  const auto u_alignment = static_cast<uint64_t>(alignment.value);
  const auto remainder = value % u_alignment;
  if (remainder == 0U) {
    return value;
  }
  return value + (u_alignment - remainder);
}

auto SafeAdd(const uint64_t lhs, const uint64_t rhs, uint64_t& out) -> bool
{
  if (rhs > (std::numeric_limits<uint64_t>::max)() - lhs) {
    return false;
  }
  out = lhs + rhs;
  return true;
}

auto ToCanonicalSourcePath(const std::filesystem::path& input)
  -> std::filesystem::path
{
  return input.lexically_normal();
}

auto ToCanonicalVirtualPath(const std::string_view path) -> std::string
{
  if (path.empty()) {
    return {};
  }

  std::string normalized(path);
  std::replace(normalized.begin(), normalized.end(), '\\', '/');
  if (!normalized.empty() && normalized.front() != '/') {
    normalized.insert(normalized.begin(), '/');
  }

  while (normalized.find("//") != std::string::npos) {
    normalized = std::string(normalized).replace(normalized.find("//"), 2, "/");
  }

  return normalized;
}

auto MeasureFileSize(const std::filesystem::path& path,
  std::vector<pak::PakDiagnostic>& diagnostics) -> std::optional<uint64_t>
{
  std::error_code ec;
  const auto size = std::filesystem::file_size(path, ec);
  if (ec) {
    AddDiagnostic(diagnostics, pak::PakDiagnosticSeverity::kError,
      pak::PakBuildPhase::kPlanning, "pak.plan.file_size_failed",
      "Failed to stat file size for source payload.", path);
    return std::nullopt;
  }
  return size;
}

auto ReadScriptSlotRangesFromTable(
  const std::filesystem::path& scripts_table_path, uint32_t slot_index_base,
  std::vector<pak::PakScriptParamRangePlan>& ranges,
  std::vector<pak::PakDiagnostic>& diagnostics) -> uint32_t
{
  const auto size_opt = MeasureFileSize(scripts_table_path, diagnostics);
  if (!size_opt.has_value()) {
    return 0;
  }

  constexpr uint64_t kSlotSize = sizeof(script::ScriptSlotRecord);
  constexpr uint64_t kParamRecordSize = sizeof(script::ScriptParamRecord);

  if (kSlotSize == 0U || (*size_opt % kSlotSize) != 0U) {
    AddDiagnostic(diagnostics, pak::PakDiagnosticSeverity::kError,
      pak::PakBuildPhase::kPlanning, "pak.plan.script_slot_table_size_invalid",
      "scripts.table size is not divisible by ScriptSlotRecord size.",
      scripts_table_path);
    return 0;
  }

  const auto slot_count64 = *size_opt / kSlotSize;
  if (slot_count64 > kMaxCountAsUint64) {
    AddDiagnostic(diagnostics, pak::PakDiagnosticSeverity::kError,
      pak::PakBuildPhase::kPlanning, "pak.plan.script_slot_table_too_large",
      "scripts.table has too many slot records.", scripts_table_path);
    return 0;
  }

  serio::FileStream<> stream(scripts_table_path, std::ios::in);
  serio::Reader<serio::FileStream<>> reader(stream);
  auto align_guard = reader.ScopedAlignment(1);
  (void)align_guard;

  const auto blob_result = reader.ReadBlob(static_cast<size_t>(*size_opt));
  if (!blob_result) {
    AddDiagnostic(diagnostics, pak::PakDiagnosticSeverity::kError,
      pak::PakBuildPhase::kPlanning, "pak.plan.script_slot_table_read_failed",
      "Failed to read scripts.table content.", scripts_table_path);
    return 0;
  }

  const auto slot_count = static_cast<uint32_t>(slot_count64);
  const auto blob = std::span<const std::byte>(*blob_result);
  for (uint32_t i = 0; i < slot_count; ++i) {
    const auto offset
      = static_cast<size_t>(i) * sizeof(script::ScriptSlotRecord);
    script::ScriptSlotRecord record {};
    std::memcpy(std::addressof(record),
      blob.subspan(offset, sizeof(record)).data(), sizeof(record));

    if ((record.params_array_offset % kParamRecordSize) != 0U) {
      AddDiagnostic(diagnostics, pak::PakDiagnosticSeverity::kError,
        pak::PakBuildPhase::kPlanning,
        "pak.plan.script_params_offset_unaligned",
        "ScriptSlotRecord.params_array_offset is not aligned to "
        "ScriptParamRecord.",
        scripts_table_path);
      continue;
    }

    const auto params_array_offset
      = static_cast<uint32_t>(record.params_array_offset / kParamRecordSize);
    ranges.push_back(pak::PakScriptParamRangePlan {
      .slot_index = slot_index_base + i,
      .params_array_offset = params_array_offset,
      .params_count = record.params_count,
    });
  }

  return slot_count;
}

auto MakeSkeletonTables() -> std::vector<pak::PakTablePlan>
{
  return {
    pak::PakTablePlan {
      .table_name = "texture_table",
      .offset = 0,
      .size_bytes = 0,
      .count = 0,
      .entry_size = static_cast<uint32_t>(sizeof(render::TextureResourceDesc)),
      .expected_entry_size
      = static_cast<uint32_t>(sizeof(render::TextureResourceDesc)),
      .alignment = kTableAlignment,
      .index_zero_required = false,
      .index_zero_present = false,
      .index_zero_forbidden = false,
    },
    pak::PakTablePlan {
      .table_name = "buffer_table",
      .offset = 0,
      .size_bytes = 0,
      .count = 0,
      .entry_size = static_cast<uint32_t>(sizeof(core::BufferResourceDesc)),
      .expected_entry_size
      = static_cast<uint32_t>(sizeof(core::BufferResourceDesc)),
      .alignment = kTableAlignment,
      .index_zero_required = false,
      .index_zero_present = false,
      .index_zero_forbidden = false,
    },
    pak::PakTablePlan {
      .table_name = "audio_table",
      .offset = 0,
      .size_bytes = 0,
      .count = 0,
      .entry_size = static_cast<uint32_t>(sizeof(audio::AudioResourceDesc)),
      .expected_entry_size
      = static_cast<uint32_t>(sizeof(audio::AudioResourceDesc)),
      .alignment = kTableAlignment,
      .index_zero_required = false,
      .index_zero_present = false,
      .index_zero_forbidden = false,
    },
    pak::PakTablePlan {
      .table_name = "script_resource_table",
      .offset = 0,
      .size_bytes = 0,
      .count = 0,
      .entry_size = static_cast<uint32_t>(sizeof(script::ScriptResourceDesc)),
      .expected_entry_size
      = static_cast<uint32_t>(sizeof(script::ScriptResourceDesc)),
      .alignment = kTableAlignment,
      .index_zero_required = false,
      .index_zero_present = false,
      .index_zero_forbidden = false,
    },
    pak::PakTablePlan {
      .table_name = "script_slot_table",
      .offset = 0,
      .size_bytes = 0,
      .count = 0,
      .entry_size = static_cast<uint32_t>(sizeof(script::ScriptSlotRecord)),
      .expected_entry_size
      = static_cast<uint32_t>(sizeof(script::ScriptSlotRecord)),
      .alignment = kTableAlignment,
      .index_zero_required = false,
      .index_zero_present = false,
      .index_zero_forbidden = false,
    },
    pak::PakTablePlan {
      .table_name = "physics_resource_table",
      .offset = 0,
      .size_bytes = 0,
      .count = 0,
      .entry_size = static_cast<uint32_t>(sizeof(physics::PhysicsResourceDesc)),
      .expected_entry_size
      = static_cast<uint32_t>(sizeof(physics::PhysicsResourceDesc)),
      .alignment = kTableAlignment,
      .index_zero_required = false,
      .index_zero_present = false,
      .index_zero_forbidden = false,
    },
  };
}

auto MakeSkeletonPlan(const pak::PakBuildRequest& request) -> pak::PakPlan::Data
{
  pak::PakPlan::Data data_plan {};

  data_plan.header = pak::PakHeaderPlan {
    .offset = 0,
    .size_bytes = static_cast<uint32_t>(sizeof(core::PakHeader)),
    .content_version = request.content_version,
    .source_key = request.source_key,
  };

  data_plan.tables = MakeSkeletonTables();
  data_plan.resources = {};
  data_plan.directory = pak::PakDirectoryPlan {
    .offset = 0,
    .size_bytes = 0,
    .entries = {},
  };
  data_plan.browse_index = pak::PakBrowseIndexPlan {
    .enabled = false,
    .offset = 0,
    .size_bytes = 0,
    .virtual_paths = {},
  };

  const auto footer_offset = static_cast<uint64_t>(sizeof(core::PakHeader));
  data_plan.footer = pak::PakFooterPlan {
    .offset = footer_offset,
    .size_bytes = static_cast<uint32_t>(sizeof(core::PakFooter)),
    .crc32_field_absolute_offset
    = footer_offset + offsetof(core::PakFooter, pak_crc32),
  };

  data_plan.script_param_record_count = 0;
  data_plan.script_param_ranges = {};
  data_plan.patch_closure = {};
  data_plan.planned_file_size
    = footer_offset + static_cast<uint64_t>(sizeof(core::PakFooter));

  return data_plan;
}

auto FindTableByName(
  std::vector<pak::PakTablePlan>& tables, const std::string_view name)
  -> std::optional<std::reference_wrapper<pak::PakTablePlan>>
{
  const auto it
    = std::ranges::find_if(tables, [name](const pak::PakTablePlan& table) {
        return table.table_name == name;
      });
  if (it == tables.end()) {
    return std::nullopt;
  }
  return std::ref(*it);
}

auto SetTableCount(std::vector<pak::PakTablePlan>& tables,
  const std::string_view table_name, uint64_t count,
  std::vector<pak::PakDiagnostic>& diagnostics) -> void
{
  auto table_ref = FindTableByName(tables, table_name);
  if (!table_ref.has_value()) {
    AddDiagnostic(diagnostics, pak::PakDiagnosticSeverity::kError,
      pak::PakBuildPhase::kPlanning, "pak.plan.table_missing",
      "Internal planner table definition is missing.");
    return;
  }

  if (count > kMaxCountAsUint64) {
    AddDiagnostic(diagnostics, pak::PakDiagnosticSeverity::kError,
      pak::PakBuildPhase::kPlanning, "pak.plan.table_count_too_large",
      "Planned table count exceeds uint32 range.");
    count = kMaxCountAsUint64;
  }

  auto& table = table_ref->get();
  table.count = static_cast<uint32_t>(count);
  table.size_bytes = static_cast<uint64_t>(table.count) * table.entry_size;
}

auto ApplyIndexZeroPolicy(std::vector<pak::PakTablePlan>& tables) -> void
{
  for (auto& table : tables) {
    table.index_zero_required = false;
    table.index_zero_present = false;
    table.index_zero_forbidden = false;

    if (table.table_name == "texture_table"
      || table.table_name == "buffer_table"
      || table.table_name == "script_resource_table"
      || table.table_name == "physics_resource_table") {
      if (table.count > 0U) {
        table.index_zero_required = true;
        table.index_zero_present = true;
      }
      continue;
    }

    if (table.table_name == "audio_table") {
      if (table.count > 0U) {
        table.index_zero_forbidden = true;
      }
      continue;
    }
  }
}

auto RegionOrder(const std::string_view region_name) -> int
{
  if (region_name == "texture_region") {
    return 0;
  }
  if (region_name == "buffer_region") {
    return 1;
  }
  if (region_name == "audio_region") {
    return 2;
  }
  if (region_name == "script_region") {
    return 3;
  }
  if (region_name == "physics_region") {
    return 4;
  }
  return kUnknownRegionOrder;
}

auto AccumulateTableCountFromFile(const std::filesystem::path& table_path,
  const uint64_t file_size, const uint64_t entry_size,
  uint64_t& count_accumulator, std::vector<pak::PakDiagnostic>& diagnostics,
  const std::string_view code) -> void
{
  if (entry_size == 0U || (file_size % entry_size) != 0U) {
    AddDiagnostic(diagnostics, pak::PakDiagnosticSeverity::kError,
      pak::PakBuildPhase::kPlanning, code,
      "Resource table file size is not divisible by expected entry size.",
      table_path);
    return;
  }

  count_accumulator += file_size / entry_size;
}

auto ComputeDescriptorDigestFromFile(const std::filesystem::path& descriptor_path,
  std::vector<pak::PakDiagnostic>& diagnostics)
  -> std::optional<oxygen::base::Sha256Digest>
{
  try {
    return oxygen::base::ComputeFileSha256(descriptor_path);
  } catch (const std::exception& ex) {
    AddDiagnostic(diagnostics, pak::PakDiagnosticSeverity::kError,
      pak::PakBuildPhase::kPlanning, "pak.plan.descriptor_digest_compute_failed",
      std::string("Failed to compute descriptor digest: ") + ex.what(),
      descriptor_path);
    return std::nullopt;
  }
}

auto ComputeDescriptorDigestFromPakEntry(const content::PakFile& pak_file,
  const core::AssetDirectoryEntry& entry, const std::filesystem::path& source_path,
  std::vector<pak::PakDiagnostic>& diagnostics)
  -> std::optional<oxygen::base::Sha256Digest>
{
  try {
    auto reader = pak_file.CreateReader(entry);
    const auto blob_result = reader.ReadBlob(entry.desc_size);
    if (!blob_result) {
      AddDiagnostic(diagnostics, pak::PakDiagnosticSeverity::kError,
        pak::PakBuildPhase::kPlanning, "pak.plan.pak_descriptor_read_failed",
        "Failed to read descriptor bytes from pak source.", source_path);
      return std::nullopt;
    }
    return oxygen::base::ComputeSha256(std::span<const std::byte>(*blob_result));
  } catch (const std::exception& ex) {
    AddDiagnostic(diagnostics, pak::PakDiagnosticSeverity::kError,
      pak::PakBuildPhase::kPlanning, "pak.plan.pak_descriptor_read_failed",
      std::string("Failed to read descriptor bytes from pak source: ") + ex.what(),
      source_path);
    return std::nullopt;
  }
}

auto ToCatalogEntry(const AggregatedAsset& asset) -> data::PakCatalogEntry
{
  return data::PakCatalogEntry {
    .asset_key = asset.key,
    .asset_type = asset.asset_type,
    .descriptor_digest = asset.descriptor_digest,
    .transitive_resource_digest = asset.transitive_resource_digest,
  };
}

struct PlanningState final {
  explicit PlanningState(const pak::PakBuildRequest& request_in)
    : request(oxygen::observer_ptr<const pak::PakBuildRequest> { std::addressof(request_in) })
    , policy(pak::DerivePakPlanPolicy(request_in))
    , data_plan(MakeSkeletonPlan(request_in))
  {
  }

  oxygen::observer_ptr<const pak::PakBuildRequest> request { nullptr };
  pak::PakPlanPolicy policy {};
  pak::PakPlan::Data data_plan {};
  pak::PakPlanBuilder::BuildResult output {};

  std::vector<AggregatedAsset> assets;
  std::unordered_map<data::AssetKey, size_t> asset_positions;
  std::vector<PendingResource> pending_resources;
  std::vector<pak::PakScriptParamRangePlan> script_param_ranges;
  std::unordered_map<std::string, data::AssetKey> browse_map;
  TableCounts table_counts {};
  uint32_t script_param_record_count = 0;
};

auto HasPlanningErrors(const PlanningState& state) -> bool
{
  return std::ranges::any_of(state.output.diagnostics,
    [](const pak::PakDiagnostic& diagnostic) {
      return diagnostic.severity == pak::PakDiagnosticSeverity::kError;
    });
}

auto AddStageInvariantDiagnostic(PlanningState& state, const std::string_view code,
  const std::string_view message) -> void
{
  AddDiagnostic(state.output.diagnostics, pak::PakDiagnosticSeverity::kError,
    pak::PakBuildPhase::kPlanning, code, message);
}

[[nodiscard]] auto CheckStageInvariant(PlanningState& state, const bool condition,
  const std::string_view code, const std::string_view message) -> bool
{
  if (condition) {
    return true;
  }
  AddStageInvariantDiagnostic(state, code, message);
  DCHECK_F(condition, "PakPlanBuilder invariant failure: {}", message);
  return false;
}

auto EnforceStageInvariant(PlanningState& state, const bool condition,
  const std::string_view code, const std::string_view message) -> void
{
  if (!condition) {
    AddStageInvariantDiagnostic(state, code, message);
    DCHECK_F(condition, "PakPlanBuilder invariant failure: {}", message);
  }
}

template <typename Fn>
auto RunStage(PlanningState& state, const std::string_view stage_name,
  const std::string_view failure_code, Fn&& fn) -> void
{
  try {
    std::forward<Fn>(fn)();
  } catch (const std::exception& ex) {
    AddStageInvariantDiagnostic(state, failure_code,
      std::string("Stage '") + std::string(stage_name)
        + "' threw exception: " + ex.what());
    DCHECK_F(false, "Stage '{}' threw exception: {}", stage_name, ex.what());
  } catch (...) {
    AddStageInvariantDiagnostic(state, failure_code,
      std::string("Stage '") + std::string(stage_name)
        + "' threw unknown exception.");
    DCHECK_F(false, "Stage '{}' threw unknown exception", stage_name);
  }
}

auto ValidatePlanningRequest(PlanningState& state) -> void
{
  using pak::PakBuildPhase;
  using pak::PakDiagnosticSeverity;

  const auto& request = *state.request;
  const auto& policy = state.policy;
  auto& diagnostics = state.output.diagnostics;

  if (request.source_key.get() == kZeroSourceKeyBytes) {
    AddDiagnostic(diagnostics, PakDiagnosticSeverity::kError,
      PakBuildPhase::kPlanning, "pak.plan.source_key_zero",
      "Planning requires a non-zero source_key.");
  }

  if (request.output_pak_path.empty()) {
    AddDiagnostic(diagnostics, PakDiagnosticSeverity::kError,
      PakBuildPhase::kPlanning, "pak.plan.output_pak_path_empty",
      "Planning requires a non-empty output_pak_path.",
      request.output_pak_path);
  }

  if (policy.requires_base_catalogs && request.base_catalogs.empty()) {
    AddDiagnostic(diagnostics, PakDiagnosticSeverity::kError,
      PakBuildPhase::kPlanning, "pak.plan.patch_requires_base_catalogs",
      "Patch planning requires at least one base catalog.");
  }

  if (policy.mode == pak::PakPlanMode::kPatch
    && request.output_manifest_path.empty()) {
    AddDiagnostic(diagnostics, PakDiagnosticSeverity::kError,
      PakBuildPhase::kPlanning, "pak.plan.patch_requires_manifest_path",
      "Patch planning requires a non-empty output_manifest_path.",
      request.output_manifest_path);
  }

  if (policy.mode == pak::PakPlanMode::kFull && policy.emits_manifest
    && request.output_manifest_path.empty()) {
    AddDiagnostic(diagnostics, PakDiagnosticSeverity::kError,
      PakBuildPhase::kPlanning, "pak.plan.full_manifest_requires_manifest_path",
      "Full planning with emit_manifest_in_full=true requires "
      "output_manifest_path.",
      request.output_manifest_path);
  }
}

auto ValidateCollectSourceDataInvariants(PlanningState& state) -> void
{
  for (const auto& [asset_key, position] : state.asset_positions) {
    const auto position_valid = position < state.assets.size();
    if (!CheckStageInvariant(state, position_valid,
          "pak.plan.stage.collect.asset_position_out_of_range",
          "Source collection produced an out-of-range asset_positions index.")) {
      continue;
    }

    const auto key_matches = state.assets[position].key == asset_key;
    EnforceStageInvariant(state, key_matches,
      "pak.plan.stage.collect.asset_position_key_mismatch",
      "Source collection asset_positions key does not match assets[position].");
  }
}

auto CollectSourceData(PlanningState& state) -> void
{
  using pak::PakBuildPhase;
  using pak::PakDiagnosticSeverity;

  auto& diagnostics = state.output.diagnostics;

  auto sources = state.request->sources;
  if (state.request->options.deterministic) {
    std::ranges::stable_sort(sources,
      [](const data::CookedSource& lhs, const data::CookedSource& rhs) {
        if (lhs.kind != rhs.kind) {
          return static_cast<uint8_t>(lhs.kind)
            < static_cast<uint8_t>(rhs.kind);
        }
        return lhs.path.generic_string() < rhs.path.generic_string();
      });
  }

  for (size_t source_order = 0; source_order < sources.size(); ++source_order) {
    const auto& source = sources[source_order];
    const auto source_root = ToCanonicalSourcePath(source.path);

    if (source.kind == data::CookedSourceKind::kLooseCooked) {
      lc::Inspection inspection;
      try {
        inspection.LoadFromRoot(source_root);
      } catch (const std::exception& ex) {
        AddDiagnostic(diagnostics, PakDiagnosticSeverity::kError,
          PakBuildPhase::kPlanning, "pak.plan.loose_source_load_failed",
          std::string("Failed to load loose cooked source: ") + ex.what(),
          source_root);
        continue;
      }

      auto source_assets = std::vector<lc::Inspection::AssetEntry>(
        inspection.Assets().begin(), inspection.Assets().end());
      std::ranges::sort(source_assets,
        [](const lc::Inspection::AssetEntry& lhs,
          const lc::Inspection::AssetEntry& rhs) {
          return IsAssetKeyLess(lhs.key, rhs.key);
        });

      bool source_has_scene_assets = false;
      bool source_has_script_assets = false;
      for (const auto& asset : source_assets) {
        const auto asset_type = static_cast<data::AssetType>(asset.asset_type);
        source_has_scene_assets
          = source_has_scene_assets || asset_type == data::AssetType::kScene;
        source_has_script_assets
          = source_has_script_assets || asset_type == data::AssetType::kScript;
      }

      for (const auto& source_asset : source_assets) {
        const auto asset_type
          = static_cast<data::AssetType>(source_asset.asset_type);
        if (!IsKnownAssetType(asset_type)) {
          AddDiagnostic(diagnostics, PakDiagnosticSeverity::kError,
            PakBuildPhase::kPlanning, "pak.plan.asset_type_invalid",
            "Loose cooked asset entry has an invalid asset_type.", source_root);
          continue;
        }

        const auto descriptor_path = source_root
          / std::filesystem::path(source_asset.descriptor_relpath);
        auto descriptor_size = source_asset.descriptor_size;
        if (descriptor_size == 0U) {
          const auto measured = MeasureFileSize(descriptor_path, diagnostics);
          if (!measured.has_value()) {
            continue;
          }
          descriptor_size = *measured;
        }

        AggregatedAsset aggregated {
          .key = source_asset.key,
          .asset_type = asset_type,
          .descriptor_path = descriptor_path,
          .descriptor_size = descriptor_size,
          .descriptor_digest = {},
          .transitive_resource_digest = {},
          .virtual_path = ToCanonicalVirtualPath(source_asset.virtual_path),
          .source_order = source_order,
        };

        if (source_asset.descriptor_sha256.has_value()) {
          aggregated.descriptor_digest = *source_asset.descriptor_sha256;
        } else {
          const auto descriptor_digest
            = ComputeDescriptorDigestFromFile(descriptor_path, diagnostics);
          if (!descriptor_digest.has_value()) {
            continue;
          }
          aggregated.descriptor_digest = *descriptor_digest;
        }
        aggregated.transitive_resource_digest = aggregated.descriptor_digest;

        const auto position_it = state.asset_positions.find(aggregated.key);
        if (position_it == state.asset_positions.end()) {
          state.asset_positions.emplace(aggregated.key, state.assets.size());
          state.assets.push_back(std::move(aggregated));
        } else {
          state.assets[position_it->second] = std::move(aggregated);
        }
      }

      auto source_files = std::vector<lc::Inspection::FileEntry>(
        inspection.Files().begin(), inspection.Files().end());
      std::ranges::sort(source_files,
        [](const lc::Inspection::FileEntry& lhs,
          const lc::Inspection::FileEntry& rhs) {
          if (lhs.kind != rhs.kind) {
            return static_cast<uint16_t>(lhs.kind)
              < static_cast<uint16_t>(rhs.kind);
          }
          return lhs.relpath < rhs.relpath;
        });

      for (const auto& file_entry : source_files) {
        const auto file_path
          = source_root / std::filesystem::path(file_entry.relpath);
        auto file_size = file_entry.size;
        if (file_size == 0U) {
          const auto measured = MeasureFileSize(file_path, diagnostics);
          if (!measured.has_value()) {
            continue;
          }
          file_size = *measured;
        }

        switch (file_entry.kind) {
        case data::loose_cooked::FileKind::kTexturesData:
          state.pending_resources.push_back(PendingResource {
            .region_name = "texture_region",
            .resource_kind = "texture",
            .size_bytes = file_size,
            .alignment = kRegionAlignment,
            .source_order = source_order,
            .path = file_path,
          });
          break;
        case data::loose_cooked::FileKind::kBuffersData:
          state.pending_resources.push_back(PendingResource {
            .region_name = "buffer_region",
            .resource_kind = "buffer",
            .size_bytes = file_size,
            .alignment = kRegionAlignment,
            .source_order = source_order,
            .path = file_path,
          });
          break;
        case data::loose_cooked::FileKind::kScriptsData:
          state.pending_resources.push_back(PendingResource {
            .region_name = "script_region",
            .resource_kind = "script",
            .size_bytes = file_size,
            .alignment = kRegionAlignment,
            .source_order = source_order,
            .path = file_path,
          });
          if ((file_size % sizeof(script::ScriptParamRecord)) == 0U) {
            const auto record_count64
              = file_size / sizeof(script::ScriptParamRecord);
            if (record_count64 <= kMaxCountAsUint64) {
              state.script_param_record_count = (std::max)(
                state.script_param_record_count,
                static_cast<uint32_t>(record_count64));
            }
          } else {
            AddDiagnostic(diagnostics, PakDiagnosticSeverity::kError,
              PakBuildPhase::kPlanning, "pak.plan.script_params_file_size_invalid",
              "scripts.data size is not divisible by ScriptParamRecord size.",
              file_path);
          }
          break;
        case data::loose_cooked::FileKind::kPhysicsData:
          state.pending_resources.push_back(PendingResource {
            .region_name = "physics_region",
            .resource_kind = "physics",
            .size_bytes = file_size,
            .alignment = kRegionAlignment,
            .source_order = source_order,
            .path = file_path,
          });
          break;
        case data::loose_cooked::FileKind::kTexturesTable:
          AccumulateTableCountFromFile(file_path, file_size,
            sizeof(render::TextureResourceDesc), state.table_counts.texture_count,
            diagnostics, "pak.plan.texture_table_size_invalid");
          break;
        case data::loose_cooked::FileKind::kBuffersTable:
          AccumulateTableCountFromFile(file_path, file_size,
            sizeof(core::BufferResourceDesc), state.table_counts.buffer_count,
            diagnostics, "pak.plan.buffer_table_size_invalid");
          break;
        case data::loose_cooked::FileKind::kPhysicsTable:
          AccumulateTableCountFromFile(file_path, file_size,
            sizeof(physics::PhysicsResourceDesc), state.table_counts.physics_count,
            diagnostics, "pak.plan.physics_table_size_invalid");
          break;
        case data::loose_cooked::FileKind::kScriptsTable: {
          const auto slot_compatible
            = (file_size % sizeof(script::ScriptSlotRecord)) == 0U;
          const auto resource_compatible
            = (file_size % sizeof(script::ScriptResourceDesc)) == 0U;

          const bool parse_as_slots
            = slot_compatible && (source_has_scene_assets || !resource_compatible);
          if (parse_as_slots) {
            const auto parsed_slots = ReadScriptSlotRangesFromTable(file_path,
              static_cast<uint32_t>(state.script_param_ranges.size()),
              state.script_param_ranges, diagnostics);
            state.table_counts.script_slot_count += parsed_slots;
          } else if (resource_compatible) {
            state.table_counts.script_resource_count
              += file_size / sizeof(script::ScriptResourceDesc);
          } else {
            AddDiagnostic(diagnostics, PakDiagnosticSeverity::kError,
              PakBuildPhase::kPlanning, "pak.plan.scripts_table_size_invalid",
              "scripts.table is incompatible with known script table layouts.",
              file_path);
          }

          if (source_has_scene_assets && source_has_script_assets && slot_compatible
            && resource_compatible) {
            AddDiagnostic(diagnostics, PakDiagnosticSeverity::kWarning,
              PakBuildPhase::kPlanning, "pak.plan.scripts_table_ambiguous_layout",
              "scripts.table is compatible with both slot and resource "
              "records; planner selected slot layout.",
              file_path);
          }
          break;
        }
        case data::loose_cooked::FileKind::kUnknown:
          break;
        }
      }

      continue;
    }

    if (source.kind == data::CookedSourceKind::kPak) {
      std::optional<content::PakFile> pak_file;
      try {
        pak_file.emplace(source_root);
      } catch (const std::exception& ex) {
        AddDiagnostic(diagnostics, PakDiagnosticSeverity::kError,
          PakBuildPhase::kPlanning, "pak.plan.pak_source_load_failed",
          std::string("Failed to load pak source: ") + ex.what(), source_root);
        continue;
      }

      auto directory = std::vector<data::pak::core::AssetDirectoryEntry>(
        pak_file->Directory().begin(), pak_file->Directory().end());
      std::ranges::sort(directory,
        [](const data::pak::core::AssetDirectoryEntry& lhs,
          const data::pak::core::AssetDirectoryEntry& rhs) {
          return IsAssetKeyLess(lhs.asset_key, rhs.asset_key);
        });

      for (const auto& entry : directory) {
        if (!IsKnownAssetType(entry.asset_type)) {
          AddDiagnostic(diagnostics, PakDiagnosticSeverity::kError,
            PakBuildPhase::kPlanning, "pak.plan.asset_type_invalid",
            "PAK source directory entry has an invalid asset_type.", source_root);
          continue;
        }

        const auto descriptor_digest
          = ComputeDescriptorDigestFromPakEntry(*pak_file, entry, source_root, diagnostics);
        if (!descriptor_digest.has_value()) {
          continue;
        }

        AggregatedAsset aggregated {
          .key = entry.asset_key,
          .asset_type = entry.asset_type,
          .descriptor_path = source_root,
          .descriptor_size = entry.desc_size,
          .descriptor_digest = *descriptor_digest,
          .transitive_resource_digest = *descriptor_digest,
          .virtual_path = {},
          .source_order = source_order,
        };

        const auto position_it = state.asset_positions.find(aggregated.key);
        if (position_it == state.asset_positions.end()) {
          state.asset_positions.emplace(aggregated.key, state.assets.size());
          state.assets.push_back(std::move(aggregated));
        } else {
          state.assets[position_it->second] = std::move(aggregated);
        }
      }

      if (pak_file->HasBrowseIndex()) {
        for (const auto& browse_entry : pak_file->BrowseIndex()) {
          state.browse_map[ToCanonicalVirtualPath(browse_entry.virtual_path)]
            = browse_entry.asset_key;
        }
      }

      AddDiagnostic(diagnostics, PakDiagnosticSeverity::kWarning,
        PakBuildPhase::kPlanning, "pak.plan.pak_source_regions_projected",
        "PAK source planning currently projects directory/browse data; "
        "resource regions are not expanded.");
      continue;
    }

    AddDiagnostic(diagnostics, PakDiagnosticSeverity::kError,
      PakBuildPhase::kPlanning, "pak.plan.source_kind_unsupported",
      "Cooked source kind is not supported by planner.", source_root);
  }
}

auto ClassifyPatchActionsAndFinalizeBrowse(PlanningState& state) -> void
{
  using pak::PakBuildPhase;
  using pak::PakDiagnosticSeverity;

  auto& diagnostics = state.output.diagnostics;
  auto& assets = state.assets;

  std::ranges::sort(assets, [](const AggregatedAsset& lhs, const AggregatedAsset& rhs) {
    return IsAssetKeyLess(lhs.key, rhs.key);
  });

  std::unordered_map<data::AssetKey, data::PakCatalogEntry> source_catalog_entries;
  source_catalog_entries.reserve(assets.size());
  for (const auto& asset : assets) {
    source_catalog_entries[asset.key] = ToCatalogEntry(asset);
  }

  std::unordered_map<data::AssetKey, data::PakCatalogEntry> base_catalog_entries;
  for (const auto& catalog : state.request->base_catalogs) {
    for (const auto& entry : catalog.entries) {
      const auto [it, inserted]
        = base_catalog_entries.emplace(entry.asset_key, entry);
      if (!inserted && it->second.asset_type != entry.asset_type) {
        AddDiagnostic(diagnostics, PakDiagnosticSeverity::kError,
          PakBuildPhase::kPlanning, "pak.plan.base_catalog_type_mismatch",
          "Same AssetKey appears in base catalogs with different asset_type.");
      }
    }
  }

  state.data_plan.patch_actions.clear();
  std::unordered_set<data::AssetKey> emitted_patch_keys;
  if (state.policy.mode == pak::PakPlanMode::kPatch) {
    std::vector<data::AssetKey> patch_keys;
    patch_keys.reserve(
      source_catalog_entries.size() + base_catalog_entries.size());
    for (const auto& entry : source_catalog_entries) {
      patch_keys.push_back(entry.first);
    }
    for (const auto& entry : base_catalog_entries) {
      patch_keys.push_back(entry.first);
    }

    std::ranges::sort(patch_keys, IsAssetKeyLess);
    patch_keys.erase(std::ranges::unique(patch_keys).begin(), patch_keys.end());
    state.data_plan.patch_actions.reserve(patch_keys.size());

    for (const auto& asset_key : patch_keys) {
      const auto source_it = source_catalog_entries.find(asset_key);
      const auto base_it = base_catalog_entries.find(asset_key);
      const auto in_source = source_it != source_catalog_entries.end();
      const auto in_base = base_it != base_catalog_entries.end();

      auto action = pak::PakPatchAction::kUnchanged;
      auto asset_type = data::AssetType::kUnknown;
      if (in_source) {
        asset_type = source_it->second.asset_type;
      } else if (in_base) {
        asset_type = base_it->second.asset_type;
      }

      if (in_source && !in_base) {
        action = pak::PakPatchAction::kCreate;
      } else if (!in_source && in_base) {
        action = pak::PakPatchAction::kDelete;
      } else if (in_source && in_base) {
        const auto descriptor_equal
          = source_it->second.descriptor_digest == base_it->second.descriptor_digest;
        const auto transitive_equal = source_it->second.transitive_resource_digest
          == base_it->second.transitive_resource_digest;
        action = (descriptor_equal && transitive_equal)
          ? pak::PakPatchAction::kUnchanged
          : pak::PakPatchAction::kReplace;
      }

      state.data_plan.patch_actions.push_back(pak::PakPatchActionRecord {
        .asset_key = asset_key,
        .asset_type = asset_type,
        .action = action,
      });

      if (action == pak::PakPatchAction::kCreate
        || action == pak::PakPatchAction::kReplace) {
        emitted_patch_keys.insert(asset_key);
      }
    }

    std::erase_if(assets, [&emitted_patch_keys](const AggregatedAsset& asset) {
      return !emitted_patch_keys.contains(asset.key);
    });
  }

  for (const auto& asset : assets) {
    if (!asset.virtual_path.empty()) {
      state.browse_map[asset.virtual_path] = asset.key;
    }
  }

  if (state.policy.mode == pak::PakPlanMode::kPatch) {
    std::erase_if(state.browse_map, [&emitted_patch_keys](const auto& pair) {
      return !emitted_patch_keys.contains(pair.second);
    });
  }
}

auto ValidatePatchClassificationInvariants(PlanningState& state) -> void
{
  std::unordered_map<data::AssetKey, pak::PakPatchAction> action_by_key;
  for (const auto& action : state.data_plan.patch_actions) {
    const auto [_, inserted] = action_by_key.emplace(action.asset_key, action.action);
    EnforceStageInvariant(state, inserted,
      "pak.plan.stage.patch.duplicate_action_key",
      "Patch classification produced duplicate action keys.");
  }

  if (state.policy.mode != pak::PakPlanMode::kPatch) {
    return;
  }

  for (const auto& asset : state.assets) {
    const auto it = action_by_key.find(asset.key);
    if (!CheckStageInvariant(state, it != action_by_key.end(),
          "pak.plan.stage.patch.missing_action_for_emitted_asset",
          "Patch classification missing action for emitted asset.")) {
      continue;
    }

    const auto emitted_action = it->second == pak::PakPatchAction::kCreate
      || it->second == pak::PakPatchAction::kReplace;
    EnforceStageInvariant(state, emitted_action,
      "pak.plan.stage.patch.non_emitted_action_retained",
      "Patch asset emission retained key without Create/Replace action.");
  }
}

auto FinalizeScriptAndTables(PlanningState& state) -> void
{
  using pak::PakBuildPhase;
  using pak::PakDiagnosticSeverity;

  auto& diagnostics = state.output.diagnostics;

  std::ranges::sort(state.script_param_ranges,
    [](const pak::PakScriptParamRangePlan& lhs,
      const pak::PakScriptParamRangePlan& rhs) {
      return lhs.slot_index < rhs.slot_index;
    });

  uint32_t max_script_param_end = state.script_param_record_count;
  for (const auto& range : state.script_param_ranges) {
    uint64_t end = 0;
    if (!SafeAdd(range.params_array_offset, range.params_count, end)
      || end > kMaxCountAsUint64) {
      AddDiagnostic(diagnostics, PakDiagnosticSeverity::kError,
        PakBuildPhase::kPlanning, "pak.plan.script_param_count_overflow",
        "Script param range overflows uint32 bounds.");
      continue;
    }
    max_script_param_end = (std::max)(max_script_param_end, static_cast<uint32_t>(end));
  }

  state.data_plan.script_param_record_count = max_script_param_end;
  state.data_plan.script_param_ranges = std::move(state.script_param_ranges);

  SetTableCount(state.data_plan.tables, "texture_table",
    state.table_counts.texture_count, diagnostics);
  SetTableCount(state.data_plan.tables, "buffer_table",
    state.table_counts.buffer_count, diagnostics);
  SetTableCount(state.data_plan.tables, "audio_table",
    state.table_counts.audio_count, diagnostics);
  SetTableCount(state.data_plan.tables, "script_resource_table",
    state.table_counts.script_resource_count, diagnostics);
  SetTableCount(state.data_plan.tables, "script_slot_table",
    state.table_counts.script_slot_count, diagnostics);
  SetTableCount(state.data_plan.tables, "physics_resource_table",
    state.table_counts.physics_count, diagnostics);
  ApplyIndexZeroPolicy(state.data_plan.tables);
}

auto ValidateScriptAndTableInvariants(PlanningState& state) -> void
{
  const auto ranges = std::span<const pak::PakScriptParamRangePlan>(
    state.data_plan.script_param_ranges.data(),
    state.data_plan.script_param_ranges.size());
  for (size_t i = 1; i < ranges.size(); ++i) {
    const auto sorted = ranges[i - 1].slot_index <= ranges[i].slot_index;
    EnforceStageInvariant(state, sorted,
      "pak.plan.stage.script.slot_ranges_unsorted",
      "Script param ranges are not sorted by slot_index.");
  }

  const auto has_core_table_count = state.data_plan.tables.size() >= 6U;
  EnforceStageInvariant(state, has_core_table_count,
    "pak.plan.stage.tables.core_tables_missing",
    "Planner table set is missing one or more core tables.");
}

auto PlanFileLayout(PlanningState& state) -> void
{
  std::ranges::stable_sort(state.pending_resources,
    [](const PendingResource& lhs, const PendingResource& rhs) {
      const auto lhs_order = RegionOrder(lhs.region_name);
      const auto rhs_order = RegionOrder(rhs.region_name);
      if (lhs_order != rhs_order) {
        return lhs_order < rhs_order;
      }
      if (lhs.source_order != rhs.source_order) {
        return lhs.source_order < rhs.source_order;
      }
      return lhs.path.generic_string() < rhs.path.generic_string();
    });

  uint64_t cursor = state.data_plan.header.size_bytes;
  state.data_plan.regions.clear();
  state.data_plan.resources.clear();
  const std::array<std::string_view, 5> region_names = { "texture_region",
    "buffer_region", "audio_region", "script_region", "physics_region" };

  std::unordered_map<std::string, uint32_t> resource_index_counters;
  for (const auto region_name : region_names) {
    cursor = AlignUp(cursor, AlignmentBytes { kRegionAlignment });
    const auto region_start = cursor;

    for (const auto& resource : state.pending_resources) {
      if (resource.region_name != region_name) {
        continue;
      }

      cursor = AlignUp(cursor, AlignmentBytes { resource.alignment });
      state.data_plan.resources.push_back(pak::PakResourcePlacementPlan {
        .resource_kind = resource.resource_kind,
        .resource_index = resource_index_counters[resource.resource_kind]++,
        .region_name = resource.region_name,
        .offset = cursor,
        .size_bytes = resource.size_bytes,
        .alignment = resource.alignment,
        .reserved_bytes_zeroed = true,
      });
      cursor += resource.size_bytes;
    }

    state.data_plan.regions.push_back(pak::PakRegionPlan {
      .region_name = std::string(region_name),
      .offset = region_start,
      .size_bytes = cursor - region_start,
      .alignment = kRegionAlignment,
    });
  }

  for (auto& table : state.data_plan.tables) {
    cursor = AlignUp(cursor, AlignmentBytes { table.alignment });
    table.offset = cursor;
    table.size_bytes = static_cast<uint64_t>(table.count) * table.entry_size;
    cursor += table.size_bytes;
  }

  state.data_plan.assets.clear();
  state.data_plan.directory.entries.clear();
  for (const auto& asset : state.assets) {
    cursor = AlignUp(cursor, AlignmentBytes { kAssetAlignment });

    if (asset.descriptor_size > (std::numeric_limits<uint32_t>::max)()) {
      AddDiagnostic(state.output.diagnostics, pak::PakDiagnosticSeverity::kError,
        pak::PakBuildPhase::kPlanning, "pak.plan.asset_descriptor_too_large",
        "Asset descriptor size exceeds uint32 range.", asset.descriptor_path);
      continue;
    }

    state.data_plan.assets.push_back(pak::PakAssetPlacementPlan {
      .asset_key = asset.key,
      .asset_type = asset.asset_type,
      .offset = cursor,
      .size_bytes = asset.descriptor_size,
      .alignment = kAssetAlignment,
      .reserved_bytes_zeroed = true,
    });
    state.data_plan.directory.entries.push_back(pak::PakAssetDirectoryEntryPlan {
      .asset_key = asset.key,
      .asset_type = asset.asset_type,
      .entry_offset = 0,
      .descriptor_offset = cursor,
      .descriptor_size = static_cast<uint32_t>(asset.descriptor_size),
    });
    cursor += asset.descriptor_size;
  }

  cursor = AlignUp(cursor, AlignmentBytes { kDirectoryAlignment });
  state.data_plan.directory.offset = cursor;
  state.data_plan.directory.size_bytes
    = static_cast<uint64_t>(state.data_plan.directory.entries.size())
    * sizeof(core::AssetDirectoryEntry);
  for (size_t i = 0; i < state.data_plan.directory.entries.size(); ++i) {
    state.data_plan.directory.entries[i].entry_offset
      = state.data_plan.directory.offset + (i * sizeof(core::AssetDirectoryEntry));
  }
  cursor += state.data_plan.directory.size_bytes;

  state.data_plan.browse_index = pak::PakBrowseIndexPlan {
    .enabled = false,
    .offset = 0,
    .size_bytes = 0,
    .virtual_paths = {},
  };
  if (state.request->options.embed_browse_index && !state.browse_map.empty()) {
    std::vector<std::string> browse_paths;
    browse_paths.reserve(state.browse_map.size());
    for (const auto& entry : state.browse_map) {
      browse_paths.push_back(entry.first);
    }
    std::ranges::sort(browse_paths);

    uint64_t string_table_size = 0;
    for (const auto& path : browse_paths) {
      string_table_size += path.size();
    }

    const auto browse_payload_size
      = static_cast<uint64_t>(sizeof(core::PakBrowseIndexHeader))
      + (static_cast<uint64_t>(browse_paths.size())
        * sizeof(core::PakBrowseIndexEntry))
      + string_table_size;

    cursor = AlignUp(cursor, AlignmentBytes { kBrowseAlignment });
    state.data_plan.browse_index = pak::PakBrowseIndexPlan {
      .enabled = true,
      .offset = cursor,
      .size_bytes = browse_payload_size,
      .virtual_paths = std::move(browse_paths),
    };
    cursor += browse_payload_size;
  }

  cursor = AlignUp(cursor, AlignmentBytes { kFooterAlignment });
  state.data_plan.footer.offset = cursor;
  state.data_plan.footer.size_bytes = static_cast<uint32_t>(sizeof(core::PakFooter));
  state.data_plan.footer.crc32_field_absolute_offset
    = cursor + offsetof(core::PakFooter, pak_crc32);
  state.data_plan.planned_file_size
    = cursor + static_cast<uint64_t>(sizeof(core::PakFooter));
}

auto ValidateLayoutInvariants(PlanningState& state) -> void
{
  const auto entries_match_assets
    = state.data_plan.directory.entries.size() == state.data_plan.assets.size();
  EnforceStageInvariant(state, entries_match_assets,
    "pak.plan.stage.layout.directory_asset_count_mismatch",
    "Directory entry count does not match planned asset count.");

  const auto pair_count = (std::min)(
    state.data_plan.directory.entries.size(), state.data_plan.assets.size());
  for (size_t i = 0; i < pair_count; ++i) {
    const auto key_matches
      = state.data_plan.directory.entries[i].asset_key == state.data_plan.assets[i].asset_key;
    EnforceStageInvariant(state, key_matches,
      "pak.plan.stage.layout.directory_asset_key_mismatch",
      "Directory entry asset key does not match planned asset key.");
  }

  const auto footer_end
    = state.data_plan.footer.offset + static_cast<uint64_t>(state.data_plan.footer.size_bytes);
  const auto size_covers_footer = state.data_plan.planned_file_size >= footer_end;
  EnforceStageInvariant(state, size_covers_footer,
    "pak.plan.stage.layout.file_size_before_footer_end",
    "Planned file size is smaller than footer end.");
}

auto ValidateAndFinalizeResult(PlanningState& state) -> void
{
  const auto validation
    = pak::PakValidation::Validate(pak::PakPlan(state.data_plan), state.policy, *state.request);
  state.output.diagnostics.insert(state.output.diagnostics.end(),
    validation.diagnostics.begin(), validation.diagnostics.end());

  std::ranges::sort(state.output.diagnostics,
    [](const pak::PakDiagnostic& lhs, const pak::PakDiagnostic& rhs) {
      if (lhs.code != rhs.code) {
        return lhs.code < rhs.code;
      }
      if (lhs.phase != rhs.phase) {
        return static_cast<uint8_t>(lhs.phase) < static_cast<uint8_t>(rhs.phase);
      }
      if (lhs.asset_key != rhs.asset_key) {
        return lhs.asset_key < rhs.asset_key;
      }
      return lhs.message < rhs.message;
    });

  const auto has_error
    = std::ranges::any_of(state.output.diagnostics,
      [](const pak::PakDiagnostic& diagnostic) {
        return diagnostic.severity == pak::PakDiagnosticSeverity::kError;
      });

  if (has_error) {
    return;
  }

  state.output.plan = pak::PakPlan(std::move(state.data_plan));
  state.output.summary.assets_processed
    = static_cast<uint32_t>(state.output.plan->Assets().size());
  state.output.summary.resources_processed
    = static_cast<uint32_t>(state.output.plan->Resources().size());

  for (const auto& action : state.output.plan->PatchActions()) {
    switch (action.action) {
    case pak::PakPatchAction::kCreate:
      ++state.output.summary.patch_created;
      break;
    case pak::PakPatchAction::kReplace:
      ++state.output.summary.patch_replaced;
      break;
    case pak::PakPatchAction::kDelete:
      ++state.output.summary.patch_deleted;
      break;
    case pak::PakPatchAction::kUnchanged:
      ++state.output.summary.patch_unchanged;
      break;
    }
  }
}

auto ValidateFinalizeInvariants(PlanningState& state) -> void
{
  const auto has_errors = HasPlanningErrors(state);
  if (state.output.plan.has_value()) {
    EnforceStageInvariant(state, !has_errors,
      "pak.plan.stage.finalize.plan_with_errors",
      "Finalized output contains a plan while errors are present.");
  }
}

} // namespace

namespace oxygen::content::pak {

auto PakPlanBuilder::Build(const PakBuildRequest& request) const -> BuildResult
{
  const auto start = std::chrono::steady_clock::now();
  PlanningState state(request);

  RunStage(state, "ValidatePlanningRequest",
    "pak.plan.stage.validate_request_exception",
    [&state]() { ValidatePlanningRequest(state); });
  RunStage(state, "CollectSourceData", "pak.plan.stage.collect_exception",
    [&state]() { CollectSourceData(state); });
  RunStage(state, "ValidateCollectSourceDataInvariants",
    "pak.plan.stage.collect_invariants_exception",
    [&state]() { ValidateCollectSourceDataInvariants(state); });
  RunStage(state, "ClassifyPatchActionsAndFinalizeBrowse",
    "pak.plan.stage.patch_classify_exception",
    [&state]() { ClassifyPatchActionsAndFinalizeBrowse(state); });
  RunStage(state, "ValidatePatchClassificationInvariants",
    "pak.plan.stage.patch_invariants_exception",
    [&state]() { ValidatePatchClassificationInvariants(state); });
  RunStage(state, "FinalizeScriptAndTables",
    "pak.plan.stage.script_tables_exception",
    [&state]() { FinalizeScriptAndTables(state); });
  RunStage(state, "ValidateScriptAndTableInvariants",
    "pak.plan.stage.script_tables_invariants_exception",
    [&state]() { ValidateScriptAndTableInvariants(state); });
  RunStage(state, "PlanFileLayout", "pak.plan.stage.layout_exception",
    [&state]() { PlanFileLayout(state); });
  RunStage(state, "ValidateLayoutInvariants",
    "pak.plan.stage.layout_invariants_exception",
    [&state]() { ValidateLayoutInvariants(state); });
  RunStage(state, "ValidateAndFinalizeResult",
    "pak.plan.stage.finalize_exception",
    [&state]() { ValidateAndFinalizeResult(state); });
  RunStage(state, "ValidateFinalizeInvariants",
    "pak.plan.stage.finalize_invariants_exception",
    [&state]() { ValidateFinalizeInvariants(state); });

  state.output.planning_duration
    = std::chrono::duration_cast<std::chrono::microseconds>(
      std::chrono::steady_clock::now() - start);

  return state.output;
}

} // namespace oxygen::content::pak
