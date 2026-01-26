//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iterator>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Base/Result.h>
#include <Oxygen/Content/Detail/LooseCookedIndex.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/LooseCookedIndexFormat.h>
#include <Oxygen/Data/SourceKey.h>
#include <Oxygen/Serio/FileStream.h>
#include <Oxygen/Serio/Reader.h>

using oxygen::data::loose_cooked::AssetEntry;
using oxygen::data::loose_cooked::FileKind;
using oxygen::data::loose_cooked::FileRecord;
using oxygen::data::loose_cooked::IndexHeader;
using oxygen::data::loose_cooked::kHasFileRecords;
using oxygen::data::loose_cooked::kHasVirtualPaths;
using oxygen::data::loose_cooked::kHeaderMagic;
using oxygen::data::loose_cooked::kKnownIndexFlags;

namespace serio = oxygen::serio;
namespace data = oxygen::data;
namespace content = oxygen::content;

namespace oxygen::serio {

//! Deserializes an IndexHeader from the stream.
static auto Load(AnyReader& reader, IndexHeader& value) -> Result<void>
{
  CHECK_RESULT(reader.AlignTo(1));
  CHECK_RESULT(
    reader.ReadBlobInto(std::as_writable_bytes(std::span(&value, 1))));
  return {};
}

//! Deserializes an AssetEntry from the stream.
static auto Load(AnyReader& reader, AssetEntry& value) -> Result<void>
{
  CHECK_RESULT(reader.AlignTo(1));
  CHECK_RESULT(
    reader.ReadBlobInto(std::as_writable_bytes(std::span(&value, 1))));
  return {};
}

//! Deserializes a FileRecord from the stream.
static auto Load(AnyReader& reader, FileRecord& value) -> Result<void>
{
  CHECK_RESULT(reader.AlignTo(1));
  CHECK_RESULT(
    reader.ReadBlobInto(std::as_writable_bytes(std::span(&value, 1))));
  return {};
}

} // namespace oxygen::serio

