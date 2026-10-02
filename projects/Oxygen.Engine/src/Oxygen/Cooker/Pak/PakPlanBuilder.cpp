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
#include <ranges>
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
#include <Oxygen/Base/Span.h>
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
#include <Oxygen/Data/PhysicsSceneAsset.h>
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
  data::AssetReferences references;
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
  uint64_t physics_count = 0;
};

struct SourceContribution final {
  TableCounts table_counts {};
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

auto BuildOutputCatalog(const pak::PakBuildRequest& request,
  std::span<const AggregatedAsset> assets,
  std::span<const pak::PakPatchActionRecord> actions,
  const std::unordered_set<data::AssetKey>& tombstones) -> data::PakCatalog
{
  auto entries = std::vector<data::PakCatalogEntry> {};
  entries.reserve(assets.size());
  for (const auto& asset : assets) {
    entries.push_back(ToCatalogEntry(asset));
  }

  std::ranges::sort(entries,
    [](const data::PakCatalogEntry& lhs, const data::PakCatalogEntry& rhs)
      -> bool { return lhs.asset_key < rhs.asset_key; });

  auto catalog = data::PakCatalog {
    .source_key = request.source_key,
    .content_version = request.content_version,
    .catalog_digest = {},
    .entries = std::move(entries),
    .deleted = {},
    .bases = {},
  };
  if (request.mode == pak::BuildMode::kPatch) {
    for (const auto& base : request.base_catalogs) {
      catalog.bases.push_back({ .source_key = base.source_key,
        .content_version = base.content_version,
        .catalog_digest = base.catalog_digest });
    }
    for (const auto& action : actions) {
      if (action.action == pak::PakPatchAction::kDelete) {
        catalog.deleted.push_back(action.asset_key);
      }
    }
  } else {
    catalog.deleted.assign(tombstones.begin(), tombstones.end());
  }
  std::ranges::sort(catalog.deleted);
  const auto digest = catalog.ComputeDigest();
  if (!digest) {
    throw std::runtime_error(digest.error());
  }
  catalog.catalog_digest = *digest;
  return catalog;
}

struct PhysicsScenePair final {
  data::AssetKey scene_key {};
  data::AssetKey sidecar_key {};
};

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
  std::vector<PhysicsScenePair> physics_scene_pairs;
  std::unordered_map<data::AssetKey, size_t> asset_positions;
  std::unordered_set<data::AssetKey> tombstones;
  std::vector<data::PakCatalogBase> layers;
  std::vector<PendingResource> pending_resources;
  std::unordered_map<size_t, SourceContribution> source_contributions;
  std::vector<size_t> included_source_orders;
  std::vector<size_t> planned_resource_source_orders;
  std::vector<std::vector<data::AssetKey>>
    planned_resource_dependent_asset_keys;
  std::unordered_map<std::string, data::AssetKey> browse_map;
  TableCounts table_counts {};
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
  data::CookedSourceKind kind { data::CookedSourceKind::kLooseCooked };
  std::filesystem::path root;
  size_t order { 0 };
  std::vector<lc::Inspection::AssetEntry> assets;
  std::vector<lc::Inspection::FileEntry> files;
  std::unordered_map<data::AssetKey, uint64_t> descriptor_offsets;
  std::unordered_map<lc::FileKind, uint64_t> file_offsets;
  std::optional<content::PakFile> pak_file;
  core::PakFooter footer {};
  SourceResourceFiles resources;
  SourceResourceDigestState digests;
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
    state.layers.push_back({ .source_key = inspection.Guid(),
      .content_version = 0U,
      .catalog_digest = {} });
  } else {
    pak_file.emplace(source_root);
    const auto& catalog = pak_file->Catalog();
    if (const auto valid = catalog.ValidateBaseLayers(state.layers); !valid) {
      throw std::runtime_error(valid.error());
    }
    state.layers.push_back({ .source_key = catalog.source_key,
      .content_version = catalog.content_version,
      .catalog_digest = catalog.catalog_digest });
    for (const auto& key : catalog.deleted) {
      state.tombstones.insert(key);
    }
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
        .references = pak_file->ReadAssetReferences(entry.asset_key),
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
      .references = source_asset.references,
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
      auto& previous = state.assets.at(position_it->second);
      if (previous.source_order == source_order) {
        AddDiagnostic(diagnostics, PakDiagnosticSeverity::kError,
          PakBuildPhase::kPlanning, "pak.plan.duplicate_layer_asset",
          "A content layer defines the same AssetKey more than once.",
          source_root);
        continue;
      }
      if (previous.asset_type != aggregated.asset_type) {
        AddDiagnostic(diagnostics, PakDiagnosticSeverity::kError,
          PakBuildPhase::kPlanning, "pak.plan.asset_override_type_mismatch",
          "A content layer cannot change an existing AssetKey's asset type.",
          source_root);
        continue;
      }
      previous = std::move(aggregated);
    }
    state.tombstones.erase(source_asset.key);
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
    case data::loose_cooked::FileKind::kAuxiliary:
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
      [&state, &diagnostics, source_order](
        const physics::PhysicsResourceDesc& record,
        PendingResource& pending) -> bool {
        if (pending.source_local_index == 0U) {
          --state.source_contributions.at(source_order)
              .table_counts.physics_count;
          if (!record.resource_asset_key.IsNil() || record.size_bytes != 0U) {
            AddDiagnostic(diagnostics, pak::PakDiagnosticSeverity::kError,
              pak::PakBuildPhase::kPlanning,
              "pak.plan.physics_sentinel_invalid",
              "Physics resource row zero must be the absent sentinel.",
              pending.descriptor_path);
          }
          // Layout emits one output sentinel for all composed physics sources.
          return false;
        }
        if (record.resource_asset_key.IsNil()) {
          AddDiagnostic(diagnostics, pak::PakDiagnosticSeverity::kError,
            pak::PakBuildPhase::kPlanning,
            "pak.plan.physics_resource_key_missing",
            "A live physics resource requires a stable key.",
            pending.descriptor_path);
          return false;
        }
        pending.resource_asset_key = record.resource_asset_key;
        return true;
      },
      [&resource_state, &state, &diagnostics](const uint32_t local_index,
        const physics::PhysicsResourceDesc& record,
        PendingResource& pending) -> void {
        const auto pending_index = state.pending_resources.size();
        resource_state.physics_pending_positions.emplace(
          local_index, pending_index);
        if (!record.resource_asset_key.IsNil()
          && !resource_state.physics_index_by_asset_key
            .emplace(record.resource_asset_key, local_index)
            .second) {
          AddDiagnostic(diagnostics, pak::PakDiagnosticSeverity::kError,
            pak::PakBuildPhase::kPlanning,
            "pak.plan.duplicate_layer_physics_resource",
            "A content layer defines the same physics resource key twice.",
            pending.descriptor_path);
        }
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

struct AssetDependencyDigest {
  oxygen::base::Sha256Digest digest {};
  std::vector<size_t> resources {};
};

auto ComputeAssetDependencyDigest(std::vector<pak::PakDiagnostic>& diagnostics,
  const SourceResourceDigestState& resources,
  std::span<const SourceResourceDigestState> sources,
  const AggregatedAsset& asset) -> AssetDependencyDigest
{
  AssetDependencyDigest result;
  std::vector<std::pair<uint16_t, oxygen::base::Sha256Digest>> inputs;
  const auto append_resource = [&](const auto& digests, const auto& positions,
                                 const uint32_t index, const uint16_t tag) {
    if (!AddResourceDigestInput(digests, index, inputs, tag)) {
      AddDiagnostic(diagnostics, pak::PakDiagnosticSeverity::kError,
        pak::PakBuildPhase::kPlanning, "pak.plan.resource_binding_missing",
        "Asset binding does not identify a resource in its source.",
        asset.descriptor_path);
      return;
    }
    result.resources.push_back(positions.at(index));
  };
  for (const auto& binding : asset.references.Resources()) {
    if (binding.kind == data::ResourceKind::kTexture
      && (binding.index == core::kFallbackResourceIndex
        || binding.index == core::kErrorTextureResourceIndex)) {
      constexpr uint16_t kFallbackDigestTag = 0x1001U;
      constexpr uint16_t kErrorDigestTag = 0x1002U;
      inputs.emplace_back(binding.index == core::kFallbackResourceIndex
          ? kFallbackDigestTag
          : kErrorDigestTag,
        oxygen::base::Sha256Digest {});
      continue;
    }
    const auto tag = static_cast<uint16_t>(binding.kind);
    switch (binding.kind) {
    case data::ResourceKind::kBuffer:
      append_resource(resources.buffer_digests,
        resources.buffer_pending_positions, binding.index.get(), tag);
      break;
    case data::ResourceKind::kTexture:
      append_resource(resources.texture_digests,
        resources.texture_pending_positions, binding.index.get(), tag);
      break;
    case data::ResourceKind::kScript:
      append_resource(resources.script_digests,
        resources.script_pending_positions, binding.index.get(), tag);
      break;
    case data::ResourceKind::kPhysics:
      append_resource(resources.physics_digests,
        resources.physics_pending_positions, binding.index.get(), tag);
      break;
    }
  }
  for (const auto& reference : asset.references.Keys()) {
    if (reference.kind != data::KeyReferenceKind::kPhysicsResource) {
      continue;
    }
    const SourceResourceDigestState* selected = nullptr;
    uint32_t index = 0;
    for (const auto& candidate : std::views::reverse(sources)) {
      const auto found
        = candidate.physics_index_by_asset_key.find(reference.key);
      if (found != candidate.physics_index_by_asset_key.end()) {
        selected = &candidate;
        index = found->second;
        break;
      }
    }
    if (selected == nullptr) {
      AddDiagnostic(diagnostics, pak::PakDiagnosticSeverity::kError,
        pak::PakBuildPhase::kPlanning, "pak.plan.physics_binding_missing",
        "Asset references a missing keyed physics resource.",
        asset.descriptor_path);
      continue;
    }
    append_resource(selected->physics_digests,
      selected->physics_pending_positions, index,
      static_cast<uint16_t>(data::ResourceKind::kPhysics));
  }
  if (!asset.references.Keys().empty()) {
    const auto key_metadata = data::AssetReferences::Create({},
      std::vector<data::KeyReference>(
        asset.references.Keys().begin(), asset.references.Keys().end()));
    if (!key_metadata) {
      throw std::runtime_error(key_metadata.error());
    }
    const auto encoded = key_metadata->Encode();
    if (!encoded) {
      throw std::runtime_error(encoded.error());
    }
    constexpr uint16_t kKeyMetadataDigestTag = 0x2001U;
    inputs.emplace_back(
      kKeyMetadataDigestTag, oxygen::base::ComputeSha256(*encoded));
  }
  result.digest = inputs.empty() ? asset.descriptor_digest
                                 : AggregateTransitiveDigest(inputs);
  return result;
}

auto ValidatePhysicalCatalog(
  PlanningState& state, const SourceCollection& context) -> void
{
  if (!context.pak_file) {
    return;
  }
  for (const auto& entry : context.pak_file->Catalog().entries) {
    const auto found = state.asset_positions.find(entry.asset_key);
    if (found == state.asset_positions.end()) {
      continue;
    }
    const auto& asset = state.assets.at(found->second);
    if (asset.source_order != context.order) {
      continue;
    }
    const auto measured = ComputeAssetDependencyDigest(state.output.diagnostics,
      context.digests, std::span { &context.digests, 1U }, asset);
    if (asset.descriptor_digest != entry.descriptor_digest
      || measured.digest != entry.transitive_resource_digest) {
      AddDiagnostic(state.output.diagnostics,
        pak::PakDiagnosticSeverity::kError, pak::PakBuildPhase::kPlanning,
        "pak.plan.catalog_content_mismatch",
        "Embedded catalog does not match physical asset "
          + data::to_string(entry.asset_key),
        context.root);
    }
  }
}

auto CollectOneSource(PlanningState& state, SourceCollection& context) -> void
{
  LoadSourceMetadata(state, context);
  CollectSourceAssets(state, context);
  CollectSourceFiles(state, context);
  CollectPakAudioResources(state, context);
  CollectTextureResources(state, context);
  CollectBufferResources(state, context);
  CollectScriptResources(state, context);
  CollectPhysicsResources(state, context);
  ValidatePhysicalCatalog(state, context);
}

auto CollectSourceData(PlanningState& state) -> void
{
  // Source order is semantic priority, including in deterministic builds.
  const auto& sources = state.request->sources;
  std::vector<SourceResourceDigestState> resource_states(sources.size());
  for (size_t order = 0; order < sources.size(); ++order) {
    const auto& source = sources.at(order);
    auto context = SourceCollection {};
    context.kind = source.kind;
    context.root = ToCanonicalSourcePath(source.path);
    context.order = order;
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
      resource_states.at(order) = std::move(context.digests);
    } catch (const std::exception& error) {
      AddDiagnostic(state.output.diagnostics,
        pak::PakDiagnosticSeverity::kError, pak::PakBuildPhase::kPlanning,
        "pak.plan.source_load_failed",
        std::string("Failed to load current cooked source: ") + error.what(),
        context.root);
    }
  }
  // Resolve dependencies after every layer is known. Overridden descriptors
  // must neither require their old dependencies nor own output resources.
  std::erase_if(state.assets, [&](const AggregatedAsset& asset) {
    return state.tombstones.contains(asset.key);
  });
  state.asset_positions.clear();
  for (size_t index = 0; index < state.assets.size(); ++index) {
    state.asset_positions.emplace(state.assets.at(index).key, index);
  }
  std::erase_if(state.browse_map,
    [&](const auto& entry) { return state.tombstones.contains(entry.second); });
  for (auto& asset : state.assets) {
    const auto dependency
      = ComputeAssetDependencyDigest(state.output.diagnostics,
        resource_states.at(asset.source_order), resource_states, asset);
    asset.transitive_resource_digest = dependency.digest;
    for (const auto index : dependency.resources) {
      AddDependentAssetKey(state.pending_resources.at(index), asset.key);
    }
  }

  std::unordered_map<data::AssetKey, size_t> physics_winners;
  for (const auto& resource : state.pending_resources) {
    if (resource.resource_kind == "physics" && resource.resource_asset_key
      && !resource.resource_asset_key->IsNil()) {
      physics_winners.insert_or_assign(
        *resource.resource_asset_key, resource.source_order);
    }
  }
  std::erase_if(state.pending_resources, [&](const PendingResource& resource) {
    if (resource.resource_kind != "physics" || !resource.resource_asset_key
      || resource.resource_asset_key->IsNil()
      || physics_winners.at(*resource.resource_asset_key)
        == resource.source_order) {
      return false;
    }
    if (!resource.dependent_asset_keys.empty()) {
      AddDiagnostic(state.output.diagnostics,
        pak::PakDiagnosticSeverity::kError, pak::PakBuildPhase::kPlanning,
        "pak.plan.shadowed_physics_binding",
        "A numeric binding addresses a shadowed keyed physics resource.",
        resource.descriptor_path);
    }
    --state.source_contributions.at(resource.source_order)
        .table_counts.physics_count;
    return true;
  });
}

