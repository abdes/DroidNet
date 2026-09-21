//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <filesystem>
#include <functional>
#include <ios>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Content/PakFile.h>
#include <Oxygen/Cooker/Loose/Inspection.h>
#include <Oxygen/Cooker/Loose/Types.h>
#include <Oxygen/Cooker/Pak/PakBuildPhase.h>
#include <Oxygen/Cooker/Pak/PakBuildReport.h>
#include <Oxygen/Cooker/Pak/PakBuildRequest.h>
#include <Oxygen/Cooker/Pak/PakMeasureStore.h>
#include <Oxygen/Cooker/Pak/PakPlan.h>
#include <Oxygen/Cooker/Pak/PakPlanBuilder.h>
#include <Oxygen/Cooker/Pak/PakPlanPolicy.h>
#include <Oxygen/Cooker/Pak/PakValidation.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/ComponentType.h>
#include <Oxygen/Data/CookedSource.h>
#include <Oxygen/Data/LooseCookedIndexFormat.h>
#include <Oxygen/Data/PakCatalog.h>
#include <Oxygen/Data/PakFormatSerioLoaders.h>
#include <Oxygen/Data/PakFormat_audio.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Data/PakFormat_geometry.h>
#include <Oxygen/Data/PakFormat_physics.h>
#include <Oxygen/Data/PakFormat_render.h>
#include <Oxygen/Data/PakFormat_scripting.h>
#include <Oxygen/Data/PakFormat_world.h>
#include <Oxygen/Data/SceneAsset.h>
#include <Oxygen/Data/SourceKey.h>
#include <Oxygen/Serio/FileStream.h>
#include <Oxygen/Serio/MemoryStream.h>
#include <Oxygen/Serio/Reader.h>
#include <Oxygen/Serio/Writer.h>

namespace {
namespace pak = oxygen::content::pak;
namespace content = oxygen::content;
namespace lc = oxygen::content::lc;
namespace data = oxygen::data;
namespace serio = oxygen::serio;
namespace core = oxygen::data::pak::core;
namespace audio = oxygen::data::pak::audio;
namespace geometry = oxygen::data::pak::geometry;
namespace render = oxygen::data::pak::render;
namespace script = oxygen::data::pak::scripting;
namespace physics = oxygen::data::pak::physics;
namespace world = oxygen::data::pak::world;

constexpr uint32_t kRegionAlignment = 256U;
constexpr uint32_t kTableAlignment = 16U;
constexpr uint32_t kAssetAlignment = 16U;
constexpr uint32_t kDirectoryAlignment = 16U;
constexpr uint32_t kBrowseAlignment = 16U;
constexpr uint32_t kFooterAlignment = 16U;
constexpr uint64_t kMaxCountAsUint64 = std::numeric_limits<uint32_t>::max();
constexpr int kUnknownRegionOrder = std::numeric_limits<int>::max();
constexpr std::string_view kPatchDiffBasisIdentifier
  = "descriptor_plus_transitive_resources_v1";

struct AlignmentBytes final {
  uint32_t value = 1U;
};

struct AggregatedAsset {
  data::AssetKey key;
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
  uint64_t descriptor_source_offset = 0;
  uint64_t descriptor_size = 0;
  uint32_t alignment = kRegionAlignment;
  size_t source_order = 0;
  uint64_t source_sort_key = 0;
  std::optional<uint32_t> source_local_index = std::nullopt;
  std::optional<data::AssetKey> resource_asset_key = std::nullopt;
  std::vector<data::AssetKey> dependent_asset_keys;
  std::filesystem::path path;
  std::filesystem::path descriptor_path;
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
  std::vector<pak::PakScriptSlotPlan> local_script_slots;
  uint32_t script_param_record_count = 0;
};

struct OwnedScriptSlotSource final {
  data::AssetKey asset_key;
  size_t source_order = 0;
  uint32_t source_slot_index = 0;
  script::ScriptSlotRecord record {};
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
  if (rhs > std::numeric_limits<uint64_t>::max() - lhs) {
    return false;
  }
  out = lhs + rhs;
  return true;
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
  std::ranges::replace(normalized, '\\', '/');
  if (!normalized.empty() && normalized.front() != '/') {
    normalized.insert(normalized.begin(), '/');
  }

  while (normalized.contains("//")) {
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

struct SourceFileSlice final {
  std::filesystem::path path;
  uint64_t size = 0;
  uint64_t offset = 0;
};

struct SourceResourceFiles final {
  std::optional<SourceFileSlice> buffers_table;
  std::optional<SourceFileSlice> buffers_data;
  std::optional<SourceFileSlice> textures_table;
  std::optional<SourceFileSlice> textures_data;
  std::optional<SourceFileSlice> scripts_table;
  std::optional<SourceFileSlice> scripts_data;
  std::optional<SourceFileSlice> script_bindings_table;
  std::optional<SourceFileSlice> script_bindings_data;
  std::optional<SourceFileSlice> physics_table;
  std::optional<SourceFileSlice> physics_data;
  std::optional<SourceFileSlice> audio_table;
  std::optional<SourceFileSlice> audio_data;
};

struct TableReadDiagnostics final {
  std::string_view size_invalid;
  std::string_view read_failed;
  std::string_view too_large;
  std::string_view label;
};

template <typename Record>
auto ReadFixedRecordFile(const SourceFileSlice& file,
  std::vector<Record>& records, std::vector<pak::PakDiagnostic>& diagnostics,
  const TableReadDiagnostics& errors) -> bool
{
  const auto& path = file.path;
  const auto file_size = MeasureFileSize(path, diagnostics);
  uint64_t slice_end = 0;
  if (!file_size || !SafeAdd(file.offset, file.size, slice_end)
    || slice_end > *file_size) {
    AddDiagnostic(diagnostics, pak::PakDiagnosticSeverity::kError,
      pak::PakBuildPhase::kPlanning, errors.size_invalid,
      "Resource table slice exceeds its source file", path);
    return false;
  }

  constexpr auto kRecordSize = uint64_t { sizeof(Record) };
  if (kRecordSize == 0U || (file.size % kRecordSize) != 0U) {
    AddDiagnostic(diagnostics, pak::PakDiagnosticSeverity::kError,
      pak::PakBuildPhase::kPlanning, errors.size_invalid,
      std::string(errors.label) + " size is not divisible by record size.",
      path);
    return false;
  }

  const auto record_count = file.size / kRecordSize;
  if (record_count
    > static_cast<uint64_t>(std::numeric_limits<size_t>::max())) {
    AddDiagnostic(diagnostics, pak::PakDiagnosticSeverity::kError,
      pak::PakBuildPhase::kPlanning, errors.too_large,
      std::string(errors.label) + " record count exceeds size_t bounds.", path);
    return false;
  }

  serio::FileStream<> stream(path, std::ios::in);
  serio::Reader<serio::FileStream<>> reader(stream);
  auto align_guard = reader.ScopedAlignment(1);
  (void)align_guard;

  if (!reader.Seek(file.offset)) {
    return false;
  }
  const auto blob_result = reader.ReadBlob(static_cast<size_t>(file.size));
  if (!blob_result) {
    AddDiagnostic(diagnostics, pak::PakDiagnosticSeverity::kError,
      pak::PakBuildPhase::kPlanning, errors.read_failed,
      std::string("Failed to read ") + std::string(errors.label) + " content.",
      path);
    return false;
  }

  const auto blob = std::span<const std::byte>(*blob_result);
  records.resize(static_cast<size_t>(record_count));
  for (size_t i = 0; i < records.size(); ++i) {
    std::memcpy(std::addressof(records.at(i)),
      blob.subspan(i * sizeof(Record), sizeof(Record)).data(), sizeof(Record));
  }

  return true;
}

template <typename Record, typename ConfigureFn, typename RecordSinkFn>
auto AppendResourcesFromTable(const SourceFileSlice& table_file,
  const SourceFileSlice& data_file, const std::string_view region_name,
  const std::string_view resource_kind, const size_t source_order,
  std::vector<PendingResource>& pending_resources,
  std::vector<pak::PakDiagnostic>& diagnostics,
  const std::string_view table_name, const ConfigureFn& configure,
  const RecordSinkFn& on_record) -> uint32_t
{
  const auto& table_path = table_file.path;
  const auto& data_path = data_file.path;
  auto records = std::vector<Record> {};
  if (!ReadFixedRecordFile<Record>(table_file, records, diagnostics,
        {
          .size_invalid = std::string("pak.plan.") + std::string(table_name)
            + "_size_invalid",
          .read_failed
          = std::string("pak.plan.") + std::string(table_name) + "_read_failed",
          .too_large
          = std::string("pak.plan.") + std::string(table_name) + "_too_large",
          .label = table_name,
        })) {
    return 0;
  }

  for (size_t i = 0; i < records.size(); ++i) {
    auto pending = PendingResource {
      .region_name = std::string(region_name),
      .resource_kind = std::string(resource_kind),
      .size_bytes = static_cast<uint64_t>(records.at(i).size_bytes),
      .source_offset = static_cast<uint64_t>(records.at(i).data_offset),
      .descriptor_source_offset
      = table_file.offset + static_cast<uint64_t>(i * sizeof(Record)),
      .descriptor_size = sizeof(Record),
      .alignment = kRegionAlignment,
      .source_order = source_order,
      .source_sort_key = i,
      .source_local_index = static_cast<uint32_t>(i),
      .resource_asset_key = std::nullopt,
      .dependent_asset_keys = {},
      .path = data_path,
      .descriptor_path = table_path,
    };

    if (!configure(records.at(i), pending)) {
      continue;
    }

    uint64_t resource_end = 0;
    uint64_t data_end = 0;
    if (!SafeAdd(pending.source_offset, pending.size_bytes, resource_end)
      || !SafeAdd(data_file.offset, data_file.size, data_end)
      || (pending.size_bytes != 0U
        && (pending.source_offset < data_file.offset
          || resource_end > data_end))) {
      AddDiagnostic(diagnostics, pak::PakDiagnosticSeverity::kError,
        pak::PakBuildPhase::kPlanning, "pak.plan.resource_slice_out_of_bounds",
        std::string(table_name)
          + " record range exceeds its backing data file bounds.",
        table_path);
      continue;
    }

    on_record(static_cast<uint32_t>(i), records.at(i), pending);
    pending_resources.push_back(std::move(pending));
  }

  if (records.size() > kMaxCountAsUint64) {
    AddDiagnostic(diagnostics, pak::PakDiagnosticSeverity::kError,
      pak::PakBuildPhase::kPlanning, "pak.plan.resource_record_count_overflow",
      std::string(table_name) + " record count overflowed uint32 bounds.",
      table_path);
    return 0;
  }
  return static_cast<uint32_t>(records.size());
}

auto ReadScriptSlotsFromTable(const std::filesystem::path& scripts_table_path,
  const ScriptSlotReadContext& context,
  std::vector<pak::PakScriptSlotPlan>& slots,
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
      "script-bindings.table size is not divisible by ScriptSlotRecord size.",
      scripts_table_path);
    return 0;
  }

  const auto slot_count64 = *size_opt / kSlotSize;
  if (slot_count64 > kMaxCountAsUint64) {
    AddDiagnostic(diagnostics, pak::PakDiagnosticSeverity::kError,
      pak::PakBuildPhase::kPlanning, "pak.plan.script_slot_table_too_large",
      "script-bindings.table has too many slot records.", scripts_table_path);
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
      "Failed to read script-bindings.table content.", scripts_table_path);
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
        "ScriptSlotRecord params range exceeds script-bindings.data bounds "
        "for source.",
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

    slots.push_back(pak::PakScriptSlotPlan {
      .slot_index = static_cast<uint32_t>(slot_index64),
      .script_asset_key = record.script_asset_key,
      .params_array_index = static_cast<uint32_t>(global_params_array_offset),
      .params_count = record.params_count,
      .execution_order = record.execution_order,
      .flags = record.flags,
    });
  }

  return slot_count;
}

struct SourceResourceDigestState final {
  std::unordered_map<uint32_t, oxygen::base::Sha256Digest> texture_digests;
  std::unordered_map<uint32_t, oxygen::base::Sha256Digest> buffer_digests;
  std::unordered_map<uint32_t, oxygen::base::Sha256Digest> script_digests;
  std::unordered_map<uint32_t, oxygen::base::Sha256Digest> physics_digests;
  std::unordered_map<data::AssetKey, uint32_t> physics_index_by_asset_key;
  std::unordered_map<uint32_t, size_t> texture_pending_positions;
  std::unordered_map<uint32_t, size_t> buffer_pending_positions;
  std::unordered_map<uint32_t, size_t> script_pending_positions;
  std::unordered_map<uint32_t, size_t> physics_pending_positions;
  std::optional<std::filesystem::path> script_bindings_data_path;
  std::vector<script::ScriptSlotRecord> raw_script_slots;
  std::vector<std::byte> raw_script_params;
  std::unordered_map<uint64_t, uint64_t> script_param_source_offsets;
};

template <typename MapT>
auto LookupDigest(const MapT& map, const uint32_t index)
  -> std::optional<oxygen::base::Sha256Digest>
{
  const auto it = map.find(index);
  if (it == map.end()) {
    return std::nullopt;
  }
  return it->second;
}

template <typename MapT>
auto AddResourceDigestInput(const MapT& digests, const uint32_t index,
  std::vector<std::pair<uint16_t, oxygen::base::Sha256Digest>>& inputs,
  const uint16_t tag) -> bool
{
  const auto digest = LookupDigest(digests, index);
  if (!digest.has_value()) {
    return false;
  }
  inputs.emplace_back(tag, *digest);
  return true;
}

