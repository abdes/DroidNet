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
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>

#include <Oxygen/Content/Detail/LooseCookedIndex.h>
#include <Oxygen/Content/Internal/LooseCookedIndexLoad.h>
#include <Oxygen/Data/LooseCookedIndexFormat.h>
#include <Oxygen/Serio/FileStream.h>
#include <Oxygen/Serio/Reader.h>

namespace oxygen::content::detail {

namespace {

  using oxygen::data::loose_cooked::v1::AssetEntry;
  using oxygen::data::loose_cooked::v1::FileKind;
  using oxygen::data::loose_cooked::v1::FileRecord;
  using oxygen::data::loose_cooked::v1::IndexHeader;
  using oxygen::data::loose_cooked::v1::kHasFileRecords;
  using oxygen::data::loose_cooked::v1::kHasVirtualPaths;
  using oxygen::data::loose_cooked::v1::kKnownIndexFlags;

  constexpr auto ToSizeT(const uint64_t value) -> size_t
  {
    return static_cast<size_t>(value);
  }

  auto ReadOrThrow(
    const oxygen::Result<void>& result, const std::string_view message) -> void
  {
    if (!result) {
      throw std::runtime_error(
        std::string(message) + ": " + result.error().message());
    }
  }

  auto ValidateMagic(const IndexHeader& header) -> void
  {
    const auto expected = oxygen::data::loose_cooked::v1::kHeaderMagic;
    const auto actual = std::string_view(header.magic, sizeof(header.magic));
    const auto expected_sv = std::string_view(expected.data(), expected.size());
    if (actual != expected_sv) {
      throw std::runtime_error("Invalid loose cooked index magic");
    }
  }

  auto ValidateSectionRange(const size_t file_size, const uint64_t offset,
    const uint64_t size, const std::string_view what) -> void
  {
    if (offset > file_size) {
      throw std::runtime_error(std::string(what) + " offset out of range");
    }
    if (size > file_size) {
      throw std::runtime_error(std::string(what) + " size out of range");
    }
    if (offset + size > file_size) {
      throw std::runtime_error(std::string(what) + " range out of bounds");
    }
  }

  auto ValidateSectionLayout(const IndexHeader& header, const size_t file_size)
    -> void
  {
    (void)file_size;

    if (header.string_table_size == 0) {
      throw std::runtime_error("String table must not be empty");
    }

    if (header.string_table_offset < sizeof(IndexHeader)) {
      throw std::runtime_error("String table must start after index header");
    }

    const auto expected_asset_entries_min
      = header.string_table_offset + header.string_table_size;
    if (header.asset_entries_offset < expected_asset_entries_min) {
      throw std::runtime_error(
        "Asset entries must start after the end of the string table");
    }

    const auto expected_file_records_min = header.asset_entries_offset
      + (static_cast<uint64_t>(header.asset_count) * sizeof(AssetEntry));
    if (header.file_records_offset < expected_file_records_min) {
      throw std::runtime_error(
        "File records must start after the end of the asset entries");
    }
  }

  auto ValidateStringOffset(const IndexHeader& header, const uint32_t offset)
    -> void
  {
    const auto str_size = header.string_table_size;
    if (static_cast<uint64_t>(offset) >= str_size) {
      throw std::runtime_error("String table offset out of range");
    }
  }

  auto ExtractNullTerminatedString(
    std::string_view table, const uint32_t offset) -> std::string_view
  {
    if (offset >= table.size()) {
      throw std::runtime_error("String table offset out of range");
    }

    const auto* begin = table.data() + offset;
    const auto remaining = table.size() - offset;
    const auto* end = static_cast<const char*>(memchr(begin, '\0', remaining));
    if (end == nullptr) {
      throw std::runtime_error("Unterminated string in string table");
    }

    return std::string_view(begin, static_cast<size_t>(end - begin));
  }

  auto ValidateNoDotSegments(
    const std::string_view path, const std::string_view what) -> void
  {
    size_t pos = 0;
    while (pos <= path.size()) {
      const auto next = path.find('/', pos);
      const auto len
        = (next == std::string_view::npos) ? (path.size() - pos) : (next - pos);
      const auto segment = path.substr(pos, len);
      if (segment == ".") {
        throw std::runtime_error(std::string(what) + " must not contain '.'");
      }
      if (segment == "..") {
        throw std::runtime_error(std::string(what) + " must not contain '..'");
      }

      if (next == std::string_view::npos) {
        break;
      }
      pos = next + 1;
    }
  }

