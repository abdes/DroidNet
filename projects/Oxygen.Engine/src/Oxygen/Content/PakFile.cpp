//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <string>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Content/Loaders/BufferLoader.h>
#include <Oxygen/Content/Loaders/Helpers.h>
#include <Oxygen/Content/Loaders/TextureLoader.h>
#include <Oxygen/Content/PakFile.h>
#include <Oxygen/Data/PakFormat.h>

using oxygen::content::PakFile;
using oxygen::data::AssetKey;
using oxygen::data::pak::core::AssetDirectoryEntry;
using oxygen::data::pak::core::PakBrowseIndexEntry;
using oxygen::data::pak::core::PakBrowseIndexHeader;
using oxygen::data::pak::core::PakFooter;
using oxygen::data::pak::core::PakHeader;

namespace {

auto ComputeCrc32Ieee(std::span<const std::byte> bytes, uint32_t state) noexcept
  -> uint32_t
{
  // Standard IEEE CRC32 (poly 0x04C11DB7 reflected => 0xEDB88320), reflected
  // in/out, init 0xFFFFFFFF, final XOR 0xFFFFFFFF.
  static constexpr auto kPoly = 0xEDB88320U;

  // NOLINTBEGIN(*-magic-numbers)
  auto table = []() consteval {
    std::array<uint32_t, 256> t {};
    for (uint32_t i = 0; i < 256; ++i) {
      uint32_t c = i;
      for (int j = 0; j < 8; ++j) {
        c = (c & 1U) ? (kPoly ^ (c >> 1U)) : (c >> 1U);
      }
      t.at(static_cast<size_t>(i)) = c;
    }
    return t;
  }();

  uint32_t crc = state;
  for (const auto b : bytes) {
    const auto u_b = static_cast<uint8_t>(b);
    const auto table_index = static_cast<size_t>((crc ^ u_b) & 0xFFU);
    crc = table.at(table_index) ^ (crc >> 8U);
  }
  // NOLINTEND(*-magic-numbers)
  return crc;
}

struct Crc32ComputationConfig final {
  size_t file_size { 0 };
  size_t crc_field_absolute_offset { 0 };
};

auto ComputePakCrc32(const std::filesystem::path& pak_path,
  const Crc32ComputationConfig& cfg) -> uint32_t
{
  oxygen::serio::FileStream<> stream(pak_path, std::ios::in);
  oxygen::serio::Reader<oxygen::serio::FileStream<>> reader(stream);

  constexpr size_t kChunkSize
    = static_cast<size_t>(256) * static_cast<size_t>(1024);
  std::array<std::byte, kChunkSize> buffer {};

  uint32_t crc = 0xFFFFFFFF; // NOLINT(*-magic-numbers)
  size_t offset = 0;

  while (offset < cfg.file_size) {
    const auto remaining = cfg.file_size - offset;
    const auto to_read = (std::min)(remaining, buffer.size());

    auto blob_result
      = reader.ReadBlobInto(std::span<std::byte>(buffer.data(), to_read));
    if (!blob_result) {
      throw std::runtime_error(
        "Failed to read pak for CRC32: " + blob_result.error().message());
    }

    // The PakGen tool computes the CRC32 over the entire file while *skipping*
    // the 4-byte pak_crc32 field itself (i.e., those bytes are excluded from
    // the CRC stream). Note that skipping is not equivalent to hashing four
    // zero bytes.
    const auto chunk_start = offset;
    const auto chunk_end = offset + to_read;
    const auto crc_skip_start
      = (std::max)(chunk_start, cfg.crc_field_absolute_offset);
    const auto crc_skip_end
      = (std::min)(chunk_end, cfg.crc_field_absolute_offset + sizeof(uint32_t));

    if (crc_skip_start < crc_skip_end) {
      const auto rel_start = crc_skip_start - chunk_start;
      const auto rel_end = crc_skip_end - chunk_start;

      if (rel_start > 0) {
        crc = ComputeCrc32Ieee(
          std::span<const std::byte>(buffer.data(), rel_start), crc);
      }
      if (rel_end < to_read) {
        crc = ComputeCrc32Ieee(std::span<const std::byte>(buffer).subspan(
                                 rel_end, to_read - rel_end),
          crc);
      }
    } else {
      crc = ComputeCrc32Ieee(
        std::span<const std::byte>(buffer.data(), to_read), crc);
    }

    offset += to_read;
  }

  return crc ^ 0xFFFFFFFFU; // NOLINT(*-magic-numbers)
}
// Helper to open a FileStream and throw with logging on error
auto OpenFileStream(const std::filesystem::path& path)
  -> std::unique_ptr<oxygen::serio::FileStream<>>
{
  try {
    return std::make_unique<oxygen::serio::FileStream<>>(path, std::ios::in);
  } catch (const std::system_error& e) {
    LOG_F(ERROR, "Failed to open pak file '{}': {}", path.string(), e.what());
    throw;
  }
}

struct ParsedPakMetadata final {
  PakHeader header {};
  PakFooter footer {};
  std::vector<AssetDirectoryEntry> directory {}; // NOLINT
  std::unordered_map<AssetKey, size_t> key_to_index {}; // NOLINT
};

struct ParsedBrowseIndex final {
  std::vector<PakFile::BrowseEntry> entries {}; // NOLINT
  std::unordered_map<std::string_view, AssetKey> vpath_to_key {}; // NOLINT
};

auto ReadPakHeader(oxygen::serio::FileStream<>& stream) -> PakHeader
{
  LOG_SCOPE_FUNCTION(INFO);

  if (auto res = stream.Seek(0); !res) {
    LOG_F(ERROR, "Failed to seek to pak header: {}", res.error().message());
    throw std::runtime_error("Failed to seek to pak header");
  }

  PakFile::Reader reader(stream);
  auto header_result = reader.Read<PakHeader>();
  if (!header_result) {
    LOG_F(
      ERROR, "Failed to read pak header: {}", header_result.error().message());
    throw std::runtime_error("Failed to read pak header");
  }

  const auto header = header_result.value();
  const auto source_key = oxygen::data::SourceKey::FromBytes(header.guid);
  LOG_F(INFO, "format version  : {}", header.version);
  LOG_F(INFO, "content version : {}", header.content_version);
  LOG_F(INFO, "pak guid        : {}", source_key);

  if (!std::ranges::equal(
        std::span { header.magic }, oxygen::data::pak::core::kPakHeaderMagic)) {
    LOG_F(ERROR, "Invalid pak file header magic");
    throw std::runtime_error("Invalid pak file header magic");
  }

  if (header.version != oxygen::data::pak::core::kCurrentPakFormatVersion) {
    const auto msg = std::string("Unsupported PAK format version: ")
      + std::to_string(header.version) + " (this build loads v"
      + std::to_string(oxygen::data::pak::core::kCurrentPakFormatVersion)
      + " only).";
    LOG_F(ERROR, "{}", msg);
    throw std::runtime_error(msg);
  }

  return header;
}

auto ReadPakFooter(oxygen::serio::FileStream<>& stream) -> PakFooter
{
  LOG_SCOPE_FUNCTION(INFO);

  constexpr size_t kPakFooterSize = sizeof(PakFooter);
  auto size_result = stream.Size();
  if (!size_result) {
    LOG_F(
      ERROR, "Failed to get pak file size: {}", size_result.error().message());
    throw std::runtime_error("Failed to get pak file size");
  }

  const size_t file_size = size_result.value();
  if (auto res = stream.Seek(file_size - kPakFooterSize); !res) {
    LOG_F(ERROR, "Failed to seek to pak footer: {}", res.error().message());
    throw std::runtime_error("Failed to seek to pak footer");
  }

  PakFile::Reader reader(stream);
  auto footer_result = reader.Read<PakFooter>();
  if (!footer_result) {
    LOG_F(
      ERROR, "Failed to read pak footer: {}", footer_result.error().message());
    throw std::runtime_error("Failed to read pak footer");
  }

  const auto footer = footer_result.value();
  LOG_F(INFO, "pak crc32        : {}", footer.pak_crc32);
  LOG_F(INFO, "directory offset : {}", footer.directory_offset);
  LOG_F(INFO, "directory size   : {}", footer.directory_size);
  LOG_F(INFO, "asset count      : {}", footer.asset_count);

  const auto footer_magic = std::as_bytes(std::span { footer.footer_magic });
  const auto expected_footer_magic
    = std::as_bytes(std::span { oxygen::data::pak::core::kPakFooterMagic });
  if (!std::ranges::equal(footer_magic, expected_footer_magic)) {
    LOG_F(ERROR, "Invalid pak file footer magic");
    throw std::runtime_error("Invalid pak file footer magic");
  }

  return footer;
}

auto ReadPakDirectory(oxygen::serio::FileStream<>& stream,
  const PakFooter& footer, std::vector<AssetDirectoryEntry>& directory,
  std::unordered_map<AssetKey, size_t>& key_to_index) -> void
{
  LOG_SCOPE_FUNCTION(INFO);

  if (auto res = stream.Seek(footer.directory_offset); !res) {
    LOG_F(
      ERROR, "Failed to seek to directory offset: {}", res.error().message());
    throw std::runtime_error("Failed to seek to directory offset");
  }

  PakFile::Reader reader(stream);
  directory.clear();
  key_to_index.clear();
  directory.reserve(footer.asset_count);

  for (uint32_t i = 0; i < footer.asset_count; ++i) {
    auto entry_result = reader.Read<AssetDirectoryEntry>();
    if (!entry_result) {
      LOG_F(ERROR, "Failed to read asset directory entry: {}",
        entry_result.error().message());
      throw std::runtime_error("Failed to read asset directory entries");
    }
    directory.push_back(*entry_result);
    key_to_index.emplace(directory.back().asset_key, directory.size() - 1);
  }
}

auto LoadPakMetadata(oxygen::serio::FileStream<>& stream) -> ParsedPakMetadata
{
  ParsedPakMetadata metadata {};
  metadata.header = ReadPakHeader(stream);
  metadata.footer = ReadPakFooter(stream);
  ReadPakDirectory(
    stream, metadata.footer, metadata.directory, metadata.key_to_index);
  return metadata;
}

auto LoadBrowseIndex(oxygen::serio::FileStream<>& stream,
  const size_t file_size, const PakFooter& footer) -> ParsedBrowseIndex
{
  LOG_SCOPE_FUNCTION(INFO);

  ParsedBrowseIndex parsed {};
  const oxygen::data::pak::core::OffsetT browse_offset
    = footer.browse_index_offset;
  const uint64_t browse_size = footer.browse_index_size;

  if (browse_offset == 0 || browse_size == 0) {
    return parsed;
  }

  const auto end_offset = browse_offset + browse_size;
  if (browse_offset >= file_size || end_offset > file_size
    || end_offset < browse_offset) {
    LOG_F(ERROR,
      "Browse index out of bounds: offset={} size={} file_size={} (ignoring)",
      browse_offset, browse_size, file_size);
    return parsed;
  }

  if (auto res = stream.Seek(static_cast<size_t>(browse_offset)); !res) {
    LOG_F(ERROR, "Failed to seek to browse index offset {}: {}", browse_offset,
      res.error().message());
    return parsed;
  }

  PakFile::Reader reader(stream);
  auto header_result = reader.Read<PakBrowseIndexHeader>();
  if (!header_result) {
    LOG_F(ERROR, "Failed to read browse index header: {}",
      header_result.error().message());
    return parsed;
  }

  const auto header = header_result.value();
  constexpr std::array<uint8_t, 8> kBrowseMagic
    = { 'O', 'X', 'P', 'A', 'K', 'B', 'I', 'X' };
  if (!std::ranges::equal(std::span { header.magic }, kBrowseMagic)) {
    LOG_F(ERROR, "Browse index magic mismatch (ignoring)");
    return parsed;
  }

  if (header.version != 1) {
    LOG_F(
      ERROR, "Unsupported browse index version {} (ignoring)", header.version);
    return parsed;
  }

  const uint64_t entries_size
    = static_cast<uint64_t>(header.entry_count) * sizeof(PakBrowseIndexEntry);
  const uint64_t expected_min_size
    = sizeof(PakBrowseIndexHeader) + entries_size + header.string_table_size;
  if (expected_min_size > browse_size) {
    LOG_F(ERROR,
      "Browse index payload is truncated: expected_at_least={} actual={}",
      expected_min_size, browse_size);
    return parsed;
  }

  std::vector<PakBrowseIndexEntry> entries;
  entries.reserve(header.entry_count);
  for (uint32_t i = 0; i < header.entry_count; ++i) {
    auto entry_result = reader.Read<PakBrowseIndexEntry>();
    if (!entry_result) {
      LOG_F(ERROR, "Failed to read browse index entry {}: {}", i,
        entry_result.error().message());
      return ParsedBrowseIndex {};
    }
    entries.push_back(entry_result.value());
  }

  auto strings_blob_result
    = reader.ReadBlob(static_cast<size_t>(header.string_table_size));
  if (!strings_blob_result) {
    LOG_F(ERROR, "Failed to read browse index string table: {}",
      strings_blob_result.error().message());
    return ParsedBrowseIndex {};
  }
  const auto& strings_blob = strings_blob_result.value();

  parsed.entries.reserve(header.entry_count);
  for (const auto& e : entries) {
    const uint64_t off = e.virtual_path_offset;
    const uint64_t len = e.virtual_path_length;
    const uint64_t end = off + len;
    if (off > header.string_table_size || len > header.string_table_size
      || end > header.string_table_size || end < off) {
      LOG_F(ERROR, "Browse index string reference out of bounds (ignoring)");
      return ParsedBrowseIndex {};
    }

    const auto path_bytes
      = std::span<const std::byte>(strings_blob)
          .subspan(static_cast<size_t>(off), static_cast<size_t>(len));
    std::string vpath;
    vpath.resize(static_cast<size_t>(len));
    std::ranges::transform(path_bytes, vpath.begin(),
      [](const std::byte byte) { return std::to_integer<char>(byte); });
    if (vpath.empty() || vpath.front() != '/') {
      LOG_F(ERROR, "Browse index virtual path is not canonical (ignoring)");
      return ParsedBrowseIndex {};
    }

    parsed.entries.push_back(PakFile::BrowseEntry {
      .virtual_path = std::move(vpath),
      .asset_key = e.asset_key,
    });
  }

  parsed.vpath_to_key.reserve(parsed.entries.size());
  for (const auto& entry : parsed.entries) {
    const std::string_view key_view(entry.virtual_path);
    if (parsed.vpath_to_key.contains(key_view)) {
      LOG_F(ERROR, "Browse index contains duplicate virtual path '{}'",
        entry.virtual_path);
      return ParsedBrowseIndex {};
    }
    parsed.vpath_to_key.emplace(key_view, entry.asset_key);
  }

  return parsed;
}

} // namespace