auto AddDependentAssetKey(PendingResource& pending, const data::AssetKey& key)
  -> void
{
  if (!std::ranges::contains(pending.dependent_asset_keys, key)) {
    pending.dependent_asset_keys.push_back(key);
  }
}

auto AttachDependentAssetToPendingIndex(
  std::vector<PendingResource>& pending_resources,
  const std::optional<size_t> pending_index, const data::AssetKey& asset_key)
  -> void
{
  if (!pending_index.has_value()
    || *pending_index >= pending_resources.size()) {
    return;
  }
  AddDependentAssetKey(pending_resources.at(*pending_index), asset_key);
}

auto FindPendingIndexByLocalIndex(
  const std::unordered_map<uint32_t, size_t>& map, const uint32_t index)
  -> std::optional<size_t>
{
  const auto it = map.find(index);
  if (it == map.end()) {
    return std::nullopt;
  }
  return it->second;
}

constexpr uint16_t kScriptSlotDigestTag = 0x1000U;
constexpr uint16_t kScriptParameterDigestTag = 0x1001U;

auto AppendSceneScriptSlotInputs(const AggregatedAsset& asset,
  const std::vector<std::byte>& descriptor_bytes, const size_t source_order,
  const bool capture_patch_ownership, SourceResourceDigestState& resource_state,
  std::vector<PendingResource>& pending_resources,
  std::vector<OwnedScriptSlotSource>& owned_script_slots,
  std::unordered_set<size_t>& source_orders_with_owned_script_slots,
  std::vector<std::pair<uint16_t, oxygen::base::Sha256Digest>>& inputs,
  std::vector<pak::PakDiagnostic>& diagnostics) -> bool
{
  if (resource_state.raw_script_slots.empty()
    || !resource_state.script_bindings_data_path.has_value()) {
    return false;
  }

  try {
    const auto scene = data::SceneAsset(asset.key,
      std::span<const std::byte>(
        descriptor_bytes.data(), descriptor_bytes.size()));
    const auto components
      = scene.GetComponents<script::ScriptingComponentRecord>();
    if (components.empty()) {
      return false;
    }

    if (capture_patch_ownership) {
      source_orders_with_owned_script_slots.insert(source_order);
    }
    for (const auto& component : components) {
      uint64_t end = 0;
      if (!SafeAdd(component.slot_start_index, component.slot_count, end)
        || end > resource_state.raw_script_slots.size()) {
        AddDiagnostic(diagnostics, pak::PakDiagnosticSeverity::kError,
          pak::PakBuildPhase::kPlanning,
          "pak.plan.scene_script_slot_range_invalid",
          "Scene scripting component slot range exceeds "
          "script-bindings.table bounds.",
          asset.descriptor_path);
        return false;
      }

      for (uint32_t i = 0; i < component.slot_count; ++i) {
        const auto slot_index = component.slot_start_index + i;
        const auto& raw_slot = resource_state.raw_script_slots.at(slot_index);

        auto slot_digest_record = raw_slot;
        slot_digest_record.params_array_offset = 0U;
        auto slot_hasher = oxygen::base::Sha256 {};
        slot_hasher.Update(std::as_bytes(std::span(&slot_digest_record, 1)));
        inputs.emplace_back(kScriptSlotDigestTag, slot_hasher.Finalize());

        if (raw_slot.params_count > 0U) {
          uint64_t end_offset = 0;
          if (!SafeAdd(raw_slot.params_array_offset,
                static_cast<uint64_t>(raw_slot.params_count)
                  * sizeof(script::ScriptParamRecord),
                end_offset)
            || end_offset > resource_state.raw_script_params.size()) {
            AddDiagnostic(diagnostics, pak::PakDiagnosticSeverity::kError,
              pak::PakBuildPhase::kPlanning,
              "pak.plan.scene_script_param_range_invalid",
              "Scene scripting component params range exceeds "
              "script-bindings.data bounds.",
              asset.descriptor_path);
            return false;
          }

          auto params_hasher = oxygen::base::Sha256 {};
          params_hasher.Update(
            std::span<const std::byte>(resource_state.raw_script_params)
              .subspan(static_cast<size_t>(raw_slot.params_array_offset),
                static_cast<size_t>(raw_slot.params_count)
                  * sizeof(script::ScriptParamRecord)));
          inputs.emplace_back(
            kScriptParameterDigestTag, params_hasher.Finalize());

          if (capture_patch_ownership) {
            pending_resources.push_back(PendingResource {
              .region_name = "script_region",
              .resource_kind = "script_param",
              .size_bytes = static_cast<uint64_t>(raw_slot.params_count)
                * sizeof(script::ScriptParamRecord),
              .source_offset
              = resource_state.script_param_source_offsets.contains(
                  raw_slot.params_array_offset)
                ? resource_state.script_param_source_offsets.at(
                    raw_slot.params_array_offset)
                : raw_slot.params_array_offset,
              .descriptor_source_offset = 0U,
              .descriptor_size = 0U,
              .alignment = 1U,
              .source_order = source_order,
              .source_sort_key = raw_slot.params_array_offset,
              .source_local_index = slot_index,
              .resource_asset_key = std::nullopt,
              .dependent_asset_keys = { asset.key },
              .path = *resource_state.script_bindings_data_path,
              .descriptor_path = {},
            });
          }
        }

        if (capture_patch_ownership) {
          owned_script_slots.push_back(OwnedScriptSlotSource {
            .asset_key = asset.key,
            .source_order = source_order,
            .source_slot_index = slot_index,
            .record = raw_slot,
          });
        }
      }
    }

    return true;
  } catch (const std::exception&) {
    return false;
  }
}

