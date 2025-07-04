//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Content/Loaders/BufferLoader.h>
#include <Oxygen/Content/Loaders/TextureLoader.h>
#include <Oxygen/Content/PakFile.h>

using oxygen::content::PakFile;
using oxygen::data::AssetKey;
using oxygen::data::pak::AssetDirectoryEntry;
using oxygen::data::pak::PakFooter;
using oxygen::data::pak::PakHeader;

namespace {
// Helper to open a FileStream and throw with logging on error
std::unique_ptr<oxygen::serio::FileStream<>> OpenFileStream(
  const std::filesystem::path& path)
{
  try {
    return std::make_unique<oxygen::serio::FileStream<>>(path, std::ios::in);
  } catch (const std::system_error& e) {
    LOG_F(ERROR, "Failed to open pak file '{}': {}", path.string(), e.what());
    throw;
  }
}

size_t EntryIndex(const AssetDirectoryEntry& entry,
  const std::vector<AssetDirectoryEntry>& directory)
{
  auto base = directory.data();
  size_t idx = static_cast<size_t>(std::distance(base, &entry));
  if (idx >= directory.size()) {
    throw std::invalid_argument("Entry is not part of this PakFile");
  }
  return idx;
}
} // namespace

void PakFile::InitBuffersTable() const
{
  DCHECK_F(!buffers_table_);

  if (footer_.buffer_table.count > 0) {
    DCHECK_GT_F(footer_.buffer_table.entry_size, 0U,
      "resource table entry size must be greater than 0");
    // Create a new FileStream from the original file path
    auto table_stream
      = std::make_unique<oxygen::serio::FileStream<>>(file_path_, std::ios::in);
    buffers_table_.emplace(std::move(table_stream), footer_.buffer_table,
      oxygen::content::loaders::LoadBufferResource<
        oxygen::serio::FileStream<>>);
  }
}

void PakFile::InitTexturesTable() const
{
  DCHECK_F(!textures_table_);

  if (footer_.texture_table.count > 0) {
    DCHECK_GT_F(footer_.texture_table.entry_size, 0U,
      "resource table entry size must be greater than 0");
    // Create a new FileStream from the original file path
    auto table_stream
      = std::make_unique<oxygen::serio::FileStream<>>(file_path_, std::ios::in);
    textures_table_.emplace(std::move(table_stream), footer_.texture_table,
      oxygen::content::loaders::LoadTextureResource<
        oxygen::serio::FileStream<>>);
  }
}

void PakFile::ReadHeader(oxygen::serio::FileStream<>* stream)
{
  LOG_SCOPE_FUNCTION(INFO);

  if (auto res = stream->seek(0); !res) {
    LOG_F(ERROR, "Failed to seek to pak header: {}", res.error().message());
    throw std::runtime_error("Failed to seek to pak header");
  }
  Reader reader(*stream);
  auto header_result = reader.read<PakHeader>();
  if (!header_result) {
    LOG_F(
      ERROR, "Failed to read pak header: {}", header_result.error().message());
    throw std::runtime_error("Failed to read pak header");
  }
  header_ = header_result.value();

  LOG_F(INFO, "format version  : {}", header_.version);
  LOG_F(INFO, "content version : {}", header_.content_version);

  if (std::memcmp(header_.magic, kHeaderMagic.data(), kHeaderMagic.size())
    != 0) {
    LOG_F(ERROR, "Invalid pak file header magic");
    throw std::runtime_error("Invalid pak file header magic");
  }
}

void PakFile::ReadFooter(oxygen::serio::FileStream<>* stream)
{
  LOG_SCOPE_FUNCTION(INFO);

  constexpr size_t kPakFooterSize = sizeof(PakFooter);
  auto size_result = stream->size();
  if (!size_result) {
    LOG_F(
      ERROR, "Failed to get pak file size: {}", size_result.error().message());
    throw std::runtime_error("Failed to get pak file size");
  }
  size_t file_size = size_result.value();
  if (auto res = stream->seek(file_size - kPakFooterSize); !res) {
    LOG_F(ERROR, "Failed to seek to pak footer: {}", res.error().message());
    throw std::runtime_error("Failed to seek to pak footer");
  }
  Reader reader(*stream);
  auto footer_result = reader.read<PakFooter>();
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

void PakFile::ReadDirectoryEntry(Reader& reader)
{
  LOG_SCOPE_FUNCTION(INFO);

  auto entry_result = reader.read<AssetDirectoryEntry>();
  if (!entry_result) {
    LOG_F(ERROR, "Failed to read asset directory entry: {}",
      entry_result.error().message());
    throw std::runtime_error("Failed to read asset directory entries");
  }

  const auto& entry = *entry_result;
  directory_.emplace_back(entry);
  key_to_index_.emplace(entry.asset_key, directory_.size() - 1);
}

void PakFile::ReadDirectory(
  oxygen::serio::FileStream<>* stream, std::uint32_t asset_count)
{
  LOG_SCOPE_FUNCTION(INFO);

  if (auto res = stream->seek(footer_.directory_offset); !res) {
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
  , stream_(OpenFileStream(path))
{
  LOG_SCOPE_FUNCTION(INFO);
  LOG_F(INFO, "file : {}", path.string());
  ReadHeader(stream_.get());
  ReadFooter(stream_.get());
  ReadDirectory(stream_.get(), static_cast<uint32_t>(footer_.asset_count));
}

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

auto PakFile::Directory() const noexcept -> std::span<const AssetDirectoryEntry>
{
  std::scoped_lock lock(mutex_);
  return std::span<const AssetDirectoryEntry>(
    directory_.data(), directory_.size());
}

auto PakFile::FormatVersion() const noexcept -> uint16_t
{
  return header_.version;
}

auto PakFile::ContentVersion() const noexcept -> uint16_t
{
  return header_.content_version;
}

auto PakFile::CreateReader(const AssetDirectoryEntry& entry) const -> Reader
{
  std::scoped_lock lock(mutex_);
  if (!stream_) {
    throw std::runtime_error("PakFile stream is not open");
  }
  // Seek to asset descriptor offset (desc_offset in new format)
  if (auto res = stream_->seek(entry.desc_offset); !res) {
    LOG_F(ERROR, "Failed to seek to asset desc offset {}: {}",
      entry.desc_offset, res.error().message());
    throw std::runtime_error("Failed to seek to asset desc offset");
  }
  return Reader(*stream_);
}

auto PakFile::BuffersTable() const -> BuffersTableT&
{
  if (!buffers_table_) {
    throw std::runtime_error(
      "PakFile: No buffer resource table present in this file");
  }
  return *buffers_table_;
}

auto PakFile::TexturesTable() const -> TexturesTableT&
{
  if (!textures_table_) {
    throw std::runtime_error(
      "PakFile: No texture resource table present in this file");
  }
  return *textures_table_;
}