  auto ValidateRelativePath(const std::string_view relpath) -> void
  {
    if (relpath.empty()) {
      throw std::runtime_error("Index path must not be empty");
    }

    if (relpath.find('\\') != std::string_view::npos) {
      throw std::runtime_error("Index path must use '/' as the separator");
    }
    if (relpath.find(':') != std::string_view::npos) {
      throw std::runtime_error("Index path must not contain ':'");
    }
    if (relpath.front() == '/') {
      throw std::runtime_error("Index path must be container-relative");
    }
    if (relpath.back() == '/') {
      throw std::runtime_error("Index path must not end with '/'");
    }
    if (relpath.find("//") != std::string_view::npos) {
      throw std::runtime_error("Index path must not contain '//'");
    }

    ValidateNoDotSegments(relpath, "Index path");

    std::filesystem::path p(relpath);
    if (p.is_absolute() || p.has_root_path() || p.has_root_name()) {
      throw std::runtime_error("Index path must be container-relative");
    }

    for (const auto& part : p) {
      if (part == "..") {
        throw std::runtime_error("Index path must not contain '..'");
      }
    }
  }

  auto ValidateVirtualPath(const std::string_view virtual_path) -> void
  {
    if (virtual_path.empty()) {
      throw std::runtime_error("Virtual path must not be empty");
    }
    if (virtual_path.find('\\') != std::string_view::npos) {
      throw std::runtime_error("Virtual path must use '/' as the separator");
    }
    if (virtual_path.front() != '/') {
      throw std::runtime_error("Virtual path must start with '/'");
    }
    if (virtual_path.size() > 1 && virtual_path.back() == '/') {
      throw std::runtime_error(
        "Virtual path must not end with '/' (except the root)");
    }
    if (virtual_path.find("//") != std::string_view::npos) {
      throw std::runtime_error("Virtual path must not contain '//'");
    }

    ValidateNoDotSegments(virtual_path, "Virtual path");
  }

  auto ValidateFileKind(const FileKind kind) -> void
  {
    switch (kind) {
    case FileKind::kBuffersTable:
    case FileKind::kBuffersData:
    case FileKind::kTexturesTable:
    case FileKind::kTexturesData:
      return;
    case FileKind::kUnknown:
    default:
      break;
    }

    throw std::runtime_error("Unsupported FileKind in loose cooked index");
  }

  auto ValidateHeaderFlags(const IndexHeader& header) -> void
  {
    const auto flags = header.flags;

    if ((flags & ~kKnownIndexFlags) != 0u) {
      throw std::runtime_error(
        "Unsupported IndexHeader flags in loose cooked index");
    }

    // Backward compatibility: flags==0 is a legacy value.
    if (flags == 0u) {
      return;
    }

    // For v1 indexes, asset virtual paths are part of the contract.
    if ((flags & static_cast<uint32_t>(kHasVirtualPaths)) == 0u) {
      throw std::runtime_error(
        "Loose cooked index flags must declare virtual-path support");
    }

    const auto declares_file_records
      = (flags & static_cast<uint32_t>(kHasFileRecords)) != 0u;

    if (!declares_file_records && header.file_record_count != 0u) {
      throw std::runtime_error("Loose cooked index flags disallow file "
                               "records, but file_record_count is non-zero");
    }

    if (declares_file_records
      && header.file_record_size != sizeof(FileRecord)) {
      throw std::runtime_error("Unexpected FileRecord size in index header");
    }
  }