auto PakFile::InitializeResourceTablesFromFooter() const -> void
{
  DCHECK_F(!buffers_table_);
  DCHECK_F(!textures_table_);
  DCHECK_F(!scripts_table_);
  DCHECK_F(!physics_table_);

  if (footer_.buffer_table.count > 0) {
    DCHECK_GT_F(footer_.buffer_table.entry_size, 0U,
      "resource table entry size must be greater than 0");
    buffers_table_.emplace(footer_.buffer_table);
  }
  if (footer_.texture_table.count > 0) {
    DCHECK_GT_F(footer_.texture_table.entry_size, 0U,
      "resource table entry size must be greater than 0");
    textures_table_.emplace(footer_.texture_table);
  }
  if (footer_.script_resource_table.count > 0) {
    DCHECK_GT_F(footer_.script_resource_table.entry_size, 0U,
      "resource table entry size must be greater than 0");
    scripts_table_.emplace(footer_.script_resource_table);
  }
  if (footer_.physics_resource_table.count > 0) {
    DCHECK_GT_F(footer_.physics_resource_table.entry_size, 0U,
      "resource table entry size must be greater than 0");
    physics_table_.emplace(footer_.physics_resource_table);
  }
}

PakFile::PakFile(const std::filesystem::path& path)
  : file_path_(path)
  , meta_stream_(OpenFileStream(path))
  , buffer_data_stream_(OpenFileStream(path))
  , texture_data_stream_(OpenFileStream(path))
  , script_data_stream_(OpenFileStream(path))
  , physics_data_stream_(OpenFileStream(path))
{
  LOG_SCOPE_FUNCTION(INFO);
  LOG_F(INFO, "file : {}", path.string());

  const auto metadata = LoadPakMetadata(*meta_stream_);
  header_ = metadata.header;
  footer_ = metadata.footer;
  directory_ = metadata.directory;
  key_to_index_ = metadata.key_to_index;
  InitializeResourceTablesFromFooter();

  const auto size_result = meta_stream_->Size();
  if (!size_result) {
    LOG_F(
      ERROR, "Failed to get pak file size: {}", size_result.error().message());
    throw std::runtime_error("Failed to get pak file size");
  }

  auto parsed_browse
    = LoadBrowseIndex(*meta_stream_, size_result.value(), footer_);
  browse_index_ = std::move(parsed_browse.entries);
  browse_vpath_to_key_ = std::move(parsed_browse.vpath_to_key);
}

