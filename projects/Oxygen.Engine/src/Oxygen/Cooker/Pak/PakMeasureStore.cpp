//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <vector>

#include <Oxygen/Cooker/Pak/PakMeasureStore.h>
#include <Oxygen/Data/PakFormat_core.h>

namespace {
namespace pak = oxygen::content::pak;
namespace core = oxygen::data::pak::core;

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

  template <typename T>
  auto WriteObject(const T& value) -> bool
  {
    return WriteBytes(std::as_bytes(std::span { std::addressof(value), size_t { 1 } }));
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

  template <typename T>
  auto WriteObject(const T& value) -> bool
  {
    return WriteBytes(std::as_bytes(std::span { std::addressof(value), size_t { 1 } }));
  }

  std::vector<std::byte>& out_bytes;
};

template <typename Sink>
auto EmitBrowseIndexPayload(const std::span<const pak::PakBrowseEntryPlan> entries,
  Sink& sink) -> bool
{
  constexpr auto kMaxU32AsU64 = static_cast<uint64_t>((std::numeric_limits<uint32_t>::max)());

  uint64_t string_table_size = 0;
  for (const auto& entry : entries) {
    const auto path_size = static_cast<uint64_t>(entry.virtual_path.size());
    if (path_size > kMaxU32AsU64
      || path_size > (std::numeric_limits<uint64_t>::max)() - string_table_size) {
      return false;
    }
    string_table_size += path_size;
  }

  if (entries.size() > kMaxU32AsU64 || string_table_size > kMaxU32AsU64) {
    return false;
  }

  core::PakBrowseIndexHeader header {};
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

    core::PakBrowseIndexEntry index_entry {
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
    const auto path_bytes
      = std::as_bytes(std::span { entry.virtual_path.data(), entry.virtual_path.size() });
    if (!sink.WriteBytes(path_bytes)) {
      return false;
    }
  }

  return true;
}

} // namespace

namespace oxygen::content::pak {

auto MeasureBrowseIndexPayload(const std::span<const PakBrowseEntryPlan> entries)
  -> std::optional<uint64_t>
{
  MeasureSink sink {};
  if (!EmitBrowseIndexPayload(entries, sink)) {
    return std::nullopt;
  }
  return sink.total_bytes;
}

auto StoreBrowseIndexPayload(const std::span<const PakBrowseEntryPlan> entries,
  std::vector<std::byte>& out_bytes) -> bool
{
  const auto measured_size = MeasureBrowseIndexPayload(entries);
  if (!measured_size.has_value()
    || *measured_size > static_cast<uint64_t>((std::numeric_limits<size_t>::max)())) {
    out_bytes.clear();
    return false;
  }

  out_bytes.clear();
  out_bytes.reserve(static_cast<size_t>(*measured_size));

  BufferSink sink(out_bytes);
  if (!EmitBrowseIndexPayload(entries, sink)) {
    out_bytes.clear();
    return false;
  }

  return out_bytes.size() == static_cast<size_t>(*measured_size);
}

} // namespace oxygen::content::pak
