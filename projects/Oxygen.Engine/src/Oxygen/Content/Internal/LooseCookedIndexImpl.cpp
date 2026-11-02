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
#include <system_error>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Base/Result.h>
#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Content/Internal/LooseCookedIndexImpl.h>
#include <Oxygen/Content/VirtualPath.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/CookedOpenError.h>
#include <Oxygen/Data/LooseCookedIndexFormat.h>
#include <Oxygen/Data/LooseCookedPaths.h>
#include <Oxygen/Data/LooseCookedResources.h>
#include <Oxygen/Data/SourceKey.h>
#include <Oxygen/Serio/ByteReader.h>
#include <Oxygen/Serio/MappedFile.h>
#include <Oxygen/Serio/Types.h>

using oxygen::data::loose_cooked::AssetEntry;
using oxygen::data::loose_cooked::FileKind;
using oxygen::data::loose_cooked::FileRecord;
using oxygen::data::loose_cooked::IndexHeader;
using oxygen::data::loose_cooked::kHasFileRecords;
using oxygen::data::loose_cooked::kHasVirtualPaths;
using oxygen::data::loose_cooked::kHeaderMagic;
using oxygen::data::loose_cooked::kKnownIndexFlags;

namespace data = oxygen::data;
namespace content = oxygen::content;