auto PakFile::ValidateCrc32Integrity() const -> void
{
  // Footer declares that CRC32 validation should be skipped.
  if (footer_.pak_crc32 == 0) {
    LOG_F(INFO, "CRC32 validation skipped (pak_crc32=0) path={}",
      file_path_.string());
    return;
  }

  auto size_result = meta_stream_->Size();
  if (!size_result) {
    throw std::runtime_error("Failed to get pak file size for CRC32: "
      + size_result.error().message());
  }

  const auto file_size = size_result.value();
  if (file_size < sizeof(PakFooter)) {
    throw std::runtime_error("Pak file too small for CRC32 validation");
  }

  const size_t crc_field_absolute_offset
    = (file_size - sizeof(PakFooter)) + offsetof(PakFooter, pak_crc32);

  const auto computed = ComputePakCrc32(file_path_,
    Crc32ComputationConfig {
      .file_size = file_size,
      .crc_field_absolute_offset = crc_field_absolute_offset,
    });

  if (computed != footer_.pak_crc32) {
    LOG_F(ERROR, "CRC32 mismatch path={} expected=0x{:08x} actual=0x{:08x}",
      file_path_.string(), footer_.pak_crc32, computed);
    throw std::runtime_error("Pak CRC32 mismatch");
  }

  LOG_F(INFO, "CRC32 OK path={} crc32=0x{:08x}", file_path_.string(), computed);
}

