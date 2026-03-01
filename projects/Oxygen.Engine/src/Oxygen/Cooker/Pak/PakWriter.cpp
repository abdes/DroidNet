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
#include <exception>
#include <filesystem>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <Oxygen/Cooker/Pak/PakMeasureStore.h>
#include <Oxygen/Cooker/Pak/PakStreamingCrc32.h>
#include <Oxygen/Cooker/Pak/PakWriter.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Serio/FileStream.h>
#include <Oxygen/Serio/Writer.h>

namespace {
namespace core = oxygen::data::pak::core;
namespace data = oxygen::data;
namespace pak = oxygen::content::pak;
namespace serio = oxygen::serio;

constexpr auto kZeroWriteChunkSize = size_t { 64U * 1024U };
constexpr auto kCrcFieldSize = uint64_t { sizeof(uint32_t) };

struct WriterState final {
  const pak::PakBuildRequest* request = nullptr;
  const pak::PakPlan* plan = nullptr;
  std::optional<serio::FileStream<>> stream;
  pak::PakWriter::WriteResult output {};
  uint64_t cursor = 0;
  pak::PakStreamingCrc32 crc
    = pak::PakStreamingCrc32(pak::PakStreamingCrc32::Config {
      .enabled = false, .skip_offset = 0, .skip_size = 0 });
};

auto AddDiagnostic(WriterState& state,
  const pak::PakDiagnosticSeverity severity, const std::string_view code,
  const std::string_view message, const std::string_view resource_kind = {},
  const std::string_view table_name = {},
  const std::optional<data::AssetKey>& asset_key = std::nullopt,
  const std::optional<uint64_t> offset = std::nullopt) -> void
{
  auto diagnostic = pak::PakDiagnostic {
    .severity = severity,
    .phase = pak::PakBuildPhase::kWriting,
    .code = std::string(code),
    .message = std::string(message),
    .asset_key
    = asset_key.has_value() ? data::to_string(*asset_key) : std::string {},
    .resource_kind = std::string(resource_kind),
    .table_name = std::string(table_name),
    .path = state.request->output_pak_path,
    .offset = offset,
  };
  state.output.diagnostics.push_back(std::move(diagnostic));
}

auto HasWriterErrors(const WriterState& state) -> bool
{
  return std::ranges::any_of(
    state.output.diagnostics, [](const pak::PakDiagnostic& diagnostic) {
      return diagnostic.severity == pak::PakDiagnosticSeverity::kError;
    });
}

auto WriteRawBytes(WriterState& state, const std::span<const std::byte> bytes,
  const std::string_view write_code, const std::string_view write_error,
  const std::string_view resource_kind = {},
  const std::string_view table_name = {},
  const std::optional<data::AssetKey>& asset_key = std::nullopt) -> bool
{
  if (bytes.empty()) {
    return true;
  }

  const auto write_result = state.stream->Write(bytes);
  if (!write_result) {
    AddDiagnostic(state, pak::PakDiagnosticSeverity::kError, write_code,
      write_error, resource_kind, table_name, asset_key, state.cursor);
    return false;
  }

  if (!state.crc.Update(state.cursor, bytes)) {
    AddDiagnostic(state, pak::PakDiagnosticSeverity::kError,
      "pak.write.crc_state_error",
      "CRC32 stream update failed due to offset overflow.", resource_kind,
      table_name, asset_key, state.cursor);
    return false;
  }

  state.cursor += static_cast<uint64_t>(bytes.size());
  return true;
}

auto WriteZeroBytes(WriterState& state, const uint64_t byte_count,
  const std::string_view section_name) -> bool
{
  if (byte_count == 0U) {
    return true;
  }

  const auto zeros = std::array<std::byte, kZeroWriteChunkSize> {};
  uint64_t remaining = byte_count;
  while (remaining > 0U) {
    const auto chunk_size = static_cast<size_t>(
      (std::min)(remaining, static_cast<uint64_t>(zeros.size())));
    if (!WriteRawBytes(state,
          std::span<const std::byte>(zeros.data(), chunk_size),
          "pak.write.stream_write_failed",
          "Failed to write deterministic zero padding.", section_name)) {
      return false;
    }
    remaining -= static_cast<uint64_t>(chunk_size);
  }
  return true;
}

auto MoveCursorToOffset(WriterState& state, const uint64_t expected_offset,
  const std::string_view section_name) -> bool
{
  if (state.cursor == expected_offset) {
    return true;
  }

  if (state.cursor < expected_offset) {
    const auto gap_size = expected_offset - state.cursor;
    return WriteZeroBytes(state, gap_size, section_name);
  }

  AddDiagnostic(state, pak::PakDiagnosticSeverity::kError,
    "pak.write.offset_mismatch",
    "Write cursor exceeded planned section offset.", section_name, {},
    std::nullopt, expected_offset);
  return false;
}

auto SeekAndWriteCrc32(WriterState& state, const uint64_t crc_field_offset,
  const uint32_t crc32_value) -> bool
{
  const auto seek_result
    = state.stream->Seek(static_cast<size_t>(crc_field_offset));
  if (!seek_result) {
    AddDiagnostic(state, pak::PakDiagnosticSeverity::kError,
      "pak.write.stream_seek_failed",
      "Failed to seek CRC32 field offset for patch write.", {}, {},
      std::nullopt, crc_field_offset);
    return false;
  }

  auto writer = serio::Writer(*state.stream);
  auto packing = writer.ScopedAlignment(1);
  (void)packing;
  const auto write_result = writer.Write(crc32_value);
  if (!write_result) {
    AddDiagnostic(state, pak::PakDiagnosticSeverity::kError,
      "pak.write.crc_patch_failed", "Failed to patch footer pak_crc32 field.",
      {}, {}, std::nullopt, crc_field_offset);
    return false;
  }

  return true;
}

auto FindRegion(const std::span<const pak::PakRegionPlan> regions,
  const std::string_view region_name) -> std::optional<pak::PakRegionPlan>
{
  const auto it = std::ranges::find_if(
    regions, [region_name](const pak::PakRegionPlan& region) {
      return region.region_name == region_name;
    });
  if (it == regions.end()) {
    return std::nullopt;
  }
  return *it;
}

auto FindTable(const std::span<const pak::PakTablePlan> tables,
  const std::string_view table_name) -> std::optional<pak::PakTablePlan>
{
  const auto it = std::ranges::find_if(
    tables, [table_name](const pak::PakTablePlan& table) {
      return table.table_name == table_name;
    });
  if (it == tables.end()) {
    return std::nullopt;
  }
  return *it;
}

auto BuildResourceIndexMap(
  const std::span<const pak::PakResourcePlacementPlan> resources)
  -> std::unordered_map<std::string, std::vector<pak::PakResourcePlacementPlan>>
{
  auto by_kind = std::unordered_map<std::string,
    std::vector<pak::PakResourcePlacementPlan>> {};
  for (const auto& resource : resources) {
    by_kind[resource.resource_kind].push_back(resource);
  }
  for (auto& [_, kind_resources] : by_kind) {
    std::ranges::sort(kind_resources,
      [](const pak::PakResourcePlacementPlan& lhs,
        const pak::PakResourcePlacementPlan& rhs) {
        return lhs.resource_index < rhs.resource_index;
      });
  }
  return by_kind;
}

auto FindResourcesByKind(const std::unordered_map<std::string,
                           std::vector<pak::PakResourcePlacementPlan>>& by_kind,
  const std::string_view resource_kind)
  -> std::span<const pak::PakResourcePlacementPlan>
{
  const auto it = by_kind.find(std::string(resource_kind));
  if (it == by_kind.end()) {
    return {};
  }
  return std::span<const pak::PakResourcePlacementPlan>(
    it->second.data(), it->second.size());
}

auto BuildFooter(WriterState& state) -> std::optional<core::PakFooter>
{
  auto footer = core::PakFooter {};

  const auto& directory = state.plan->Directory();
  footer.directory_offset = directory.offset;
  footer.directory_size = directory.size_bytes;
  footer.asset_count = directory.entries.size();

  const auto regions = state.plan->Regions();
  const auto tables = state.plan->Tables();

  const auto assign_region
    = [&state, regions](const std::string_view region_name,
        core::ResourceRegion& region) -> bool {
    const auto region_plan = FindRegion(regions, region_name);
    if (!region_plan.has_value()) {
      AddDiagnostic(state, pak::PakDiagnosticSeverity::kError,
        "pak.write.footer_region_missing",
        "Footer serialization requires all canonical region plans.",
        region_name);
      return false;
    }
    region.offset = region_plan->offset;
    region.size = region_plan->size_bytes;
    return true;
  };

  if (!assign_region("texture_region", footer.texture_region)
    || !assign_region("buffer_region", footer.buffer_region)
    || !assign_region("audio_region", footer.audio_region)
    || !assign_region("script_region", footer.script_region)
    || !assign_region("physics_region", footer.physics_region)) {
    return std::nullopt;
  }

  const auto assign_table = [&state, tables](const std::string_view table_name,
                              core::ResourceTable& table) -> bool {
    const auto table_plan = FindTable(tables, table_name);
    if (!table_plan.has_value()) {
      AddDiagnostic(state, pak::PakDiagnosticSeverity::kError,
        "pak.write.footer_table_missing",
        "Footer serialization requires all canonical table plans.", {},
        table_name);
      return false;
    }
    table.offset = table_plan->offset;
    table.count = table_plan->count;
    table.entry_size = table_plan->entry_size;
    return true;
  };

  if (!assign_table("texture_table", footer.texture_table)
    || !assign_table("buffer_table", footer.buffer_table)
    || !assign_table("audio_table", footer.audio_table)
    || !assign_table("script_resource_table", footer.script_resource_table)
    || !assign_table("script_slot_table", footer.script_slot_table)
    || !assign_table("physics_resource_table", footer.physics_resource_table)) {
    return std::nullopt;
  }

  const auto& browse = state.plan->BrowseIndex();
  footer.browse_index_offset = browse.enabled ? browse.offset : 0;
  footer.browse_index_size = browse.enabled ? browse.size_bytes : 0;
  footer.pak_crc32 = 0;

  return footer;
}

auto EmitPhaseMarker(WriterState& state, const std::string_view code,
  const std::string_view message) -> void
{
  AddDiagnostic(state, pak::PakDiagnosticSeverity::kInfo, code, message);
}

} // namespace