auto MakeSkeletonTables() -> std::vector<pak::PakTablePlan>
{
  return {
    pak::PakTablePlan {
      .table_name = "texture_table",
      .offset = 0,
      .size_bytes = 0,
      .count = 0,
      .entry_size = static_cast<uint32_t>(sizeof(core::TextureResourceDesc)),
      .expected_entry_size
      = static_cast<uint32_t>(sizeof(core::TextureResourceDesc)),
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
  data_plan.script_slots = {};
  data_plan.patch_closure = {};
  data_plan.planned_file_size
    = footer_offset + static_cast<uint64_t>(sizeof(core::PakFooter));

  return data_plan;
}

auto FindTableByName(
  std::vector<pak::PakTablePlan>& tables, const std::string_view name)
  -> std::optional<std::reference_wrapper<pak::PakTablePlan>>
{
  const auto it = std::ranges::find_if(
    tables, [name](const pak::PakTablePlan& table) -> bool {
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

auto ResourceOrderWithinRegion(const PendingResource& resource) -> int
{
  if (resource.region_name == "script_region") {
    if (resource.resource_kind == "script_param") {
      return 0;
    }
    if (resource.resource_kind == "script") {
      return 1;
    }
  }
  return 0;
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

auto ReadFileSliceBytes(const SourceFileSlice& file,
  std::vector<pak::PakDiagnostic>& diagnostics, const std::string_view code,
  const std::string_view message_prefix)
  -> std::optional<std::vector<std::byte>>
{
  const auto& path = file.path;
  const auto offset = file.offset;
  const auto size = file.size;
  auto stream = serio::FileStream<>(path, std::ios::in);
  serio::Reader<serio::FileStream<>> reader(stream);
  auto align_guard = reader.ScopedAlignment(1);
  (void)align_guard;

  if (offset > 0U) {
    const auto seek_result = reader.Seek(offset);
    if (!seek_result) {
      AddDiagnostic(diagnostics, pak::PakDiagnosticSeverity::kError,
        pak::PakBuildPhase::kPlanning, code,
        std::string(message_prefix) + " seek failed.", path);
      return std::nullopt;
    }
  }

  const auto blob_result = reader.ReadBlob(static_cast<size_t>(size));
  if (!blob_result) {
    AddDiagnostic(diagnostics, pak::PakDiagnosticSeverity::kError,
      pak::PakBuildPhase::kPlanning, code,
      std::string(message_prefix) + " read failed.", path);
    return std::nullopt;
  }

  return std::vector<std::byte>(blob_result->begin(), blob_result->end());
}

template <typename Record, typename NormalizeFn>
auto ComputeNormalizedResourceDigest(const Record& record,
  const PendingResource& pending, const NormalizeFn& normalize,
  std::vector<pak::PakDiagnostic>& diagnostics)
  -> std::optional<oxygen::base::Sha256Digest>
{
  auto normalized = record;
  normalize(normalized);

  const auto payload_bytes = ReadFileSliceBytes(
    {
      .path = pending.path,
      .size = pending.size_bytes,
      .offset = pending.source_offset,
    },
    diagnostics, "pak.plan.resource_payload_read_failed",
    "Failed to read resource payload slice for digest computation.");
  if (!payload_bytes.has_value()) {
    return std::nullopt;
  }

  auto hasher = oxygen::base::Sha256 {};
  hasher.Update(std::as_bytes(std::span(&normalized, 1)));
  if (!payload_bytes->empty()) {
    hasher.Update(
      std::span<const std::byte>(payload_bytes->data(), payload_bytes->size()));
  }
  return hasher.Finalize();
}

auto ReadSourceDescriptorBytes(
  const AggregatedAsset& asset, std::vector<pak::PakDiagnostic>& diagnostics)
  -> std::optional<std::vector<std::byte>>
{
  std::error_code ec;
  if (!std::filesystem::exists(asset.descriptor_path, ec) || ec) {
    return std::nullopt;
  }

  return ReadFileSliceBytes(
    {
      .path = asset.descriptor_path,
      .size = asset.descriptor_size,
      .offset = asset.descriptor_source_offset,
    },
    diagnostics, "pak.plan.descriptor_read_failed",
    "Failed to read asset descriptor bytes.");
}

auto RewriteSceneScriptingComponentRanges(
  std::vector<std::byte>& descriptor_bytes,
  const std::unordered_map<uint32_t, uint32_t>& rewritten_slot_indices,
  std::vector<pak::PakDiagnostic>& diagnostics,
  const std::filesystem::path& descriptor_path) -> bool
{
  if (descriptor_bytes.size() < sizeof(world::SceneAssetDesc)) {
    AddDiagnostic(diagnostics, pak::PakDiagnosticSeverity::kError,
      pak::PakBuildPhase::kPlanning, "pak.plan.patch_scene_descriptor_invalid",
      "Patched scene descriptor is invalid or too small for scripting rewrite.",
      descriptor_path);
    return false;
  }

  auto header = core::AssetHeader {};
  std::memcpy(std::addressof(header), descriptor_bytes.data(), sizeof(header));
  if (header.asset_type != static_cast<uint8_t>(data::AssetType::kScene)
    || header.version != world::kSceneAssetVersion) {
    AddDiagnostic(diagnostics, pak::PakDiagnosticSeverity::kError,
      pak::PakBuildPhase::kPlanning, "pak.plan.patch_scene_descriptor_invalid",
      "Patched scene descriptor header is invalid for scripting rewrite.",
      descriptor_path);
    return false;
  }

  auto scene_desc = world::SceneAssetDesc {};
  std::memcpy(
    std::addressof(scene_desc), descriptor_bytes.data(), sizeof(scene_desc));

  const auto directory_offset = scene_desc.component_table_directory_offset;
  const auto directory_count = scene_desc.component_table_count;
  const auto directory_size = static_cast<uint64_t>(directory_count)
    * sizeof(world::SceneComponentTableDesc);
  if (directory_offset > descriptor_bytes.size()
    || directory_size > descriptor_bytes.size() - directory_offset) {
    AddDiagnostic(diagnostics, pak::PakDiagnosticSeverity::kError,
      pak::PakBuildPhase::kPlanning,
      "pak.plan.patch_scene_component_directory_invalid",
      "Patched scene component table directory is out of bounds.",
      descriptor_path);
    return false;
  }

  auto rewrote_any = false;
  for (uint32_t i = 0; i < directory_count; ++i) {
    const auto entry_offset = static_cast<size_t>(directory_offset)
      + (static_cast<size_t>(i) * sizeof(world::SceneComponentTableDesc));
    auto entry = world::SceneComponentTableDesc {};
    std::memcpy(std::addressof(entry),
      std::span(descriptor_bytes).subspan(entry_offset, sizeof(entry)).data(),
      sizeof(entry));

    if (entry.component_type
      != static_cast<uint32_t>(data::ComponentType::kScripting)) {
      continue;
    }
    if (entry.table.entry_size != sizeof(script::ScriptingComponentRecord)) {
      AddDiagnostic(diagnostics, pak::PakDiagnosticSeverity::kError,
        pak::PakBuildPhase::kPlanning,
        "pak.plan.patch_scene_scripting_entry_size_invalid",
        "Patched scene scripting component entry_size does not match the "
        "packed scripting record size.",
        descriptor_path);
      return false;
    }

    const auto table_offset = entry.table.offset;
    const auto table_size = static_cast<uint64_t>(entry.table.count)
      * sizeof(script::ScriptingComponentRecord);
    if (table_offset > descriptor_bytes.size()
      || table_size > descriptor_bytes.size() - table_offset) {
      AddDiagnostic(diagnostics, pak::PakDiagnosticSeverity::kError,
        pak::PakBuildPhase::kPlanning,
        "pak.plan.patch_scene_scripting_table_invalid",
        "Patched scene scripting component table is out of bounds.",
        descriptor_path);
      return false;
    }

    for (uint32_t record_index = 0; record_index < entry.table.count;
      ++record_index) {
      const auto record_offset = static_cast<size_t>(table_offset)
        + (static_cast<size_t>(record_index)
          * sizeof(script::ScriptingComponentRecord));
      auto record = script::ScriptingComponentRecord {};
      std::memcpy(std::addressof(record),
        std::span(descriptor_bytes)
          .subspan(record_offset, sizeof(record))
          .data(),
        sizeof(record));

      if (record.slot_count == 0U) {
        continue;
      }

      const auto first_rewrite
        = rewritten_slot_indices.find(record.slot_start_index);
      if (first_rewrite == rewritten_slot_indices.end()) {
        AddDiagnostic(diagnostics, pak::PakDiagnosticSeverity::kError,
          pak::PakBuildPhase::kPlanning,
          "pak.plan.patch_scene_slot_rewrite_missing",
          "Patched scene scripting component references a source slot that "
          "was not remapped into the emitted patch slot table.",
          descriptor_path);
        return false;
      }

      const auto rewritten_start = first_rewrite->second;
      for (uint32_t delta = 1; delta < record.slot_count; ++delta) {
        const auto original_slot_index = record.slot_start_index + delta;
        const auto rewritten_it
          = rewritten_slot_indices.find(original_slot_index);
        if (rewritten_it == rewritten_slot_indices.end()
          || rewritten_it->second != rewritten_start + delta) {
          AddDiagnostic(diagnostics, pak::PakDiagnosticSeverity::kError,
            pak::PakBuildPhase::kPlanning,
            "pak.plan.patch_scene_slot_rewrite_noncontiguous",
            "Patched scene scripting slots do not remap to a contiguous patch "
            "slot range.",
            descriptor_path);
          return false;
        }
      }

      record.slot_start_index = rewritten_start;
      std::memcpy(std::span(descriptor_bytes)
                    .subspan(record_offset, sizeof(record))
                    .data(),
        std::addressof(record), sizeof(record));
      rewrote_any = true;
    }
  }

  if (!rewrote_any) {
    AddDiagnostic(diagnostics, pak::PakDiagnosticSeverity::kError,
      pak::PakBuildPhase::kPlanning,
      "pak.plan.patch_scene_slot_rewrite_not_applied",
      "Patched scene descriptor expected a scripting slot remap but no "
      "scripting component records were rewritten.",
      descriptor_path);
    return false;
  }

  return true;
}

auto IsValidDescriptorHeader(std::span<const std::byte> bytes,
  const data::AssetType expected_type, const uint8_t expected_version) -> bool
{
  if (bytes.size() < sizeof(core::AssetHeader)) {
    return false;
  }

  core::AssetHeader header {};
  std::memcpy(std::addressof(header), bytes.data(), sizeof(header));
  return header.asset_type == static_cast<uint8_t>(expected_type)
    && header.version == expected_version;
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

auto ComputeCatalogDigest(const data::SourceKey& source_key,
  const uint16_t content_version,
  std::span<const data::PakCatalogEntry> entries) -> oxygen::base::Sha256Digest
{
  auto hasher = oxygen::base::Sha256 {};

  const auto version_bytes = std::array<std::byte, sizeof(content_version)> {
    std::byte(static_cast<uint8_t>(content_version & 0xFFU)),
    std::byte(static_cast<uint8_t>(
      (static_cast<uint32_t>(content_version) >> 8U) & 0xFFU)),
  };
  hasher.Update(
    std::span<const std::byte>(version_bytes.data(), version_bytes.size()));
  hasher.Update(data::as_bytes(source_key));

  for (const auto& entry : entries) {
    hasher.Update(data::as_bytes(entry.asset_key));
    const auto asset_type_byte = std::array<std::byte, 1> {
      std::byte(static_cast<uint8_t>(entry.asset_type)),
    };
    hasher.Update(std::span<const std::byte>(
      asset_type_byte.data(), asset_type_byte.size()));
    hasher.Update(std::as_bytes(std::span(entry.descriptor_digest)));
    hasher.Update(std::as_bytes(std::span(entry.transitive_resource_digest)));
  }

  return hasher.Finalize();
}

auto BuildOutputCatalog(const pak::PakBuildRequest& request,
  std::span<const AggregatedAsset> assets) -> data::PakCatalog
{
  auto entries = std::vector<data::PakCatalogEntry> {};
  entries.reserve(assets.size());
  for (const auto& asset : assets) {
    entries.push_back(ToCatalogEntry(asset));
  }

  std::ranges::sort(entries,
    [](const data::PakCatalogEntry& lhs, const data::PakCatalogEntry& rhs)
      -> bool { return lhs.asset_key < rhs.asset_key; });

  return data::PakCatalog {
    .source_key = request.source_key,
    .content_version = request.content_version,
    .catalog_digest = ComputeCatalogDigest(
      request.source_key, request.content_version, entries),
    .entries = std::move(entries),
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
  std::vector<std::vector<data::AssetKey>>
    planned_resource_dependent_asset_keys;
  std::vector<pak::PakScriptSlotPlan> script_slots;
  std::vector<OwnedScriptSlotSource> owned_script_slots;
  std::unordered_set<size_t> source_orders_with_owned_script_slots;
  std::unordered_map<data::AssetKey, std::vector<std::byte>>
    rewritten_asset_payloads;
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
    state.output.diagnostics, [](const pak::PakDiagnostic& diagnostic) -> bool {
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

    const auto key_matches = state.assets.at(position).key == asset_key;
    EnforceStageInvariant(state, key_matches,
      "pak.plan.stage.collect.asset_position_key_mismatch",
      "Source collection asset_positions key does not match assets[position].");
  }
}

// Temporary decoding state for one source. Its lifetime ends after dependency
// ownership has been attached to the planning records.
struct SourceCollection final {
  data::CookedSourceKind kind;
  std::filesystem::path root;
  size_t order;
  std::vector<lc::Inspection::AssetEntry> assets;
  std::vector<lc::Inspection::FileEntry> files;
  std::unordered_map<data::AssetKey, uint64_t> descriptor_offsets;
  std::unordered_map<lc::FileKind, uint64_t> file_offsets;
  std::optional<content::PakFile> pak_file;
  core::PakFooter footer {};
  std::vector<data::AssetKey> asset_keys;
  SourceResourceFiles resources;
  SourceResourceDigestState digests;
  uint32_t script_param_record_count = 0U;
};

auto ProjectPakResourceFiles(SourceCollection& context) -> void
{
  auto& source_files = context.files;
  auto& file_offsets = context.file_offsets;
  auto& pak_footer = context.footer;

  struct Projection {
    lc::FileKind table_kind;
    lc::FileKind data_kind;
    const core::ResourceTable* table;
    const core::ResourceRegion* region;
    uint32_t entry_size;
  };
  const auto projections = std::array {
    Projection {
      .table_kind = lc::FileKind::kTexturesTable,
      .data_kind = lc::FileKind::kTexturesData,
      .table = &pak_footer.texture_table,
      .region = &pak_footer.texture_region,
      .entry_size = sizeof(core::TextureResourceDesc),
    },
    Projection {
      .table_kind = lc::FileKind::kBuffersTable,
      .data_kind = lc::FileKind::kBuffersData,
      .table = &pak_footer.buffer_table,
      .region = &pak_footer.buffer_region,
      .entry_size = sizeof(core::BufferResourceDesc),
    },
    Projection {
      .table_kind = lc::FileKind::kScriptsTable,
      .data_kind = lc::FileKind::kScriptsData,
      .table = &pak_footer.script_resource_table,
      .region = &pak_footer.script_region,
      .entry_size = sizeof(script::ScriptResourceDesc),
    },
    Projection {
      .table_kind = lc::FileKind::kPhysicsTable,
      .data_kind = lc::FileKind::kPhysicsData,
      .table = &pak_footer.physics_resource_table,
      .region = &pak_footer.physics_region,
      .entry_size = sizeof(physics::PhysicsResourceDesc),
    },
  };
  for (const auto& projection : projections) {
    if (projection.table->count == 0U) {
      continue;
    }
    if (projection.table->entry_size != projection.entry_size) {
      throw std::runtime_error(
        "PAK resource table does not use the current record layout");
    }
    source_files.push_back(lc::FileEntry {
      .kind = projection.table_kind,
      .relpath = {},
      .size
      = static_cast<uint64_t>(projection.table->count) * projection.entry_size,
    });
    source_files.push_back(lc::FileEntry {
      .kind = projection.data_kind,
      .relpath = {},
      .size = projection.region->size,
    });
    file_offsets.emplace(projection.table_kind, projection.table->offset);
    file_offsets.emplace(projection.data_kind, projection.region->offset);
  }
}

auto LoadSourceMetadata(PlanningState& state, SourceCollection& context) -> void
{
  auto& source_root = context.root;
  auto& source_assets = context.assets;
  auto& source_files = context.files;
  auto& descriptor_offsets = context.descriptor_offsets;
  auto& pak_file = context.pak_file;
  auto& pak_footer = context.footer;
  auto& diagnostics = state.output.diagnostics;

  if (context.kind == data::CookedSourceKind::kLooseCooked) {
    lc::Inspection inspection;
    inspection.LoadFromRoot(source_root);
    source_assets.assign(
      inspection.Assets().begin(), inspection.Assets().end());
    source_files.assign(inspection.Files().begin(), inspection.Files().end());
  } else {
    pak_file.emplace(source_root);
    const auto file_size = std::filesystem::file_size(source_root);
    serio::FileStream<> stream(source_root, std::ios::in);
    serio::Reader reader(stream);
    if (file_size < sizeof(pak_footer)
      || !reader.Seek(file_size - sizeof(pak_footer))
      || !serio::Load(reader, pak_footer)) {
      throw std::runtime_error("PAK footer could not be decoded");
    }
    for (const auto& entry : pak_file->Directory()) {
      const auto digest = ComputeDescriptorDigestFromPakEntry(
        *pak_file, entry, source_root, diagnostics);
      if (!digest) {
        continue;
      }
      source_assets.push_back(lc::AssetEntry {
        .key = entry.asset_key,
        .virtual_path = {},
        .descriptor_relpath = {},
        .descriptor_size = entry.desc_size,
        .asset_type = static_cast<uint8_t>(entry.asset_type),
        .descriptor_sha256 = *digest,
      });
      descriptor_offsets.emplace(entry.asset_key, entry.desc_offset);
    }
    for (const auto& entry : pak_file->BrowseIndex()) {
      state.browse_map[ToCanonicalVirtualPath(entry.virtual_path)]
        = entry.asset_key;
    }
    ProjectPakResourceFiles(context);
  }
}

auto CollectSourceAssets(PlanningState& state, SourceCollection& context)
  -> void
{
  using pak::PakBuildPhase;
  using pak::PakDiagnosticSeverity;
  auto& source_root = context.root;
  auto& source_order = context.order;
  auto& source_assets = context.assets;
  auto& descriptor_offsets = context.descriptor_offsets;
  auto& pak_file = context.pak_file;
  auto& source_asset_keys = context.asset_keys;
  auto& diagnostics = state.output.diagnostics;

  std::ranges::sort(source_assets,
    [](const lc::Inspection::AssetEntry& lhs,
      const lc::Inspection::AssetEntry& rhs) -> bool {
      return IsAssetKeyLess(lhs.key, rhs.key);
    });

  for (const auto& source_asset : source_assets) {
    const auto asset_type
      = static_cast<data::AssetType>(source_asset.asset_type);
    if (!IsKnownAssetType(asset_type)) {
      AddDiagnostic(diagnostics, PakDiagnosticSeverity::kError,
        PakBuildPhase::kPlanning, "pak.plan.asset_type_invalid",
        "Cooked asset entry has an invalid asset_type.", source_root);
      continue;
    }

    const auto descriptor_path = pak_file
      ? source_root
      : source_root / std::filesystem::path(source_asset.descriptor_relpath);
    auto descriptor_size = source_asset.descriptor_size;
    if (descriptor_size == 0U && !pak_file) {
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
      .descriptor_source_offset
      = pak_file ? descriptor_offsets.at(source_asset.key) : 0U,
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
      state.assets.at(position_it->second) = std::move(aggregated);
    }
    source_asset_keys.push_back(source_asset.key);
  }
}

auto CollectLooseScriptParameters(PlanningState& state,
  SourceCollection& context, const SourceFileSlice& file) -> void
{
  using pak::PakBuildPhase;
  using pak::PakDiagnosticSeverity;
  auto& resource_state = context.digests;
  auto& source_script_param_record_count = context.script_param_record_count;
  auto& diagnostics = state.output.diagnostics;

  const auto& file_path = file.path;
  const auto file_size = file.size;
  if ((file_size % sizeof(script::ScriptParamRecord)) != 0U) {
    AddDiagnostic(diagnostics, PakDiagnosticSeverity::kError,
      PakBuildPhase::kPlanning, "pak.plan.script_params_file_size_invalid",
      "script-bindings.data size is not divisible by "
      "ScriptParamRecord size.",
      file_path);
    return;
  }
  {
    const auto record_count64 = file_size / sizeof(script::ScriptParamRecord);
    if (record_count64 > kMaxCountAsUint64) {
      AddDiagnostic(diagnostics, PakDiagnosticSeverity::kError,
        PakBuildPhase::kPlanning, "pak.plan.script_params_count_too_large",
        "script-bindings.data contains too many ScriptParamRecord "
        "entries.",
        file_path);
      return;
    }

    uint64_t source_count_sum = 0;
    if (!SafeAdd(
          source_script_param_record_count, record_count64, source_count_sum)
      || source_count_sum > kMaxCountAsUint64) {
      AddDiagnostic(diagnostics, PakDiagnosticSeverity::kError,
        PakBuildPhase::kPlanning, "pak.plan.script_params_count_overflow",
        "Combined script-bindings.data ScriptParamRecord count "
        "overflowed uint32.",
        file_path);
      return;
    }
    source_script_param_record_count = static_cast<uint32_t>(source_count_sum);
  }
  resource_state.script_bindings_data_path = file_path;
  if (auto params_bytes = ReadFileSliceBytes(file, diagnostics,
        "pak.plan.script_params_data_read_failed",
        "Failed to read script-bindings.data for dependency "
        "analysis.");
    params_bytes.has_value()) {
    resource_state.raw_script_params = *params_bytes;
  }
}

auto CollectSourceFiles(PlanningState& state, SourceCollection& context) -> void
{
  auto& source_root = context.root;
  auto& source_files = context.files;
  auto& file_offsets = context.file_offsets;
  auto& pak_file = context.pak_file;
  auto& resource_files = context.resources;
  auto& source_contribution = state.source_contributions[context.order];
  auto& diagnostics = state.output.diagnostics;

  std::ranges::sort(source_files,
    [](const lc::Inspection::FileEntry& lhs,
      const lc::Inspection::FileEntry& rhs) -> bool {
      if (lhs.kind != rhs.kind) {
        return static_cast<uint16_t>(lhs.kind)
          < static_cast<uint16_t>(rhs.kind);
      }
      return lhs.relpath < rhs.relpath;
    });

  for (const auto& file_entry : source_files) {
    const auto file_path = pak_file
      ? source_root
      : source_root / std::filesystem::path(file_entry.relpath);
    const auto file_offset = pak_file ? file_offsets.at(file_entry.kind) : 0U;
    auto file_size = file_entry.size;
    if (file_size == 0U && !pak_file) {
      const auto measured = MeasureFileSize(file_path, diagnostics);
      if (!measured.has_value()) {
        continue;
      }
      file_size = *measured;
    }

    switch (file_entry.kind) {
    case data::loose_cooked::FileKind::kTexturesData:
      resource_files.textures_data = SourceFileSlice {
        .path = file_path,
        .size = file_size,
        .offset = file_offset,
      };
      break;
    case data::loose_cooked::FileKind::kBuffersData:
      resource_files.buffers_data = SourceFileSlice {
        .path = file_path,
        .size = file_size,
        .offset = file_offset,
      };
      break;
    case data::loose_cooked::FileKind::kScriptsData:
      resource_files.scripts_data = SourceFileSlice {
        .path = file_path,
        .size = file_size,
        .offset = file_offset,
      };
      break;
    case data::loose_cooked::FileKind::kScriptBindingsData:
      resource_files.script_bindings_data = SourceFileSlice {
        .path = file_path,
        .size = file_size,
        .offset = file_offset,
      };
      CollectLooseScriptParameters(
        state, context, *resource_files.script_bindings_data);
      break;
    case data::loose_cooked::FileKind::kPhysicsData:
      resource_files.physics_data = SourceFileSlice {
        .path = file_path,
        .size = file_size,
        .offset = file_offset,
      };
      break;
    case data::loose_cooked::FileKind::kTexturesTable:
      resource_files.textures_table = SourceFileSlice {
        .path = file_path,
        .size = file_size,
        .offset = file_offset,
      };
      AccumulateTableCountFromFile(file_path, file_size,
        sizeof(core::TextureResourceDesc),
        source_contribution.table_counts.texture_count, diagnostics,
        "pak.plan.texture_table_size_invalid");
      break;
    case data::loose_cooked::FileKind::kBuffersTable:
      resource_files.buffers_table = SourceFileSlice {
        .path = file_path,
        .size = file_size,
        .offset = file_offset,
      };
      AccumulateTableCountFromFile(file_path, file_size,
        sizeof(core::BufferResourceDesc),
        source_contribution.table_counts.buffer_count, diagnostics,
        "pak.plan.buffer_table_size_invalid");
      break;
    case data::loose_cooked::FileKind::kPhysicsTable:
      resource_files.physics_table = SourceFileSlice {
        .path = file_path,
        .size = file_size,
        .offset = file_offset,
      };
      AccumulateTableCountFromFile(file_path, file_size,
        sizeof(physics::PhysicsResourceDesc),
        source_contribution.table_counts.physics_count, diagnostics,
        "pak.plan.physics_table_size_invalid");
      break;
    case data::loose_cooked::FileKind::kScriptsTable:
      resource_files.scripts_table = SourceFileSlice {
        .path = file_path,
        .size = file_size,
        .offset = file_offset,
      };
      AccumulateTableCountFromFile(file_path, file_size,
        sizeof(script::ScriptResourceDesc),
        source_contribution.table_counts.script_resource_count, diagnostics,
        "pak.plan.scripts_table_size_invalid");
      break;
    case data::loose_cooked::FileKind::kScriptBindingsTable:
      resource_files.script_bindings_table = SourceFileSlice {
        .path = file_path,
        .size = file_size,
        .offset = file_offset,
      };
      break;
    case data::loose_cooked::FileKind::kUnknown:
      break;
    }
  }
}

auto CollectPakAudioResources(PlanningState& state, SourceCollection& context)
  -> void
{
  auto& source_root = context.root;
  auto& source_order = context.order;
  auto& pak_file = context.pak_file;
  auto& pak_footer = context.footer;
  auto& resource_files = context.resources;
  auto& source_contribution = state.source_contributions[context.order];
  auto& diagnostics = state.output.diagnostics;

  if (pak_file && pak_footer.audio_table.count != 0U) {
    if (pak_footer.audio_table.entry_size != sizeof(audio::AudioResourceDesc)) {
      throw std::runtime_error(
        "PAK audio table does not use the current record layout");
    }
    resource_files.audio_table = SourceFileSlice {
      .path = source_root,
      .size = static_cast<uint64_t>(pak_footer.audio_table.count)
        * sizeof(audio::AudioResourceDesc),
      .offset = pak_footer.audio_table.offset,
    };
    resource_files.audio_data = SourceFileSlice {
      .path = source_root,
      .size = pak_footer.audio_region.size,
      .offset = pak_footer.audio_region.offset,
    };
    source_contribution.table_counts.audio_count
      = AppendResourcesFromTable<audio::AudioResourceDesc>(
        *resource_files.audio_table, *resource_files.audio_data, "audio_region",
        "audio", source_order, state.pending_resources, diagnostics,
        "audio_table",
        [](const audio::AudioResourceDesc& record,
          PendingResource& pending) -> bool {
          pending.alignment
            = record.alignment == 0U ? kRegionAlignment : record.alignment;
          return true;
        },
        [](uint32_t, const audio::AudioResourceDesc&,
          PendingResource&) -> void { });
  }
}

auto CollectPakScriptBindings(PlanningState& state, SourceCollection& context)
  -> void
{
  auto& source_root = context.root;
  auto& source_order = context.order;
  auto& pak_file = context.pak_file;
  auto& pak_footer = context.footer;
  auto& resource_state = context.digests;
  auto& source_script_param_record_count = context.script_param_record_count;
  auto& source_contribution = state.source_contributions[context.order];

  if (pak_file && pak_footer.script_slot_table.count != 0U) {
    resource_state.script_bindings_data_path = source_root;
    resource_state.raw_script_slots
      = pak_file->ReadScriptSlotRecords(0U, pak_footer.script_slot_table.count);
    for (std::size_t i = 0; i < resource_state.raw_script_slots.size(); ++i) {
      auto& slot = resource_state.raw_script_slots.at(i);
      const auto source_offset = slot.params_array_offset;
      const auto params = pak_file->ReadScriptParamRecords({
        .absolute_offset = source_offset,
        .count = slot.params_count,
      });
      const auto normalized_offset = resource_state.raw_script_params.size();
      const auto params_bytes = std::as_bytes(std::span { params });
      resource_state.raw_script_params.insert(
        resource_state.raw_script_params.end(), params_bytes.begin(),
        params_bytes.end());
      slot.params_array_offset = normalized_offset;
      const auto param_index
        = normalized_offset / sizeof(script::ScriptParamRecord);
      if (param_index > kMaxCountAsUint64
        || params.size() > kMaxCountAsUint64 - param_index) {
        throw std::runtime_error(
          "PAK script parameter count exceeds uint32 bounds");
      }
      source_contribution.local_script_slots.push_back(pak::PakScriptSlotPlan {
        .slot_index = static_cast<uint32_t>(i),
        .script_asset_key = slot.script_asset_key,
        .params_array_index = static_cast<uint32_t>(param_index),
        .params_count = slot.params_count,
        .execution_order = slot.execution_order,
        .flags = slot.flags,
      });
      if (!params.empty()) {
        resource_state.script_param_source_offsets.emplace(
          normalized_offset, source_offset);
        state.pending_resources.push_back(PendingResource {
          .region_name = "script_region",
          .resource_kind = "script_param",
          .size_bytes = params_bytes.size(),
          .source_offset = source_offset,
          .descriptor_source_offset = 0U,
          .descriptor_size = 0U,
          .alignment = 1U,
          .source_order = source_order,
          .source_sort_key = normalized_offset,
          .source_local_index = std::nullopt,
          .resource_asset_key = std::nullopt,
          .dependent_asset_keys = {},
          .path = source_root,
          .descriptor_path = {},
        });
      }
    }
    source_contribution.table_counts.script_slot_count
      = resource_state.raw_script_slots.size();
    source_script_param_record_count
      = static_cast<uint32_t>(resource_state.raw_script_params.size()
        / sizeof(script::ScriptParamRecord));
  }
}

auto CollectTextureResources(PlanningState& state, SourceCollection& context)
  -> void
{
  auto& source_order = context.order;
  auto& resource_files = context.resources;
  auto& resource_state = context.digests;
  auto& diagnostics = state.output.diagnostics;

  if (resource_files.textures_table.has_value()
    && resource_files.textures_data.has_value()) {
    AppendResourcesFromTable<core::TextureResourceDesc>(
      *resource_files.textures_table, *resource_files.textures_data,
      "texture_region", "texture", source_order, state.pending_resources,
      diagnostics, "texture_table",
      [](const core::TextureResourceDesc& record,
        PendingResource& pending) -> bool {
        pending.alignment
          = record.alignment == 0U ? kRegionAlignment : record.alignment;
        return true;
      },
      [&resource_state, &state, &diagnostics](const uint32_t local_index,
        const core::TextureResourceDesc& record,
        PendingResource& pending) -> void {
        const auto pending_index = state.pending_resources.size();
        resource_state.texture_pending_positions.emplace(
          local_index, pending_index);
        if (const auto digest = ComputeNormalizedResourceDigest(
              record, pending,
              [](core::TextureResourceDesc& normalized) -> void {
                normalized.data_offset = 0U;
              },
              diagnostics);
          digest.has_value()) {
          resource_state.texture_digests.emplace(local_index, *digest);
        }
      });
  }
}

auto CollectBufferResources(PlanningState& state, SourceCollection& context)
  -> void
{
  auto& source_order = context.order;
  auto& resource_files = context.resources;
  auto& resource_state = context.digests;
  auto& diagnostics = state.output.diagnostics;

  if (resource_files.buffers_table.has_value()
    && resource_files.buffers_data.has_value()) {
    AppendResourcesFromTable<core::BufferResourceDesc>(
      *resource_files.buffers_table, *resource_files.buffers_data,
      "buffer_region", "buffer", source_order, state.pending_resources,
      diagnostics, "buffer_table",
      [](const core::BufferResourceDesc&, PendingResource&) -> bool {
        return true;
      },
      [&resource_state, &state, &diagnostics](const uint32_t local_index,
        const core::BufferResourceDesc& record,
        PendingResource& pending) -> void {
        const auto pending_index = state.pending_resources.size();
        resource_state.buffer_pending_positions.emplace(
          local_index, pending_index);
        if (const auto digest = ComputeNormalizedResourceDigest(
              record, pending,
              [](core::BufferResourceDesc& normalized) -> void {
                normalized.data_offset = 0U;
              },
              diagnostics);
          digest.has_value()) {
          resource_state.buffer_digests.emplace(local_index, *digest);
        }
      });
  }
}

auto CollectScriptResources(PlanningState& state, SourceCollection& context)
  -> void
{
  auto& source_order = context.order;
  auto& resource_files = context.resources;
  auto& resource_state = context.digests;
  auto& diagnostics = state.output.diagnostics;

  if (resource_files.scripts_table.has_value()
    && resource_files.scripts_data.has_value()) {
    AppendResourcesFromTable<script::ScriptResourceDesc>(
      *resource_files.scripts_table, *resource_files.scripts_data,
      "script_region", "script", source_order, state.pending_resources,
      diagnostics, "script_resource_table",
      [](const script::ScriptResourceDesc&, PendingResource&) -> bool {
        return true;
      },
      [&resource_state, &state, &diagnostics](const uint32_t local_index,
        const script::ScriptResourceDesc& record,
        PendingResource& pending) -> void {
        const auto pending_index = state.pending_resources.size();
        resource_state.script_pending_positions.emplace(
          local_index, pending_index);
        if (const auto digest = ComputeNormalizedResourceDigest(
              record, pending,
              [](script::ScriptResourceDesc& normalized) -> void {
                normalized.data_offset = 0U;
              },
              diagnostics);
          digest.has_value()) {
          resource_state.script_digests.emplace(local_index, *digest);
        }
      });
  }
}

auto CollectPhysicsResources(PlanningState& state, SourceCollection& context)
  -> void
{
  auto& source_order = context.order;
  auto& resource_files = context.resources;
  auto& resource_state = context.digests;
  auto& diagnostics = state.output.diagnostics;

  if (resource_files.physics_table.has_value()
    && resource_files.physics_data.has_value()) {
    AppendResourcesFromTable<physics::PhysicsResourceDesc>(
      *resource_files.physics_table, *resource_files.physics_data,
      "physics_region", "physics", source_order, state.pending_resources,
      diagnostics, "physics_resource_table",
      [](const physics::PhysicsResourceDesc& record,
        PendingResource& pending) -> bool {
        pending.resource_asset_key = record.resource_asset_key;
        return true;
      },
      [&resource_state, &state, &diagnostics](const uint32_t local_index,
        const physics::PhysicsResourceDesc& record,
        PendingResource& pending) -> void {
        const auto pending_index = state.pending_resources.size();
        resource_state.physics_pending_positions.emplace(
          local_index, pending_index);
        resource_state.physics_index_by_asset_key.emplace(
          record.resource_asset_key, local_index);
        if (const auto digest = ComputeNormalizedResourceDigest(
              record, pending,
              [](physics::PhysicsResourceDesc& normalized) -> void {
                normalized.data_offset = 0U;
              },
              diagnostics);
          digest.has_value()) {
          resource_state.physics_digests.emplace(local_index, *digest);
        }
      });
  }
}

auto CollectLooseScriptBindings(PlanningState& state, SourceCollection& context)
  -> void
{
  using pak::PakBuildPhase;
  using pak::PakDiagnosticSeverity;
  auto& resource_files = context.resources;
  auto& resource_state = context.digests;
  auto& source_script_param_record_count = context.script_param_record_count;
  auto& source_contribution = state.source_contributions[context.order];
  auto& diagnostics = state.output.diagnostics;

  if (resource_files.script_bindings_table.has_value()) {
    if (source_contribution.table_counts.script_slot_count
      > kMaxCountAsUint64) {
      AddDiagnostic(diagnostics, PakDiagnosticSeverity::kError,
        PakBuildPhase::kPlanning, "pak.plan.script_slot_index_overflow",
        "Combined script slot count overflowed uint32.",
        resource_files.script_bindings_table->path);
    } else {
      const auto slot_context = ScriptSlotReadContext {
        .slot_index_base = static_cast<uint32_t>(
          source_contribution.table_counts.script_slot_count),
        .params_array_index_base = 0U,
        .source_params_record_count = source_script_param_record_count,
      };
      const auto parsed_slots
        = ReadScriptSlotsFromTable(resource_files.script_bindings_table->path,
          slot_context, source_contribution.local_script_slots, diagnostics);
      uint64_t script_slot_count_sum = 0;
      if (!SafeAdd(source_contribution.table_counts.script_slot_count,
            parsed_slots, script_slot_count_sum)
        || script_slot_count_sum > kMaxCountAsUint64) {
        AddDiagnostic(diagnostics, PakDiagnosticSeverity::kError,
          PakBuildPhase::kPlanning, "pak.plan.script_slot_index_overflow",
          "Combined script slot count overflowed uint32.",
          resource_files.script_bindings_table->path);
      } else {
        source_contribution.table_counts.script_slot_count
          = script_slot_count_sum;
      }
    }

    auto raw_slots = std::vector<script::ScriptSlotRecord> {};
    if (ReadFixedRecordFile<script::ScriptSlotRecord>(
          *resource_files.script_bindings_table, raw_slots, diagnostics,
          {
            .size_invalid = "pak.plan.script_slot_table_size_invalid",
            .read_failed = "pak.plan.script_slot_table_read_failed",
            .too_large = "pak.plan.script_slot_table_too_large",
            .label = "script_slot_table",
          })) {
      resource_state.raw_script_slots = std::move(raw_slots);
    }
  }

  source_contribution.script_param_record_count
    = source_script_param_record_count;
}

template <typename AddReference>
auto CollectMaterialResourceReferences(
  std::span<const std::byte> bytes, const AddReference& add_texture_ref) -> void
{
  if (IsValidDescriptorHeader(
        bytes, data::AssetType::kMaterial, render::kMaterialAssetVersion)
    && bytes.size() >= sizeof(render::MaterialAssetDesc)) {
    auto desc = render::MaterialAssetDesc {};
    std::memcpy(std::addressof(desc), bytes.data(), sizeof(desc));
    if ((desc.flags & render::kMaterialFlag_NoTextureSampling) == 0U) {
      add_texture_ref(desc.base_color_texture);
      add_texture_ref(desc.normal_texture);
      add_texture_ref(desc.metallic_texture);
      add_texture_ref(desc.roughness_texture);
      add_texture_ref(desc.ambient_occlusion_texture);
      add_texture_ref(desc.emissive_texture);
      add_texture_ref(desc.specular_texture);
      add_texture_ref(desc.sheen_color_texture);
      add_texture_ref(desc.clearcoat_texture);
      add_texture_ref(desc.clearcoat_normal_texture);
      add_texture_ref(desc.transmission_texture);
      add_texture_ref(desc.thickness_texture);
    }
  }
}

template <typename AddReference>
auto CollectGeometryResourceReferences(
  std::span<const std::byte> bytes, const AddReference& add_buffer_ref) -> void
{
  if (IsValidDescriptorHeader(
        bytes, data::AssetType::kGeometry, geometry::kGeometryAssetVersion)
    && bytes.size() >= sizeof(geometry::GeometryAssetDesc)) {
    auto desc = geometry::GeometryAssetDesc {};
    std::memcpy(std::addressof(desc), bytes.data(), sizeof(desc));
    size_t cursor = sizeof(desc);
    auto geometry_valid = true;
    for (uint32_t lod = 0; lod < desc.lod_count && geometry_valid; ++lod) {
      if ((cursor + sizeof(geometry::MeshDesc)) > bytes.size()) {
        geometry_valid = false;
        break;
      }
      auto mesh = geometry::MeshDesc {};
      std::memcpy(
        std::addressof(mesh), bytes.subspan(cursor).data(), sizeof(mesh));
      cursor += sizeof(mesh);

      if (mesh.IsStandard()) {
        add_buffer_ref(mesh.info.standard.vertex_buffer);
        add_buffer_ref(mesh.info.standard.index_buffer);
      } else if (mesh.IsSkinned()) {
        add_buffer_ref(mesh.info.skinned.vertex_buffer);
        add_buffer_ref(mesh.info.skinned.index_buffer);
        add_buffer_ref(mesh.info.skinned.joint_index_buffer);
        add_buffer_ref(mesh.info.skinned.joint_weight_buffer);
        add_buffer_ref(mesh.info.skinned.inverse_bind_buffer);
        add_buffer_ref(mesh.info.skinned.joint_remap_buffer);
      } else if (mesh.IsProcedural()) {
        const auto params_size
          = static_cast<size_t>(mesh.info.procedural.params_size);
        if ((cursor + params_size) > bytes.size()) {
          geometry_valid = false;
          break;
        }
        cursor += params_size;
      }

      for (uint32_t sub = 0; sub < mesh.submesh_count && geometry_valid;
        ++sub) {
        if ((cursor + sizeof(geometry::SubMeshDesc)) > bytes.size()) {
          geometry_valid = false;
          break;
        }
        auto submesh = geometry::SubMeshDesc {};
        std::memcpy(std::addressof(submesh), bytes.subspan(cursor).data(),
          sizeof(submesh));
        cursor += sizeof(submesh);
        const auto views_bytes = static_cast<size_t>(submesh.mesh_view_count)
          * sizeof(geometry::MeshViewDesc);
        if ((cursor + views_bytes) > bytes.size()) {
          geometry_valid = false;
          break;
        }
        cursor += views_bytes;
      }
    }
  }
}

template <typename AddReference>
auto CollectScriptResourceReferences(
  std::span<const std::byte> bytes, const AddReference& add_script_ref) -> void
{
  if (bytes.size() >= sizeof(script::ScriptAssetDesc)) {
    core::AssetHeader header {};
    std::memcpy(std::addressof(header), bytes.data(), sizeof(header));
    if (header.asset_type != static_cast<uint8_t>(data::AssetType::kScript)) {
      return;
    }
    auto desc = script::ScriptAssetDesc {};
    std::memcpy(std::addressof(desc), bytes.data(), sizeof(desc));
    const auto external_only
      = (desc.flags & script::ScriptAssetFlags::kAllowExternalSource)
        == script::ScriptAssetFlags::kAllowExternalSource
      && desc.bytecode_resource_index == core::kNoResourceIndex
      && desc.source_resource_index == core::kNoResourceIndex;
    if (!external_only) {
      add_script_ref(desc.bytecode_resource_index);
      add_script_ref(desc.source_resource_index);
    }
  }
}

auto CollectAssetDependencies(PlanningState& state, SourceCollection& context,
  AggregatedAsset& asset) -> void
{
  auto& source_order = context.order;
  auto& resource_state = context.digests;
  auto& diagnostics = state.output.diagnostics;

  auto inputs = std::vector<std::pair<uint16_t, oxygen::base::Sha256Digest>> {};
  const auto descriptor_bytes = ReadSourceDescriptorBytes(asset, diagnostics);
  if (descriptor_bytes.has_value()) {
    const auto bytes = std::span<const std::byte>(
      descriptor_bytes->data(), descriptor_bytes->size());

    const auto add_texture_ref = [&asset, &resource_state, &state, &inputs](
                                   const core::ResourceIndexT index) -> void {
      if (index == core::kNoResourceIndex) {
        return;
      }
      const auto local_index = static_cast<uint32_t>(index);
      if (AddResourceDigestInput(
            resource_state.texture_digests, local_index, inputs, 0x0001U)) {
        AttachDependentAssetToPendingIndex(state.pending_resources,
          FindPendingIndexByLocalIndex(
            resource_state.texture_pending_positions, local_index),
          asset.key);
      }
    };
    const auto add_buffer_ref = [&asset, &resource_state, &state, &inputs](
                                  const core::ResourceIndexT index) -> void {
      if (index == core::kNoResourceIndex) {
        return;
      }
      const auto local_index = static_cast<uint32_t>(index);
      if (AddResourceDigestInput(
            resource_state.buffer_digests, local_index, inputs, 0x0002U)) {
        AttachDependentAssetToPendingIndex(state.pending_resources,
          FindPendingIndexByLocalIndex(
            resource_state.buffer_pending_positions, local_index),
          asset.key);
      }
    };
    const auto add_script_ref = [&asset, &resource_state, &state, &inputs](
                                  const core::ResourceIndexT index) -> void {
      if (index == core::kNoResourceIndex) {
        return;
      }
      const auto local_index = static_cast<uint32_t>(index);
      if (AddResourceDigestInput(
            resource_state.script_digests, local_index, inputs, 0x0003U)) {
        AttachDependentAssetToPendingIndex(state.pending_resources,
          FindPendingIndexByLocalIndex(
            resource_state.script_pending_positions, local_index),
          asset.key);
      }
    };

    switch (asset.asset_type) {
    case data::AssetType::kMaterial:
      CollectMaterialResourceReferences(bytes, add_texture_ref);
      break;
    case data::AssetType::kGeometry:
      CollectGeometryResourceReferences(bytes, add_buffer_ref);
      break;
    case data::AssetType::kScript:
      CollectScriptResourceReferences(bytes, add_script_ref);
      break;
    case data::AssetType::kCollisionShape: {
      if (IsValidDescriptorHeader(bytes, data::AssetType::kCollisionShape,
            physics::kCollisionShapeAssetVersion)
        && bytes.size() >= sizeof(physics::CollisionShapeAssetDesc)) {
        auto desc = physics::CollisionShapeAssetDesc {};
        std::memcpy(std::addressof(desc), bytes.data(), sizeof(desc));
        if (!desc.cooked_shape_ref.payload_asset_key.IsNil()) {
          const auto physics_it
            = resource_state.physics_index_by_asset_key.find(
              desc.cooked_shape_ref.payload_asset_key);
          if (physics_it != resource_state.physics_index_by_asset_key.end()
            && AddResourceDigestInput(resource_state.physics_digests,
              physics_it->second, inputs, 0x0004U)) {
            AttachDependentAssetToPendingIndex(state.pending_resources,
              FindPendingIndexByLocalIndex(
                resource_state.physics_pending_positions, physics_it->second),
              asset.key);
          }
        }
      }
      break;
    }
    case data::AssetType::kScene: {
      try {
        const auto scene = data::SceneAsset(asset.key, bytes);
        if (const auto post = scene.TryGetPostProcessVolumeEnvironment();
          post && post->auto_exposure_metering_mask != core::kNoResourceIndex) {
          const auto u_mask_index = post->auto_exposure_metering_mask.get();
          if (!resource_state.texture_digests.contains(u_mask_index)) {
            AddDiagnostic(diagnostics, pak::PakDiagnosticSeverity::kError,
              pak::PakBuildPhase::kPlanning,
              "pak.plan.scene_mask_source_invalid",
              "Scene exposure mask index is outside the source texture "
              "table",
              asset.descriptor_path);
            break;
          }
          add_texture_ref(post->auto_exposure_metering_mask);
        }
      } catch (const std::exception& error) {
        AddDiagnostic(diagnostics, pak::PakDiagnosticSeverity::kError,
          pak::PakBuildPhase::kPlanning, "pak.plan.scene_descriptor_invalid",
          error.what(), asset.descriptor_path);
        break;
      }
      (void)AppendSceneScriptSlotInputs(asset, *descriptor_bytes, source_order,
        state.policy.mode == pak::PakPlanMode::kPatch, resource_state,
        state.pending_resources, state.owned_script_slots,
        state.source_orders_with_owned_script_slots, inputs, diagnostics);
      break;
    }
    default:
      break;
    }
  }

  std::ranges::sort(inputs, [](const auto& lhs, const auto& rhs) -> auto {
    if (lhs.first != rhs.first) {
      return lhs.first < rhs.first;
    }
    return lhs.second < rhs.second;
  });
  asset.transitive_resource_digest = inputs.empty()
    ? asset.descriptor_digest
    : AggregateTransitiveDigest(
        std::span<const std::pair<uint16_t, oxygen::base::Sha256Digest>>(
          inputs.data(), inputs.size()));
}

auto CollectSourceDependencies(PlanningState& state, SourceCollection& context)
  -> void
{
  auto& source_asset_keys = context.asset_keys;

  for (const auto& key : source_asset_keys) {
    const auto position_it = state.asset_positions.find(key);
    if (position_it == state.asset_positions.end()) {
      continue;
    }

    auto& asset = state.assets.at(position_it->second);
    CollectAssetDependencies(state, context, asset);
  }
}

auto AppendUnownedScriptParameters(
  PlanningState& state, SourceCollection& context) -> void
{
  auto& source_order = context.order;
  auto& resource_files = context.resources;

  if (resource_files.script_bindings_data.has_value()
    && (state.policy.mode != pak::PakPlanMode::kPatch
      || !state.source_orders_with_owned_script_slots.contains(source_order))) {
    state.pending_resources.push_back(PendingResource {
      .region_name = "script_region",
      .resource_kind = "script_param",
      .size_bytes = resource_files.script_bindings_data->size,
      .source_offset = resource_files.script_bindings_data->offset,
      .descriptor_source_offset = 0U,
      .descriptor_size = 0U,
      .alignment = 1U,
      .source_order = source_order,
      .source_sort_key = 0U,
      .source_local_index = std::nullopt,
      .resource_asset_key = std::nullopt,
      .dependent_asset_keys = {},
      .path = resource_files.script_bindings_data->path,
      .descriptor_path = {},
    });
  }
}

auto CollectOneSource(PlanningState& state, SourceCollection& context) -> void
{
  LoadSourceMetadata(state, context);
  CollectSourceAssets(state, context);
  CollectSourceFiles(state, context);
  CollectPakAudioResources(state, context);
  CollectPakScriptBindings(state, context);
  CollectTextureResources(state, context);
  CollectBufferResources(state, context);
  CollectScriptResources(state, context);
  CollectPhysicsResources(state, context);
  CollectLooseScriptBindings(state, context);
  CollectSourceDependencies(state, context);
  AppendUnownedScriptParameters(state, context);
}

auto CollectSourceData(PlanningState& state) -> void
{
  auto sources = state.request->sources;
  if (state.request->options.deterministic) {
    std::ranges::stable_sort(sources,
      [](const data::CookedSource& lhs, const data::CookedSource& rhs) -> bool {
        if (lhs.kind != rhs.kind) {
          return static_cast<uint8_t>(lhs.kind)
            < static_cast<uint8_t>(rhs.kind);
        }
        return lhs.path.generic_string() < rhs.path.generic_string();
      });
  }
  for (size_t order = 0; order < sources.size(); ++order) {
    const auto& source = sources.at(order);
    auto context = SourceCollection {
      .kind = source.kind,
      .root = ToCanonicalSourcePath(source.path),
      .order = order,
      .assets = {},
      .files = {},
      .descriptor_offsets = {},
      .file_offsets = {},
      .pak_file = {},
      .footer = {},
      .asset_keys = {},
      .resources = {},
      .digests = {},
      .script_param_record_count = 0U,
    };
    if (source.kind != data::CookedSourceKind::kLooseCooked
      && source.kind != data::CookedSourceKind::kPak) {
      AddDiagnostic(state.output.diagnostics,
        pak::PakDiagnosticSeverity::kError, pak::PakBuildPhase::kPlanning,
        "pak.plan.source_kind_unsupported",
        "Cooked source kind is not supported by planner.", context.root);
      continue;
    }
    try {
      CollectOneSource(state, context);
    } catch (const std::exception& error) {
      AddDiagnostic(state.output.diagnostics,
        pak::PakDiagnosticSeverity::kError, pak::PakBuildPhase::kPlanning,
        "pak.plan.source_load_failed",
        std::string("Failed to load current cooked source: ") + error.what(),
        context.root);
    }
  }
}

auto ClassifyPatchActionsAndFinalizeBrowse(PlanningState& state) -> void
{
  using pak::PakBuildPhase;
  using pak::PakDiagnosticSeverity;

  auto& diagnostics = state.output.diagnostics;
  auto& assets = state.assets;

  std::ranges::sort(
    assets, [](const AggregatedAsset& lhs, const AggregatedAsset& rhs) -> bool {
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

    std::erase_if(
      assets, [&emitted_patch_keys](const AggregatedAsset& asset) -> bool {
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
    browse_assets, [](const auto& lhs_ref, const auto& rhs_ref) -> auto {
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
    std::erase_if(
      state.browse_map, [&emitted_patch_keys](const auto& pair) -> auto {
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
  state.table_counts = {};
  state.script_slots.clear();
  state.script_param_record_count = 0;

  if (state.policy.mode != pak::PakPlanMode::kPatch) {
    uint64_t slot_index_base = 0;
    for (const auto source_order : state.included_source_orders) {
      const auto contribution_it
        = state.source_contributions.find(source_order);
      if (!CheckStageInvariant(state,
            contribution_it != state.source_contributions.end(),
            "pak.plan.stage.patch.missing_source_contribution",
            "Included source order has no source contribution record.")) {
        continue;
      }

      const auto& contribution = contribution_it->second;
      state.table_counts.texture_count
        += contribution.table_counts.texture_count;
      state.table_counts.buffer_count += contribution.table_counts.buffer_count;
      state.table_counts.audio_count += contribution.table_counts.audio_count;
      state.table_counts.script_resource_count
        += contribution.table_counts.script_resource_count;
      state.table_counts.script_slot_count
        += contribution.table_counts.script_slot_count;
      state.table_counts.physics_count
        += contribution.table_counts.physics_count;

      for (const auto& local_slot : contribution.local_script_slots) {
        const auto global_slot_index
          = static_cast<uint32_t>(slot_index_base + local_slot.slot_index);
        const auto global_param_offset
          = (state.script_param_record_count + local_slot.params_array_index);
        state.script_slots.push_back(pak::PakScriptSlotPlan {
          .slot_index = global_slot_index,
          .script_asset_key = local_slot.script_asset_key,
          .params_array_index = global_param_offset,
          .params_count = local_slot.params_count,
          .execution_order = local_slot.execution_order,
          .flags = local_slot.flags,
        });
      }

      slot_index_base += contribution.table_counts.script_slot_count;
      state.script_param_record_count += contribution.script_param_record_count;
    }
    return;
  }

  const auto included_source_orders = std::unordered_set<size_t>(
    state.included_source_orders.begin(), state.included_source_orders.end());
  auto emitted_asset_keys = std::unordered_set<data::AssetKey> {};
  emitted_asset_keys.reserve(state.assets.size());
  for (const auto& asset : state.assets) {
    emitted_asset_keys.insert(asset.key);
  }
  auto explicitly_owned_resource_kinds_by_source
    = std::unordered_map<size_t, std::unordered_set<std::string>> {};
  for (const auto& resource : state.pending_resources) {
    if (resource.dependent_asset_keys.empty()) {
      continue;
    }
    explicitly_owned_resource_kinds_by_source[resource.source_order].insert(
      resource.resource_kind);
  }

  std::erase_if(state.pending_resources,
    [&included_source_orders, &emitted_asset_keys,
      &explicitly_owned_resource_kinds_by_source](
      const PendingResource& resource) -> bool {
      if (!included_source_orders.contains(resource.source_order)) {
        return true;
      }
      if (resource.dependent_asset_keys.empty()) {
        const auto owned_kinds_it
          = explicitly_owned_resource_kinds_by_source.find(
            resource.source_order);
        return owned_kinds_it != explicitly_owned_resource_kinds_by_source.end()
          && owned_kinds_it->second.contains(resource.resource_kind);
      }
      return !std::ranges::any_of(resource.dependent_asset_keys,
        [&emitted_asset_keys](const data::AssetKey& key) -> bool {
          return emitted_asset_keys.contains(key);
        });
    });

  for (const auto& resource : state.pending_resources) {
    if (resource.resource_kind == "texture") {
      ++state.table_counts.texture_count;
    } else if (resource.resource_kind == "buffer") {
      ++state.table_counts.buffer_count;
    } else if (resource.resource_kind == "audio") {
      ++state.table_counts.audio_count;
    } else if (resource.resource_kind == "script") {
      ++state.table_counts.script_resource_count;
    } else if (resource.resource_kind == "physics") {
      ++state.table_counts.physics_count;
    }
  }

  auto owned_slots = std::vector<OwnedScriptSlotSource> {};
  owned_slots.reserve(state.owned_script_slots.size());
  for (const auto& slot : state.owned_script_slots) {
    if (emitted_asset_keys.contains(slot.asset_key)) {
      owned_slots.push_back(slot);
    }
  }
  std::ranges::sort(owned_slots,
    [](const OwnedScriptSlotSource& lhs,
      const OwnedScriptSlotSource& rhs) -> bool {
      if (lhs.source_order != rhs.source_order) {
        return lhs.source_order < rhs.source_order;
      }
      if (lhs.asset_key != rhs.asset_key) {
        return lhs.asset_key < rhs.asset_key;
      }
      return lhs.source_slot_index < rhs.source_slot_index;
    });

  auto source_orders_with_owned_slots = std::unordered_set<size_t> {};
  for (const auto& owned_slot : owned_slots) {
    source_orders_with_owned_slots.insert(owned_slot.source_order);
    state.script_slots.push_back(pak::PakScriptSlotPlan {
      .slot_index = static_cast<uint32_t>(state.script_slots.size()),
      .script_asset_key = owned_slot.record.script_asset_key,
      .params_array_index = state.script_param_record_count,
      .params_count = owned_slot.record.params_count,
      .execution_order = owned_slot.record.execution_order,
      .flags = owned_slot.record.flags,
    });
    state.script_param_record_count += owned_slot.record.params_count;
  }

  uint64_t slot_index_base = state.script_slots.size();
  for (const auto source_order : state.included_source_orders) {
    if (source_orders_with_owned_slots.contains(source_order)) {
      continue;
    }

    const auto contribution_it = state.source_contributions.find(source_order);
    if (!CheckStageInvariant(state,
          contribution_it != state.source_contributions.end(),
          "pak.plan.stage.patch.missing_source_contribution",
          "Included source order has no source contribution record.")) {
      continue;
    }

    const auto& contribution = contribution_it->second;
    for (const auto& local_slot : contribution.local_script_slots) {
      uint64_t global_slot_index = 0;
      if (!SafeAdd(slot_index_base, local_slot.slot_index, global_slot_index)
        || global_slot_index > kMaxCountAsUint64) {
        AddDiagnostic(state.output.diagnostics, PakDiagnosticSeverity::kError,
          PakBuildPhase::kPlanning, "pak.plan.script_slot_index_overflow",
          "Patch-local script slot index overflowed uint32 bounds.");
        continue;
      }

      uint64_t global_param_offset = 0;
      if (!SafeAdd(state.script_param_record_count,
            local_slot.params_array_index, global_param_offset)
        || global_param_offset > kMaxCountAsUint64) {
        AddDiagnostic(state.output.diagnostics, PakDiagnosticSeverity::kError,
          PakBuildPhase::kPlanning, "pak.plan.script_params_offset_overflow",
          "Patch-local script param offset overflowed uint32 bounds.");
        continue;
      }

      state.script_slots.push_back(pak::PakScriptSlotPlan {
        .slot_index = static_cast<uint32_t>(global_slot_index),
        .script_asset_key = local_slot.script_asset_key,
        .params_array_index = static_cast<uint32_t>(global_param_offset),
        .params_count = local_slot.params_count,
        .execution_order = local_slot.execution_order,
        .flags = local_slot.flags,
      });
    }

    slot_index_base += contribution.table_counts.script_slot_count;
    state.script_param_record_count += contribution.script_param_record_count;
  }

  state.table_counts.script_slot_count
    = static_cast<uint64_t>(state.script_slots.size());
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

  std::ranges::sort(state.script_slots,
    [](const pak::PakScriptSlotPlan& lhs, const pak::PakScriptSlotPlan& rhs)
      -> bool { return lhs.slot_index < rhs.slot_index; });

  for (const auto& slot : state.script_slots) {
    uint64_t end = 0;
    if (!SafeAdd(slot.params_array_index, slot.params_count, end)
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
  state.data_plan.script_slots = std::move(state.script_slots);

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

auto RewriteSceneScriptBindings(PlanningState& state) -> void
{
  if (state.policy.mode != pak::PakPlanMode::kPatch) {
    auto source_bases = std::unordered_map<size_t, uint64_t> {};
    uint64_t base = 0U;
    for (const auto source_order : state.included_source_orders) {
      source_bases.emplace(source_order, base);
      const auto count = state.source_contributions.at(source_order)
                           .table_counts.script_slot_count;
      if (!SafeAdd(base, count, base) || base > kMaxCountAsUint64) {
        throw std::runtime_error(
          "Combined script slot count exceeds uint32 bounds");
      }
    }
    for (auto& asset : state.assets) {
      if (asset.asset_type != data::AssetType::kScene) {
        continue;
      }
      auto bytes = ReadSourceDescriptorBytes(asset, state.output.diagnostics);
      if (!bytes) {
        continue;
      }
      const auto scene
        = data::SceneAsset(asset.key, std::span<const std::byte>(*bytes));
      const auto slots
        = scene.GetComponents<script::ScriptingComponentRecord>();
      const auto source_base = source_bases.at(asset.source_order);
      const auto source_count
        = state.source_contributions.at(asset.source_order)
            .table_counts.script_slot_count;
      auto remaps = std::unordered_map<uint32_t, uint32_t> {};
      for (const auto& binding : slots) {
        uint64_t end = 0U;
        if (!SafeAdd(binding.slot_start_index, binding.slot_count, end)
          || end > source_count) {
          throw std::runtime_error(
            "Scene script binding exceeds its source slot table");
        }
        if (source_base == 0U) {
          continue;
        }
        for (uint64_t local = binding.slot_start_index; local < end; ++local) {
          remaps.emplace(static_cast<uint32_t>(local),
            static_cast<uint32_t>(source_base + local));
        }
      }
      if (remaps.empty()) {
        continue;
      }
      if (!RewriteSceneScriptingComponentRanges(
            *bytes, remaps, state.output.diagnostics, asset.descriptor_path)) {
        continue;
      }
      asset.descriptor_digest
        = oxygen::base::ComputeSha256(std::span<const std::byte>(*bytes));
      asset.descriptor_source_offset = 0U;
      state.rewritten_asset_payloads.insert_or_assign(
        asset.key, std::move(*bytes));
    }
    return;
  }
  if (state.owned_script_slots.empty()) {
    return;
  }

  auto emitted_asset_keys = std::unordered_set<data::AssetKey> {};
  emitted_asset_keys.reserve(state.assets.size());
  for (const auto& asset : state.assets) {
    emitted_asset_keys.insert(asset.key);
  }

  auto owned_slots = std::vector<OwnedScriptSlotSource> {};
  owned_slots.reserve(state.owned_script_slots.size());
  for (const auto& slot : state.owned_script_slots) {
    if (emitted_asset_keys.contains(slot.asset_key)) {
      owned_slots.push_back(slot);
    }
  }
  if (owned_slots.empty()) {
    return;
  }

  std::ranges::sort(owned_slots,
    [](const OwnedScriptSlotSource& lhs,
      const OwnedScriptSlotSource& rhs) -> bool {
      if (lhs.source_order != rhs.source_order) {
        return lhs.source_order < rhs.source_order;
      }
      if (lhs.asset_key != rhs.asset_key) {
        return lhs.asset_key < rhs.asset_key;
      }
      return lhs.source_slot_index < rhs.source_slot_index;
    });

  auto rewritten_slot_indices = std::unordered_map<data::AssetKey,
    std::unordered_map<uint32_t, uint32_t>> {};
  auto next_slot_index = uint32_t { 0U };
  for (const auto& owned_slot : owned_slots) {
    rewritten_slot_indices[owned_slot.asset_key].emplace(
      owned_slot.source_slot_index, next_slot_index++);
  }

  for (auto& asset : state.assets) {
    if (asset.asset_type != data::AssetType::kScene) {
      continue;
    }

    const auto rewritten_it = rewritten_slot_indices.find(asset.key);
    if (rewritten_it == rewritten_slot_indices.end()) {
      continue;
    }

    auto descriptor_bytes
      = ReadSourceDescriptorBytes(asset, state.output.diagnostics);
    if (!descriptor_bytes.has_value()) {
      AddDiagnostic(state.output.diagnostics,
        pak::PakDiagnosticSeverity::kError, pak::PakBuildPhase::kPlanning,
        "pak.plan.patch_scene_descriptor_read_failed",
        "Failed to read scene descriptor bytes for patch-local script slot "
        "rewrite.",
        asset.descriptor_path);
      continue;
    }

    if (!RewriteSceneScriptingComponentRanges(*descriptor_bytes,
          rewritten_it->second, state.output.diagnostics,
          asset.descriptor_path)) {
      continue;
    }

    asset.descriptor_digest
      = oxygen::base::ComputeSha256(std::span<const std::byte>(
        descriptor_bytes->data(), descriptor_bytes->size()));
    asset.descriptor_source_offset = 0U;
    asset.descriptor_size = descriptor_bytes->size();
    state.rewritten_asset_payloads[asset.key] = std::move(*descriptor_bytes);
  }
}

auto ValidateScriptAndTableInvariants(PlanningState& state) -> void
{
  const auto& slots = state.data_plan.script_slots;
  for (size_t i = 1; i < slots.size(); ++i) {
    const auto sorted = slots.at(i - 1).slot_index <= slots.at(i).slot_index;
    EnforceStageInvariant(state, sorted,
      "pak.plan.stage.script.slot_ranges_unsorted",
      "Script param ranges are not sorted by slot_index.");
  }

  const auto has_core_table_count = state.data_plan.tables.size() >= 6U;
  EnforceStageInvariant(state, has_core_table_count,
    "pak.plan.stage.tables.core_tables_missing",
    "Planner table set is missing one or more core tables.");
}

using SourceIndexRemaps
  = std::unordered_map<size_t, std::unordered_map<uint32_t, uint32_t>>;
using ResourceIndexRemaps = std::unordered_map<std::string, SourceIndexRemaps>;

// Every active source-local reference follows the same final placement map.
[[nodiscard]] auto RewriteResourceReferences(PlanningState& state,
  AggregatedAsset& asset, const ResourceIndexRemaps& remaps) -> bool
{
  if (asset.asset_type != data::AssetType::kScene
    && asset.asset_type != data::AssetType::kMaterial
    && asset.asset_type != data::AssetType::kGeometry
    && asset.asset_type != data::AssetType::kScript) {
    return true;
  }
  auto bytes = std::optional<std::vector<std::byte>> {};
  if (const auto rewritten = state.rewritten_asset_payloads.find(asset.key);
    rewritten != state.rewritten_asset_payloads.end()) {
    bytes = rewritten->second;
  } else {
    bytes = ReadSourceDescriptorBytes(asset, state.output.diagnostics);
  }
  if (!bytes) {
    return false;
  }
  try {
    bool changed = false;
    const auto remap = [&](const core::ResourceIndexT index,
                         const std::string& kind) -> core::ResourceIndexT {
      if (index == core::kNoResourceIndex) {
        return index;
      }
      const auto domain = remaps.find(kind);
      if (domain == remaps.end() || !domain->second.contains(asset.source_order)
        || !domain->second.at(asset.source_order).contains(index.get())) {
        throw std::runtime_error(
          "Asset references an unplanned " + kind + " resource");
      }
      const auto mapped = domain->second.at(asset.source_order).at(index.get());
      if (mapped == core::kNoResourceIndex.get()) {
        throw std::runtime_error(
          "A resource reference cannot map to the null entry");
      }
      changed |= mapped != index.get();
      return core::ResourceIndexT { mapped };
    };
    serio::MemoryStream stream { std::span<std::byte>(*bytes) };
    serio::Writer writer(stream);
    const auto packed = writer.ScopedAlignment(1);
    const auto read = [&]<typename Record>(const size_t offset) -> Record {
      if (offset > bytes->size() || sizeof(Record) > bytes->size() - offset) {
        throw std::runtime_error("Asset record exceeds its descriptor bounds");
      }
      auto record = Record {};
      std::memcpy(std::addressof(record),
        std::span<const std::byte>(*bytes)
          .subspan(offset, sizeof(Record))
          .data(),
        sizeof(Record));
      return record;
    };
    const auto write = [&](const size_t offset, const uint32_t record) -> void {
      if (!stream.Seek(offset) || !writer.Write(record)) {
        throw std::runtime_error(
          "Asset resource reference could not be serialized");
      }
    };
    const auto remap_at
      = [&](const size_t offset, const std::string& kind) -> void {
      const auto index = read.template operator()<uint32_t>(offset);
      write(offset, remap(core::ResourceIndexT { index }, kind).get());
    };
    const auto view = std::span<const std::byte>(*bytes);
    if (asset.asset_type == data::AssetType::kScene) {
      const auto scene = data::SceneAsset(asset.key, view);
      if (const auto post = scene.TryGetPostProcessVolumeEnvironment(); post) {
        const auto records = scene.GetEnvironmentSystemRecords();
        const auto record
          = std::ranges::find_if(records, [](const auto& entry) -> auto {
              return entry.header.system_type
                == static_cast<uint32_t>(
                  world::EnvironmentComponentType::kPostProcessVolume);
            });
        if (record == records.end()) {
          throw std::runtime_error("Scene exposure record is missing");
        }
        write(record->record_offset
            + offsetof(world::PostProcessVolumeEnvironmentRecord,
              auto_exposure_metering_mask),
          remap(post->auto_exposure_metering_mask, "texture").get());
      }
    } else if (asset.asset_type == data::AssetType::kMaterial
      && IsValidDescriptorHeader(
        view, data::AssetType::kMaterial, render::kMaterialAssetVersion)
      && view.size() >= sizeof(render::MaterialAssetDesc)) {
      auto material = read.template operator()<render::MaterialAssetDesc>(0U);
      if ((material.flags & render::kMaterialFlag_NoTextureSampling) == 0U) {
        constexpr auto kTextureOffsets = std::array {
          offsetof(render::MaterialAssetDesc, base_color_texture),
          offsetof(render::MaterialAssetDesc, normal_texture),
          offsetof(render::MaterialAssetDesc, metallic_texture),
          offsetof(render::MaterialAssetDesc, roughness_texture),
          offsetof(render::MaterialAssetDesc, ambient_occlusion_texture),
          offsetof(render::MaterialAssetDesc, emissive_texture),
          offsetof(render::MaterialAssetDesc, specular_texture),
          offsetof(render::MaterialAssetDesc, sheen_color_texture),
          offsetof(render::MaterialAssetDesc, clearcoat_texture),
          offsetof(render::MaterialAssetDesc, clearcoat_normal_texture),
          offsetof(render::MaterialAssetDesc, transmission_texture),
          offsetof(render::MaterialAssetDesc, thickness_texture),
        };
        for (const auto offset : kTextureOffsets) {
          remap_at(offset, "texture");
        }
      }
    } else if (asset.asset_type == data::AssetType::kScript
      && view.size() >= sizeof(script::ScriptAssetDesc)
      && read.template operator()<core::AssetHeader>(0U).asset_type
        == static_cast<uint8_t>(data::AssetType::kScript)) {
      remap_at(
        offsetof(script::ScriptAssetDesc, bytecode_resource_index), "script");
      remap_at(
        offsetof(script::ScriptAssetDesc, source_resource_index), "script");
    } else if (asset.asset_type == data::AssetType::kGeometry
      && IsValidDescriptorHeader(
        view, data::AssetType::kGeometry, geometry::kGeometryAssetVersion)
      && view.size() >= sizeof(geometry::GeometryAssetDesc)) {
      const auto geometry_asset
        = read.template operator()<geometry::GeometryAssetDesc>(0U);
      size_t cursor = sizeof(geometry_asset);
      const auto advance = [&](const size_t amount) -> void {
        if (cursor > view.size() || amount > view.size() - cursor) {
          throw std::runtime_error("Geometry range exceeds descriptor bounds");
        }
        cursor += amount;
      };
      for (uint32_t lod = 0; lod < geometry_asset.lod_count; ++lod) {
        auto mesh = read.template operator()<geometry::MeshDesc>(cursor);
        if (mesh.IsStandard()) {
          remap_at(
            cursor + offsetof(geometry::MeshDesc, info.standard.vertex_buffer),
            "buffer");
          remap_at(
            cursor + offsetof(geometry::MeshDesc, info.standard.index_buffer),
            "buffer");
        } else if (mesh.IsSkinned()) {
          remap_at(
            cursor + offsetof(geometry::MeshDesc, info.skinned.vertex_buffer),
            "buffer");
          remap_at(
            cursor + offsetof(geometry::MeshDesc, info.skinned.index_buffer),
            "buffer");
          remap_at(cursor
              + offsetof(geometry::MeshDesc, info.skinned.joint_index_buffer),
            "buffer");
          remap_at(cursor
              + offsetof(geometry::MeshDesc, info.skinned.joint_weight_buffer),
            "buffer");
          remap_at(cursor
              + offsetof(geometry::MeshDesc, info.skinned.inverse_bind_buffer),
            "buffer");
          remap_at(cursor
              + offsetof(geometry::MeshDesc, info.skinned.joint_remap_buffer),
            "buffer");
        }
        advance(sizeof(mesh));
        if (mesh.IsProcedural()) {
          advance(mesh.info.procedural.params_size);
        }
        for (uint32_t sub = 0; sub < mesh.submesh_count; ++sub) {
          const auto submesh
            = read.template operator()<geometry::SubMeshDesc>(cursor);
          advance(sizeof(submesh));
          advance(static_cast<size_t>(submesh.mesh_view_count)
            * sizeof(geometry::MeshViewDesc));
        }
      }
    }
    if (changed) {
      asset.descriptor_digest
        = oxygen::base::ComputeSha256(std::span<const std::byte>(*bytes));
      asset.descriptor_source_offset = 0U;
      state.rewritten_asset_payloads.insert_or_assign(
        asset.key, std::move(*bytes));
    }
    return true;
  } catch (const std::exception& error) {
    AddDiagnostic(state.output.diagnostics, pak::PakDiagnosticSeverity::kError,
      pak::PakBuildPhase::kPlanning,
      "pak.plan.resource_reference_rewrite_failed", error.what(),
      asset.descriptor_path);
    return false;
  }
}

auto PlanFileLayout(PlanningState& state) -> void
{
  std::ranges::stable_sort(state.pending_resources,
    [patch = state.policy.mode == pak::PakPlanMode::kPatch](
      const PendingResource& lhs, const PendingResource& rhs) -> bool {
      const auto lhs_order = RegionOrder(lhs.region_name);
      const auto rhs_order = RegionOrder(rhs.region_name);
      if (lhs_order != rhs_order) {
        return lhs_order < rhs_order;
      }
      const auto lhs_resource_order = ResourceOrderWithinRegion(lhs);
      const auto rhs_resource_order = ResourceOrderWithinRegion(rhs);
      if (lhs_resource_order != rhs_resource_order) {
        return lhs_resource_order < rhs_resource_order;
      }
      // Patch slots are emitted by owner and slot index, followed by any
      // unowned source tables. Their parameter payloads must use that same
      // order, regardless of the original physical parameter offsets.
      const auto owned_params = patch && lhs.resource_kind == "script_param";
      if (owned_params
        && lhs.dependent_asset_keys.empty()
          != rhs.dependent_asset_keys.empty()) {
        return !lhs.dependent_asset_keys.empty();
      }
      if (lhs.source_order != rhs.source_order) {
        return lhs.source_order < rhs.source_order;
      }
      if (owned_params && !lhs.dependent_asset_keys.empty()) {
        if (lhs.dependent_asset_keys.front()
          != rhs.dependent_asset_keys.front()) {
          return lhs.dependent_asset_keys.front()
            < rhs.dependent_asset_keys.front();
        }
        return lhs.source_local_index < rhs.source_local_index;
      }
      if (lhs.source_sort_key != rhs.source_sort_key) {
        return lhs.source_sort_key < rhs.source_sort_key;
      }
      if (lhs.path.generic_string() != rhs.path.generic_string()) {
        return lhs.path.generic_string() < rhs.path.generic_string();
      }
      return lhs.source_offset < rhs.source_offset;
    });

  uint64_t cursor = state.data_plan.header.size_bytes;
  state.data_plan.regions.clear();
  state.data_plan.resources.clear();
  state.data_plan.resource_payload_sources.clear();
  state.data_plan.resource_descriptor_sources.clear();
  state.planned_resource_source_orders.clear();
  state.planned_resource_dependent_asset_keys.clear();
  const std::array<std::string_view, 5> region_names = {
    "texture_region",
    "buffer_region",
    "audio_region",
    "script_region",
    "physics_region",
  };

  std::unordered_map<std::string, uint32_t> resource_index_counters;
  auto resource_remaps = ResourceIndexRemaps {};
  auto added_null_records = std::unordered_set<std::string> {};
  for (const auto region_name : region_names) {
    cursor = AlignUp(cursor, AlignmentBytes { kRegionAlignment });
    const auto region_start = cursor;

    for (const auto& resource : state.pending_resources) {
      if (resource.region_name != region_name) {
        continue;
      }

      cursor = AlignUp(cursor, AlignmentBytes { resource.alignment });
      auto& next_index = resource_index_counters[resource.resource_kind];
      const bool reserves_zero = resource.resource_kind == "texture"
        || resource.resource_kind == "buffer"
        || resource.resource_kind == "script"
        || resource.resource_kind == "physics";
      if (next_index == 0U && reserves_zero
        && resource.source_local_index.value_or(0U) != 0U) {
        next_index = 1U;
        added_null_records.insert(resource.resource_kind);
      }
      const auto output_index = next_index++;
      if (reserves_zero && resource.source_local_index) {
        resource_remaps[resource.resource_kind][resource.source_order].emplace(
          *resource.source_local_index, output_index);
      }
      state.data_plan.resources.push_back(pak::PakResourcePlacementPlan {
        .resource_kind = resource.resource_kind,
        .resource_index = output_index,
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
          .inline_bytes = {},
        });
      state.data_plan.resource_descriptor_sources.push_back(
        pak::PakPayloadSourceSlicePlan {
          .source_path = resource.descriptor_path,
          .source_offset = resource.descriptor_source_offset,
          .size_bytes = resource.descriptor_size,
          .inline_bytes = {},
        });
      state.planned_resource_source_orders.push_back(resource.source_order);
      state.planned_resource_dependent_asset_keys.push_back(
        resource.dependent_asset_keys);
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
    const auto null_record_kinds = std::array {
      std::pair {
        std::string_view("texture_table"), std::string_view("texture") },
      std::pair {
        std::string_view("buffer_table"), std::string_view("buffer") },
      std::pair {
        std::string_view("script_resource_table"), std::string_view("script") },
      std::pair { std::string_view("physics_resource_table"),
        std::string_view("physics") },
    };
    const auto kind = std::ranges::find(null_record_kinds, table.table_name,
      [](const auto& entry) -> std::string_view { return entry.first; });
    if (kind != null_record_kinds.end()
      && added_null_records.contains(std::string(kind->second))) {
      if (table.count == std::numeric_limits<uint32_t>::max()) {
        throw std::runtime_error(
          "Resource table count cannot reserve its null entry");
      }
      ++table.count;
    }
    cursor = AlignUp(cursor, AlignmentBytes { table.alignment });
    table.offset = cursor;
    table.size_bytes = static_cast<uint64_t>(table.count) * table.entry_size;
    cursor += table.size_bytes;
  }

  state.data_plan.assets.clear();
  state.data_plan.asset_payload_sources.clear();
  state.data_plan.directory.entries.clear();
  for (auto& asset : state.assets) {
    if (!RewriteResourceReferences(state, asset, resource_remaps)) {
      continue;
    }
    cursor = AlignUp(cursor, AlignmentBytes { kAssetAlignment });

    if (asset.descriptor_size > std::numeric_limits<uint32_t>::max()) {
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
    auto payload_source = pak::PakPayloadSourceSlicePlan {
      .source_path = asset.descriptor_path,
      .source_offset = asset.descriptor_source_offset,
      .size_bytes = asset.descriptor_size,
      .inline_bytes = {},
    };
    if (const auto rewritten_it
      = state.rewritten_asset_payloads.find(asset.key);
      rewritten_it != state.rewritten_asset_payloads.end()) {
      payload_source.source_offset = 0U;
      payload_source.size_bytes = rewritten_it->second.size();
      payload_source.inline_bytes = rewritten_it->second;
    }
    state.data_plan.asset_payload_sources.push_back(std::move(payload_source));
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
    state.data_plan.directory.entries.at(i).entry_offset
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
      [](const pak::PakBrowseEntryPlan& lhs, const pak::PakBrowseEntryPlan& rhs)
        -> bool { return lhs.virtual_path < rhs.virtual_path; });

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
  EnforceStageInvariant(state,
    state.planned_resource_dependent_asset_keys.size()
      == state.data_plan.resources.size(),
    "pak.plan.stage.patch.resource_dependency_count_mismatch",
    "Planned resource dependency metadata count does not match resource "
    "plan.");

  auto emitted_asset_keys = std::unordered_set<data::AssetKey> {};
  emitted_asset_keys.reserve(state.assets.size());
  for (const auto& asset : state.assets) {
    emitted_asset_keys.insert(asset.key);
  }

  for (size_t i = 0; i < state.data_plan.resources.size(); ++i) {
    const auto& resource = state.data_plan.resources.at(i);
    const auto& dependents = state.planned_resource_dependent_asset_keys.at(i);
    if (dependents.empty()) {
      for (const auto& asset : state.assets) {
        if (asset.source_order != state.planned_resource_source_orders.at(i)) {
          continue;
        }
        state.data_plan.patch_closure.push_back(pak::PakPatchClosureRecord {
          .asset_key = asset.key,
          .resource_kind = resource.resource_kind,
          .resource_index = resource.resource_index,
        });
      }
      continue;
    }

    for (const auto& asset_key : dependents) {
      if (!emitted_asset_keys.contains(asset_key)) {
        continue;
      }
      state.data_plan.patch_closure.push_back(pak::PakPatchClosureRecord {
        .asset_key = asset_key,
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

  auto closure_count_by_asset = std::unordered_map<data::AssetKey, uint64_t> {};
  for (const auto& closure : state.data_plan.patch_closure) {
    ++closure_count_by_asset[closure.asset_key];
  }

  for (const auto& asset : state.assets) {
    uint64_t expected = 0;
    for (size_t i = 0; i < state.planned_resource_source_orders.size(); ++i) {
      const auto& dependents
        = state.planned_resource_dependent_asset_keys.at(i);
      if (dependents.empty()) {
        if (state.planned_resource_source_orders.at(i) == asset.source_order) {
          ++expected;
        }
        continue;
      }
      if (std::ranges::contains(dependents, asset.key)) {
        ++expected;
      }
    }
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

  const auto resource_descriptor_count_matches
    = state.data_plan.resource_descriptor_sources.size()
    == state.data_plan.resources.size();
  EnforceStageInvariant(state, resource_descriptor_count_matches,
    "pak.plan.stage.layout.resource_descriptor_source_count_mismatch",
    "Resource descriptor source slice count must match planned resources.");

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
    const auto& source = state.data_plan.resource_payload_sources.at(i);
    const auto& planned = state.data_plan.resources.at(i);
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
    const auto& source = state.data_plan.asset_payload_sources.at(i);
    const auto& planned = state.data_plan.assets.at(i);
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
    const auto key_matches = state.data_plan.directory.entries.at(i).asset_key
      == state.data_plan.assets.at(i).asset_key;
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
    [](const pak::PakDiagnostic& lhs, const pak::PakDiagnostic& rhs) -> bool {
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
    state.output.diagnostics, [](const pak::PakDiagnostic& diagnostic) -> bool {
      return diagnostic.severity == pak::PakDiagnosticSeverity::kError;
    });

  if (has_error) {
    return;
  }

  state.output.output_catalog
    = BuildOutputCatalog(*state.request, state.assets);
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
  PlanningState state(request);

  RunStage(state, "ValidatePlanningRequest",
    "pak.plan.stage.validate_request_exception",
    [&state] -> void { ValidatePlanningRequest(state); });
  RunStage(state, "CollectSourceData", "pak.plan.stage.collect_exception",
    [&state] -> void { CollectSourceData(state); });
  RunStage(state, "ValidateCollectSourceDataInvariants",
    "pak.plan.stage.collect_invariants_exception",
    [&state] -> void { ValidateCollectSourceDataInvariants(state); });
  RunStage(state, "ClassifyPatchActionsAndFinalizeBrowse",
    "pak.plan.stage.patch_classify_exception",
    [&state] -> void { ClassifyPatchActionsAndFinalizeBrowse(state); });
  RunStage(state, "ValidatePatchClassificationInvariants",
    "pak.plan.stage.patch_invariants_exception",
    [&state] -> void { ValidatePatchClassificationInvariants(state); });
  RunStage(state, "RebuildPatchLocalContributions",
    "pak.plan.stage.patch_local_rebuild_exception",
    [&state] -> void { RebuildPatchLocalContributions(state); });
  RunStage(state, "PreparePatchCompatibilityEnvelope",
    "pak.plan.stage.patch_compatibility_exception",
    [&state] -> void { PreparePatchCompatibilityEnvelope(state); });
  RunStage(state, "ValidatePatchContributionInvariants",
    "pak.plan.stage.patch_local_invariants_exception",
    [&state] -> void { ValidatePatchContributionInvariants(state); });
  RunStage(state, "FinalizeScriptAndTables",
    "pak.plan.stage.script_tables_exception",
    [&state] -> void { FinalizeScriptAndTables(state); });
  RunStage(state, "RewriteSceneScriptBindings",
    "pak.plan.stage.patch_scene_rewrite_exception",
    [&state] -> void { RewriteSceneScriptBindings(state); });
  RunStage(state, "ValidateScriptAndTableInvariants",
    "pak.plan.stage.script_tables_invariants_exception",
    [&state] -> void { ValidateScriptAndTableInvariants(state); });
  RunStage(state, "PlanFileLayout", "pak.plan.stage.layout_exception",
    [&state] -> void { PlanFileLayout(state); });
  RunStage(state, "BuildPatchClosure", "pak.plan.stage.patch_closure_exception",
    [&state] -> void { BuildPatchClosure(state); });
  RunStage(state, "ValidatePatchClosureInvariants",
    "pak.plan.stage.patch_closure_invariants_exception",
    [&state] -> void { ValidatePatchClosureInvariants(state); });
  RunStage(state, "ValidateLayoutInvariants",
    "pak.plan.stage.layout_invariants_exception",
    [&state] -> void { ValidateLayoutInvariants(state); });
  RunStage(state, "ValidateAndFinalizeResult",
    "pak.plan.stage.finalize_exception",
    [&state] -> void { ValidateAndFinalizeResult(state); });
  RunStage(state, "ValidateFinalizeInvariants",
    "pak.plan.stage.finalize_invariants_exception",
    [&state] -> void { ValidateFinalizeInvariants(state); });

  return state.output;
}

} // namespace oxygen::content::pak
