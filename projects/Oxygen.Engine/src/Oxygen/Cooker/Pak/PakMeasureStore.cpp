//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <vector>

#include <Oxygen/Cooker/Pak/PakMeasureStore.h>
#include <Oxygen/Data/PakFormat_audio.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Data/PakFormat_physics.h>
#include <Oxygen/Data/PakFormat_render.h>
#include <Oxygen/Data/PakFormat_scripting.h>
#include <Oxygen/Serio/FileStream.h>
#include <Oxygen/Serio/Reader.h>

namespace {
namespace audio = oxygen::data::pak::audio;
namespace core = oxygen::data::pak::core;
namespace pak = oxygen::content::pak;
namespace physics = oxygen::data::pak::physics;
namespace render = oxygen::data::pak::render;
namespace serio = oxygen::serio;
namespace script = oxygen::data::pak::scripting;

constexpr auto kBufferElementStride = uint32_t { 1U };

struct MeasureSink final {
  uint64_t total_bytes = 0;

  auto WriteBytes(const std::span<const std::byte> bytes) -> bool
  {
    const auto byte_count = static_cast<uint64_t>(bytes.size_bytes());
    if (byte_count > (std::numeric_limits<uint64_t>::max)() - total_bytes) {
      return false;
    }
    total_bytes += byte_count;
    return true;
  }

  template <typename T> auto WriteObject(const T& value) -> bool
  {
    return WriteBytes(
      std::as_bytes(std::span { std::addressof(value), size_t { 1 } }));
  }
};

struct BufferSink final {
  explicit BufferSink(std::vector<std::byte>& out_bytes_in)
    : out_bytes(out_bytes_in)
  {
  }

  auto WriteBytes(const std::span<const std::byte> bytes) -> bool
  {
    if (bytes.size() > out_bytes.max_size() - out_bytes.size()) {
      return false;
    }
    out_bytes.insert(out_bytes.end(), bytes.begin(), bytes.end());
    return true;
  }

  template <typename T> auto WriteObject(const T& value) -> bool
  {
    return WriteBytes(
      std::as_bytes(std::span { std::addressof(value), size_t { 1 } }));
  }