// Check the original pair before relocation; never repair stale authored input.
auto ValidateAssetReferences(PlanningState& state) -> void
{
  std::vector<size_t> dependency_counts(state.assets.size(), 0U);
  std::vector<std::vector<size_t>> dependents(state.assets.size());
  for (size_t owner = 0; owner < state.assets.size(); ++owner) {
    const auto& asset = state.assets.at(owner);
    for (const auto& reference : asset.references.Keys()) {
      if (reference.kind != data::KeyReferenceKind::kAsset) {
        continue;
      }
      const auto target = state.asset_positions.find(reference.key);
      if (target == state.asset_positions.end()) {
        AddDiagnostic(state.output.diagnostics,
          pak::PakDiagnosticSeverity::kError, pak::PakBuildPhase::kPlanning,
          "pak.plan.asset_reference_missing",
          "Asset " + data::to_string(asset.key) + " requires missing asset "
            + data::to_string(reference.key),
          asset.descriptor_path);
        continue;
      }
      const auto actual_type = state.assets.at(target->second).asset_type;
      if (actual_type != reference.expected_type) {
        AddDiagnostic(state.output.diagnostics,
          pak::PakDiagnosticSeverity::kError, pak::PakBuildPhase::kPlanning,
          "pak.plan.asset_reference_type_mismatch",
          "Asset " + data::to_string(asset.key) + " requires "
            + data::to_string(reference.expected_type) + " "
            + data::to_string(reference.key) + "; target is "
            + data::to_string(actual_type),
          asset.descriptor_path);
      }
      ++dependency_counts.at(owner);
      dependents.at(target->second).push_back(owner);
    }
  }

  // Iterative traversal also handles deep graphs without consuming the stack.
  std::vector<size_t> ready;
  ready.reserve(state.assets.size());
  for (size_t index = 0; index < dependency_counts.size(); ++index) {
    if (dependency_counts.at(index) == 0U) {
      ready.push_back(index);
    }
  }
  for (size_t next = 0; next < ready.size(); ++next) {
    for (const auto dependent : dependents.at(ready.at(next))) {
      if (--dependency_counts.at(dependent) == 0U) {
        ready.push_back(dependent);
      }
    }
  }
  if (ready.size() != state.assets.size()) {
    AddDiagnostic(state.output.diagnostics, pak::PakDiagnosticSeverity::kError,
      pak::PakBuildPhase::kPlanning, "pak.plan.asset_reference_cycle",
      "Hard asset references contain a cycle");
  }
}

