//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/LooseCookedIndexFormat.h>

#include "./LooseCookedTestLayout.h"

namespace oxygen::content::testing {

struct LooseCookedTestWriteResult {
  std::filesystem::path index_path;
  uint32_t asset_count = 0;
  uint32_t file_record_count = 0;
};

//! Minimal test writer for loose cooked fixtures in Content tests.
class LooseCookedTestWriter final {
public:
  explicit LooseCookedTestWriter(std::filesystem::path cooked_root)
    : cooked_root_(std::move(cooked_root))
  {
    std::filesystem::create_directories(cooked_root_);
  }

  auto SetComputeSha256(const bool enabled) -> void
  {
    compute_sha256_ = enabled;
  }

  auto WriteFile(data::loose_cooked::FileKind kind, std::string relpath,
    std::span<const std::byte> bytes) -> void
  {
    const auto absolute_path = cooked_root_ / std::filesystem::path(relpath);
    if (const auto parent = absolute_path.parent_path(); !parent.empty()) {
      std::filesystem::create_directories(parent);
    }

    std::ofstream out(
      absolute_path, std::ios::binary | std::ios::trunc | std::ios::out);
    if (!bytes.empty()) {
      out.write(reinterpret_cast<const char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size()));
    }

    files_.push_back(PendingFileRecord {
      .kind = kind,
      .relpath = std::move(relpath),
      .size = static_cast<uint64_t>(bytes.size()),
    });
  }

  auto WriteAssetDescriptor(const data::AssetKey& asset_key,
    const data::AssetType asset_type, std::string virtual_path,
    std::string descriptor_relpath, std::span<const std::byte> bytes) -> void
  {
    const auto absolute_path
      = cooked_root_ / std::filesystem::path(descriptor_relpath);
    if (const auto parent = absolute_path.parent_path(); !parent.empty()) {
      std::filesystem::create_directories(parent);
    }

    std::ofstream out(
      absolute_path, std::ios::binary | std::ios::trunc | std::ios::out);
    if (!bytes.empty()) {
      out.write(reinterpret_cast<const char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size()));
    }

    std::array<uint8_t, data::loose_cooked::kSha256Size> digest_bytes {};
    if (compute_sha256_) {
      const auto digest = base::ComputeSha256(bytes);
      std::copy_n(digest.begin(), digest.size(), digest_bytes.begin());
    }

    assets_.push_back(PendingAssetRecord {
      .asset_key = asset_key,
      .asset_type = static_cast<uint8_t>(asset_type),
      .descriptor_relpath = std::move(descriptor_relpath),
      .virtual_path = std::move(virtual_path),
      .descriptor_size = static_cast<uint64_t>(bytes.size()),
      .descriptor_sha256 = digest_bytes,
    });
  }

  [[nodiscard]] auto Finish() const -> LooseCookedTestWriteResult
  {
    using data::loose_cooked::AssetEntry;
    using data::loose_cooked::FileRecord;
    using data::loose_cooked::IndexHeader;

    std::string strings;
    strings.push_back('\0');

    std::vector<AssetEntry> asset_entries;
    asset_entries.reserve(assets_.size());
    for (const auto& pending_asset : assets_) {
      AssetEntry entry {};
      entry.asset_key = pending_asset.asset_key;
      entry.descriptor_relpath_offset
        = AppendString(strings, pending_asset.descriptor_relpath);
      entry.virtual_path_offset
        = AppendString(strings, pending_asset.virtual_path);
      entry.asset_type = pending_asset.asset_type;
      entry.descriptor_size = pending_asset.descriptor_size;
      std::copy_n(pending_asset.descriptor_sha256.begin(),
        pending_asset.descriptor_sha256.size(),
        std::begin(entry.descriptor_sha256));
      asset_entries.push_back(entry);
    }

    std::vector<FileRecord> file_records;
    file_records.reserve(files_.size());
    for (const auto& pending_file : files_) {
      FileRecord record {};
      record.kind = pending_file.kind;
      record.relpath_offset = AppendString(strings, pending_file.relpath);
      record.size = pending_file.size;
      file_records.push_back(record);
    }

    IndexHeader header {};
    FillGuid(header);
    header.version = 1;
    header.content_version = 0;
    header.flags = data::loose_cooked::kHasVirtualPaths
      | data::loose_cooked::kHasFileRecords;
    header.string_table_offset = sizeof(IndexHeader);
    header.string_table_size = static_cast<uint64_t>(strings.size());
    header.asset_entries_offset
      = header.string_table_offset + header.string_table_size;
    header.asset_count = static_cast<uint32_t>(asset_entries.size());
    header.asset_entry_size = sizeof(AssetEntry);
    header.file_records_offset = header.asset_entries_offset
      + static_cast<uint64_t>(asset_entries.size()) * sizeof(AssetEntry);
    header.file_record_count = static_cast<uint32_t>(file_records.size());
    header.file_record_size = sizeof(FileRecord);

    const auto index_path = cooked_root_ / layout_.index_file_name;
    std::ofstream out(index_path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(&header),
      static_cast<std::streamsize>(sizeof(header)));
    out.write(strings.data(), static_cast<std::streamsize>(strings.size()));
    if (!asset_entries.empty()) {
      out.write(reinterpret_cast<const char*>(asset_entries.data()),
        static_cast<std::streamsize>(
          asset_entries.size() * sizeof(AssetEntry)));
    }
    if (!file_records.empty()) {
      out.write(reinterpret_cast<const char*>(file_records.data()),
        static_cast<std::streamsize>(file_records.size() * sizeof(FileRecord)));
    }

    return {
      .index_path = index_path,
      .asset_count = static_cast<uint32_t>(asset_entries.size()),
      .file_record_count = static_cast<uint32_t>(file_records.size()),
    };
  }

private:
  struct PendingAssetRecord {
    data::AssetKey asset_key {};
    uint8_t asset_type = 0;
    std::string descriptor_relpath;
    std::string virtual_path;
    uint64_t descriptor_size = 0;
    std::array<uint8_t, data::loose_cooked::kSha256Size> descriptor_sha256 {};
  };

  struct PendingFileRecord {
    data::loose_cooked::FileKind kind = data::loose_cooked::FileKind::kUnknown;
    std::string relpath;
    uint64_t size = 0;
  };

  [[nodiscard]] static auto AppendString(
    std::string& table, const std::string& value) -> uint32_t
  {
    const auto offset = static_cast<uint32_t>(table.size());
    table += value;
    table.push_back('\0');
    return offset;
  }

  static auto FillGuid(data::loose_cooked::IndexHeader& header) -> void
  {
    for (uint8_t i = 0; i < 16; ++i) {
      header.source_identity[i] = static_cast<uint8_t>(i + 1);
    }
    header.source_identity[6]
      = static_cast<uint8_t>((header.source_identity[6] & 0x0FU) | 0x70U);
    header.source_identity[8]
      = static_cast<uint8_t>((header.source_identity[8] & 0x3FU) | 0x80U);
  }

  std::filesystem::path cooked_root_;
  LooseCookedLayout layout_ {};
  bool compute_sha256_ = true;
  std::vector<PendingAssetRecord> assets_;
  std::vector<PendingFileRecord> files_;
};

} // namespace oxygen::content::testing