  std::vector<std::byte>& out_bytes;
};

template <typename T> auto NarrowTo(const uint64_t value) -> std::optional<T>
{
  if (value > static_cast<uint64_t>((std::numeric_limits<T>::max)())) {
    return std::nullopt;
  }
  return static_cast<T>(value);
}

template <typename Emitter>
auto MeasurePayload(const Emitter& emitter) -> std::optional<uint64_t>
{
  auto sink = MeasureSink {};
  if (!emitter(sink)) {
    return std::nullopt;
  }
  return sink.total_bytes;
}

template <typename Emitter>
auto StorePayload(const Emitter& emitter, std::vector<std::byte>& out_bytes)
  -> bool
{
  const auto measured_size = MeasurePayload(emitter);
  if (!measured_size.has_value()
    || *measured_size
      > static_cast<uint64_t>((std::numeric_limits<size_t>::max)())) {
    out_bytes.clear();
    return false;
  }

  out_bytes.clear();
  out_bytes.reserve(static_cast<size_t>(*measured_size));

  auto sink = BufferSink(out_bytes);
  if (!emitter(sink)) {
    out_bytes.clear();
    return false;
  }

  return out_bytes.size() == static_cast<size_t>(*measured_size);
}

template <typename Record, typename Sink, typename FillRecordFn>
auto EmitFixedRecordPayload(
  const uint32_t entry_count, FillRecordFn&& fill_record, Sink& sink) -> bool
{
  for (uint32_t i = 0; i < entry_count; ++i) {
    auto record = Record {};
    if (!fill_record(i, record)) {
      return false;
    }
    if (!sink.WriteObject(record)) {
      return false;
    }
  }
  return true;
}

template <typename Record, typename Sink, typename AssignResourceFn>
auto EmitResourceTablePayload(
  const pak::PakResourceTableSerializationInput& input,
  AssignResourceFn&& assign_resource, Sink& sink) -> bool
{
  size_t resource_cursor = 0;
  const auto write_ok = EmitFixedRecordPayload<Record>(
    input.entry_count,
    [&input, &resource_cursor, &assign_resource](
      const uint32_t index, Record& record) -> bool {
      if (resource_cursor >= input.resources.size()) {
        return true;
      }

      const auto& resource = input.resources[resource_cursor];
      if (resource.resource_index < index) {
        return false;
      }
      if (resource.resource_index > index) {
        return true;
      }

      if (!assign_resource(resource, record)) {
        return false;
      }
      ++resource_cursor;
      return true;
    },
    sink);
  return write_ok && resource_cursor == input.resources.size();
}

template <typename Sink>
auto EmitTextureTablePayload(
  const pak::PakResourceTableSerializationInput& input, Sink& sink) -> bool
{
  return EmitResourceTablePayload<core::TextureResourceDesc>(
    input,
    [](const pak::PakResourcePlacementPlan& resource,
      core::TextureResourceDesc& record) -> bool {
      const auto size_bytes = NarrowTo<uint32_t>(resource.size_bytes);
      const auto alignment = NarrowTo<uint16_t>(resource.alignment);
      if (!size_bytes.has_value() || !alignment.has_value()) {
        return false;
      }

      record.data_offset = resource.offset;
      record.size_bytes = *size_bytes;
      record.alignment = *alignment;
      return true;
    },
    sink);
}

template <typename Sink>
auto EmitBufferTablePayload(
  const pak::PakResourceTableSerializationInput& input, Sink& sink) -> bool
{
  return EmitResourceTablePayload<core::BufferResourceDesc>(
    input,
    [](const pak::PakResourcePlacementPlan& resource,
      core::BufferResourceDesc& record) -> bool {
      const auto size_bytes = NarrowTo<uint32_t>(resource.size_bytes);
      if (!size_bytes.has_value()) {
        return false;
      }

      record.data_offset = resource.offset;
      record.size_bytes = *size_bytes;
      record.element_stride = kBufferElementStride;
      return true;
    },
    sink);
}

template <typename Sink>
auto EmitAudioTablePayload(
  const pak::PakResourceTableSerializationInput& input, Sink& sink) -> bool
{
  return EmitResourceTablePayload<audio::AudioResourceDesc>(
    input,
    [](const pak::PakResourcePlacementPlan& resource,
      audio::AudioResourceDesc& record) -> bool {
      const auto size_bytes = NarrowTo<uint32_t>(resource.size_bytes);
      const auto alignment = NarrowTo<uint16_t>(resource.alignment);
      if (!size_bytes.has_value() || !alignment.has_value()) {
        return false;
      }

      record.data_offset = resource.offset;
      record.size_bytes = *size_bytes;
      record.alignment = *alignment;
      return true;
    },
    sink);
}

template <typename Sink>
auto EmitScriptResourceTablePayload(
  const pak::PakResourceTableSerializationInput& input, Sink& sink) -> bool
{
  return EmitResourceTablePayload<script::ScriptResourceDesc>(
    input,
    [](const pak::PakResourcePlacementPlan& resource,
      script::ScriptResourceDesc& record) -> bool {
      const auto size_bytes = NarrowTo<uint32_t>(resource.size_bytes);
      if (!size_bytes.has_value()) {
        return false;
      }

      record.data_offset = resource.offset;
      record.size_bytes = *size_bytes;
      return true;
    },
    sink);
}

template <typename Sink>
auto EmitScriptSlotTablePayload(
  const pak::PakScriptSlotTableSerializationInput& input, Sink& sink) -> bool
{
  size_t range_cursor = 0;
  const auto write_ok = EmitFixedRecordPayload<script::ScriptSlotRecord>(
    input.entry_count,
    [&input, &range_cursor](
      const uint32_t slot_index, script::ScriptSlotRecord& record) -> bool {
      if (range_cursor >= input.ranges.size()) {
        return true;
      }

      const auto& range = input.ranges[range_cursor];
      if (range.slot_index < slot_index) {
        return false;
      }
      if (range.slot_index > slot_index) {
        return true;
      }

      constexpr auto kParamRecordSize
        = uint64_t { sizeof(script::ScriptParamRecord) };
      if (range.params_array_offset
        > (std::numeric_limits<uint64_t>::max)() / kParamRecordSize) {
        return false;
      }
      record.params_array_offset
        = static_cast<uint64_t>(range.params_array_offset) * kParamRecordSize;
      record.params_count = range.params_count;
      ++range_cursor;
      return true;
    },
    sink);

  return write_ok && range_cursor == input.ranges.size();
}

template <typename Sink>
auto EmitPhysicsTablePayload(
  const pak::PakResourceTableSerializationInput& input, Sink& sink) -> bool
{
  return EmitResourceTablePayload<physics::PhysicsResourceDesc>(
    input,
    [](const pak::PakResourcePlacementPlan& resource,
      physics::PhysicsResourceDesc& record) -> bool {
      const auto size_bytes = NarrowTo<uint32_t>(resource.size_bytes);
      if (!size_bytes.has_value()) {
        return false;
      }

      record.data_offset = resource.offset;
      record.size_bytes = *size_bytes;
      return true;
    },
    sink);
}

template <typename Sink>
auto EmitAssetDirectoryPayload(
  const std::span<const pak::PakAssetDirectoryEntryPlan> entries, Sink& sink)
  -> bool
{
  for (const auto& entry : entries) {
    auto record = core::AssetDirectoryEntry {};
    record.asset_key = entry.asset_key;
    record.asset_type = entry.asset_type;
    record.entry_offset = entry.entry_offset;
    record.desc_offset = entry.descriptor_offset;
    record.desc_size = entry.descriptor_size;
    if (!sink.WriteObject(record)) {
      return false;
    }
  }

  return true;
}

template <typename Sink>
auto EmitBrowseIndexPayload(
  const std::span<const pak::PakBrowseEntryPlan> entries, Sink& sink) -> bool
{
  constexpr auto kMaxU32AsU64
    = static_cast<uint64_t>((std::numeric_limits<uint32_t>::max)());

  uint64_t string_table_size = 0;
  for (const auto& entry : entries) {
    const auto path_size = static_cast<uint64_t>(entry.virtual_path.size());
    if (path_size > kMaxU32AsU64
      || path_size
        > (std::numeric_limits<uint64_t>::max)() - string_table_size) {
      return false;
    }
    string_table_size += path_size;
  }

  if (entries.size() > kMaxU32AsU64 || string_table_size > kMaxU32AsU64) {
    return false;
  }

  auto header = core::PakBrowseIndexHeader {};
  header.entry_count = static_cast<uint32_t>(entries.size());
  header.string_table_size = static_cast<uint32_t>(string_table_size);
  if (!sink.WriteObject(header)) {
    return false;
  }

  uint64_t current_offset = 0;
  for (const auto& entry : entries) {
    const auto path_size = static_cast<uint64_t>(entry.virtual_path.size());
    if (current_offset > kMaxU32AsU64 || path_size > kMaxU32AsU64
      || path_size > kMaxU32AsU64 - current_offset) {
      return false;
    }

    auto index_entry = core::PakBrowseIndexEntry {
      .asset_key = entry.asset_key,
      .virtual_path_offset = static_cast<uint32_t>(current_offset),
      .virtual_path_length = static_cast<uint32_t>(path_size),
    };
    if (!sink.WriteObject(index_entry)) {
      return false;
    }
    current_offset += path_size;
  }

  for (const auto& entry : entries) {
    const auto path_bytes = std::as_bytes(
      std::span { entry.virtual_path.data(), entry.virtual_path.size() });
    if (!sink.WriteBytes(path_bytes)) {
      return false;
    }
  }

  return true;
}

} // namespace