auto ValidatePhysicsScenePairs(PlanningState& state) -> void
{
  std::unordered_map<data::AssetKey, const AggregatedAsset*> scenes;
  std::unordered_set<data::AssetKey> paired_scene_keys;
  for (const auto& asset : state.assets) {
    if (asset.asset_type == data::AssetType::kScene) {
      scenes.emplace(asset.key, &asset);
    }
  }
  for (const auto& asset : state.assets) {
    if (asset.asset_type != data::AssetType::kPhysicsScene) {
      continue;
    }
    try {
      const auto sidecar_bytes
        = ReadSourceDescriptorBytes(asset, state.output.diagnostics);
      if (!sidecar_bytes) {
        continue;
      }
      const auto sidecar = data::PhysicsSceneAsset(
        asset.key, std::span<const std::byte>(*sidecar_bytes));
      if (!paired_scene_keys.insert(sidecar.GetTargetSceneKey()).second) {
        throw std::runtime_error(
          "Multiple physics sidecars target the same scene");
      }
      const auto found = scenes.find(sidecar.GetTargetSceneKey());
      if (found == scenes.end()) {
        throw std::runtime_error("Physics sidecar target scene is missing");
      }
      const auto scene_bytes
        = ReadSourceDescriptorBytes(*found->second, state.output.diagnostics);
      if (!scene_bytes) {
        continue;
      }
      const auto scene = data::SceneAsset(
        found->first, std::span<const std::byte>(*scene_bytes));
      if (scene.GetNodes().size() != sidecar.GetTargetNodeCount()
        || !std::ranges::equal(sidecar.GetTargetSceneContentHash(),
          oxygen::base::ComputeSha256(*scene_bytes))) {
        throw std::runtime_error(
          "Physics sidecar does not match its target scene");
      }
      state.physics_scene_pairs.push_back(PhysicsScenePair {
        .scene_key = found->first,
        .sidecar_key = asset.key,
      });
    } catch (const std::exception& error) {
      AddDiagnostic(state.output.diagnostics,
        pak::PakDiagnosticSeverity::kError, pak::PakBuildPhase::kPlanning,
        "pak.plan.physics_scene_pair_invalid", error.what(),
        asset.descriptor_path);
    }
  }
}