namespace {

auto ValidateMagic(const IndexHeader& header) -> void
{
  const auto actual = std::span<const char>(header.magic);
  const auto is_index_magic
    = std::ranges::equal(actual, std::span<const char>(kHeaderMagic));
  if (!is_index_magic) {
    throw data::CookedOpenError(
      data::CookedDefect::kWrongMagic, "Invalid loose cooked index magic");
  }
}

auto ValidateSectionRange(const size_t file_size, const uint64_t offset,
  const uint64_t size, const std::string_view what) -> void
{
  const auto in_file = offset <= file_size && size <= file_size - offset;
  if (!in_file) {
    throw data::CookedOpenError(data::CookedDefect::kOutOfBounds,
      std::string(what) + " lies outside the index file");
  }
}

auto ValidateSectionLayout(const IndexHeader& header) -> void
{
  const auto has_string_table = header.string_table_size != 0;
  if (!has_string_table) {
    throw data::CookedOpenError(
      data::CookedDefect::kInvalidString, "String table must not be empty");
  }

  const auto strings_after_header
    = header.string_table_offset >= sizeof(IndexHeader);
  if (!strings_after_header) {
    throw data::CookedOpenError(data::CookedDefect::kOverlap,
      "String table must start after index header");
  }

  const auto expected_asset_entries_min
    = header.string_table_offset + header.string_table_size;
  const auto entries_after_strings
    = header.asset_entries_offset >= expected_asset_entries_min;
  if (!entries_after_strings) {
    throw data::CookedOpenError(data::CookedDefect::kOverlap,
      "Asset entries must start after the end of the string table");
  }

  const auto expected_file_records_min = header.asset_entries_offset
    + (static_cast<uint64_t>(header.asset_count) * sizeof(AssetEntry));
  const auto records_after_entries
    = header.file_records_offset >= expected_file_records_min;
  if (!records_after_entries) {
    throw data::CookedOpenError(data::CookedDefect::kOverlap,
      "File records must start after the end of the asset entries");
  }
}

auto ValidateStringOffset(const IndexHeader& header, const uint32_t offset)
  -> void
{
  const auto in_table
    = static_cast<uint64_t>(offset) < header.string_table_size;
  if (!in_table) {
    throw data::CookedOpenError(
      data::CookedDefect::kOutOfBounds, "String table offset out of range");
  }
}

auto ExtractNullTerminatedString(std::string_view table, const uint32_t offset)
  -> std::string_view
{
  const auto in_table = offset < table.size();
  if (!in_table) {
    throw data::CookedOpenError(
      data::CookedDefect::kOutOfBounds, "String table offset out of range");
  }

  const auto chars = std::span<const char>(table.data(), table.size());
  const auto tail = chars.subspan(offset);
  const auto end = std::ranges::find(tail, '\0');
  const auto is_terminated = end != tail.end();
  if (!is_terminated) {
    throw data::CookedOpenError(data::CookedDefect::kInvalidString,
      "Unterminated string in string table");
  }

  const auto len = static_cast<size_t>(std::distance(tail.begin(), end));
  return { tail.data(), len };
}

auto RequireCanonicalVirtualPath(const std::string_view virtual_path) -> void
{
  if (const auto error = content::ValidateVirtualPath(virtual_path);
    error.has_value()) {
    throw data::CookedOpenError(data::CookedDefect::kInvalidPath,
      std::string("Virtual path is not canonical: ")
        + nostd::to_string(*error));
  }
}

//! `RequireIndexRelPath` is shared with the index writers, so it reports a
//! plain `std::runtime_error`; for a read index the broken rule is typed.
auto RequireCanonicalRelPath(const std::string_view relpath) -> void
{
  try {
    data::loose_cooked::RequireIndexRelPath(relpath);
  } catch (const std::runtime_error& ex) {
    throw data::CookedOpenError(data::CookedDefect::kInvalidPath, ex.what());
  }
}

auto ValidateFileKind(const FileKind kind) -> void
{
  switch (kind) {
  case FileKind::kBuffersTable:
  case FileKind::kBuffersData:
  case FileKind::kTexturesTable:
  case FileKind::kTexturesData:
  case FileKind::kScriptsTable:
  case FileKind::kScriptsData:
  case FileKind::kPhysicsTable:
  case FileKind::kPhysicsData:
  case FileKind::kAuxiliary:
  case FileKind::kTextureDescriptor:
    return;
  case FileKind::kUnknown:
  default:
    break;
  }

  throw data::CookedOpenError(data::CookedDefect::kInvalidValue,
    "Unsupported FileKind in loose cooked index");
}

auto ValidateHeaderFlags(const IndexHeader& header) -> void
{
  const auto flags = header.flags;

  const auto flags_known = (flags & ~kKnownIndexFlags) == 0U;
  if (!flags_known) {
    throw data::CookedOpenError(data::CookedDefect::kInvalidValue,
      "Unsupported IndexHeader flags in loose cooked index");
  }

  // Virtual paths are mandatory in the current format.
  const auto declares_virtual_paths
    = (flags & static_cast<uint32_t>(kHasVirtualPaths)) != 0U;
  if (!declares_virtual_paths) {
    throw data::CookedOpenError(data::CookedDefect::kInvalidValue,
      "Loose cooked index flags must declare virtual-path support");
  }

  const auto declares_file_records
    = (flags & static_cast<uint32_t>(kHasFileRecords)) != 0U;

  const auto records_match_flags
    = declares_file_records || header.file_record_count == 0U;
  if (!records_match_flags) {
    throw data::CookedOpenError(data::CookedDefect::kInconsistent,
      "Loose cooked index flags disallow file "
      "records, but file_record_count is non-zero");
  }

  const auto record_size_matches
    = !declares_file_records || header.file_record_size == sizeof(FileRecord);
  if (!record_size_matches) {
    throw data::CookedOpenError(data::CookedDefect::kWrongRowSize,
      "Unexpected FileRecord size in index header");
  }
}

auto ParseSourceKeyOrThrow(const std::array<uint8_t, 16>& source_identity,
  const char* error_message) -> data::SourceKey
{
  const auto source_key = data::SourceKey::FromBytes(source_identity);
  if (!source_key.has_value()) {
    throw data::CookedOpenError(
      data::CookedDefect::kInvalidIdentity, error_message);
  }
  return source_key.value();
}

auto ValidateGuid(const IndexHeader& header) -> void
{
  (void)ParseSourceKeyOrThrow(header.source_identity,
    "Loose cooked index must have a non-zero UUIDv7 source identity");
}

} // namespace

namespace oxygen::content::internal {

struct LooseCookedIndexImpl::IndexLoadContext {
  oxygen::observer_ptr<serio::ByteReader> reader;
  std::span<const std::byte> bytes;
  uint64_t file_size;
  uint64_t reference_start = 0;
  uint64_t next_reference_offset = 0;
  IndexHeader header;
  oxygen::observer_ptr<LooseCookedIndexImpl> index;
  std::unordered_set<std::string_view> unique_virtual_paths {}; // NOLINT
  std::unordered_set<std::string_view> unique_file_paths {}; // NOLINT