auto PakFile::HasBrowseIndex() const noexcept -> bool
{
  return !browse_index_.empty();
}

auto PakFile::BrowseIndex() const noexcept -> std::span<const BrowseEntry>
{
  return { browse_index_.data(), browse_index_.size() };
}

auto PakFile::ResolveAssetKeyByVirtualPath(
  const std::string_view virtual_path) const noexcept
  -> std::optional<data::AssetKey>
{
  const auto it = browse_vpath_to_key_.find(virtual_path);
  if (it == browse_vpath_to_key_.end()) {
    return std::nullopt;
  }
  return it->second;
}

auto PakFile::CreateReaderAtOffset(serio::FileStream<>* stream,
  const uint64_t offset, const std::string_view operation) const -> Reader
{
  if (stream == nullptr) {
    throw std::runtime_error("PakFile stream is not open");
  }
  if (const auto res = stream->Seek(static_cast<size_t>(offset)); !res) {
    LOG_F(ERROR, "Failed to seek to {} offset {}: {}", operation, offset,
      res.error().message());
    throw std::runtime_error(
      std::string("Failed to seek to ") + std::string(operation));
  }
  return Reader(*stream);
}

/*!
  Looks up an asset directory entry by its key.
  @param key Asset key to search for.
  @return Optional directory entry if found, `std::nullopt` otherwise.
*/
auto PakFile::FindEntry(const AssetKey& key) const noexcept
  -> std::optional<AssetDirectoryEntry>
{
  std::scoped_lock lock(mutex_);
  auto it = key_to_index_.find(key);
  if (it != key_to_index_.end()) {
    return directory_[it->second];
  }
  return std::nullopt;
}