// The current format loads a scene and its sidecars from the same mount.
auto IncludePhysicsScenePatchPartners(
  PlanningState& state, std::unordered_set<data::AssetKey>& emitted) -> void
{
  std::unordered_set<data::AssetKey> changed_scenes;
  for (const auto& pair : state.physics_scene_pairs) {
    if (emitted.contains(pair.scene_key)
      || emitted.contains(pair.sidecar_key)) {
      changed_scenes.insert(pair.scene_key);
    }
  }
  for (const auto& pair : state.physics_scene_pairs) {
    if (changed_scenes.contains(pair.scene_key)) {
      emitted.insert(pair.scene_key);
      emitted.insert(pair.sidecar_key);
    }
  }
  for (auto& record : state.data_plan.patch_actions) {
    if (record.action == pak::PakPatchAction::kUnchanged
      && emitted.contains(record.asset_key)) {
      record.action = pak::PakPatchAction::kReplace;
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
  std::unordered_map<data::AssetKey, data::AssetType> base_asset_types;
  std::unordered_set<data::SourceKey> base_sources;
  std::vector<data::PakCatalogBase> base_layers;
  for (const auto& catalog : state.request->base_catalogs) {
    if (const auto valid = catalog.Validate(); !valid) {
      AddDiagnostic(diagnostics, PakDiagnosticSeverity::kError,
        PakBuildPhase::kPlanning, "pak.plan.base_catalog_invalid",
        valid.error());
      continue;
    }
    if (const auto valid = catalog.ValidateBaseLayers(base_layers); !valid) {
      AddDiagnostic(diagnostics, PakDiagnosticSeverity::kError,
        PakBuildPhase::kPlanning, "pak.plan.base_catalog_order_invalid",
        valid.error());
      continue;
    }
    base_layers.push_back({ .source_key = catalog.source_key,
      .content_version = catalog.content_version,
      .catalog_digest = catalog.catalog_digest });
    if (catalog.source_key == state.request->source_key
      || !base_sources.insert(catalog.source_key).second) {
      AddDiagnostic(diagnostics, PakDiagnosticSeverity::kError,
        PakBuildPhase::kPlanning, "pak.plan.base_catalog_identity_conflict",
        "Base layers must have distinct identities different from the output.");
      continue;
    }
    for (const auto& entry : catalog.entries) {
      const auto [it, inserted]
        = base_asset_types.emplace(entry.asset_key, entry.asset_type);
      if (!inserted && it->second != entry.asset_type) {
        AddDiagnostic(diagnostics, PakDiagnosticSeverity::kError,
          PakBuildPhase::kPlanning, "pak.plan.base_catalog_type_mismatch",
          "Same AssetKey appears in base catalogs with different asset_type.");
        continue;
      }
      base_catalog_entries.insert_or_assign(entry.asset_key, entry);
    }
    for (const auto& key : catalog.deleted) {
      base_catalog_entries.erase(key);
    }
  }
  for (const auto& [key, entry] : source_catalog_entries) {
    const auto previous = base_asset_types.find(key);
    if (previous != base_asset_types.end()
      && previous->second != entry.asset_type) {
      AddDiagnostic(diagnostics, PakDiagnosticSeverity::kError,
        PakBuildPhase::kPlanning, "pak.plan.asset_override_type_mismatch",
        "A patch cannot change an existing AssetKey's asset type.");
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

    IncludePhysicsScenePatchPartners(state, emitted_patch_keys);

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
    std::unordered_set<data::AssetKey> emitted;
    for (const auto& asset : state.assets) {
      state.included_source_orders.push_back(asset.source_order);
      emitted.insert(asset.key);
    }
    for (const auto& resource : state.pending_resources) {
      if (std::ranges::any_of(resource.dependent_asset_keys,
            [&](const data::AssetKey& key) { return emitted.contains(key); })) {
        state.included_source_orders.push_back(resource.source_order);
      }
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

  if (state.policy.mode != pak::PakPlanMode::kPatch) {
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
      state.table_counts.physics_count
        += contribution.table_counts.physics_count;
    }
    return;
  }

  std::unordered_set<size_t> descriptor_source_orders;
  auto emitted_asset_keys = std::unordered_set<data::AssetKey> {};
  emitted_asset_keys.reserve(state.assets.size());
  for (const auto& asset : state.assets) {
    emitted_asset_keys.insert(asset.key);
    descriptor_source_orders.insert(asset.source_order);
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
    [&descriptor_source_orders, &emitted_asset_keys,
      &explicitly_owned_resource_kinds_by_source](
      const PendingResource& resource) -> bool {
      if (resource.dependent_asset_keys.empty()) {
        if (!descriptor_source_orders.contains(resource.source_order)) {
          return true;
        }
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
}

auto ValidatePatchContributionInvariants(PlanningState& state) -> void
{
  if (state.policy.mode != pak::PakPlanMode::kPatch) {
    return;
  }

  for (const auto& resource : state.pending_resources) {
    EnforceStageInvariant(state,
      std::ranges::find(state.included_source_orders, resource.source_order)
        != state.included_source_orders.end(),
      "pak.plan.stage.patch.resource_not_patch_local",
      "Patch resource owner is missing from source contributions.");
  }
}

auto FinalizeResourceTables(PlanningState& state) -> void
{
  using pak::PakBuildPhase;
  using pak::PakDiagnosticSeverity;

  auto& diagnostics = state.output.diagnostics;

  SetTableCount(state.data_plan.tables, "texture_table",
    state.table_counts.texture_count, diagnostics);
  SetTableCount(state.data_plan.tables, "buffer_table",
    state.table_counts.buffer_count, diagnostics);
  SetTableCount(state.data_plan.tables, "audio_table",
    state.table_counts.audio_count, diagnostics);
  SetTableCount(state.data_plan.tables, "script_resource_table",
    state.table_counts.script_resource_count, diagnostics);
  SetTableCount(state.data_plan.tables, "physics_resource_table",
    state.table_counts.physics_count, diagnostics);
  ApplyIndexZeroPolicy(state.data_plan.tables);
}

auto ValidateResourceTableInvariants(PlanningState& state) -> void
{
  const auto has_core_table_count = state.data_plan.tables.size() == 5U;
  EnforceStageInvariant(state, has_core_table_count,
    "pak.plan.stage.tables.core_tables_missing",
    "Planner table set is missing one or more core tables.");
}

using SourceIndexRemaps
  = std::unordered_map<size_t, std::unordered_map<uint32_t, uint32_t>>;
using ResourceIndexRemaps = std::unordered_map<std::string, SourceIndexRemaps>;

auto RelocateResourceBindings(
  AggregatedAsset& asset, const ResourceIndexRemaps& remaps) -> void
{
  std::vector<data::ResourceBinding> bindings(
    asset.references.Resources().begin(), asset.references.Resources().end());
  for (auto& binding : bindings) {
    if (binding.kind == data::ResourceKind::kTexture
      && (binding.index == core::kFallbackResourceIndex
        || binding.index == core::kErrorTextureResourceIndex)) {
      continue;
    }
    std::string kind;
    switch (binding.kind) {
    case data::ResourceKind::kBuffer:
      kind = "buffer";
      break;
    case data::ResourceKind::kTexture:
      kind = "texture";
      break;
    case data::ResourceKind::kScript:
      kind = "script";
      break;
    case data::ResourceKind::kPhysics:
      kind = "physics";
      break;
    }
    const auto domain = remaps.find(kind);
    if (domain == remaps.end()) {
      throw std::runtime_error(
        "Asset binding resource kind has no planned table");
    }
    const auto source = domain->second.find(asset.source_order);
    if (source == domain->second.end()) {
      throw std::runtime_error(
        "Asset binding source has no planned resource remap");
    }
    const auto target = source->second.find(binding.index.get());
    if (target == source->second.end()) {
      throw std::runtime_error(
        "Asset binding target was not included in packaging");
    }
    binding.index = core::ResourceIndexT { target->second };
  }
  auto relocated = data::AssetReferences::Create(std::move(bindings),
    std::vector<data::KeyReference>(
      asset.references.Keys().begin(), asset.references.Keys().end()));
  if (!relocated) {
    throw std::runtime_error(relocated.error());
  }
  asset.references = std::move(*relocated);
}

auto PlanFileLayout(PlanningState& state) -> void
{
  std::ranges::stable_sort(state.pending_resources,
    [](const PendingResource& lhs, const PendingResource& rhs) -> bool {
      const auto lhs_order = RegionOrder(lhs.region_name);
      const auto rhs_order = RegionOrder(rhs.region_name);
      if (lhs_order != rhs_order) {
        return lhs_order < rhs_order;
      }
      if (lhs.source_order != rhs.source_order) {
        return lhs.source_order < rhs.source_order;
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
    RelocateResourceBindings(asset, resource_remaps);
  }
  for (const auto& asset : state.assets) {
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
    state.data_plan.asset_payload_sources.push_back(std::move(payload_source));
    auto reference_bytes = asset.references.Encode();
    if (!reference_bytes) {
      throw std::runtime_error(reference_bytes.error());
    }
    state.data_plan.directory.entries.push_back(
      pak::PakAssetDirectoryEntryPlan {
        .asset_key = asset.key,
        .asset_type = asset.asset_type,
        .entry_offset = 0,
        .descriptor_offset = cursor,
        .descriptor_size = static_cast<uint32_t>(asset.descriptor_size),
        .references = { .offset = 0,
          .resource_count = static_cast<uint32_t>(asset.references.Resources().size()),
          .key_count = static_cast<uint32_t>(asset.references.Keys().size()), },
        .reference_bytes = std::move(*reference_bytes),
      });
    cursor += asset.descriptor_size;
  }

  for (auto& entry : state.data_plan.directory.entries) {
    if (!entry.reference_bytes.empty()) {
      entry.references.offset = cursor;
      if (!SafeAdd(cursor, entry.reference_bytes.size(), cursor)) {
        throw std::overflow_error("Asset reference metadata layout overflow");
      }
    }
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

  state.output.output_catalog = BuildOutputCatalog(*state.request, state.assets,
    state.data_plan.patch_actions, state.tombstones);
  auto catalog_bytes = state.output.output_catalog.Encode();
  if (!catalog_bytes) {
    throw std::runtime_error(catalog_bytes.error());
  }
  state.data_plan.catalog
    = { .offset = cursor, .bytes = std::move(*catalog_bytes) };
  if (!SafeAdd(cursor, state.data_plan.catalog.bytes.size(), cursor)) {
    throw std::overflow_error("PAK catalog layout overflow");
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
  RunStage(state, "ValidateAssetReferences",
    "pak.plan.stage.references_exception",
    [&state] -> void { ValidateAssetReferences(state); });
  RunStage(state, "ValidatePhysicsScenePairs",
    "pak.plan.stage.physics_scene_pairs_exception",
    [&state] -> void { ValidatePhysicsScenePairs(state); });
  RunStage(state, "ClassifyPatchActionsAndFinalizeBrowse",
    "pak.plan.stage.patch_classify_exception",
    [&state] -> void { ClassifyPatchActionsAndFinalizeBrowse(state); });
  RunStage(state, "ValidatePatchClassificationInvariants",
    "pak.plan.stage.patch_invariants_exception",
    [&state] -> void { ValidatePatchClassificationInvariants(state); });
  RunStage(state, "RebuildPatchLocalContributions",
    "pak.plan.stage.patch_local_rebuild_exception",
    [&state] -> void { RebuildPatchLocalContributions(state); });
  RunStage(state, "ValidatePatchContributionInvariants",
    "pak.plan.stage.patch_local_invariants_exception",
    [&state] -> void { ValidatePatchContributionInvariants(state); });
  RunStage(state, "FinalizeResourceTables",
    "pak.plan.stage.script_tables_exception",
    [&state] -> void { FinalizeResourceTables(state); });
  RunStage(state, "ValidateResourceTableInvariants",
    "pak.plan.stage.script_tables_invariants_exception",
    [&state] -> void { ValidateResourceTableInvariants(state); });
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
