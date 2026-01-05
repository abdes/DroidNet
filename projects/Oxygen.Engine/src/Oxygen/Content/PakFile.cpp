//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Content/Loaders/BufferLoader.h>
#include <Oxygen/Content/Loaders/TextureLoader.h>
#include <Oxygen/Content/PakFile.h>
// ReSharper disable once CppUnusedIncludeDirective
#include <Oxygen/Content/Loaders/Helpers.h>

#include <cstring>

#include <array>

using oxygen::content::PakFile;
using oxygen::data::AssetKey;
using oxygen::data::pak::AssetDirectoryEntry;
using oxygen::data::pak::PakBrowseIndexEntry;
using oxygen::data::pak::PakBrowseIndexHeader;
using oxygen::data::pak::PakFooter;
using oxygen::data::pak::PakHeader;

namespace {

auto ComputeCrc32Ieee(std::span<const std::byte> bytes, uint32_t state) noexcept
  -> uint32_t
{
  // Standard IEEE CRC32 (poly 0x04C11DB7 reflected => 0xEDB88320), reflected
  // in/out, init 0xFFFFFFFF, final XOR 0xFFFFFFFF.
  static constexpr auto kPoly = 0xEDB88320u;

  auto table = []() consteval {
    std::array<uint32_t, 256> t {};
    for (uint32_t i = 0; i < 256; ++i) {
      uint32_t c = i;
      for (int j = 0; j < 8; ++j) {
        c = (c & 1u) ? (kPoly ^ (c >> 1u)) : (c >> 1u);
      }
      t[i] = c;
    }
    return t;
  }();

  uint32_t crc = state;
  for (const auto b : bytes) {
    const auto u_b = static_cast<uint8_t>(b);
    crc = table[(crc ^ u_b) & 0xFFu] ^ (crc >> 8u);
  }
  return crc;
}

auto ComputePakCrc32(const std::filesystem::path& pak_path,
  const size_t file_size, const size_t crc_field_absolute_offset) -> uint32_t
{
  oxygen::serio::FileStream<> stream(pak_path, std::ios::in);
  oxygen::serio::Reader<oxygen::serio::FileStream<>> reader(stream);

  constexpr size_t kChunkSize = 256 * 1024;
  std::array<std::byte, kChunkSize> buffer {};

  uint32_t crc = 0xFFFFFFFFu;
  size_t offset = 0;

  while (offset < file_size) {
    const auto remaining = file_size - offset;
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
      = (std::max)(chunk_start, crc_field_absolute_offset);
    const auto crc_skip_end
      = (std::min)(chunk_end, crc_field_absolute_offset + sizeof(uint32_t));

    if (crc_skip_start < crc_skip_end) {
      const auto rel_start = crc_skip_start - chunk_start;
      const auto rel_end = crc_skip_end - chunk_start;

      if (rel_start > 0) {
        crc = ComputeCrc32Ieee(
          std::span<const std::byte>(buffer.data(), rel_start), crc);
      }
      if (rel_end < to_read) {
        crc = ComputeCrc32Ieee(std::span<const std::byte>(
                                 buffer.data() + rel_end, to_read - rel_end),
          crc);
      }
    } else {
      crc = ComputeCrc32Ieee(
        std::span<const std::byte>(buffer.data(), to_read), crc);
    }

    offset += to_read;
  }

  return crc ^ 0xFFFFFFFFu;
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

} // namespace

auto PakFile::InitBuffersTable() const -> void
{
  DCHECK_F(!buffers_table_);

  if (footer_.buffer_table.count > 0) {
    DCHECK_GT_F(footer_.buffer_table.entry_size, 0U,
      "resource table entry size must be greater than 0");
    buffers_table_.emplace(footer_.buffer_table);
  }
}

auto PakFile::InitTexturesTable() const -> void
{
  DCHECK_F(!textures_table_);

  if (footer_.texture_table.count > 0) {
    DCHECK_GT_F(footer_.texture_table.entry_size, 0U,
      "resource table entry size must be greater than 0");
    textures_table_.emplace(footer_.texture_table);
  }
}

auto PakFile::ReadHeader(serio::FileStream<>* stream) -> void
{
  LOG_SCOPE_FUNCTION(INFO);

  if (auto res = stream->Seek(0); !res) {
    LOG_F(ERROR, "Failed to seek to pak header: {}", res.error().message());
    throw std::runtime_error("Failed to seek to pak header");
  }
  Reader reader(*stream);
  auto header_result = reader.Read<PakHeader>();
  if (!header_result) {
    LOG_F(
      ERROR, "Failed to read pak header: {}", header_result.error().message());
    throw std::runtime_error("Failed to read pak header");
  }
  header_ = header_result.value();

  LOG_F(INFO, "format version  : {}", header_.version);
  LOG_F(INFO, "content version : {}", header_.content_version);
  LOG_F(INFO, "pak guid        : {}",
    oxygen::data::to_string(data::SourceKey::FromBytes(header_.guid)));

  if (std::memcmp(header_.magic, kHeaderMagic.data(), kHeaderMagic.size())
    != 0) {
    LOG_F(ERROR, "Invalid pak file header magic");
    throw std::runtime_error("Invalid pak file header magic");
  }

  if (header_.version != 2 && header_.version != 3) {
    LOG_F(ERROR, "Unsupported PAK format version: {} (expected 2 or 3)",
      header_.version);
    throw std::runtime_error("Unsupported PAK format version");
  }

  if (header_.version == 2) {
    LOG_F(
      WARNING, "Loading legacy PAK v2 container. Consider regenerating as v3.");
  }
}

auto PakFile::ReadFooter(serio::FileStream<>* stream) -> void
{
  LOG_SCOPE_FUNCTION(INFO);

  constexpr size_t kPakFooterSize = sizeof(PakFooter);
  auto size_result = stream->Size();
  if (!size_result) {
    LOG_F(
      ERROR, "Failed to get pak file size: {}", size_result.error().message());
    throw std::runtime_error("Failed to get pak file size");
  }
  size_t file_size = size_result.value();
  if (auto res = stream->Seek(file_size - kPakFooterSize); !res) {
    LOG_F(ERROR, "Failed to seek to pak footer: {}", res.error().message());
    throw std::runtime_error("Failed to seek to pak footer");
  }
  Reader reader(*stream);
  auto footer_result = reader.Read<PakFooter>();
  if (!footer_result) {
    LOG_F(
      ERROR, "Failed to read pak footer: {}", footer_result.error().message());
    throw std::runtime_error("Failed to read pak footer");
  }
  footer_ = footer_result.value();

  // Initialize resource tables if present
  InitBuffersTable();
  InitTexturesTable();

  LOG_F(INFO, "pak crc32        : {}", footer_.pak_crc32);
  LOG_F(INFO, "directory offset : {}", footer_.directory_offset);
  LOG_F(INFO, "directory size   : {}", footer_.directory_size);
  LOG_F(INFO, "asset count      : {}", footer_.asset_count);

  if (std::memcmp(
        footer_.footer_magic, kFooterMagic.data(), kFooterMagic.size())
    != 0) {
    LOG_F(ERROR, "Invalid pak file footer magic");
    throw std::runtime_error("Invalid pak file footer magic");
  }
}

auto PakFile::ValidateCrc32Integrity() const -> void
{
  // Footer declares that CRC32 validation should be skipped.
  if (footer_.pak_crc32 == 0) {
    LOG_F(INFO, "PakFile: CRC32 validation skipped (pak_crc32=0) path={}",
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

  const auto computed
    = ComputePakCrc32(file_path_, file_size, crc_field_absolute_offset);

  if (computed != footer_.pak_crc32) {
    LOG_F(ERROR,
      "PakFile: CRC32 mismatch path={} expected=0x{:08x} actual=0x{:08x}",
      file_path_.string(), footer_.pak_crc32, computed);
    throw std::runtime_error("Pak CRC32 mismatch");
  }

  LOG_F(INFO, "PakFile: CRC32 OK path={} crc32=0x{:08x}", file_path_.string(),
    computed);
}

auto PakFile::ReadBrowseIndex(
  serio::FileStream<>* stream, const size_t file_size) -> void
{
  LOG_SCOPE_FUNCTION(INFO);

  browse_index_.clear();
  browse_vpath_to_key_.clear();

  const data::pak::OffsetT browse_offset = footer_.browse_index_offset;
  const uint64_t browse_size = footer_.browse_index_size;

  if (browse_offset == 0 || browse_size == 0) {
    return;
  }

  const auto end_offset = browse_offset + browse_size;
  if (browse_offset >= file_size || end_offset > file_size
    || end_offset < browse_offset) {
    LOG_F(ERROR,
      "Browse index out of bounds: offset={} size={} file_size={} (ignoring)",
      browse_offset, browse_size, file_size);
    return;
  }

  if (auto res = stream->Seek(static_cast<size_t>(browse_offset)); !res) {
    LOG_F(ERROR, "Failed to seek to browse index offset {}: {}", browse_offset,
      res.error().message());
    return;
  }

  Reader reader(*stream);
  auto header_result = reader.Read<PakBrowseIndexHeader>();
  if (!header_result) {
    LOG_F(ERROR, "Failed to read browse index header: {}",
      header_result.error().message());
    return;
  }

  const auto header = header_result.value();
  constexpr std::array<uint8_t, 8> kBrowseMagic
    = { 'O', 'X', 'P', 'A', 'K', 'B', 'I', 'X' };
  if (std::memcmp(header.magic, kBrowseMagic.data(), kBrowseMagic.size())
    != 0) {
    LOG_F(ERROR, "Browse index magic mismatch (ignoring)");
    return;
  }

  if (header.version != 1) {
    LOG_F(
      ERROR, "Unsupported browse index version {} (ignoring)", header.version);
    return;
  }

  const uint64_t entries_size
    = static_cast<uint64_t>(header.entry_count) * sizeof(PakBrowseIndexEntry);
  const uint64_t expected_min_size
    = sizeof(PakBrowseIndexHeader) + entries_size + header.string_table_size;
  if (expected_min_size > browse_size) {
    LOG_F(ERROR,
      "Browse index payload is truncated: expected_at_least={} actual={}",
      expected_min_size, browse_size);
    return;
  }

  std::vector<PakBrowseIndexEntry> entries;
  entries.reserve(header.entry_count);
  for (uint32_t i = 0; i < header.entry_count; ++i) {
    auto entry_result = reader.Read<PakBrowseIndexEntry>();
    if (!entry_result) {
      LOG_F(ERROR, "Failed to read browse index entry {}: {}", i,
        entry_result.error().message());
      return;
    }
    entries.push_back(entry_result.value());
  }

  auto strings_blob_result
    = reader.ReadBlob(static_cast<size_t>(header.string_table_size));
  if (!strings_blob_result) {
    LOG_F(ERROR, "Failed to read browse index string table: {}",
      strings_blob_result.error().message());
    return;
  }
  const auto& strings_blob = strings_blob_result.value();

  browse_index_.reserve(header.entry_count);
  for (const auto& e : entries) {
    const uint64_t off = e.virtual_path_offset;
    const uint64_t len = e.virtual_path_length;
    const uint64_t end = off + len;
    if (off > header.string_table_size || len > header.string_table_size
      || end > header.string_table_size || end < off) {
      LOG_F(ERROR, "Browse index string reference out of bounds (ignoring)");
      return;
    }

    const auto* base = reinterpret_cast<const char*>(strings_blob.data());
    std::string vpath(base + off, base + end);
    if (vpath.empty() || vpath.front() != '/') {
      LOG_F(ERROR, "Browse index virtual path is not canonical (ignoring)");
      return;
    }

    browse_index_.push_back(BrowseEntry {
      .virtual_path = std::move(vpath),
      .asset_key = e.asset_key,
    });
  }

  browse_vpath_to_key_.reserve(browse_index_.size());
  for (const auto& entry : browse_index_) {
    const std::string_view key_view(entry.virtual_path);
    if (browse_vpath_to_key_.contains(key_view)) {
      LOG_F(ERROR, "Browse index contains duplicate virtual path '{}'",
        entry.virtual_path);
      browse_index_.clear();
      browse_vpath_to_key_.clear();
      return;
    }
    browse_vpath_to_key_.emplace(key_view, entry.asset_key);
  }
}

auto PakFile::ReadDirectoryEntry(Reader& reader) -> void
{
  LOG_SCOPE_FUNCTION(INFO);

  auto entry_result = reader.Read<AssetDirectoryEntry>();
  if (!entry_result) {
    LOG_F(ERROR, "Failed to read asset directory entry: {}",
      entry_result.error().message());
    throw std::runtime_error("Failed to read asset directory entries");
  }

  const auto& entry = *entry_result;
  directory_.emplace_back(entry);
  key_to_index_.emplace(entry.asset_key, directory_.size() - 1);
}

auto PakFile::ReadDirectory(
  serio::FileStream<>* stream, std::uint32_t asset_count) -> void
{
  LOG_SCOPE_FUNCTION(INFO);

  if (auto res = stream->Seek(footer_.directory_offset); !res) {
    LOG_F(
      ERROR, "Failed to seek to directory offset: {}", res.error().message());
    throw std::runtime_error("Failed to seek to directory offset");
  }
  Reader reader(*stream);
  directory_.clear();
  key_to_index_.clear();
  for (std::uint32_t i = 0; i < asset_count; ++i) {
    ReadDirectoryEntry(reader); // places the entry at the end of directory_
  }
}

PakFile::PakFile(const std::filesystem::path& path)
  : file_path_(path)
  , meta_stream_(OpenFileStream(path))
  , buffer_data_stream_(OpenFileStream(path))
  , texture_data_stream_(OpenFileStream(path))
{
  LOG_SCOPE_FUNCTION(INFO);
  LOG_F(INFO, "file : {}", path.string());
  ReadHeader(meta_stream_.get());
  ReadFooter(meta_stream_.get());

  const auto size_result = meta_stream_->Size();
  if (!size_result) {
    LOG_F(
      ERROR, "Failed to get pak file size: {}", size_result.error().message());
    throw std::runtime_error("Failed to get pak file size");
  }
  ReadBrowseIndex(meta_stream_.get(), size_result.value());

  ReadDirectory(meta_stream_.get(), static_cast<uint32_t>(footer_.asset_count));
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
  if (!meta_stream_) {
    throw std::runtime_error("PakFile stream is not open");
  }
  // Seek to asset descriptor offset (desc_offset in new format)
  if (const auto res = meta_stream_->Seek(entry.desc_offset); !res) {
    LOG_F(ERROR, "Failed to seek to asset desc offset {}: {}",
      entry.desc_offset, res.error().message());
    throw std::runtime_error("Failed to seek to asset desc offset");
  }
  return Reader(*meta_stream_);
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
  if (!buffer_data_stream_) {
    throw std::runtime_error("PakFile buffer data stream is not open");
  }
  // Seek to asset descriptor offset (desc_offset in new format)
  if (const auto res = buffer_data_stream_->Seek(footer_.buffer_region.offset);
    !res) {
    LOG_F(ERROR, "Failed to seek to buffer data region offset {}: {}",
      footer_.buffer_region.offset, res.error().message());
    throw std::runtime_error("Failed to seek to buffer data region offset");
  }
  return Reader(*buffer_data_stream_);
}

/*!
  Returns a Reader positioned at the start of the texture data region.

  @return Reader for texture data region.

  @see CreateBufferDataReader, CreateDataReader
*/
auto PakFile::CreateTextureDataReader() const -> Reader
{
  std::scoped_lock lock(mutex_);
  if (!texture_data_stream_) {
    throw std::runtime_error("PakFile texture data stream is not open");
  }
  // Seek to asset descriptor offset (desc_offset in new format)
  if (const auto res
    = texture_data_stream_->Seek(footer_.texture_region.offset);
    !res) {
    LOG_F(ERROR, "Failed to seek to texture data region offset {}: {}",
      footer_.texture_region.offset, res.error().message());
    throw std::runtime_error("Failed to seek to texture data region offset");
  }
  return Reader(*texture_data_stream_);
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
    throw std::runtime_error(
      "PakFile: No buffer resource table present in this file");
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
    throw std::runtime_error(
      "PakFile: No texture resource table present in this file");
  }
  return *textures_table_;
}
