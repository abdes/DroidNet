//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Cooker/Pak/Internal/PakResourceRows.h>
#include <Oxygen/Cooker/Pak/Internal/PakSourceDevices.h>
#include <Oxygen/Cooker/Pak/PakMeasureStore.h>
#include <Oxygen/Cooker/Pak/PakPlan.h>
#include <Oxygen/Data/PakFormat_audio.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Data/PakFormat_render.h> // IWYU pragma: keep
#include <Oxygen/Serio/Types.h>

namespace {
namespace audio = oxygen::data::pak::audio;
namespace core = oxygen::data::pak::core;
namespace pak = oxygen::content::pak;

struct MeasureSink final {
  uint64_t total_bytes = 0;

  [[nodiscard]] auto WriteBytes(const std::span<const std::byte> bytes) -> bool
  {
    const auto byte_count = static_cast<uint64_t>(bytes.size_bytes());
    if (byte_count > std::numeric_limits<uint64_t>::max() - total_bytes) {
      return false;
    }
    total_bytes += byte_count;
    return true;
  }

  template <typename T> [[nodiscard]] auto WriteObject(const T& value) -> bool
  {
    return WriteBytes(
      std::as_bytes(std::span { std::addressof(value), size_t { 1 } }));
  }
};

struct BufferSink final {
  explicit BufferSink(std::vector<std::byte>& out_bytes_in)
    : out_bytes(&out_bytes_in)
  {
  }

  [[nodiscard]] auto WriteBytes(const std::span<const std::byte> bytes) const
    -> bool
  {
    if (bytes.size() > out_bytes->max_size() - out_bytes->size()) {
      return false;
    }
    out_bytes->insert(out_bytes->end(), bytes.begin(), bytes.end());
    return true;
  }

  template <typename T> [[nodiscard]] auto WriteObject(const T& value) -> bool
  {
    return WriteBytes(
      std::as_bytes(std::span { std::addressof(value), size_t { 1 } }));
  }

  oxygen::observer_ptr<std::vector<std::byte>> out_bytes;
};

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
      > static_cast<uint64_t>(std::numeric_limits<size_t>::max())) {
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
auto EmitFixedRecordPayload(const uint32_t entry_count,
  const FillRecordFn& fill_record, Sink& sink) -> bool
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
  const AssignResourceFn& assign_resource, Sink& sink) -> bool
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
auto EmitAudioTablePayload(
  const pak::PakResourceTableSerializationInput& input, Sink& sink) -> bool
{
  return EmitResourceTablePayload<audio::AudioResourceDesc>(
    input, pak::internal::PlaceResourceRow<audio::AudioResourceDesc>, sink);
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
    record.references = entry.references;
    if (!sink.WriteObject(record)) {
      return false;
    }
  }

  return true;
}

template <typename Sink>
auto EmitBrowseIndexPayload(const pak::PakBrowseIndexPlan& browse, Sink& sink)
  -> bool
{
  constexpr auto kMaxU32AsU64
    = static_cast<uint64_t>(std::numeric_limits<uint32_t>::max());

  auto paths = std::vector<std::string_view> {};
  paths.reserve(browse.entries.size() + browse.textures.size());
  for (const auto& entry : browse.entries) {
    paths.emplace_back(entry.virtual_path);
  }
  for (const auto& entry : browse.textures) {
    paths.emplace_back(entry.virtual_path);
  }

  uint64_t string_table_size = 0;
  for (const auto path : paths) {
    const auto path_size = static_cast<uint64_t>(path.size());
    if (path_size > kMaxU32AsU64 - string_table_size) {
      return false;
    }
    string_table_size += path_size;
  }
  if (browse.entries.size() > kMaxU32AsU64
    || browse.textures.size() > kMaxU32AsU64) {
    return false;
  }

  auto header = core::PakBrowseIndexHeader {};
  header.entry_count = static_cast<uint32_t>(browse.entries.size());
  header.texture_entry_count = static_cast<uint32_t>(browse.textures.size());
  header.string_table_size = static_cast<uint32_t>(string_table_size);
  if (!sink.WriteObject(header)) {
    return false;
  }

  // Paths are stored in entry order, so each one starts where the last ended.
  uint32_t current_offset = 0;
  const auto next_slice
    = [&current_offset](
        const std::string& path) -> std::pair<uint32_t, uint32_t> {
    const auto slice
      = std::pair { current_offset, static_cast<uint32_t>(path.size()) };
    current_offset += slice.second;
    return slice;
  };
  for (const auto& entry : browse.entries) {
    const auto [offset, length] = next_slice(entry.virtual_path);
    const auto index_entry = core::PakBrowseIndexEntry {
      .asset_key = entry.asset_key,
      .virtual_path_offset = offset,
      .virtual_path_length = length,
    };
    if (!sink.WriteObject(index_entry)) {
      return false;
    }
  }
  for (const auto& entry : browse.textures) {
    const auto [offset, length] = next_slice(entry.virtual_path);
    const auto texture_entry = core::PakBrowseIndexTextureEntry {
      .texture_index = core::ResourceIndexT { entry.texture_index },
      .virtual_path_offset = offset,
      .virtual_path_length = length,
    };
    if (!sink.WriteObject(texture_entry)) {
      return false;
    }
  }

  return std::ranges::all_of(paths, [&sink](const auto path) -> bool {
    return sink.WriteBytes(
      std::as_bytes(std::span { path.data(), path.size() }));
  });
}

} // namespace

