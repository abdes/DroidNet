//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <ios>
#include <numeric>
#include <span>
#include <string>

#include <Oxygen/Data/LooseCookedIndexFormat.h>
#include <Oxygen/Serio/FileStream.h>
#include <Oxygen/Serio/Writer.h>

namespace oxygen::content::lc::testing {

inline auto CreateDummyIndex(
  const std::filesystem::path& index_path, uint16_t version = 1) -> void
{
  using oxygen::data::loose_cooked::AssetEntry;
  using oxygen::data::loose_cooked::FileKind;
  using oxygen::data::loose_cooked::FileRecord;
  using oxygen::data::loose_cooked::IndexHeader;

  std::string strings;
  strings.push_back('\0');
  const auto off_desc = static_cast<uint32_t>(strings.size());
  strings += "MyAsset.bin";
  strings.push_back('\0');
  const auto off_vpath = static_cast<uint32_t>(strings.size());
  strings += "/.cooked/MyAsset.bin";
  strings.push_back('\0');
  const auto off_file = static_cast<uint32_t>(strings.size());
  strings += "Resources/buffers.table";
  strings.push_back('\0');
  const auto off_file_data = static_cast<uint32_t>(strings.size());
  strings += "Resources/buffers.data";
  strings.push_back('\0');

  IndexHeader header {};
  std::ranges::iota(header.source_identity, static_cast<uint8_t>(1));
  header.version = version;
  header.content_version = 0;
  header.flags = oxygen::data::loose_cooked::kHasVirtualPaths
    | oxygen::data::loose_cooked::kHasFileRecords;
  header.string_table_offset = sizeof(IndexHeader);
  header.string_table_size = strings.size();
  header.asset_entries_offset
    = header.string_table_offset + header.string_table_size;
  header.asset_count = 1;
  header.asset_entry_size = sizeof(AssetEntry);
  header.file_records_offset
    = header.asset_entries_offset + (sizeof(AssetEntry) * header.asset_count);
  header.file_record_count = 2;
  header.file_record_size = sizeof(FileRecord);

  constexpr uint8_t kKeyFirst = 0xAA;
  constexpr uint32_t kAssetType = 12;
  constexpr uint32_t kDescriptorSize = 42;
  constexpr uint8_t kShaFirst = 0x01;
  constexpr uint8_t kShaLast = 0x11;
  constexpr uint64_t kRecordTableSize = 100;
  constexpr uint64_t kRecordDataSize = 200;
  constexpr size_t kShaLastIndex = 31;

  auto key_bytes = std::array<uint8_t, oxygen::data::AssetKey::kSizeBytes> {};
  key_bytes[0] = kKeyFirst;
  const auto key = oxygen::data::AssetKey::FromBytes(key_bytes);
  AssetEntry entry {};
  entry.asset_key = key;
  entry.descriptor_relpath_offset = off_desc;
  entry.virtual_path_offset = off_vpath;
  entry.asset_type = kAssetType;
  entry.descriptor_size = kDescriptorSize;
  entry.descriptor_sha256[0] = kShaFirst;
  entry.descriptor_sha256[kShaLastIndex] = kShaLast;

  FileRecord record {};
  record.kind = FileKind::kBuffersTable;
  record.relpath_offset = off_file;
  record.size = kRecordTableSize;

  FileRecord record2 {};
  record2.kind = FileKind::kBuffersData;
  record2.relpath_offset = off_file_data;
  record2.size = kRecordDataSize;

  oxygen::serio::FileStream<> stream(
    index_path, std::ios::out | std::ios::binary | std::ios::trunc);
  oxygen::serio::Writer<oxygen::serio::FileStream<>> writer(stream);

  auto _ = writer.Write(header);
  _ = writer.WriteBlob(
    std::as_bytes(std::span { strings.data(), strings.size() }));
  _ = writer.Write(entry);
  _ = writer.Write(record);
  _ = writer.Write(record2);
}

} // namespace oxygen::content::lc::testing
