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

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Content/PakFile.h>
#include <Oxygen/Cooker/Loose/Inspection.h>
#include <Oxygen/Cooker/Pak/PakMeasureStore.h>
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

constexpr uint32_t kRegionAlignment = 256U;
constexpr uint32_t kTableAlignment = 16U;
constexpr uint32_t kAssetAlignment = 16U;
constexpr uint32_t kDirectoryAlignment = 16U;
constexpr uint32_t kBrowseAlignment = 16U;
constexpr uint32_t kFooterAlignment = 16U;
constexpr uint64_t kMaxCountAsUint64 = (std::numeric_limits<uint32_t>::max)();
constexpr int kUnknownRegionOrder = (std::numeric_limits<int>::max)();
constexpr std::string_view kPatchDiffBasisIdentifier
  = "descriptor_plus_transitive_resources_v1";

struct AlignmentBytes final {
  uint32_t value = 1U;
};

struct AggregatedAsset {
  data::AssetKey key {};
  data::AssetType asset_type = data::AssetType::kUnknown;
  std::filesystem::path descriptor_path;
  uint64_t descriptor_source_offset = 0;
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
  uint64_t source_offset = 0;
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

struct SourceContribution final {
  TableCounts table_counts {};
  std::vector<pak::PakScriptParamRangePlan> local_script_param_ranges;
  uint32_t script_param_record_count = 0;
  std::vector<std::pair<uint16_t, oxygen::base::Sha256Digest>>
    transitive_inputs;
  oxygen::base::Sha256Digest transitive_resource_digest {};
};

struct PatchCompatibilityEnvelopeData final {
  std::vector<data::SourceKey> required_base_source_keys;
  std::vector<uint16_t> required_base_content_versions;
  std::vector<oxygen::base::Sha256Digest> required_base_catalog_digests;
  uint16_t patch_content_version = 0;
};

struct PatchCompatibilityPolicySnapshotData final {
  bool require_exact_base_set = true;
  bool require_content_version_match = true;
  bool require_base_source_key_match = true;
  bool require_catalog_digest_match = true;
};

struct ScriptSlotReadContext final {
  uint32_t slot_index_base = 0;
  uint32_t params_array_index_base = 0;
  uint32_t source_params_record_count = 0;
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
  return lhs < rhs;
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

auto EmptyDigest() -> oxygen::base::Sha256Digest
{
  static const auto digest
    = oxygen::base::ComputeSha256(std::span<const std::byte> {});
  return digest;
}

auto AggregateTransitiveDigest(
  std::span<const std::pair<uint16_t, oxygen::base::Sha256Digest>> inputs)
  -> oxygen::base::Sha256Digest
{
  auto hasher = oxygen::base::Sha256 {};
  for (const auto& [kind, digest] : inputs) {
    hasher.Update(std::as_bytes(std::span(&kind, 1)));
    hasher.Update(std::as_bytes(std::span(digest)));
  }
  return hasher.Finalize();
}

auto AddTransitiveInputDigest(SourceContribution& contribution,
  const uint16_t kind, const std::filesystem::path& file_path,
  std::vector<pak::PakDiagnostic>& diagnostics) -> void
{
  try {
    contribution.transitive_inputs.emplace_back(
      kind, oxygen::base::ComputeFileSha256(file_path));
  } catch (const std::exception& ex) {
    AddDiagnostic(diagnostics, pak::PakDiagnosticSeverity::kError,
      pak::PakBuildPhase::kPlanning, "pak.plan.transitive_digest_input_failed",
      std::string("Failed to compute transitive digest input: ") + ex.what(),
      file_path);
  }
}

auto SortAndUniqueSourceKeys(std::vector<data::SourceKey>& keys) -> void
{
  std::ranges::sort(keys);
  keys.erase(std::ranges::unique(keys).begin(), keys.end());
}

auto SortAndUniqueDigests(std::vector<oxygen::base::Sha256Digest>& digests)
  -> void
{
  std::ranges::sort(digests);
  digests.erase(std::ranges::unique(digests).begin(), digests.end());
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
  const std::filesystem::path& scripts_table_path,
  const ScriptSlotReadContext& context,
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

    const auto local_params_array_offset
      = static_cast<uint64_t>(record.params_array_offset / kParamRecordSize);
    uint64_t local_params_array_end = 0;
    if (!SafeAdd(local_params_array_offset, record.params_count,
          local_params_array_end)
      || local_params_array_end > context.source_params_record_count) {
      AddDiagnostic(diagnostics, pak::PakDiagnosticSeverity::kError,
        pak::PakBuildPhase::kPlanning,
        "pak.plan.script_params_range_out_of_bounds",
        "ScriptSlotRecord params range exceeds scripts.data bounds for source.",
        scripts_table_path);
      continue;
    }

    uint64_t global_params_array_offset = 0;
    if (!SafeAdd(context.params_array_index_base, local_params_array_offset,
          global_params_array_offset)
      || global_params_array_offset > kMaxCountAsUint64) {
      AddDiagnostic(diagnostics, pak::PakDiagnosticSeverity::kError,
        pak::PakBuildPhase::kPlanning, "pak.plan.script_params_offset_overflow",
        "ScriptSlotRecord params offset overflowed global script param index.",
        scripts_table_path);
      continue;
    }

    uint64_t slot_index64 = 0;
    if (!SafeAdd(context.slot_index_base, i, slot_index64)
      || slot_index64 > kMaxCountAsUint64) {
      AddDiagnostic(diagnostics, pak::PakDiagnosticSeverity::kError,
        pak::PakBuildPhase::kPlanning, "pak.plan.script_slot_index_overflow",
        "ScriptSlotRecord slot index overflowed uint32 bounds.",
        scripts_table_path);
      continue;
    }

    ranges.push_back(pak::PakScriptParamRangePlan {
      .slot_index = static_cast<uint32_t>(slot_index64),
      .params_array_offset = static_cast<uint32_t>(global_params_array_offset),
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
    .entries = {},
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

auto ComputeDescriptorDigestFromFile(
  const std::filesystem::path& descriptor_path,
  std::vector<pak::PakDiagnostic>& diagnostics)
  -> std::optional<oxygen::base::Sha256Digest>
{
  try {
    return oxygen::base::ComputeFileSha256(descriptor_path);
  } catch (const std::exception& ex) {
    AddDiagnostic(diagnostics, pak::PakDiagnosticSeverity::kError,
      pak::PakBuildPhase::kPlanning,
      "pak.plan.descriptor_digest_compute_failed",
      std::string("Failed to compute descriptor digest: ") + ex.what(),
      descriptor_path);
    return std::nullopt;
  }
}

auto ComputeDescriptorDigestFromPakEntry(const content::PakFile& pak_file,
  const core::AssetDirectoryEntry& entry,
  const std::filesystem::path& source_path,
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
    return oxygen::base::ComputeSha256(
      std::span<const std::byte>(*blob_result));
  } catch (const std::exception& ex) {
    AddDiagnostic(diagnostics, pak::PakDiagnosticSeverity::kError,
      pak::PakBuildPhase::kPlanning, "pak.plan.pak_descriptor_read_failed",
      std::string("Failed to read descriptor bytes from pak source: ")
        + ex.what(),
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
    : request(oxygen::observer_ptr<const pak::PakBuildRequest> {
        std::addressof(request_in) })
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
  std::unordered_map<size_t, SourceContribution> source_contributions;
  std::vector<size_t> included_source_orders;
  std::vector<size_t> planned_resource_source_orders;
  std::vector<pak::PakScriptParamRangePlan> script_param_ranges;
  std::unordered_map<std::string, data::AssetKey> browse_map;
  TableCounts table_counts {};
  uint32_t script_param_record_count = 0;

  PatchCompatibilityEnvelopeData patch_compatibility_envelope {};
  PatchCompatibilityPolicySnapshotData patch_compatibility_policy_snapshot {};
  std::string patch_diff_basis_identifier
    = std::string(kPatchDiffBasisIdentifier);
  bool patch_manifest_basis_ready = false;
};

auto HasPlanningErrors(const PlanningState& state) -> bool
{
  return std::ranges::any_of(
    state.output.diagnostics, [](const pak::PakDiagnostic& diagnostic) {
      return diagnostic.severity == pak::PakDiagnosticSeverity::kError;
    });
}

auto AddStageInvariantDiagnostic(PlanningState& state,
  const std::string_view code, const std::string_view message) -> void
{
  AddDiagnostic(state.output.diagnostics, pak::PakDiagnosticSeverity::kError,
    pak::PakBuildPhase::kPlanning, code, message);
}

[[nodiscard]] auto CheckStageInvariant(PlanningState& state,
  const bool condition, const std::string_view code,
  const std::string_view message) -> bool
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

  if (request.source_key.IsNil()) {
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
          "Source collection produced an out-of-range asset_positions "
          "index.")) {
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
    auto& source_contribution = state.source_contributions[source_order];
    auto source_asset_keys = std::vector<data::AssetKey> {};

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
          .descriptor_source_offset = 0U,
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
        source_asset_keys.push_back(source_asset.key);
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

      struct ScriptTableCandidate final {
        std::filesystem::path path;
        uint64_t size_bytes = 0;
        bool resource_compatible = false;
        bool parse_as_slots = false;
      };

      auto script_table_candidates = std::vector<ScriptTableCandidate> {};
      auto script_data_files
        = std::vector<std::pair<std::filesystem::path, uint64_t>> {};
      bool source_uses_script_slot_layout = false;

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
          AddTransitiveInputDigest(source_contribution,
            static_cast<uint16_t>(file_entry.kind), file_path, diagnostics);
          state.pending_resources.push_back(PendingResource {
            .region_name = "texture_region",
            .resource_kind = "texture",
            .size_bytes = file_size,
            .source_offset = 0U,
            .alignment = kRegionAlignment,
            .source_order = source_order,
            .path = file_path,
          });
          break;
        case data::loose_cooked::FileKind::kBuffersData:
          AddTransitiveInputDigest(source_contribution,
            static_cast<uint16_t>(file_entry.kind), file_path, diagnostics);
          state.pending_resources.push_back(PendingResource {
            .region_name = "buffer_region",
            .resource_kind = "buffer",
            .size_bytes = file_size,
            .source_offset = 0U,
            .alignment = kRegionAlignment,
            .source_order = source_order,
            .path = file_path,
          });
          break;
        case data::loose_cooked::FileKind::kScriptsData:
          AddTransitiveInputDigest(source_contribution,
            static_cast<uint16_t>(file_entry.kind), file_path, diagnostics);
          script_data_files.emplace_back(file_path, file_size);
          break;
        case data::loose_cooked::FileKind::kPhysicsData:
          AddTransitiveInputDigest(source_contribution,
            static_cast<uint16_t>(file_entry.kind), file_path, diagnostics);
          state.pending_resources.push_back(PendingResource {
            .region_name = "physics_region",
            .resource_kind = "physics",
            .size_bytes = file_size,
            .source_offset = 0U,
            .alignment = kRegionAlignment,
            .source_order = source_order,
            .path = file_path,
          });
          break;
        case data::loose_cooked::FileKind::kTexturesTable:
          AddTransitiveInputDigest(source_contribution,
            static_cast<uint16_t>(file_entry.kind), file_path, diagnostics);
          AccumulateTableCountFromFile(file_path, file_size,
            sizeof(render::TextureResourceDesc),
            source_contribution.table_counts.texture_count, diagnostics,
            "pak.plan.texture_table_size_invalid");
          break;
        case data::loose_cooked::FileKind::kBuffersTable:
          AddTransitiveInputDigest(source_contribution,
            static_cast<uint16_t>(file_entry.kind), file_path, diagnostics);
          AccumulateTableCountFromFile(file_path, file_size,
            sizeof(core::BufferResourceDesc),
            source_contribution.table_counts.buffer_count, diagnostics,
            "pak.plan.buffer_table_size_invalid");
          break;
        case data::loose_cooked::FileKind::kPhysicsTable:
          AddTransitiveInputDigest(source_contribution,
            static_cast<uint16_t>(file_entry.kind), file_path, diagnostics);
          AccumulateTableCountFromFile(file_path, file_size,
            sizeof(physics::PhysicsResourceDesc),
            source_contribution.table_counts.physics_count, diagnostics,
            "pak.plan.physics_table_size_invalid");
          break;
        case data::loose_cooked::FileKind::kScriptsTable: {
          AddTransitiveInputDigest(source_contribution,
            static_cast<uint16_t>(file_entry.kind), file_path, diagnostics);
          const auto slot_compatible
            = (file_size % sizeof(script::ScriptSlotRecord)) == 0U;
          const auto resource_compatible
            = (file_size % sizeof(script::ScriptResourceDesc)) == 0U;

          const bool parse_as_slots = slot_compatible
            && (source_has_scene_assets || !resource_compatible);
          script_table_candidates.push_back(ScriptTableCandidate {
            .path = file_path,
            .size_bytes = file_size,
            .resource_compatible = resource_compatible,
            .parse_as_slots = parse_as_slots,
          });
          source_uses_script_slot_layout
            = source_uses_script_slot_layout || parse_as_slots;

          if (source_has_scene_assets && source_has_script_assets
            && slot_compatible && resource_compatible) {
            AddDiagnostic(diagnostics, PakDiagnosticSeverity::kWarning,
              PakBuildPhase::kPlanning,
              "pak.plan.scripts_table_ambiguous_layout",
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

      uint32_t source_script_param_record_count = 0;
      for (const auto& [scripts_data_path, scripts_data_size] :
        script_data_files) {
        state.pending_resources.push_back(PendingResource {
          .region_name = "script_region",
          .resource_kind = "script",
          .size_bytes = scripts_data_size,
          .source_offset = 0U,
          .alignment = kRegionAlignment,
          .source_order = source_order,
          .path = scripts_data_path,
        });

        if (!source_uses_script_slot_layout) {
          continue;
        }

        if ((scripts_data_size % sizeof(script::ScriptParamRecord)) != 0U) {
          AddDiagnostic(diagnostics, PakDiagnosticSeverity::kError,
            PakBuildPhase::kPlanning,
            "pak.plan.script_params_file_size_invalid",
            "scripts.data size is not divisible by ScriptParamRecord size.",
            scripts_data_path);
          continue;
        }

        const auto record_count64
          = scripts_data_size / sizeof(script::ScriptParamRecord);
        if (record_count64 > kMaxCountAsUint64) {
          AddDiagnostic(diagnostics, PakDiagnosticSeverity::kError,
            PakBuildPhase::kPlanning, "pak.plan.script_params_count_too_large",
            "scripts.data contains too many ScriptParamRecord entries.",
            scripts_data_path);
          continue;
        }

        uint64_t source_count_sum = 0;
        if (!SafeAdd(source_script_param_record_count, record_count64,
              source_count_sum)
          || source_count_sum > kMaxCountAsUint64) {
          AddDiagnostic(diagnostics, PakDiagnosticSeverity::kError,
            PakBuildPhase::kPlanning, "pak.plan.script_params_count_overflow",
            "Combined scripts.data ScriptParamRecord count overflowed uint32.",
            scripts_data_path);
          continue;
        }
        source_script_param_record_count
          = static_cast<uint32_t>(source_count_sum);
      }

      for (const auto& script_table : script_table_candidates) {
        if (script_table.parse_as_slots) {
          const auto slot_context = ScriptSlotReadContext {
            .slot_index_base = static_cast<uint32_t>(
              source_contribution.local_script_param_ranges.size()),
            .params_array_index_base = 0U,
            .source_params_record_count = source_script_param_record_count,
          };
          const auto parsed_slots
            = ReadScriptSlotRangesFromTable(script_table.path, slot_context,
              source_contribution.local_script_param_ranges, diagnostics);
          source_contribution.table_counts.script_slot_count += parsed_slots;
          continue;
        }

        if (script_table.resource_compatible) {
          source_contribution.table_counts.script_resource_count
            += script_table.size_bytes / sizeof(script::ScriptResourceDesc);
          continue;
        }

        AddDiagnostic(diagnostics, PakDiagnosticSeverity::kError,
          PakBuildPhase::kPlanning, "pak.plan.scripts_table_size_invalid",
          "scripts.table is incompatible with known script table layouts.",
          script_table.path);
      }

      if (source_uses_script_slot_layout) {
        source_contribution.script_param_record_count
          = source_script_param_record_count;
      }

      std::ranges::sort(source_contribution.transitive_inputs,
        [](const auto& lhs, const auto& rhs) {
          if (lhs.first != rhs.first) {
            return lhs.first < rhs.first;
          }
          return lhs.second < rhs.second;
        });
      source_contribution.transitive_resource_digest
        = source_contribution.transitive_inputs.empty()
        ? EmptyDigest()
        : AggregateTransitiveDigest(
            std::span<const std::pair<uint16_t, oxygen::base::Sha256Digest>>(
              source_contribution.transitive_inputs.data(),
              source_contribution.transitive_inputs.size()));

      for (const auto& key : source_asset_keys) {
        const auto position_it = state.asset_positions.find(key);
        if (position_it == state.asset_positions.end()) {
          continue;
        }
        state.assets[position_it->second].transitive_resource_digest
          = source_contribution.transitive_resource_digest;
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
            "PAK source directory entry has an invalid asset_type.",
            source_root);
          continue;
        }

        const auto descriptor_digest = ComputeDescriptorDigestFromPakEntry(
          *pak_file, entry, source_root, diagnostics);
        if (!descriptor_digest.has_value()) {
          continue;
        }

        AggregatedAsset aggregated {
          .key = entry.asset_key,
          .asset_type = entry.asset_type,
          .descriptor_path = source_root,
          .descriptor_source_offset = entry.desc_offset,
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

  std::ranges::sort(
    assets, [](const AggregatedAsset& lhs, const AggregatedAsset& rhs) {
      return IsAssetKeyLess(lhs.key, rhs.key);
    });

  std::unordered_map<data::AssetKey, data::PakCatalogEntry>
    source_catalog_entries;
  source_catalog_entries.reserve(assets.size());
  for (const auto& asset : assets) {
    source_catalog_entries[asset.key] = ToCatalogEntry(asset);
  }

  std::unordered_map<data::AssetKey, data::PakCatalogEntry>
    base_catalog_entries;
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
        const auto descriptor_equal = source_it->second.descriptor_digest
          == base_it->second.descriptor_digest;
        const auto transitive_equal
          = source_it->second.transitive_resource_digest
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

  auto browse_assets
    = std::vector<std::reference_wrapper<const AggregatedAsset>> {};
  browse_assets.reserve(assets.size());
  for (const auto& asset : assets) {
    browse_assets.emplace_back(asset);
  }
  std::ranges::stable_sort(
    browse_assets, [](const auto& lhs_ref, const auto& rhs_ref) {
      const auto& lhs = lhs_ref.get();
      const auto& rhs = rhs_ref.get();
      if (lhs.source_order != rhs.source_order) {
        return lhs.source_order < rhs.source_order;
      }
      return IsAssetKeyLess(lhs.key, rhs.key);
    });
  for (const auto& asset_ref : browse_assets) {
    const auto& asset = asset_ref.get();
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
    const auto [_, inserted]
      = action_by_key.emplace(action.asset_key, action.action);
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

auto CollectIncludedSourceOrders(PlanningState& state) -> void
{
  state.included_source_orders.clear();
  if (state.policy.mode == pak::PakPlanMode::kPatch) {
    for (const auto& asset : state.assets) {
      state.included_source_orders.push_back(asset.source_order);
    }
  } else {
    state.included_source_orders.reserve(state.source_contributions.size());
    for (const auto& [source_order, _] : state.source_contributions) {
      state.included_source_orders.push_back(source_order);
    }
  }

  std::ranges::sort(state.included_source_orders);
  state.included_source_orders.erase(
    std::ranges::unique(state.included_source_orders).begin(),
    state.included_source_orders.end());
}

auto RebuildPatchLocalContributions(PlanningState& state) -> void
{
  using pak::PakBuildPhase;
  using pak::PakDiagnosticSeverity;

  CollectIncludedSourceOrders(state);
  const auto included_source_orders = std::unordered_set<size_t>(
    state.included_source_orders.begin(), state.included_source_orders.end());

  std::erase_if(state.pending_resources,
    [&included_source_orders](const PendingResource& resource) {
      return !included_source_orders.contains(resource.source_order);
    });

  state.table_counts = {};
  state.script_param_ranges.clear();
  state.script_param_record_count = 0;

  uint64_t slot_index_base = 0;
  for (const auto source_order : state.included_source_orders) {
    const auto contribution_it = state.source_contributions.find(source_order);
    if (!CheckStageInvariant(state,
          contribution_it != state.source_contributions.end(),
          "pak.plan.stage.patch.missing_source_contribution",
          "Included source order has no source contribution record.")) {
      continue;
    }

    const auto& contribution = contribution_it->second;
    const auto accumulate_count
      = [&state](const uint64_t addend, uint64_t& target,
          const std::string_view code, const std::string_view message) -> void {
      uint64_t sum = 0;
      if (!SafeAdd(target, addend, sum) || sum > kMaxCountAsUint64) {
        AddDiagnostic(state.output.diagnostics, PakDiagnosticSeverity::kError,
          PakBuildPhase::kPlanning, code, message);
        target = kMaxCountAsUint64;
        return;
      }
      target = sum;
    };

    accumulate_count(contribution.table_counts.texture_count,
      state.table_counts.texture_count, "pak.plan.table_count_overflow.texture",
      "Patch-local texture table count overflowed uint32 range.");
    accumulate_count(contribution.table_counts.buffer_count,
      state.table_counts.buffer_count, "pak.plan.table_count_overflow.buffer",
      "Patch-local buffer table count overflowed uint32 range.");
    accumulate_count(contribution.table_counts.audio_count,
      state.table_counts.audio_count, "pak.plan.table_count_overflow.audio",
      "Patch-local audio table count overflowed uint32 range.");
    accumulate_count(contribution.table_counts.script_resource_count,
      state.table_counts.script_resource_count,
      "pak.plan.table_count_overflow.script_resource",
      "Patch-local script resource table count overflowed uint32 range.");
    accumulate_count(contribution.table_counts.script_slot_count,
      state.table_counts.script_slot_count,
      "pak.plan.table_count_overflow.script_slot",
      "Patch-local script slot table count overflowed uint32 range.");
    accumulate_count(contribution.table_counts.physics_count,
      state.table_counts.physics_count, "pak.plan.table_count_overflow.physics",
      "Patch-local physics table count overflowed uint32 range.");

    for (const auto& local_range : contribution.local_script_param_ranges) {
      uint64_t global_slot_index = 0;
      if (!SafeAdd(slot_index_base, local_range.slot_index, global_slot_index)
        || global_slot_index > kMaxCountAsUint64) {
        AddDiagnostic(state.output.diagnostics, PakDiagnosticSeverity::kError,
          PakBuildPhase::kPlanning, "pak.plan.script_slot_index_overflow",
          "Patch-local script slot index overflowed uint32 bounds.");
        continue;
      }

      uint64_t global_param_offset = 0;
      if (!SafeAdd(state.script_param_record_count,
            local_range.params_array_offset, global_param_offset)
        || global_param_offset > kMaxCountAsUint64) {
        AddDiagnostic(state.output.diagnostics, PakDiagnosticSeverity::kError,
          PakBuildPhase::kPlanning, "pak.plan.script_params_offset_overflow",
          "Patch-local script param offset overflowed uint32 bounds.");
        continue;
      }

      state.script_param_ranges.push_back(pak::PakScriptParamRangePlan {
        .slot_index = static_cast<uint32_t>(global_slot_index),
        .params_array_offset = static_cast<uint32_t>(global_param_offset),
        .params_count = local_range.params_count,
      });
    }

    uint64_t next_slot_index_base = 0;
    if (!SafeAdd(slot_index_base, contribution.table_counts.script_slot_count,
          next_slot_index_base)) {
      AddDiagnostic(state.output.diagnostics, PakDiagnosticSeverity::kError,
        PakBuildPhase::kPlanning, "pak.plan.script_slot_index_overflow",
        "Patch-local script slot count overflowed uint64 bounds.");
      slot_index_base = kMaxCountAsUint64;
    } else {
      slot_index_base = next_slot_index_base;
    }

    uint64_t next_param_count = 0;
    if (!SafeAdd(state.script_param_record_count,
          contribution.script_param_record_count, next_param_count)
      || next_param_count > kMaxCountAsUint64) {
      AddDiagnostic(state.output.diagnostics, PakDiagnosticSeverity::kError,
        PakBuildPhase::kPlanning, "pak.plan.script_params_count_overflow",
        "Patch-local ScriptParamRecord count overflowed uint32 bounds.");
      state.script_param_record_count
        = static_cast<uint32_t>(kMaxCountAsUint64);
    } else {
      state.script_param_record_count = static_cast<uint32_t>(next_param_count);
    }
  }
}

auto PreparePatchCompatibilityEnvelope(PlanningState& state) -> void
{
  using pak::PakBuildPhase;
  using pak::PakDiagnosticSeverity;

  state.patch_manifest_basis_ready = false;
  state.patch_compatibility_envelope = {};
  state.patch_compatibility_envelope.patch_content_version
    = state.request->content_version;
  state.patch_compatibility_policy_snapshot
    = PatchCompatibilityPolicySnapshotData {
        .require_exact_base_set
        = state.request->patch_compat.require_exact_base_set,
        .require_content_version_match
        = state.request->patch_compat.require_content_version_match,
        .require_base_source_key_match
        = state.request->patch_compat.require_base_source_key_match,
        .require_catalog_digest_match
        = state.request->patch_compat.require_catalog_digest_match,
      };
  state.patch_diff_basis_identifier = std::string(kPatchDiffBasisIdentifier);

  if (!state.policy.emits_manifest) {
    return;
  }

  if (state.policy.mode == pak::PakPlanMode::kPatch) {
    for (const auto& base_catalog : state.request->base_catalogs) {
      state.patch_compatibility_envelope.required_base_source_keys.push_back(
        base_catalog.source_key);
      state.patch_compatibility_envelope.required_base_content_versions
        .push_back(base_catalog.content_version);
      state.patch_compatibility_envelope.required_base_catalog_digests
        .push_back(base_catalog.catalog_digest);
    }

    SortAndUniqueSourceKeys(
      state.patch_compatibility_envelope.required_base_source_keys);
    std::ranges::sort(
      state.patch_compatibility_envelope.required_base_content_versions);
    state.patch_compatibility_envelope.required_base_content_versions.erase(
      std::ranges::unique(
        state.patch_compatibility_envelope.required_base_content_versions)
        .begin(),
      state.patch_compatibility_envelope.required_base_content_versions.end());
    SortAndUniqueDigests(
      state.patch_compatibility_envelope.required_base_catalog_digests);

    if (state.patch_compatibility_envelope.required_base_source_keys.empty()
      || state.patch_compatibility_envelope.required_base_content_versions
        .empty()
      || state.patch_compatibility_envelope.required_base_catalog_digests
        .empty()) {
      AddDiagnostic(state.output.diagnostics, PakDiagnosticSeverity::kError,
        PakBuildPhase::kPlanning, "pak.patch.compatibility_envelope_incomplete",
        "Patch compatibility envelope requires non-empty base source/content/"
        "catalog requirements.");
      return;
    }
  }

  if (state.policy.mode == pak::PakPlanMode::kFull) {
    state.patch_compatibility_envelope.required_base_source_keys.clear();
    state.patch_compatibility_envelope.required_base_content_versions.clear();
    state.patch_compatibility_envelope.required_base_catalog_digests.clear();
  }

  state.patch_manifest_basis_ready = true;
}

auto ValidatePatchContributionInvariants(PlanningState& state) -> void
{
  EnforceStageInvariant(state,
    state.patch_diff_basis_identifier == kPatchDiffBasisIdentifier,
    "pak.plan.stage.patch.diff_basis_identifier_mismatch",
    "Patch diff basis identifier must match the required "
    "descriptor_plus_transitive_resources_v1 value.");

  if (!CheckStageInvariant(state,
        !state.policy.emits_manifest || state.patch_manifest_basis_ready,
        "pak.plan.stage.patch.compatibility_basis_missing",
        "Patch/full-manifest build must prepare compatibility envelope "
        "basis.")) {
    return;
  }

  EnforceStageInvariant(state,
    state.patch_compatibility_envelope.patch_content_version
      == state.request->content_version,
    "pak.plan.stage.patch.patch_content_version_mismatch",
    "Patch compatibility envelope patch_content_version must match request.");
  EnforceStageInvariant(state,
    state.patch_compatibility_policy_snapshot.require_exact_base_set
        == state.request->patch_compat.require_exact_base_set
      && state.patch_compatibility_policy_snapshot.require_content_version_match
        == state.request->patch_compat.require_content_version_match
      && state.patch_compatibility_policy_snapshot.require_base_source_key_match
        == state.request->patch_compat.require_base_source_key_match
      && state.patch_compatibility_policy_snapshot.require_catalog_digest_match
        == state.request->patch_compat.require_catalog_digest_match,
    "pak.plan.stage.patch.policy_snapshot_mismatch",
    "Patch compatibility policy snapshot must match the request policy.");

  if (state.policy.mode != pak::PakPlanMode::kPatch) {
    return;
  }

  for (const auto& resource : state.pending_resources) {
    EnforceStageInvariant(state,
      std::ranges::find(state.included_source_orders, resource.source_order)
        != state.included_source_orders.end(),
      "pak.plan.stage.patch.resource_not_patch_local",
      "Patch-local resource filtering retained a resource from a non-emitted "
      "source.");
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

  for (const auto& range : state.script_param_ranges) {
    uint64_t end = 0;
    if (!SafeAdd(range.params_array_offset, range.params_count, end)
      || end > kMaxCountAsUint64) {
      AddDiagnostic(diagnostics, PakDiagnosticSeverity::kError,
        PakBuildPhase::kPlanning, "pak.plan.script_param_count_overflow",
        "Script param range overflows uint32 bounds.");
      continue;
    }

    if (end > state.script_param_record_count) {
      AddDiagnostic(diagnostics, PakDiagnosticSeverity::kError,
        PakBuildPhase::kPlanning, "pak.plan.script_param_out_of_bounds",
        "Script param range exceeds available ScriptParamRecord count.");
    }
  }

  state.data_plan.script_param_record_count = state.script_param_record_count;
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
  state.data_plan.resource_payload_sources.clear();
  state.planned_resource_source_orders.clear();
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
      state.data_plan.resource_payload_sources.push_back(
        pak::PakPayloadSourceSlicePlan {
          .source_path = resource.path,
          .source_offset = resource.source_offset,
          .size_bytes = resource.size_bytes,
        });
      state.planned_resource_source_orders.push_back(resource.source_order);
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
  state.data_plan.asset_payload_sources.clear();
  state.data_plan.directory.entries.clear();
  for (const auto& asset : state.assets) {
    cursor = AlignUp(cursor, AlignmentBytes { kAssetAlignment });

    if (asset.descriptor_size > (std::numeric_limits<uint32_t>::max)()) {
      AddDiagnostic(state.output.diagnostics,
        pak::PakDiagnosticSeverity::kError, pak::PakBuildPhase::kPlanning,
        "pak.plan.asset_descriptor_too_large",
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
    state.data_plan.asset_payload_sources.push_back(
      pak::PakPayloadSourceSlicePlan {
        .source_path = asset.descriptor_path,
        .source_offset = asset.descriptor_source_offset,
        .size_bytes = asset.descriptor_size,
      });
    state.data_plan.directory.entries.push_back(
      pak::PakAssetDirectoryEntryPlan {
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
      = state.data_plan.directory.offset
      + (i * sizeof(core::AssetDirectoryEntry));
  }
  cursor += state.data_plan.directory.size_bytes;

  state.data_plan.browse_index = pak::PakBrowseIndexPlan {
    .enabled = false,
    .offset = 0,
    .size_bytes = 0,
    .entries = {},
  };
  if (state.request->options.embed_browse_index && !state.browse_map.empty()) {
    std::vector<pak::PakBrowseEntryPlan> browse_entries;
    browse_entries.reserve(state.browse_map.size());
    for (const auto& entry : state.browse_map) {
      browse_entries.push_back(pak::PakBrowseEntryPlan {
        .asset_key = entry.second,
        .virtual_path = entry.first,
      });
    }
    std::ranges::sort(browse_entries,
      [](const pak::PakBrowseEntryPlan& lhs,
        const pak::PakBrowseEntryPlan& rhs) {
        return lhs.virtual_path < rhs.virtual_path;
      });

    const auto browse_payload_size
      = pak::MeasureBrowseIndexPayload(std::span<const pak::PakBrowseEntryPlan>(
        browse_entries.data(), browse_entries.size()));
    if (!browse_payload_size.has_value()) {
      AddDiagnostic(state.output.diagnostics,
        pak::PakDiagnosticSeverity::kError, pak::PakBuildPhase::kPlanning,
        "pak.plan.browse_index_measure_failed",
        "Browse index payload size measurement overflowed or failed.");
      return;
    }

    cursor = AlignUp(cursor, AlignmentBytes { kBrowseAlignment });
    state.data_plan.browse_index = pak::PakBrowseIndexPlan {
      .enabled = true,
      .offset = cursor,
      .size_bytes = *browse_payload_size,
      .entries = std::move(browse_entries),
    };
    cursor += *browse_payload_size;
  }

  cursor = AlignUp(cursor, AlignmentBytes { kFooterAlignment });
  state.data_plan.footer.offset = cursor;
  state.data_plan.footer.size_bytes
    = static_cast<uint32_t>(sizeof(core::PakFooter));
  state.data_plan.footer.crc32_field_absolute_offset
    = cursor + offsetof(core::PakFooter, pak_crc32);
  state.data_plan.planned_file_size
    = cursor + static_cast<uint64_t>(sizeof(core::PakFooter));
}

auto BuildPatchClosure(PlanningState& state) -> void
{
  state.data_plan.patch_closure.clear();
  if (state.policy.mode != pak::PakPlanMode::kPatch) {
    return;
  }

  EnforceStageInvariant(state,
    state.planned_resource_source_orders.size()
      == state.data_plan.resources.size(),
    "pak.plan.stage.patch.resource_source_count_mismatch",
    "Planned resource source-order metadata count does not match resource "
    "plan.");

  auto resources_by_source_order
    = std::unordered_map<size_t, std::vector<size_t>> {};
  resources_by_source_order.reserve(
    state.planned_resource_source_orders.size());
  for (size_t i = 0; i < state.planned_resource_source_orders.size(); ++i) {
    resources_by_source_order[state.planned_resource_source_orders[i]]
      .push_back(i);
  }

  for (const auto& asset : state.assets) {
    const auto resource_indices_it
      = resources_by_source_order.find(asset.source_order);
    if (resource_indices_it == resources_by_source_order.end()) {
      continue;
    }

    for (const auto resource_position : resource_indices_it->second) {
      const auto& resource = state.data_plan.resources[resource_position];
      state.data_plan.patch_closure.push_back(pak::PakPatchClosureRecord {
        .asset_key = asset.key,
        .resource_kind = resource.resource_kind,
        .resource_index = resource.resource_index,
      });
    }
  }
}

auto ValidatePatchClosureInvariants(PlanningState& state) -> void
{
  if (state.policy.mode != pak::PakPlanMode::kPatch) {
    return;
  }

  auto resources_by_source_order = std::unordered_map<size_t, uint64_t> {};
  for (const auto source_order : state.planned_resource_source_orders) {
    ++resources_by_source_order[source_order];
  }

  auto closure_count_by_asset = std::unordered_map<data::AssetKey, uint64_t> {};
  for (const auto& closure : state.data_plan.patch_closure) {
    ++closure_count_by_asset[closure.asset_key];
  }

  for (const auto& asset : state.assets) {
    const auto expected = resources_by_source_order[asset.source_order];
    const auto actual_it = closure_count_by_asset.find(asset.key);
    const auto actual
      = actual_it == closure_count_by_asset.end() ? 0U : actual_it->second;
    EnforceStageInvariant(state, expected == actual,
      "pak.plan.stage.patch.closure_incomplete",
      "Patch closure must include every patch-local resource for each emitted "
      "asset.");
  }
}

auto ValidateLayoutInvariants(PlanningState& state) -> void
{
  const auto resource_source_count_matches
    = state.data_plan.resource_payload_sources.size()
    == state.data_plan.resources.size();
  EnforceStageInvariant(state, resource_source_count_matches,
    "pak.plan.stage.layout.resource_source_count_mismatch",
    "Resource payload source slice count must match planned resources.");

  const auto asset_source_count_matches
    = state.data_plan.asset_payload_sources.size()
    == state.data_plan.assets.size();
  EnforceStageInvariant(state, asset_source_count_matches,
    "pak.plan.stage.layout.asset_source_count_mismatch",
    "Asset payload source slice count must match planned assets.");

  const auto resource_pair_count
    = (std::min)(state.data_plan.resource_payload_sources.size(),
      state.data_plan.resources.size());
  for (size_t i = 0; i < resource_pair_count; ++i) {
    const auto& source = state.data_plan.resource_payload_sources[i];
    const auto& planned = state.data_plan.resources[i];
    uint64_t source_end = 0;
    const auto no_overflow
      = SafeAdd(source.source_offset, source.size_bytes, source_end);
    EnforceStageInvariant(state,
      no_overflow && source.size_bytes == planned.size_bytes,
      "pak.plan.stage.layout.resource_source_size_mismatch",
      "Resource payload source size must match planned resource size.");
  }

  const auto asset_pair_count
    = (std::min)(state.data_plan.asset_payload_sources.size(),
      state.data_plan.assets.size());
  for (size_t i = 0; i < asset_pair_count; ++i) {
    const auto& source = state.data_plan.asset_payload_sources[i];
    const auto& planned = state.data_plan.assets[i];
    uint64_t source_end = 0;
    const auto no_overflow
      = SafeAdd(source.source_offset, source.size_bytes, source_end);
    EnforceStageInvariant(state,
      no_overflow && source.size_bytes == planned.size_bytes,
      "pak.plan.stage.layout.asset_source_size_mismatch",
      "Asset payload source size must match planned descriptor size.");
  }

  const auto entries_match_assets
    = state.data_plan.directory.entries.size() == state.data_plan.assets.size();
  EnforceStageInvariant(state, entries_match_assets,
    "pak.plan.stage.layout.directory_asset_count_mismatch",
    "Directory entry count does not match planned asset count.");

  const auto pair_count = (std::min)(state.data_plan.directory.entries.size(),
    state.data_plan.assets.size());
  for (size_t i = 0; i < pair_count; ++i) {
    const auto key_matches = state.data_plan.directory.entries[i].asset_key
      == state.data_plan.assets[i].asset_key;
    EnforceStageInvariant(state, key_matches,
      "pak.plan.stage.layout.directory_asset_key_mismatch",
      "Directory entry asset key does not match planned asset key.");
  }

  const auto footer_end = state.data_plan.footer.offset
    + static_cast<uint64_t>(state.data_plan.footer.size_bytes);
  const auto size_covers_footer
    = state.data_plan.planned_file_size >= footer_end;
  EnforceStageInvariant(state, size_covers_footer,
    "pak.plan.stage.layout.file_size_before_footer_end",
    "Planned file size is smaller than footer end.");

  if (state.data_plan.browse_index.enabled) {
    std::vector<std::byte> browse_payload;
    const auto stored = pak::StoreBrowseIndexPayload(
      std::span<const pak::PakBrowseEntryPlan>(
        state.data_plan.browse_index.entries.data(),
        state.data_plan.browse_index.entries.size()),
      browse_payload);
    EnforceStageInvariant(state, stored,
      "pak.plan.stage.layout.browse_store_failed",
      "Browse index measure/store serialization failed.");

    if (stored) {
      const auto measured_matches = state.data_plan.browse_index.size_bytes
        == static_cast<uint64_t>(browse_payload.size());
      EnforceStageInvariant(state, measured_matches,
        "pak.plan.stage.layout.browse_size_mismatch",
        "Browse index planned size does not match serialized payload size.");
    }
  }
}

auto ValidateAndFinalizeResult(PlanningState& state) -> void
{
  const auto validation = pak::PakValidation::Validate(
    pak::PakPlan(state.data_plan), state.policy, *state.request);
  state.output.diagnostics.insert(state.output.diagnostics.end(),
    validation.diagnostics.begin(), validation.diagnostics.end());

  std::ranges::sort(state.output.diagnostics,
    [](const pak::PakDiagnostic& lhs, const pak::PakDiagnostic& rhs) {
      if (lhs.code != rhs.code) {
        return lhs.code < rhs.code;
      }
      if (lhs.phase != rhs.phase) {
        return static_cast<uint8_t>(lhs.phase)
          < static_cast<uint8_t>(rhs.phase);
      }
      if (lhs.asset_key != rhs.asset_key) {
        return lhs.asset_key < rhs.asset_key;
      }
      return lhs.message < rhs.message;
    });

  const auto has_error = std::ranges::any_of(
    state.output.diagnostics, [](const pak::PakDiagnostic& diagnostic) {
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
  RunStage(state, "RebuildPatchLocalContributions",
    "pak.plan.stage.patch_local_rebuild_exception",
    [&state]() { RebuildPatchLocalContributions(state); });
  RunStage(state, "PreparePatchCompatibilityEnvelope",
    "pak.plan.stage.patch_compatibility_exception",
    [&state]() { PreparePatchCompatibilityEnvelope(state); });
  RunStage(state, "ValidatePatchContributionInvariants",
    "pak.plan.stage.patch_local_invariants_exception",
    [&state]() { ValidatePatchContributionInvariants(state); });
  RunStage(state, "FinalizeScriptAndTables",
    "pak.plan.stage.script_tables_exception",
    [&state]() { FinalizeScriptAndTables(state); });
  RunStage(state, "ValidateScriptAndTableInvariants",
    "pak.plan.stage.script_tables_invariants_exception",
    [&state]() { ValidateScriptAndTableInvariants(state); });
  RunStage(state, "PlanFileLayout", "pak.plan.stage.layout_exception",
    [&state]() { PlanFileLayout(state); });
  RunStage(state, "BuildPatchClosure", "pak.plan.stage.patch_closure_exception",
    [&state]() { BuildPatchClosure(state); });
  RunStage(state, "ValidatePatchClosureInvariants",
    "pak.plan.stage.patch_closure_invariants_exception",
    [&state]() { ValidatePatchClosureInvariants(state); });
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