namespace oxygen::content::pak {

auto PakWriter::Write(const PakBuildRequest& request, const PakPlan& plan) const
  -> WriteResult
{
  auto state = WriterState {
    .request = std::addressof(request),
    .plan = std::addressof(plan),
    .stream = std::nullopt,
    .output = {},
    .cursor = 0,
    .crc = PakStreamingCrc32(PakStreamingCrc32::Config {
      .enabled = request.options.compute_crc32,
      .skip_offset = plan.Footer().crc32_field_absolute_offset,
      .skip_size = kCrcFieldSize,
    }),
  };

  const auto start = std::chrono::steady_clock::now();

  const auto footer = plan.Footer();
  const auto expected_crc_offset
    = footer.offset + offsetof(core::PakFooter, pak_crc32);
  if (footer.crc32_field_absolute_offset != expected_crc_offset) {
    AddDiagnostic(state, PakDiagnosticSeverity::kError,
      "pak.write.crc_field_offset_invalid",
      "Plan CRC32 field absolute offset does not match PakFooter layout.",
      "footer", "footer", std::nullopt, footer.crc32_field_absolute_offset);
  }
  if (plan.ResourcePayloadSources().size() != plan.Resources().size()) {
    AddDiagnostic(state, PakDiagnosticSeverity::kError,
      "pak.write.resource_source_count_mismatch",
      "Resource payload source slice count must match planned resources.");
  }
  if (plan.AssetPayloadSources().size() != plan.Assets().size()) {
    AddDiagnostic(state, PakDiagnosticSeverity::kError,
      "pak.write.asset_source_count_mismatch",
      "Asset payload source slice count must match planned assets.");
  }
  if (HasWriterErrors(state)) {
    state.output.writing_duration
      = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - start);
    return std::move(state.output);
  }