  auto ValidateGuid(const IndexHeader& header) -> void
  {
    bool all_zeros = true;
    for (const auto b : header.guid) {
      if (b != 0) {
        all_zeros = false;
        break;
      }
    }
    if (all_zeros) {
      throw std::runtime_error("Loose cooked index must have a non-zero GUID");
    }
  }

} // namespace

auto LooseCookedIndex::LoadFromFile(const std::filesystem::path& index_path)
  -> LooseCookedIndex
{
  serio::FileStream<> stream(index_path, std::ios::in);
  auto size_result = stream.Size();
  if (!size_result) {
    throw std::runtime_error(
      "Failed to get index file size: " + size_result.error().message());
  }

  const auto file_size = size_result.value();
  if (file_size < sizeof(IndexHeader)) {
    throw std::runtime_error("Index file too small: " + index_path.string());
  }

  if (auto res = stream.Seek(0); !res) {
    throw std::runtime_error(
      "Failed to seek index file: " + res.error().message());
  }

  serio::Reader<serio::FileStream<>> reader(stream);
  auto header_result = reader.Read<IndexHeader>();
  if (!header_result) {
    throw std::runtime_error(
      "Failed to read index header: " + header_result.error().message());
  }
  const auto header = header_result.value();

  ValidateMagic(header);

  if (header.version != 1) {
    throw std::runtime_error("Unsupported loose cooked index version");
  }

  ValidateHeaderFlags(header);
  ValidateGuid(header);

  if (header.asset_entry_size != sizeof(AssetEntry)) {
    throw std::runtime_error("Unexpected AssetEntry size in index header");
  }

  if (header.file_record_count != 0 && header.file_record_size == 0) {
    throw std::runtime_error("Invalid file record size in index header");
  }

  if (header.file_record_count != 0
    && header.file_record_size != sizeof(FileRecord)) {
    throw std::runtime_error("Unexpected FileRecord size in index header");
  }

  ValidateSectionLayout(header, file_size);

  ValidateSectionRange(file_size, header.string_table_offset,
    header.string_table_size, "string table");

  ValidateSectionRange(file_size, header.asset_entries_offset,
    static_cast<uint64_t>(header.asset_count) * sizeof(AssetEntry),
    "asset entries");

  const auto declares_file_records = header.flags != 0u
    && (header.flags & static_cast<uint32_t>(kHasFileRecords)) != 0u;

  if (declares_file_records || header.file_record_count > 0) {
    ValidateSectionRange(file_size, header.file_records_offset,
      static_cast<uint64_t>(header.file_record_count) * sizeof(FileRecord),
      "file records");
  }

  LooseCookedIndex out;
  out.guid_ = data::SourceKey::FromBytes(header.guid);
  out.string_storage_.resize(ToSizeT(header.string_table_size));
  if (auto res = reader.Seek(ToSizeT(header.string_table_offset)); !res) {
    throw std::runtime_error(
      "Failed to seek to string table: " + res.error().message());
  }
  ReadOrThrow(reader.ReadBlobInto(std::span(
                // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
                reinterpret_cast<std::byte*>(out.string_storage_.data()),
                out.string_storage_.size())),
    "Failed to read string table");

  if (out.string_storage_.empty() || out.string_storage_.front() != '\0') {
    throw std::runtime_error("String table must start with a NUL byte");
  }
  const auto stored_table
    = std::string_view(out.string_storage_.data(), out.string_storage_.size());

  if (auto res = reader.Seek(ToSizeT(header.asset_entries_offset)); !res) {
    throw std::runtime_error(
      "Failed to seek to asset entries: " + res.error().message());
  }

  std::unordered_set<std::string_view> unique_virtual_paths;

  for (uint32_t i = 0; i < header.asset_count; ++i) {
    auto entry_result = reader.Read<AssetEntry>();
    if (!entry_result) {
      throw std::runtime_error(
        "Failed to read asset entry: " + entry_result.error().message());
    }
    const auto& entry = entry_result.value();

    ValidateStringOffset(header, entry.descriptor_relpath_offset);
    ValidateStringOffset(header, entry.virtual_path_offset);

    const auto descriptor_rel = ExtractNullTerminatedString(
      stored_table, entry.descriptor_relpath_offset);
    const auto virtual_path
      = ExtractNullTerminatedString(stored_table, entry.virtual_path_offset);

    ValidateRelativePath(descriptor_rel);
    ValidateVirtualPath(virtual_path);

    if (out.key_to_asset_info_.contains(entry.asset_key)) {
      throw std::runtime_error("Duplicate AssetKey in loose cooked index");
    }
    if (out.virtual_path_offset_to_key_.contains(entry.virtual_path_offset)) {
      throw std::runtime_error(
        "Duplicate virtual path offset in loose cooked index");
    }

    if (!unique_virtual_paths.insert(virtual_path).second) {
      throw std::runtime_error(
        "Duplicate virtual path string in loose cooked index");
    }

    LooseCookedIndex::AssetInfo info {
      .descriptor_relpath_offset = entry.descriptor_relpath_offset,
      .virtual_path_offset = entry.virtual_path_offset,
      .descriptor_size = entry.descriptor_size,
      .asset_type = entry.asset_type,
    };
    static_assert(
      sizeof(info.descriptor_sha256) == sizeof(entry.descriptor_sha256));
    std::copy_n(std::begin(entry.descriptor_sha256),
      info.descriptor_sha256.size(), info.descriptor_sha256.begin());

    out.asset_keys_.push_back(entry.asset_key);
    out.key_to_asset_info_.insert_or_assign(entry.asset_key, info);
    out.virtual_path_offset_to_key_.insert_or_assign(
      entry.virtual_path_offset, entry.asset_key);
  }

  if (header.file_record_count > 0) {
    if (auto res = reader.Seek(ToSizeT(header.file_records_offset)); !res) {
      throw std::runtime_error(
        "Failed to seek to file records: " + res.error().message());
    }

    for (uint32_t i = 0; i < header.file_record_count; ++i) {
      auto record_result = reader.Read<FileRecord>();
      if (!record_result) {
        throw std::runtime_error(
          "Failed to read file record: " + record_result.error().message());
      }
      const auto& record = record_result.value();

      ValidateFileKind(record.kind);

      ValidateStringOffset(header, record.relpath_offset);
      const auto rel
        = ExtractNullTerminatedString(stored_table, record.relpath_offset);
      ValidateRelativePath(rel);

      if (out.kind_to_file_.contains(record.kind)) {
        throw std::runtime_error(
          "Duplicate FileKind record in loose cooked index");
      }

      LooseCookedIndex::FileInfo info {
        .relpath_offset = record.relpath_offset,
        .size = record.size,
      };

      out.kind_to_file_.insert_or_assign(record.kind, info);
      out.file_kinds_.push_back(record.kind);
    }
  }

  const auto has_buffers_table
    = out.kind_to_file_.contains(FileKind::kBuffersTable);
  const auto has_buffers_data
    = out.kind_to_file_.contains(FileKind::kBuffersData);
  if (has_buffers_table != has_buffers_data) {
    throw std::runtime_error(
      "Loose cooked index must provide both buffers.table and buffers.data");
  }

  const auto has_textures_table
    = out.kind_to_file_.contains(FileKind::kTexturesTable);
  const auto has_textures_data
    = out.kind_to_file_.contains(FileKind::kTexturesData);
  if (has_textures_table != has_textures_data) {
    throw std::runtime_error(
      "Loose cooked index must provide both textures.table and textures.data");
  }

  return out;
}

auto LooseCookedIndex::Guid() const noexcept -> data::SourceKey
{
  return guid_;
}

auto LooseCookedIndex::FindDescriptorRelPath(
  const data::AssetKey& key) const noexcept -> std::optional<std::string_view>
{
  const auto it = key_to_asset_info_.find(key);
  if (it == key_to_asset_info_.end()) {
    return std::nullopt;
  }

  try {
    const auto table
      = std::string_view(string_storage_.data(), string_storage_.size());
    return ExtractNullTerminatedString(
      table, it->second.descriptor_relpath_offset);
  } catch (...) {
    return std::nullopt;
  }
}

auto LooseCookedIndex::FindDescriptorSize(
  const data::AssetKey& key) const noexcept -> std::optional<uint64_t>
{
  const auto it = key_to_asset_info_.find(key);
  if (it == key_to_asset_info_.end()) {
    return std::nullopt;
  }
  return it->second.descriptor_size;
}

auto LooseCookedIndex::FindDescriptorSha256(
  const data::AssetKey& key) const noexcept
  -> std::optional<
    std::span<const uint8_t, oxygen::data::loose_cooked::v1::kSha256Size>>
{
  const auto it = key_to_asset_info_.find(key);
  if (it == key_to_asset_info_.end()) {
    return std::nullopt;
  }

  return std::span<const uint8_t, oxygen::data::loose_cooked::v1::kSha256Size>(
    it->second.descriptor_sha256);
}

auto LooseCookedIndex::FindVirtualPath(const data::AssetKey& key) const noexcept
  -> std::optional<std::string_view>
{
  const auto it = key_to_asset_info_.find(key);
  if (it == key_to_asset_info_.end()) {
    return std::nullopt;
  }

  try {
    const auto table
      = std::string_view(string_storage_.data(), string_storage_.size());
    return ExtractNullTerminatedString(table, it->second.virtual_path_offset);
  } catch (...) {
    return std::nullopt;
  }
}

auto LooseCookedIndex::FindAssetType(const data::AssetKey& key) const noexcept
  -> std::optional<uint8_t>
{
  const auto it = key_to_asset_info_.find(key);
  if (it == key_to_asset_info_.end()) {
    return std::nullopt;
  }
  return it->second.asset_type;
}

auto LooseCookedIndex::GetAllFileKinds() const noexcept
  -> std::span<const data::loose_cooked::v1::FileKind>
{
  return file_kinds_;
}

auto LooseCookedIndex::FindAssetKeyByVirtualPath(
  std::string_view virtual_path) const noexcept -> std::optional<data::AssetKey>
{
  try {
    const auto table
      = std::string_view(string_storage_.data(), string_storage_.size());

    for (const auto& [offset, key] : virtual_path_offset_to_key_) {
      const auto stored = ExtractNullTerminatedString(table, offset);
      if (stored == virtual_path) {
        return key;
      }
    }
    return std::nullopt;
  } catch (...) {
    return std::nullopt;
  }
}

auto LooseCookedIndex::FindFileRelPath(FileKind kind) const noexcept
  -> std::optional<std::string_view>
{
  const auto it = kind_to_file_.find(kind);
  if (it == kind_to_file_.end()) {
    return std::nullopt;
  }

  try {
    const auto table
      = std::string_view(string_storage_.data(), string_storage_.size());
    return ExtractNullTerminatedString(table, it->second.relpath_offset);
  } catch (...) {
    return std::nullopt;
  }
}

auto LooseCookedIndex::FindFileSize(FileKind kind) const noexcept
  -> std::optional<uint64_t>
{
  const auto it = kind_to_file_.find(kind);
  if (it == kind_to_file_.end()) {
    return std::nullopt;
  }
  return it->second.size;
}

} // namespace oxygen::content::detail