  auto InitializeReferenceBlock() -> void;
  auto ValidateReferenceBlockCoverage() const -> void;
  auto StoreReferenceTail() const -> void;
  auto ValidateAssetReferences(const AssetEntry& entry) -> void;
  auto ReadAndValidateAssetPaths(const AssetEntry& entry) -> std::string_view;
  auto ValidateAssetIntegrityAndIdentity(
    const AssetEntry& entry, std::string_view virtual_path) -> void;
  auto RecordAsset(const AssetEntry& entry) const -> void;
  auto ValidateFileMetadata(const FileRecord& record) -> void;
  auto ValidateFileVirtualAndSourcePaths(const FileRecord& record) -> void;
  auto RecordFile(const FileRecord& record) const -> void;
};

auto LooseCookedIndexImpl::LockPathOf(const std::filesystem::path& index_path)
  -> std::filesystem::path
{
  return index_path.parent_path()
    / std::string(data::loose_cooked::kIndexLockFileName);
}

auto LooseCookedIndexImpl::LoadFromFile(const std::filesystem::path& index_path)
  -> LooseCookedIndexImpl
{
  // The mapping shares deletion, so it never blocks the cooker replacing the
  // published index, and keeps showing the version it mapped.
  auto mapped = serio::MappedFile::Open(index_path);
  if (!mapped) {
    throw std::system_error(mapped.error(), "Failed to map index file");
  }
  return Load(std::move(*mapped));
}

auto LooseCookedIndexImpl::LoadFromBytes(const std::span<const std::byte> bytes)
  -> LooseCookedIndexImpl
{
  return Load(std::vector<std::byte>(bytes.begin(), bytes.end()));
}

auto LooseCookedIndexImpl::LoadFromBytes(std::vector<std::byte>&& bytes)
  -> LooseCookedIndexImpl
{
  return Load(std::move(bytes));
}

auto LooseCookedIndexImpl::Load(IndexBytes bytes) -> LooseCookedIndexImpl
{
  LooseCookedIndexImpl out;
  out.bytes_ = std::move(bytes);
  // Every refusal of the bytes is a typed `CookedOpenError`.
  out.Parse();
  // Moving the index moves its bytes, which its views keep pointing into.
  return out;
}

auto LooseCookedIndexImpl::Parse() -> void
{
  const auto bytes = std::holds_alternative<serio::MappedFile>(bytes_)
    ? std::get<serio::MappedFile>(bytes_).Bytes()
    : std::span<const std::byte>(std::get<std::vector<std::byte>>(bytes_));
  const auto file_size = bytes.size();
  const auto holds_header = file_size >= sizeof(IndexHeader);
  if (!holds_header) {
    throw data::CookedOpenError(data::CookedDefect::kTooSmall,
      "File too small to hold a loose cooked index header");
  }

  auto reader = serio::ByteReader(bytes);
  IndexLoadContext context {
    .reader = oxygen::make_observer(&reader),
    .bytes = bytes,
    .file_size = file_size,
    .reference_start = 0,
    .next_reference_offset = 0,
    .header = {},
    .index = oxygen::make_observer(this),
  };

  LoadAndValidateHeader(context);
  content_version_ = context.header.content_version;
  context.InitializeReferenceBlock();
  ReadStringTable(context);
  ReadAssetEntries(context);
  context.ValidateReferenceBlockCoverage();
  ReadFileRecords(context);
  ValidateFilePairs(*this);
  context.StoreReferenceTail();
}

auto LooseCookedIndexImpl::IndexLoadContext::InitializeReferenceBlock() -> void
{
  reference_start = header.file_records_offset
    + (uint64_t { header.file_record_count } * sizeof(FileRecord));
  ValidateSectionRange(file_size, reference_start, 0, "reference block start");
  next_reference_offset = reference_start;
}

auto LooseCookedIndexImpl::IndexLoadContext::ValidateReferenceBlockCoverage()
  const -> void
{
  const auto blocks_cover_tail = next_reference_offset == file_size;
  if (!blocks_cover_tail) {
    throw data::CookedOpenError(data::CookedDefect::kOverlap,
      "Loose cooked reference blocks do not cover the index tail");
  }
}

auto LooseCookedIndexImpl::IndexLoadContext::StoreReferenceTail() const -> void
{
  index->reference_tail_offset_ = reference_start;
  if (!reader->Seek(serio::ByteOffset { reference_start })) {
    throw data::CookedOpenError(data::CookedDefect::kTruncated,
      "Failed to seek loose cooked reference blocks");
  }
  const auto tail
    = reader->ReadBytes(serio::ByteCount { file_size - reference_start });
  if (!tail) {
    throw data::CookedOpenError(data::CookedDefect::kTruncated,
      "Failed to read loose cooked reference blocks");
  }
  index->reference_tail_ = *tail;
}

auto LooseCookedIndexImpl::LoadAndValidateHeader(IndexLoadContext& context)
  -> void
{
  auto header_result = context.reader->Read<IndexHeader>();
  if (!header_result.has_value()) {
    throw data::CookedOpenError(data::CookedDefect::kTruncated,
      "Failed to read index header: " + header_result.error().message());
  }
  const IndexHeader header = header_result.value();

  ValidateMagic(header);

  const auto version_supported
    = header.version == data::loose_cooked::kIndexVersion;
  if (!version_supported) {
    throw data::CookedOpenError(data::CookedDefect::kUnsupportedVersion,
      "Unsupported loose cooked index version");
  }

  ValidateHeaderFlags(header);
  ValidateGuid(header);

  const auto entry_size_matches = header.asset_entry_size == sizeof(AssetEntry);
  if (!entry_size_matches) {
    throw data::CookedOpenError(data::CookedDefect::kWrongRowSize,
      "Unexpected AssetEntry size in index header");
  }

  // A zero record size is a mismatch too.
  const auto record_size_matches = header.file_record_count == 0
    || header.file_record_size == sizeof(FileRecord);
  if (!record_size_matches) {
    throw data::CookedOpenError(data::CookedDefect::kWrongRowSize,
      "Unexpected FileRecord size in index header");
  }

  const auto reserved_zero = std::ranges::all_of(
    header._reserved, [](const uint8_t value) -> bool { return value == 0U; });
  if (!reserved_zero) {
    throw data::CookedOpenError(data::CookedDefect::kReservedNotZero,
      "Loose cooked index header reserved bytes are not zero");
  }

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

  // Every section is within the file, so the layout's sums cannot overflow.
  ValidateSectionLayout(header);

  context.header = header;
}

auto LooseCookedIndexImpl::ReadStringTable(IndexLoadContext& context) -> void
{
  context.index->guid_ = ParseSourceKeyOrThrow(context.header.source_identity,
    "Loose cooked index must have a non-zero UUIDv7 source identity");
  if (auto res = context.reader->Seek(
        serio::ByteOffset { context.header.string_table_offset });
    !res) {
    throw data::CookedOpenError(data::CookedDefect::kTruncated,
      "Failed to seek to string table: " + res.error().message());
  }
  const auto table = context.reader->ReadStringView(
    serio::ByteCount { context.header.string_table_size });
  if (!table) {
    throw data::CookedOpenError(data::CookedDefect::kTruncated,
      "Failed to read string table: " + table.error().message());
  }
  const auto starts_with_nul = !table->empty() && table->front() == '\0';
  if (!starts_with_nul) {
    throw data::CookedOpenError(data::CookedDefect::kInvalidString,
      "String table must start with a NUL byte");
  }
  context.index->string_table_ = *table;
}

auto LooseCookedIndexImpl::ReadAssetEntries(IndexLoadContext& context) -> void
{
  if (auto res = context.reader->Seek(
        serio::ByteOffset { context.header.asset_entries_offset });
    !res) {
    throw data::CookedOpenError(data::CookedDefect::kTruncated,
      "Failed to seek to asset entries: " + res.error().message());
  }

  for (uint32_t i = 0; i < context.header.asset_count; ++i) {
    auto entry_result = context.reader->Read<AssetEntry>();
    if (!entry_result) {
      throw data::CookedOpenError(data::CookedDefect::kTruncated,
        "Failed to read asset entry: " + entry_result.error().message());
    }
    const auto& entry = entry_result.value();
    context.ValidateAssetReferences(entry);
    const auto virtual_path = context.ReadAndValidateAssetPaths(entry);
    context.ValidateAssetIntegrityAndIdentity(entry, virtual_path);
    context.RecordAsset(entry);
  }
}

auto LooseCookedIndexImpl::IndexLoadContext::ValidateAssetReferences(
  const AssetEntry& entry) -> void
{
  const auto reference_size = data::AssetReferences::EncodedSize(
    entry.references.resource_count, entry.references.key_count);
  if (reference_size == 0U) {
    if (entry.references.offset != 0U) {
      throw data::CookedOpenError(data::CookedDefect::kInvalidReference,
        "Empty asset reference block must have zero offset");
    }
  } else {
    const auto follows_previous
      = entry.references.offset == next_reference_offset;
    if (!follows_previous) {
      throw data::CookedOpenError(data::CookedDefect::kOverlap,
        "Asset reference blocks must follow directory "
        "order without gaps or overlap");
    }
    ValidateSectionRange(
      file_size, entry.references.offset, reference_size, "asset references");
    const auto decoded = data::AssetReferences::Decode(
      bytes.subspan(static_cast<size_t>(entry.references.offset),
        static_cast<size_t>(reference_size)),
      entry.references.resource_count, entry.references.key_count);
    if (!decoded) {
      throw data::CookedOpenError(data::CookedDefect::kInvalidReference,
        "Asset reference block: " + decoded.error());
    }
    next_reference_offset += reference_size;
  }
}

auto LooseCookedIndexImpl::IndexLoadContext::ReadAndValidateAssetPaths(
  const AssetEntry& entry) -> std::string_view
{
  ValidateStringOffset(header, entry.descriptor_relpath_offset);
  ValidateStringOffset(header, entry.virtual_path_offset);
  ValidateStringOffset(header, entry.source_path_offset);

  const auto descriptor_rel = ExtractNullTerminatedString(
    index->string_table_, entry.descriptor_relpath_offset);
  const auto virtual_path = ExtractNullTerminatedString(
    index->string_table_, entry.virtual_path_offset);
  const auto source_path = ExtractNullTerminatedString(
    index->string_table_, entry.source_path_offset);

  RequireCanonicalRelPath(descriptor_rel);
  RequireCanonicalVirtualPath(virtual_path);
  RequireCanonicalVirtualPath(source_path);
  const auto unique_content_path
    = unique_file_paths.insert(descriptor_rel).second;
  if (!unique_content_path) {
    throw data::CookedOpenError(data::CookedDefect::kDuplicate,
      "Duplicate content path in loose cooked index");
  }
  return virtual_path;
}

auto LooseCookedIndexImpl::IndexLoadContext::ValidateAssetIntegrityAndIdentity(
  const AssetEntry& entry, const std::string_view virtual_path) -> void
{
  const auto has_descriptor_digest = !std::ranges::all_of(
    entry.descriptor_sha256, [](uint8_t value) -> bool { return value == 0; });
  if (!has_descriptor_digest) {
    throw data::CookedOpenError(data::CookedDefect::kMissingDigest,
      "Missing descriptor SHA-256 in loose cooked index");
  }

  const auto unique_asset_key
    = !index->key_to_asset_info_.contains(entry.asset_key);
  if (!unique_asset_key) {
    throw data::CookedOpenError(data::CookedDefect::kDuplicate,
      "Duplicate AssetKey in loose cooked index");
  }
  const auto unique_virtual_path_offset
    = !index->virtual_path_offset_to_key_.contains(entry.virtual_path_offset);
  if (!unique_virtual_path_offset) {
    throw data::CookedOpenError(data::CookedDefect::kDuplicate,
      "Duplicate virtual path offset in loose cooked index");
  }

  const auto unique_virtual_path
    = unique_virtual_paths.insert(virtual_path).second;
  if (!unique_virtual_path) {
    throw data::CookedOpenError(data::CookedDefect::kDuplicate,
      "Duplicate virtual path string in loose cooked index");
  }
}

auto LooseCookedIndexImpl::IndexLoadContext::RecordAsset(
  const AssetEntry& entry) const -> void
{
  AssetInfo info {
    .descriptor_relpath_offset = entry.descriptor_relpath_offset,
    .virtual_path_offset = entry.virtual_path_offset,
    .source_path_offset = entry.source_path_offset,
    .descriptor_size = entry.descriptor_size,
    .asset_type = entry.asset_type,
    .descriptor_sha256 = {},
    .references = entry.references,
  };
  static_assert(
    sizeof(info.descriptor_sha256) == sizeof(entry.descriptor_sha256));
  std::copy_n(std::begin(entry.descriptor_sha256),
    info.descriptor_sha256.size(), info.descriptor_sha256.begin());

  index->asset_keys_.push_back(entry.asset_key);
  index->key_to_asset_info_.insert_or_assign(entry.asset_key, info);
  index->virtual_path_offset_to_key_.insert_or_assign(
    entry.virtual_path_offset, entry.asset_key);
}

auto LooseCookedIndexImpl::HasKeyReferences(
  const data::AssetKey& key) const noexcept -> bool
{
  const auto found = key_to_asset_info_.find(key);
  return found != key_to_asset_info_.end()
    && found->second.references.key_count != 0U;
}

auto LooseCookedIndexImpl::FindAssetReferences(const data::AssetKey& key) const
  -> std::optional<data::AssetReferences>
{
  const auto found = key_to_asset_info_.find(key);
  if (found == key_to_asset_info_.end()) {
    return std::nullopt;
  }
  const auto& table = found->second.references;
  const auto size
    = data::AssetReferences::EncodedSize(table.resource_count, table.key_count);
  const auto offset = size == 0U ? 0U : table.offset - reference_tail_offset_;
  const auto bytes = reference_tail_.subspan(
    static_cast<size_t>(offset), static_cast<size_t>(size));
  auto decoded = data::AssetReferences::Decode(
    bytes, table.resource_count, table.key_count);
  if (!decoded) {
    throw std::runtime_error(decoded.error());
  }
  return std::move(*decoded);
}

auto LooseCookedIndexImpl::ReadFileRecords(IndexLoadContext& context) -> void
{
  if (context.header.file_record_count == 0) {
    return;
  }

  if (auto res = context.reader->Seek(
        serio::ByteOffset { context.header.file_records_offset });
    !res) {
    throw data::CookedOpenError(data::CookedDefect::kTruncated,
      "Failed to seek to file records: " + res.error().message());
  }

  for (uint32_t i = 0; i < context.header.file_record_count; ++i) {
    auto record_result = context.reader->Read<FileRecord>();
    if (!record_result) {
      throw data::CookedOpenError(data::CookedDefect::kTruncated,
        "Failed to read file record: " + record_result.error().message());
    }
    const auto& record = record_result.value();

    context.ValidateFileMetadata(record);
    context.ValidateFileVirtualAndSourcePaths(record);
    context.RecordFile(record);
  }
}

auto LooseCookedIndexImpl::IndexLoadContext::ValidateFileMetadata(
  const FileRecord& record) -> void
{
  ValidateFileKind(record.kind);

  ValidateStringOffset(header, record.relpath_offset);
  const auto rel
    = ExtractNullTerminatedString(index->string_table_, record.relpath_offset);
  RequireCanonicalRelPath(rel);
  const auto unique_content_path = unique_file_paths.insert(rel).second;
  if (!unique_content_path) {
    throw data::CookedOpenError(data::CookedDefect::kDuplicate,
      "Duplicate content path in loose cooked index");
  }
  const auto is_data_file = data::loose_cooked::IsResourceDataFile(record.kind);
  const auto has_digest = !base::IsAllZero(record.sha256);
  const auto data_file_has_no_digest = !is_data_file || !has_digest;
  if (!data_file_has_no_digest) {
    throw data::CookedOpenError(data::CookedDefect::kInvalidValue,
      "Resource data file SHA-256 in loose cooked index");
  }
  const auto file_has_digest = is_data_file || has_digest;
  if (!file_has_digest) {
    throw data::CookedOpenError(data::CookedDefect::kMissingDigest,
      "Missing file SHA-256 in loose cooked index");
  }

  const auto unique_kind = !index->kind_to_file_.contains(record.kind);
  if (!unique_kind) {
    throw data::CookedOpenError(data::CookedDefect::kDuplicate,
      "Duplicate FileKind record in loose cooked index");
  }
  // A record is identified before its size is judged.
  const auto is_table
    = data::loose_cooked::DataFileKindOf(record.kind).has_value();
  const auto holds_whole_rows = !is_table
    || data::loose_cooked::IsResourceTableSize(record.kind, record.size);
  if (!holds_whole_rows) {
    throw data::CookedOpenError(data::CookedDefect::kWrongRowSize,
      "Loose cooked resource table does not hold whole rows");
  }
}

auto LooseCookedIndexImpl::IndexLoadContext::ValidateFileVirtualAndSourcePaths(
  const FileRecord& record) -> void
{
  ValidateStringOffset(header, record.virtual_path_offset);
  ValidateStringOffset(header, record.source_path_offset);
  const auto virtual_path = ExtractNullTerminatedString(
    index->string_table_, record.virtual_path_offset);
  const auto source_path = ExtractNullTerminatedString(
    index->string_table_, record.source_path_offset);
  if (record.kind == FileKind::kTextureDescriptor) {
    RequireCanonicalVirtualPath(virtual_path);
    RequireCanonicalVirtualPath(source_path);
    const auto unique_virtual_path
      = unique_virtual_paths.insert(virtual_path).second;
    if (!unique_virtual_path) {
      throw data::CookedOpenError(data::CookedDefect::kDuplicate,
        "Duplicate virtual path in loose cooked index file records");
    }
  } else if (!virtual_path.empty()) {
    throw data::CookedOpenError(data::CookedDefect::kInvalidValue,
      "Only texture descriptor records may have a virtual path");
  } else if (!source_path.empty()) {
    // An auxiliary file, such as a buffer sidecar, may record its producer.
    if (record.kind != FileKind::kAuxiliary) {
      throw data::CookedOpenError(data::CookedDefect::kInvalidValue,
        "Only texture descriptor and auxiliary records "
        "may have a source");
    }
    RequireCanonicalVirtualPath(source_path);
  }
}

auto LooseCookedIndexImpl::IndexLoadContext::RecordFile(
  const FileRecord& record) const -> void
{
  FileInfo info {
    .relpath_offset = record.relpath_offset,
    .virtual_path_offset = record.virtual_path_offset,
    .source_path_offset = record.source_path_offset,
    .size = record.size,
    .sha256 = record.sha256,
  };

  if (record.kind == FileKind::kAuxiliary) {
    index->auxiliary_files_.push_back(info);
  } else if (record.kind == FileKind::kTextureDescriptor) {
    index->texture_descriptors_.push_back(info);
  } else {
    index->kind_to_file_.insert_or_assign(record.kind, info);
    index->file_kinds_.push_back(record.kind);
  }
}

auto LooseCookedIndexImpl::ValidateFilePairs(const LooseCookedIndexImpl& index)
  -> void
{
  const auto has_buffers_table
    = index.kind_to_file_.contains(FileKind::kBuffersTable);
  const auto has_buffers_data
    = index.kind_to_file_.contains(FileKind::kBuffersData);
  if (has_buffers_table != has_buffers_data) {
    throw data::CookedOpenError(data::CookedDefect::kMissingPart,
      "Loose cooked index must provide both buffers.table and buffers.data");
  }

  const auto has_textures_table
    = index.kind_to_file_.contains(FileKind::kTexturesTable);
  const auto has_textures_data
    = index.kind_to_file_.contains(FileKind::kTexturesData);
  if (has_textures_table != has_textures_data) {
    throw data::CookedOpenError(data::CookedDefect::kMissingPart,
      "Loose cooked index must provide both textures.table and textures.data");
  }

  const auto has_scripts_table
    = index.kind_to_file_.contains(FileKind::kScriptsTable);
  const auto has_scripts_data
    = index.kind_to_file_.contains(FileKind::kScriptsData);
  if (has_scripts_table != has_scripts_data) {
    throw data::CookedOpenError(data::CookedDefect::kMissingPart,
      "Loose cooked index must provide both scripts.table and scripts.data");
  }

  const auto has_physics_table
    = index.kind_to_file_.contains(FileKind::kPhysicsTable);
  const auto has_physics_data
    = index.kind_to_file_.contains(FileKind::kPhysicsData);
  if (has_physics_table != has_physics_data) {
    throw data::CookedOpenError(data::CookedDefect::kMissingPart,
      "Loose cooked index must provide both physics.table and physics.data");
  }
}

auto LooseCookedIndexImpl::Guid() const noexcept -> data::SourceKey
{
  return guid_;
}

auto LooseCookedIndexImpl::FindDescriptorRelPath(
  const data::AssetKey& key) const noexcept -> std::optional<std::string_view>
{
  const auto it = key_to_asset_info_.find(key);
  if (it == key_to_asset_info_.end()) {
    return std::nullopt;
  }

  try {
    const auto table = string_table_;
    return ExtractNullTerminatedString(
      table, it->second.descriptor_relpath_offset);
  } catch (...) {
    return std::nullopt;
  }
}

auto LooseCookedIndexImpl::FindDescriptorSize(
  const data::AssetKey& key) const noexcept -> std::optional<uint64_t>
{
  const auto it = key_to_asset_info_.find(key);
  if (it == key_to_asset_info_.end()) {
    return std::nullopt;
  }
  return it->second.descriptor_size;
}

auto LooseCookedIndexImpl::FindDescriptorSha256(
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

auto LooseCookedIndexImpl::FindSourcePath(
  const data::AssetKey& key) const noexcept -> std::optional<std::string_view>
{
  const auto it = key_to_asset_info_.find(key);
  if (it == key_to_asset_info_.end()) {
    return std::nullopt;
  }

  try {
    const auto table = string_table_;
    return ExtractNullTerminatedString(table, it->second.source_path_offset);
  } catch (...) {
    return std::nullopt;
  }
}

auto LooseCookedIndexImpl::FindVirtualPath(
  const data::AssetKey& key) const noexcept -> std::optional<std::string_view>
{
  const auto it = key_to_asset_info_.find(key);
  if (it == key_to_asset_info_.end()) {
    return std::nullopt;
  }

  try {
    const auto table = string_table_;
    return ExtractNullTerminatedString(table, it->second.virtual_path_offset);
  } catch (...) {
    return std::nullopt;
  }
}

auto LooseCookedIndexImpl::FindAssetType(
  const data::AssetKey& key) const noexcept -> std::optional<uint8_t>
{
  const auto it = key_to_asset_info_.find(key);
  if (it == key_to_asset_info_.end()) {
    return std::nullopt;
  }
  return it->second.asset_type;
}

auto LooseCookedIndexImpl::GetAllFileKinds() const noexcept
  -> std::span<const FileKind>
{
  return file_kinds_;
}

auto LooseCookedIndexImpl::FindAssetKeyByVirtualPath(
  std::string_view virtual_path) const noexcept -> std::optional<data::AssetKey>
{
  try {
    const auto table = string_table_;

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

auto LooseCookedIndexImpl::FindTextureDescriptorRelPath(
  std::string_view virtual_path) const noexcept
  -> std::optional<std::string_view>
{
  try {
    const auto table = string_table_;
    for (const auto& file : texture_descriptors_) {
      if (ExtractNullTerminatedString(table, file.virtual_path_offset)
        == virtual_path) {
        return ExtractNullTerminatedString(table, file.relpath_offset);
      }
    }
    return std::nullopt;
  } catch (...) {
    return std::nullopt;
  }
}

auto LooseCookedIndexImpl::TextureDescriptorVirtualPaths() const
  -> std::vector<std::string>
{
  const auto table = string_table_;
  std::vector<std::string> paths;
  paths.reserve(texture_descriptors_.size());
  for (const auto& file : texture_descriptors_) {
    paths.emplace_back(
      ExtractNullTerminatedString(table, file.virtual_path_offset));
  }
  return paths;
}

auto LooseCookedIndexImpl::FindFileRelPath(FileKind kind) const noexcept
  -> std::optional<std::string_view>
{
  const auto it = kind_to_file_.find(kind);
  if (it == kind_to_file_.end()) {
    return std::nullopt;
  }

  try {
    const auto table = string_table_;
    return ExtractNullTerminatedString(table, it->second.relpath_offset);
  } catch (...) {
    return std::nullopt;
  }
}

auto LooseCookedIndexImpl::FindFileSize(FileKind kind) const noexcept
  -> std::optional<uint64_t>
{
  const auto it = kind_to_file_.find(kind);
  if (it == kind_to_file_.end()) {
    return std::nullopt;
  }
  return it->second.size;
}

} // namespace oxygen::content::internal