  try {
    state.stream.emplace(request.output_pak_path,
      std::ios::binary | std::ios::in | std::ios::out | std::ios::trunc);
  } catch (const std::exception& ex) {
    AddDiagnostic(state, PakDiagnosticSeverity::kError,
      "pak.write.stream_open_failed",
      std::string("Failed to open output pak file for writing: ") + ex.what());
    state.output.writing_duration
      = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - start);
    return std::move(state.output);
  }

  EmitPhaseMarker(state, "pak.write.phase_begin", "Write PakHeader.");
  {
    const auto header_plan = plan.Header();
    if (header_plan.size_bytes != sizeof(core::PakHeader)) {
      AddDiagnostic(state, PakDiagnosticSeverity::kError,
        "pak.write.plan_header_size_invalid",
        "PakHeader planned size does not match schema size.");
    } else if (MoveCursorToOffset(state, header_plan.offset, "header")) {
      auto header = core::PakHeader {};
      header.version = core::kPakVersion;
      header.content_version = header_plan.content_version;
      const auto source_key = data::as_bytes(header_plan.source_key);
      std::ranges::transform(source_key, std::begin(header.guid),
        [](const auto byte) { return std::to_integer<uint8_t>(byte); });
      const auto header_bytes = std::as_bytes(std::span(&header, size_t { 1 }));
      (void)WriteRawBytes(state, header_bytes, "pak.write.stream_write_failed",
        "Failed to write PakHeader bytes.", "header");
    }
  }
  EmitPhaseMarker(state, "pak.write.phase_end", "PakHeader write complete.");

  if (!HasWriterErrors(state)) {
    EmitPhaseMarker(state, "pak.write.phase_begin", "Write resource regions.");
    const auto resources = plan.Resources();
    const auto resource_sources = plan.ResourcePayloadSources();
    for (const auto& region : plan.Regions()) {
      if (!MoveCursorToOffset(state, region.offset, region.region_name)) {
        break;
      }

      uint64_t region_end = 0;
      if (region.size_bytes
        > (std::numeric_limits<uint64_t>::max)() - region.offset) {
        AddDiagnostic(state, PakDiagnosticSeverity::kError,
          "pak.write.region_size_overflow",
          "Resource region offset + size overflowed uint64.",
          region.region_name, {}, std::nullopt, region.offset);
        break;
      }
      region_end = region.offset + region.size_bytes;

      auto region_resource_indices = std::vector<size_t> {};
      for (size_t i = 0; i < resources.size(); ++i) {
        if (resources[i].region_name == region.region_name) {
          region_resource_indices.push_back(i);
        }
      }
      std::ranges::sort(region_resource_indices,
        [&resources](const size_t lhs, const size_t rhs) {
          return resources[lhs].offset < resources[rhs].offset;
        });

      for (const auto resource_index : region_resource_indices) {
        const auto& resource = resources[resource_index];
        const auto& source = resource_sources[resource_index];
        if (!MoveCursorToOffset(state, resource.offset, resource.region_name)) {
          break;
        }

        auto payload = std::vector<std::byte> {};
        const auto stored = StorePayloadSourceSlice(source, payload);
        if (!stored) {
          AddDiagnostic(state, PakDiagnosticSeverity::kError,
            "pak.write.resource_store_failed",
            "Failed to serialize resource payload from source slice.",
            resource.resource_kind, {}, std::nullopt, resource.offset);
          break;
        }
        if (static_cast<uint64_t>(payload.size()) != resource.size_bytes) {
          AddDiagnostic(state, PakDiagnosticSeverity::kError,
            "pak.write.resource_size_mismatch",
            "Serialized resource payload size does not match planned size.",
            resource.resource_kind, {}, std::nullopt, resource.offset);
          break;
        }

        if (!WriteRawBytes(state,
              std::span<const std::byte>(payload.data(), payload.size()),
              "pak.write.stream_write_failed",
              "Failed to write resource payload bytes.",
              resource.resource_kind)) {
          break;
        }
      }

      if (HasWriterErrors(state)) {
        break;
      }

      if (!MoveCursorToOffset(state, region_end, region.region_name)) {
        break;
      }
    }
    EmitPhaseMarker(
      state, "pak.write.phase_end", "Resource regions write complete.");
  }

  if (!HasWriterErrors(state)) {
    EmitPhaseMarker(state, "pak.write.phase_begin", "Write resource tables.");
    const auto resources_by_kind = BuildResourceIndexMap(plan.Resources());
    const auto write_table
      = [&state, &resources_by_kind](const pak::PakTablePlan& table) -> bool {
      if (!MoveCursorToOffset(state, table.offset, table.table_name)) {
        return false;
      }

      auto payload = std::vector<std::byte> {};
      auto store_ok = false;

      if (table.table_name == "texture_table") {
        const auto input = pak::PakResourceTableSerializationInput {
          .entry_count = table.count,
          .resources = FindResourcesByKind(resources_by_kind, "texture"),
        };
        store_ok = StoreTextureTablePayload(input, payload);
      } else if (table.table_name == "buffer_table") {
        const auto input = pak::PakResourceTableSerializationInput {
          .entry_count = table.count,
          .resources = FindResourcesByKind(resources_by_kind, "buffer"),
        };
        store_ok = StoreBufferTablePayload(input, payload);
      } else if (table.table_name == "audio_table") {
        const auto input = pak::PakResourceTableSerializationInput {
          .entry_count = table.count,
          .resources = FindResourcesByKind(resources_by_kind, "audio"),
        };
        store_ok = StoreAudioTablePayload(input, payload);
      } else if (table.table_name == "script_resource_table") {
        const auto input = pak::PakResourceTableSerializationInput {
          .entry_count = table.count,
          .resources = FindResourcesByKind(resources_by_kind, "script"),
        };
        store_ok = StoreScriptResourceTablePayload(input, payload);
      } else if (table.table_name == "script_slot_table") {
        const auto input = pak::PakScriptSlotTableSerializationInput {
          .entry_count = table.count,
          .ranges = state.plan->ScriptParamRanges(),
        };
        store_ok = StoreScriptSlotTablePayload(input, payload);
      } else if (table.table_name == "physics_resource_table") {
        const auto input = pak::PakResourceTableSerializationInput {
          .entry_count = table.count,
          .resources = FindResourcesByKind(resources_by_kind, "physics"),
        };
        store_ok = StorePhysicsTablePayload(input, payload);
      } else {
        AddDiagnostic(state, PakDiagnosticSeverity::kError,
          "pak.write.unknown_table_name",
          "Writer encountered an unknown table_name in plan.", {},
          table.table_name);
        return false;
      }

      if (!store_ok) {
        AddDiagnostic(state, PakDiagnosticSeverity::kError,
          "pak.write.table_store_failed",
          "Failed to serialize resource table payload.", {}, table.table_name);
        return false;
      }

      if (static_cast<uint64_t>(payload.size()) != table.size_bytes) {
        AddDiagnostic(state, PakDiagnosticSeverity::kError,
          "pak.write.table_size_mismatch",
          "Serialized table payload size does not match planned table size.",
          {}, table.table_name, std::nullopt, table.offset);
        return false;
      }

      return WriteRawBytes(state,
        std::span<const std::byte>(payload.data(), payload.size()),
        "pak.write.stream_write_failed",
        "Failed to write serialized table payload.", {}, table.table_name);
    };

    for (const auto& table : plan.Tables()) {
      if (!write_table(table)) {
        break;
      }
    }
    EmitPhaseMarker(
      state, "pak.write.phase_end", "Resource tables write complete.");
  }

  if (!HasWriterErrors(state)) {
    EmitPhaseMarker(state, "pak.write.phase_begin", "Write asset descriptors.");
    const auto assets = plan.Assets();
    const auto asset_sources = plan.AssetPayloadSources();
    for (size_t i = 0; i < assets.size(); ++i) {
      const auto& asset = assets[i];
      const auto& source = asset_sources[i];
      if (!MoveCursorToOffset(state, asset.offset, "asset_descriptor")) {
        break;
      }

      auto payload = std::vector<std::byte> {};
      const auto stored = StorePayloadSourceSlice(source, payload);
      if (!stored) {
        AddDiagnostic(state, PakDiagnosticSeverity::kError,
          "pak.write.asset_descriptor_store_failed",
          "Failed to serialize asset descriptor payload from source slice.", {},
          "asset_descriptor", asset.asset_key, asset.offset);
        break;
      }
      if (static_cast<uint64_t>(payload.size()) != asset.size_bytes) {
        AddDiagnostic(state, PakDiagnosticSeverity::kError,
          "pak.write.asset_descriptor_size_mismatch",
          "Serialized asset descriptor size does not match planned size.", {},
          "asset_descriptor", asset.asset_key, asset.offset);
        break;
      }
      if (!WriteRawBytes(state,
            std::span<const std::byte>(payload.data(), payload.size()),
            "pak.write.stream_write_failed",
            "Failed to write asset descriptor payload bytes.", {},
            "asset_descriptor", asset.asset_key)) {
        break;
      }
    }
    EmitPhaseMarker(
      state, "pak.write.phase_end", "Asset descriptors write complete.");
  }

  if (!HasWriterErrors(state)) {
    EmitPhaseMarker(state, "pak.write.phase_begin", "Write asset directory.");
    const auto& directory = plan.Directory();
    if (MoveCursorToOffset(state, directory.offset, "asset_directory")) {
      auto payload = std::vector<std::byte> {};
      const auto stored = StoreAssetDirectoryPayload(
        std::span<const pak::PakAssetDirectoryEntryPlan>(
          directory.entries.data(), directory.entries.size()),
        payload);
      if (!stored) {
        AddDiagnostic(state, PakDiagnosticSeverity::kError,
          "pak.write.directory_store_failed",
          "Failed to serialize asset directory payload.", {},
          "asset_directory");
      } else if (static_cast<uint64_t>(payload.size())
        != directory.size_bytes) {
        AddDiagnostic(state, PakDiagnosticSeverity::kError,
          "pak.write.directory_size_mismatch",
          "Serialized directory size does not match planned directory size.",
          {}, "asset_directory", std::nullopt, directory.offset);
      } else {
        (void)WriteRawBytes(state,
          std::span<const std::byte>(payload.data(), payload.size()),
          "pak.write.stream_write_failed",
          "Failed to write asset directory payload.", {}, "asset_directory");
      }
    }
    EmitPhaseMarker(
      state, "pak.write.phase_end", "Asset directory write complete.");
  }

  if (!HasWriterErrors(state) && plan.BrowseIndex().enabled) {
    EmitPhaseMarker(
      state, "pak.write.phase_begin", "Write browse index payload.");
    const auto& browse = plan.BrowseIndex();
    if (MoveCursorToOffset(state, browse.offset, "browse_index")) {
      auto payload = std::vector<std::byte> {};
      const auto stored = StoreBrowseIndexPayload(
        std::span<const pak::PakBrowseEntryPlan>(
          browse.entries.data(), browse.entries.size()),
        payload);
      if (!stored) {
        AddDiagnostic(state, PakDiagnosticSeverity::kError,
          "pak.write.browse_index_store_failed",
          "Failed to serialize browse index payload from plan entries.",
          "browse_index");
      } else {
        if (static_cast<uint64_t>(payload.size()) != browse.size_bytes) {
          AddDiagnostic(state, PakDiagnosticSeverity::kError,
            "pak.write.browse_index_size_mismatch",
            "Serialized browse index size does not match planned size.",
            "browse_index", "browse_index");
        } else {
          (void)WriteRawBytes(state,
            std::span<const std::byte>(payload.data(), payload.size()),
            "pak.write.stream_write_failed",
            "Failed to write browse index payload.", "browse_index",
            "browse_index");
        }
      }
    }
    EmitPhaseMarker(
      state, "pak.write.phase_end", "Browse index payload write complete.");
  }

  if (!HasWriterErrors(state)) {
    EmitPhaseMarker(state, "pak.write.phase_begin", "Write PakFooter.");
    const auto footer_plan = plan.Footer();
    if (!MoveCursorToOffset(state, footer_plan.offset, "footer")) {
      AddDiagnostic(state, PakDiagnosticSeverity::kError,
        "pak.write.offset_mismatch",
        "Failed to move cursor to planned footer offset.", "footer");
    } else {
      const auto footer_bytes = BuildFooter(state);
      if (footer_bytes.has_value()) {
        (void)WriteRawBytes(state,
          std::as_bytes(std::span(&(*footer_bytes), size_t { 1 })),
          "pak.write.stream_write_failed", "Failed to write PakFooter bytes.",
          "footer");
      }
    }
    EmitPhaseMarker(state, "pak.write.phase_end", "PakFooter write complete.");
  }

  if (!HasWriterErrors(state)) {
    if (!MoveCursorToOffset(
          state, plan.PlannedFileSize(), "planned_file_size")) {
      AddDiagnostic(state, PakDiagnosticSeverity::kError,
        "pak.write.final_size_mismatch",
        "Failed to match planned file size after write phases.",
        "planned_file_size", {}, std::nullopt, plan.PlannedFileSize());
    }
  }

  if (!HasWriterErrors(state)) {
    const auto flush_result = state.stream->Flush();
    if (!flush_result) {
      AddDiagnostic(state, PakDiagnosticSeverity::kError,
        "pak.write.stream_flush_failed", "Failed to flush output pak stream.");
    }
  }

  if (!HasWriterErrors(state) && request.options.compute_crc32) {
    if (state.crc.SkippedByteCount() != kCrcFieldSize) {
      AddDiagnostic(state, PakDiagnosticSeverity::kError,
        "pak.write.crc_state_error",
        "CRC32 stream did not skip exactly pak_crc32 field bytes.", "footer",
        "footer", std::nullopt, plan.Footer().crc32_field_absolute_offset);
    } else {
      state.output.pak_crc32 = state.crc.Finalize();
      if (SeekAndWriteCrc32(state, plan.Footer().crc32_field_absolute_offset,
            state.output.pak_crc32)) {
        const auto flush_result = state.stream->Flush();
        if (!flush_result) {
          AddDiagnostic(state, PakDiagnosticSeverity::kError,
            "pak.write.stream_flush_failed",
            "Failed to flush output pak stream after CRC patch.");
        }
      }
    }
  }

  if (!request.options.compute_crc32) {
    state.output.pak_crc32 = 0U;
  }

  if (!HasWriterErrors(state)) {
    state.output.file_size = plan.PlannedFileSize();
  }

  state.output.writing_duration
    = std::chrono::duration_cast<std::chrono::microseconds>(
      std::chrono::steady_clock::now() - start);
  return std::move(state.output);
}

} // namespace oxygen::content::pak