/*!
  Returns a span over all asset directory entries in the PAK file.
  @return Span of asset directory entries.
  @note Entries are ordered as in the PAK file.
  @see FindEntry, AssetDirectoryEntry
*/
auto PakFile::Directory() const noexcept -> std::span<const AssetDirectoryEntry>
{
  std::scoped_lock lock(mutex_);
  return { directory_.data(), directory_.size() };
}

/*!
  Creates a Reader object positioned at the start of the asset's data region.

  @param entry Asset directory entry to read from.
  @return Reader for the asset's data region.

  @note Reader is valid only for the lifetime of the PakFile.
  @see Reader, AssetDirectoryEntry
*/
auto PakFile::CreateReader(const AssetDirectoryEntry& entry) const -> Reader
{
  std::scoped_lock lock(mutex_);
  return CreateReaderAtOffset(
    meta_stream_.get(), entry.desc_offset, "asset descriptor");
}

/*!
  Returns the format version number from the PAK file header.
  @return Format version number.

  @note Used for compatibility checks.
  @see ContentVersion
*/
auto PakFile::FormatVersion() const noexcept -> uint16_t
{
  return header_.version;
}

/*!
  Returns the content version number from the PAK file header.
  @return Content version number.

  @note Indicates asset content revision.
  @see FormatVersion
*/
auto PakFile::ContentVersion() const noexcept -> uint16_t
{
  return header_.content_version;
}