namespace oxygen::content::pak {

auto StoreAudioTablePayload(const PakResourceTableSerializationInput& input,
  std::vector<std::byte>& out_bytes) -> bool
{
  return StorePayload(
    [&input](auto& sink) -> auto { return EmitAudioTablePayload(input, sink); },
    out_bytes);
}

auto StoreAssetDirectoryPayload(
  const std::span<const PakAssetDirectoryEntryPlan> entries,
  std::vector<std::byte>& out_bytes) -> bool
{
  return StorePayload(
    [entries](
      auto& sink) -> auto { return EmitAssetDirectoryPayload(entries, sink); },
    out_bytes);
}

auto MeasureBrowseIndexPayload(const PakBrowseIndexPlan& browse)
  -> std::optional<uint64_t>
{
  return MeasurePayload([&browse](auto& sink) -> auto {
    return EmitBrowseIndexPayload(browse, sink);
  });
}

auto StoreBrowseIndexPayload(
  const PakBrowseIndexPlan& browse, std::vector<std::byte>& out_bytes) -> bool
{
  return StorePayload(
    [&browse](
      auto& sink) -> auto { return EmitBrowseIndexPayload(browse, sink); },
    out_bytes);
}

auto MeasurePayloadSourceSlice(internal::PakSourceDevices& sources,
  const PakPayloadSourceSlicePlan& input) -> std::optional<uint64_t>
{
  if (input.size_bytes == 0U) {
    return uint64_t { 0U };
  }
  if (!input.inline_bytes.empty()) {
    const auto inline_size = static_cast<uint64_t>(input.inline_bytes.size());
    if (input.source_offset > inline_size) {
      return std::nullopt;
    }
    if (input.size_bytes > inline_size - input.source_offset) {
      return std::nullopt;
    }
    return input.size_bytes;
  }
  if (input.source_path.empty()) {
    return std::nullopt;
  }

  const auto size = sources.Size(input.source_path);
  if (!size) {
    return std::nullopt;
  }
  const auto file_size = size->get();

  if (input.source_offset > file_size) {
    return std::nullopt;
  }
  if (input.size_bytes > file_size - input.source_offset) {
    return std::nullopt;
  }

  return input.size_bytes;
}

auto StorePayloadSourceSlice(internal::PakSourceDevices& sources,
  const PakPayloadSourceSlicePlan& input, std::vector<std::byte>& out_bytes)
  -> bool
{
  const auto measured = MeasurePayloadSourceSlice(sources, input);
  if (!measured.has_value()) {
    out_bytes.clear();
    return false;
  }

  if (*measured > static_cast<uint64_t>(std::numeric_limits<size_t>::max())) {
    out_bytes.clear();
    return false;
  }
  if (input.source_offset
    > static_cast<uint64_t>(std::numeric_limits<size_t>::max())) {
    out_bytes.clear();
    return false;
  }

  out_bytes.clear();
  if (*measured == 0U) {
    return true;
  }
  out_bytes.reserve(static_cast<size_t>(*measured));

  if (!input.inline_bytes.empty()) {
    const auto start = static_cast<size_t>(input.source_offset);
    const auto count = static_cast<size_t>(*measured);
    const auto bytes
      = std::span<const std::byte>(input.inline_bytes).subspan(start, count);
    out_bytes.insert(out_bytes.end(), bytes.begin(), bytes.end());
    return out_bytes.size() == count;
  }

  out_bytes.resize(static_cast<size_t>(*measured));
  if (!sources.ReadInto(input.source_path,
        serio::ByteOffset { input.source_offset }, out_bytes)) {
    out_bytes.clear();
    return false;
  }
  return true;
}

} // namespace oxygen::content::pak