namespace {

auto ValidateMagic(const IndexHeader& header) -> void
{
  const auto actual = std::span<const char>(header.magic);
  if (!std::ranges::equal(actual, std::span<const char>(kHeaderMagic))) {
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

auto ValidateSectionLayout(const IndexHeader& header) -> void
{
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

auto ExtractNullTerminatedString(std::string_view table, const uint32_t offset)
  -> std::string_view
{
  if (offset >= table.size()) {
    throw std::runtime_error("String table offset out of range");
  }

  const auto chars = std::span<const char>(table.data(), table.size());
  const auto tail = chars.subspan(offset);
  const auto end = std::ranges::find(tail, '\0');
  if (end == tail.end()) {
    throw std::runtime_error("Unterminated string in string table");
  }

  const auto len = static_cast<size_t>(std::distance(tail.begin(), end));
  return { tail.data(), len };
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

  if (relpath.contains('\\')) {
    throw std::runtime_error("Index path must use '/' as the separator");
  }
  if (relpath.contains(':')) {
    throw std::runtime_error("Index path must not contain ':'");
  }
  if (relpath.front() == '/') {
    throw std::runtime_error("Index path must be container-relative");
  }
  if (relpath.back() == '/') {
    throw std::runtime_error("Index path must not end with '/'");
  }
  if (relpath.contains("//")) {
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
  if (virtual_path.contains('\\')) {
    throw std::runtime_error("Virtual path must use '/' as the separator");
  }
  if (virtual_path.front() != '/') {
    throw std::runtime_error("Virtual path must start with '/'");
  }
  if (virtual_path.size() > 1 && virtual_path.back() == '/') {
    throw std::runtime_error(
      "Virtual path must not end with '/' (except the root)");
  }
  if (virtual_path.contains("//")) {
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

  if ((flags & ~kKnownIndexFlags) != 0U) {
    throw std::runtime_error(
      "Unsupported IndexHeader flags in loose cooked index");
  }

  // Backward compatibility: flags==0 is a legacy value.
  if (flags == 0U) {
    return;
  }

  // For v1 indexes, asset virtual paths are part of the contract.
  if ((flags & static_cast<uint32_t>(kHasVirtualPaths)) == 0U) {
    throw std::runtime_error(
      "Loose cooked index flags must declare virtual-path support");
  }

  const auto declares_file_records
    = (flags & static_cast<uint32_t>(kHasFileRecords)) != 0U;

  if (!declares_file_records && header.file_record_count != 0U) {
    throw std::runtime_error("Loose cooked index flags disallow file "
                             "records, but file_record_count is non-zero");
  }

  if (declares_file_records && header.file_record_size != sizeof(FileRecord)) {
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

namespace oxygen::content::detail {

struct LooseCookedIndex::IndexLoadContext {
  oxygen::observer_ptr<serio::Reader<serio::FileStream<>>> reader;
  uint64_t file_size;
  IndexHeader header;
  oxygen::observer_ptr<LooseCookedIndex> index;
  std::string_view stored_table;
  std::unordered_set<std::string_view> unique_virtual_paths;
};

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
  LooseCookedIndex out;
  IndexLoadContext context {
    .reader = oxygen::make_observer(&reader),
    .file_size = file_size,
    .header = {},
    .index = oxygen::make_observer(&out),
  };

  LoadAndValidateHeader(context);
  ReadStringTable(context);
  ReadAssetEntries(context);
  ReadFileRecords(context);
  ValidateFilePairs(out);

  return out;
}

auto LooseCookedIndex::LoadAndValidateHeader(IndexLoadContext& context) -> void
{
  auto header_result = context.reader->Read<IndexHeader>();
  if (!header_result.has_value()) {
    throw std::runtime_error(
      "Failed to read index header: " + header_result.error().message());
  }
  const IndexHeader header = header_result.value();

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

  ValidateSectionLayout(header);

  ValidateSectionRange(context.file_size, header.string_table_offset,
    header.string_table_size, "string table");

  ValidateSectionRange(context.file_size, header.asset_entries_offset,
    static_cast<uint64_t>(header.asset_count) * sizeof(AssetEntry),
    "asset entries");

  const auto declares_file_records = header.flags != 0U
    && (header.flags & static_cast<uint32_t>(kHasFileRecords)) != 0U;

  if (declares_file_records || header.file_record_count > 0) {
    ValidateSectionRange(context.file_size, header.file_records_offset,
      static_cast<uint64_t>(header.file_record_count) * sizeof(FileRecord),
      "file records");
  }

  context.header = header;
}

auto LooseCookedIndex::ReadStringTable(IndexLoadContext& context) -> void
{
  context.index->guid_ = data::SourceKey::FromBytes(context.header.guid);
  context.index->string_storage_.resize(context.header.string_table_size);
  if (auto res = context.reader->Seek(context.header.string_table_offset);
    !res) {
    throw std::runtime_error(
      "Failed to seek to string table: " + res.error().message());
  }
  const auto result = context.reader->ReadBlobInto(std::span(
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    reinterpret_cast<std::byte*>(context.index->string_storage_.data()),
    context.index->string_storage_.size()));
  if (!result) {
    throw std::runtime_error(
      std::string("Failed to read string table: " + result.error().message()));
  }

  if (context.index->string_storage_.empty()
    || context.index->string_storage_.front() != '\0') {
    throw std::runtime_error("String table must start with a NUL byte");
  }
  context.stored_table = std::string_view(context.index->string_storage_.data(),
    context.index->string_storage_.size());
}

auto LooseCookedIndex::ReadAssetEntries(IndexLoadContext& context) -> void
{
  if (auto res = context.reader->Seek(context.header.asset_entries_offset);
    !res) {
    throw std::runtime_error(
      "Failed to seek to asset entries: " + res.error().message());
  }

  for (uint32_t i = 0; i < context.header.asset_count; ++i) {
    auto entry_result = context.reader->Read<AssetEntry>();
    if (!entry_result) {
      throw std::runtime_error(
        "Failed to read asset entry: " + entry_result.error().message());
    }
    const auto& entry = entry_result.value();

    ValidateStringOffset(context.header, entry.descriptor_relpath_offset);
    ValidateStringOffset(context.header, entry.virtual_path_offset);

    const auto descriptor_rel = ExtractNullTerminatedString(
      context.stored_table, entry.descriptor_relpath_offset);
    const auto virtual_path = ExtractNullTerminatedString(
      context.stored_table, entry.virtual_path_offset);

    ValidateRelativePath(descriptor_rel);
    ValidateVirtualPath(virtual_path);

    if (context.index->key_to_asset_info_.contains(entry.asset_key)) {
      throw std::runtime_error("Duplicate AssetKey in loose cooked index");
    }
    if (context.index->virtual_path_offset_to_key_.contains(
          entry.virtual_path_offset)) {
      throw std::runtime_error(
        "Duplicate virtual path offset in loose cooked index");
    }

    if (!context.unique_virtual_paths.insert(virtual_path).second) {
      throw std::runtime_error(
        "Duplicate virtual path string in loose cooked index");
    }

    AssetInfo info {
      .descriptor_relpath_offset = entry.descriptor_relpath_offset,
      .virtual_path_offset = entry.virtual_path_offset,
      .descriptor_size = entry.descriptor_size,
      .asset_type = entry.asset_type,
    };
    static_assert(
      sizeof(info.descriptor_sha256) == sizeof(entry.descriptor_sha256));
    std::copy_n(std::begin(entry.descriptor_sha256),
      info.descriptor_sha256.size(), info.descriptor_sha256.begin());

    context.index->asset_keys_.push_back(entry.asset_key);
    context.index->key_to_asset_info_.insert_or_assign(entry.asset_key, info);
    context.index->virtual_path_offset_to_key_.insert_or_assign(
      entry.virtual_path_offset, entry.asset_key);
  }
}

auto LooseCookedIndex::ReadFileRecords(IndexLoadContext& context) -> void
{
  if (context.header.file_record_count == 0) {
    return;
  }

  if (auto res = context.reader->Seek(context.header.file_records_offset);
    !res) {
    throw std::runtime_error(
      "Failed to seek to file records: " + res.error().message());
  }

  for (uint32_t i = 0; i < context.header.file_record_count; ++i) {
    auto record_result = context.reader->Read<FileRecord>();
    if (!record_result) {
      throw std::runtime_error(
        "Failed to read file record: " + record_result.error().message());
    }
    const auto& record = record_result.value();

    ValidateFileKind(record.kind);

    ValidateStringOffset(context.header, record.relpath_offset);
    const auto rel = ExtractNullTerminatedString(
      context.stored_table, record.relpath_offset);
    ValidateRelativePath(rel);

    if (context.index->kind_to_file_.contains(record.kind)) {
      throw std::runtime_error(
        "Duplicate FileKind record in loose cooked index");
    }

    FileInfo info {
      .relpath_offset = record.relpath_offset,
      .size = record.size,
    };

    context.index->kind_to_file_.insert_or_assign(record.kind, info);
    context.index->file_kinds_.push_back(record.kind);
  }
}

auto LooseCookedIndex::ValidateFilePairs(const LooseCookedIndex& index) -> void
{
  const auto has_buffers_table
    = index.kind_to_file_.contains(FileKind::kBuffersTable);
  const auto has_buffers_data
    = index.kind_to_file_.contains(FileKind::kBuffersData);
  if (has_buffers_table != has_buffers_data) {
    throw std::runtime_error(
      "Loose cooked index must provide both buffers.table and buffers.data");
  }

  const auto has_textures_table
    = index.kind_to_file_.contains(FileKind::kTexturesTable);
  const auto has_textures_data
    = index.kind_to_file_.contains(FileKind::kTexturesData);
  if (has_textures_table != has_textures_data) {
    throw std::runtime_error(
      "Loose cooked index must provide both textures.table and textures.data");
  }
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
  -> std::optional<std::span<const uint8_t, data::loose_cooked::kSha256Size>>
{
  const auto it = key_to_asset_info_.find(key);
  if (it == key_to_asset_info_.end()) {
    return std::nullopt;
  }

  return std::span<const uint8_t, data::loose_cooked::kSha256Size>(
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
  -> std::span<const FileKind>
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