auto PakFile::Guid() const noexcept -> data::SourceKey
{
  return data::SourceKey::FromBytes(header_.guid);
}

/*!
  Returns a Reader positioned at the start of the buffer data region.

  @return Reader for buffer data region.

  @see CreateTextureDataReader, CreateDataReader
*/
auto PakFile::CreateBufferDataReader() const -> Reader
{
  std::scoped_lock lock(mutex_);
  return CreateReaderAtOffset(buffer_data_stream_.get(),
    footer_.buffer_region.offset, "buffer data region");
}

/*!
  Returns a Reader positioned at the start of the texture data region.

  @return Reader for texture data region.

  @see CreateBufferDataReader, CreateDataReader
*/
auto PakFile::CreateTextureDataReader() const -> Reader
{
  std::scoped_lock lock(mutex_);
  return CreateReaderAtOffset(texture_data_stream_.get(),
    footer_.texture_region.offset, "texture data region");
}

auto PakFile::CreateScriptDataReader() const -> Reader
{
  std::scoped_lock lock(mutex_);
  return CreateReaderAtOffset(script_data_stream_.get(),
    footer_.script_region.offset, "script data region");
}

auto PakFile::CreatePhysicsDataReader() const -> Reader
{
  std::scoped_lock lock(mutex_);
  return CreateReaderAtOffset(physics_data_stream_.get(),
    footer_.physics_region.offset, "physics data region");
}

/*!
  Returns a reference to the ResourceTable for buffer resources.

  @return Reference to ResourceTable for buffers.
  @throw std::runtime_error if table is not present.

  @see ResourceTable, HasTableOf
*/
auto PakFile::BuffersTable() const -> BuffersTableT&
{
  if (!buffers_table_) {
    throw std::runtime_error("No buffer resource table present in this file");
  }
  return *buffers_table_;
}

/*!
  Returns a reference to the ResourceTable for texture resources.

  @return Reference to ResourceTable for textures.
  @throw std::runtime_error if table is not present.

  @see ResourceTable, HasTableOf
*/
auto PakFile::TexturesTable() const -> TexturesTableT&
{
  if (!textures_table_) {
    throw std::runtime_error("No texture resource table present in this file");
  }
  return *textures_table_;
}

auto PakFile::ScriptsTable() const -> ScriptsTableT&
{
  if (!scripts_table_) {
    throw std::runtime_error("No script resource table present in this file");
  }
  return *scripts_table_;
}

auto PakFile::PhysicsTable() const -> PhysicsTableT&
{
  if (!physics_table_) {
    throw std::runtime_error("No physics resource table present in this file");
  }
  return *physics_table_;
}

auto PakFile::ReadScriptSlotRecord(const uint32_t index) const
  -> data::pak::scripting::ScriptSlotRecord
{
  std::scoped_lock lock(mutex_);

  if (index >= footer_.script_slot_table.count) {
    throw std::out_of_range("Script slot index out of bounds");
  }
  if (footer_.script_slot_table.entry_size
    != sizeof(data::pak::scripting::ScriptSlotRecord)) {
    throw std::runtime_error("Script slot table entry size mismatch");
  }

  const auto byte_offset = footer_.script_slot_table.offset
    + (static_cast<uint64_t>(index) * footer_.script_slot_table.entry_size);
  if (const auto seek_res
    = meta_stream_->Seek(static_cast<size_t>(byte_offset));
    !seek_res) {
    throw std::runtime_error("Failed to seek script slot table");
  }

  Reader reader(*meta_stream_);
  auto blob_res
    = reader.ReadBlob(sizeof(data::pak::scripting::ScriptSlotRecord));
  if (!blob_res) {
    throw std::runtime_error("Failed to read script slot record");
  }

  data::pak::scripting::ScriptSlotRecord record {};
  std::memcpy(&record, blob_res->data(), sizeof(record));
  return record;
}