namespace oxygen::content::pak {

auto MeasureTextureTablePayload(const PakResourceTableSerializationInput& input)
  -> std::optional<uint64_t>
{
  return MeasurePayload(
    [&input](auto& sink) { return EmitTextureTablePayload(input, sink); });
}

auto StoreTextureTablePayload(const PakResourceTableSerializationInput& input,
  std::vector<std::byte>& out_bytes) -> bool
{
  return StorePayload(
    [&input](auto& sink) { return EmitTextureTablePayload(input, sink); },
    out_bytes);
}

auto MeasureBufferTablePayload(const PakResourceTableSerializationInput& input)
  -> std::optional<uint64_t>
{
  return MeasurePayload(
    [&input](auto& sink) { return EmitBufferTablePayload(input, sink); });
}

auto StoreBufferTablePayload(const PakResourceTableSerializationInput& input,
  std::vector<std::byte>& out_bytes) -> bool
{
  return StorePayload(
    [&input](auto& sink) { return EmitBufferTablePayload(input, sink); },
    out_bytes);
}

auto MeasureAudioTablePayload(const PakResourceTableSerializationInput& input)
  -> std::optional<uint64_t>
{
  return MeasurePayload(
    [&input](auto& sink) { return EmitAudioTablePayload(input, sink); });
}

auto StoreAudioTablePayload(const PakResourceTableSerializationInput& input,
  std::vector<std::byte>& out_bytes) -> bool
{
  return StorePayload(
    [&input](auto& sink) { return EmitAudioTablePayload(input, sink); },
    out_bytes);
}

auto MeasureScriptResourceTablePayload(
  const PakResourceTableSerializationInput& input) -> std::optional<uint64_t>
{
  return MeasurePayload([&input](auto& sink) {
    return EmitScriptResourceTablePayload(input, sink);
  });
}

auto StoreScriptResourceTablePayload(
  const PakResourceTableSerializationInput& input,
  std::vector<std::byte>& out_bytes) -> bool
{
  return StorePayload(
    [&input](
      auto& sink) { return EmitScriptResourceTablePayload(input, sink); },
    out_bytes);
}

auto MeasureScriptSlotTablePayload(
  const PakScriptSlotTableSerializationInput& input) -> std::optional<uint64_t>
{
  return MeasurePayload(
    [&input](auto& sink) { return EmitScriptSlotTablePayload(input, sink); });
}

auto StoreScriptSlotTablePayload(
  const PakScriptSlotTableSerializationInput& input,
  std::vector<std::byte>& out_bytes) -> bool
{
  return StorePayload(
    [&input](auto& sink) { return EmitScriptSlotTablePayload(input, sink); },
    out_bytes);
}

auto MeasurePhysicsTablePayload(const PakResourceTableSerializationInput& input)
  -> std::optional<uint64_t>
{
  return MeasurePayload(
    [&input](auto& sink) { return EmitPhysicsTablePayload(input, sink); });
}

auto StorePhysicsTablePayload(const PakResourceTableSerializationInput& input,
  std::vector<std::byte>& out_bytes) -> bool
{
  return StorePayload(
    [&input](auto& sink) { return EmitPhysicsTablePayload(input, sink); },
    out_bytes);
}

auto MeasureAssetDirectoryPayload(
  const std::span<const PakAssetDirectoryEntryPlan> entries)
  -> std::optional<uint64_t>
{
  return MeasurePayload(
    [entries](auto& sink) { return EmitAssetDirectoryPayload(entries, sink); });
}

auto StoreAssetDirectoryPayload(
  const std::span<const PakAssetDirectoryEntryPlan> entries,
  std::vector<std::byte>& out_bytes) -> bool
{
  return StorePayload(
    [entries](auto& sink) { return EmitAssetDirectoryPayload(entries, sink); },
    out_bytes);
}

auto MeasureBrowseIndexPayload(
  const std::span<const PakBrowseEntryPlan> entries) -> std::optional<uint64_t>
{
  return MeasurePayload(
    [entries](auto& sink) { return EmitBrowseIndexPayload(entries, sink); });
}

auto StoreBrowseIndexPayload(const std::span<const PakBrowseEntryPlan> entries,
  std::vector<std::byte>& out_bytes) -> bool
{
  return StorePayload(
    [entries](auto& sink) { return EmitBrowseIndexPayload(entries, sink); },
    out_bytes);
}

auto MeasurePayloadSourceSlice(const PakPayloadSourceSlicePlan& input)
  -> std::optional<uint64_t>
{
  if (input.size_bytes == 0U) {
    return uint64_t { 0U };
  }
  if (input.source_path.empty()) {
    return std::nullopt;
  }

  std::error_code ec;
  const auto file_size = std::filesystem::file_size(input.source_path, ec);
  if (ec) {
    return std::nullopt;
  }

  if (input.source_offset > file_size) {
    return std::nullopt;
  }
  if (input.size_bytes > file_size - input.source_offset) {
    return std::nullopt;
  }

  return input.size_bytes;
}

auto StorePayloadSourceSlice(const PakPayloadSourceSlicePlan& input,
  std::vector<std::byte>& out_bytes) -> bool
{
  const auto measured = MeasurePayloadSourceSlice(input);
  if (!measured.has_value()) {
    out_bytes.clear();
    return false;
  }

  if (*measured > static_cast<uint64_t>((std::numeric_limits<size_t>::max)())) {
    out_bytes.clear();
    return false;
  }
  if (input.source_offset
    > static_cast<uint64_t>((std::numeric_limits<size_t>::max)())) {
    out_bytes.clear();
    return false;
  }

  out_bytes.clear();
  if (*measured == 0U) {
    return true;
  }
  out_bytes.reserve(static_cast<size_t>(*measured));

  try {
    auto stream
      = serio::FileStream<>(input.source_path, std::ios::binary | std::ios::in);
    if (!stream.Seek(static_cast<size_t>(input.source_offset))) {
      out_bytes.clear();
      return false;
    }

    auto reader = serio::Reader(stream);
    auto align_guard = reader.ScopedAlignment(1);
    (void)align_guard;
    const auto blob_result = reader.ReadBlob(static_cast<size_t>(*measured));
    if (!blob_result) {
      out_bytes.clear();
      return false;
    }

    out_bytes.insert(out_bytes.end(), blob_result->begin(), blob_result->end());
    return out_bytes.size() == static_cast<size_t>(*measured);
  } catch (const std::exception&) {
    out_bytes.clear();
    return false;
  }
}

} // namespace oxygen::content::pak