auto PakFile::ReadScriptSlotRecords(
  const uint32_t start_index, const uint32_t count) const
  -> std::vector<data::pak::scripting::ScriptSlotRecord>
{
  std::vector<data::pak::scripting::ScriptSlotRecord> result;
  result.reserve(count);

  if (count == 0) {
    return result;
  }
  if (start_index > footer_.script_slot_table.count
    || count > footer_.script_slot_table.count - start_index) {
    throw std::out_of_range("Script slot range out of bounds");
  }

  for (uint32_t i = 0; i < count; ++i) {
    result.push_back(ReadScriptSlotRecord(start_index + i));
  }
  return result;
}

auto PakFile::ReadScriptParamRecords(const ScriptParamReadRequest request) const
  -> std::vector<data::pak::scripting::ScriptParamRecord>
{
  std::vector<data::pak::scripting::ScriptParamRecord> result;
  if (request.count == 0) {
    return result;
  }

  std::scoped_lock lock(mutex_);
  constexpr auto kRecordSize
    = static_cast<uint64_t>(sizeof(data::pak::scripting::ScriptParamRecord));
  const auto total_bytes = static_cast<uint64_t>(request.count) * kRecordSize;

  const auto stream_size_result = meta_stream_->Size();
  if (!stream_size_result) {
    throw std::runtime_error("Failed to query script parameter stream size");
  }

  const auto stream_size_u64
    = static_cast<uint64_t>(stream_size_result.value());
  if (request.absolute_offset > stream_size_u64
    || total_bytes > stream_size_u64 - request.absolute_offset) {
    throw std::runtime_error("Script parameter array range out of bounds");
  }

  if (total_bytes
    > static_cast<uint64_t>((std::numeric_limits<size_t>::max)())) {
    throw std::runtime_error("Script parameter array exceeds addressable size");
  }

  result.reserve(request.count);

  if (const auto seek_res
    = meta_stream_->Seek(static_cast<size_t>(request.absolute_offset));
    !seek_res) {
    throw std::runtime_error("Failed to seek script parameter array");
  }

  Reader reader(*meta_stream_);
  auto blob_res = reader.ReadBlob(static_cast<size_t>(total_bytes));
  if (!blob_res) {
    throw std::runtime_error("Failed to read script parameter array");
  }
  if (blob_res->size() != static_cast<size_t>(total_bytes)) {
    throw std::runtime_error("Script parameter array size mismatch");
  }

  result.resize(request.count);
  std::memcpy(
    result.data(), blob_res->data(), static_cast<size_t>(total_bytes));
  return result;
}

auto PakFile::ReadScriptResource(
  const data::pak::core::ResourceIndexT index) const
  -> std::shared_ptr<const data::ScriptResource>
{
  if (index == data::pak::core::kNoResourceIndex) {
    return nullptr;
  }

  std::scoped_lock lock(mutex_);
  if (!scripts_table_) {
    throw std::runtime_error("No script resource table present in this file");
  }
  if (!scripts_table_->IsValidKey(index)) {
    throw std::out_of_range("Script resource index out of bounds");
  }

  const auto desc_offset = scripts_table_->GetResourceOffset(index);
  if (!desc_offset) {
    throw std::runtime_error("Failed to resolve script resource descriptor");
  }

  if (const auto seek_res
    = meta_stream_->Seek(static_cast<size_t>(*desc_offset));
    !seek_res) {
    throw std::runtime_error("Failed to seek script resource descriptor");
  }

  Reader meta_reader(*meta_stream_);
  auto desc_blob
    = meta_reader.ReadBlob(sizeof(data::pak::scripting::ScriptResourceDesc));
  if (!desc_blob) {
    throw std::runtime_error("Failed to read script resource descriptor");
  }

  data::pak::scripting::ScriptResourceDesc desc {};
  std::memcpy(&desc, desc_blob->data(), sizeof(desc));

  std::vector<uint8_t> payload;
  if (desc.size_bytes > 0) {
    if (!script_data_stream_) {
      throw std::runtime_error("PakFile script data stream is not open");
    }
    if (const auto seek_res
      = script_data_stream_->Seek(static_cast<size_t>(desc.data_offset));
      !seek_res) {
      throw std::runtime_error("Failed to seek script resource payload");
    }

    Reader script_reader(*script_data_stream_);
    auto payload_blob = script_reader.ReadBlob(desc.size_bytes);
    if (!payload_blob) {
      throw std::runtime_error("Failed to read script resource payload");
    }
    payload.resize(payload_blob->size());
    std::memcpy(payload.data(), payload_blob->data(), payload.size());
  }

  return std::make_shared<const data::ScriptResource>(desc, std::move(payload));
}
