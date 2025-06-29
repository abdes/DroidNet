//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Content/PakFile.h>

using namespace oxygen::content;

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

void SkipDependencies(
  oxygen::serio::Stream auto* stream, uint16_t dependency_count)
{
  if (dependency_count > 0) {
    size_t deps_size = static_cast<size_t>(dependency_count) * sizeof(AssetKey);
    if (auto res = stream->forward(deps_size); !res) {
      throw std::runtime_error("Failed to seek past dependencies");
    }
  }
}
} // namespace

namespace oxygen::content {

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

  LOG_F(INFO, "pak hash         : {}", footer_.pak_hash);
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
    LOG_F(ERROR, "Failed to read asset directory entry {}: {}",
      entry_result.error().message());
    throw std::runtime_error("Failed to read asset directory entries");
  }

  const auto& entry = *entry_result;
  LOG_F(INFO, "entry offset    : {}", entry.entry_offset);
  LOG_F(INFO, "data offset     : {}", entry.data_offset);
  LOG_F(INFO, "data size       : {}", entry.data_size);
  LOG_F(INFO, "alignment       : {}", entry.alignment);
  LOG_F(INFO, "dependencies    : {}", entry.dependency_count);
  LOG_F(INFO, "compression     : {}", static_cast<int>(entry.compression));

  directory_.emplace_back(std::move(*entry_result));
  key_to_index_.emplace(directory_.back().key, directory_.size() - 1);
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
    const auto& entry = directory_.back();
    SkipDependencies(stream, entry.dependency_count);
  }
}

PakFile::PakFile(const std::filesystem::path& path)
  : stream_(OpenFileStream(path))
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

auto PakFile::CreateReader(const AssetDirectoryEntry& entry) const -> Reader
{
  std::scoped_lock lock(mutex_);
  if (!stream_) {
    throw std::runtime_error("PakFile stream is not open");
  }
  // Seek to asset offset
  if (auto res = stream_->seek(entry.data_offset); !res) {
    LOG_F(ERROR, "Failed to seek to asset offset {}: {}", entry.data_offset,
      res.error().message());
    throw std::runtime_error("Failed to seek to asset offset");
  }
  return Reader(*stream_);
}

auto PakFile::Dependencies(const AssetDirectoryEntry& entry) const
  -> std::vector<AssetKey>
{
  std::scoped_lock lock(mutex_);

  if (!stream_) {
    throw std::runtime_error("PakFile stream is not open");
  }

  // Ensure entry is part of this PakFile
  const auto idx = key_to_index_.at(entry.key);
  size_t deps_offset = entry.entry_offset + sizeof(AssetDirectoryEntry);
  if (auto res = stream_->seek(deps_offset); !res) {
    LOG_F(ERROR, "Failed to seek to dependencies for entry {}: {}", idx,
      res.error().message());
    throw std::runtime_error("Failed to seek to dependencies");
  }
  Reader reader(*stream_);
  std::vector<AssetKey> deps;
  if (entry.dependency_count > 0) {
    deps.reserve(entry.dependency_count);
    for (uint16_t i = 0; i < entry.dependency_count; ++i) {
      auto key_result = reader.read<AssetKey>();
      if (!key_result) {
        LOG_F(ERROR, "Failed to read dependency {} for entry {}: {}", i, idx,
          key_result.error().message());
        throw std::runtime_error("Failed to read dependencies");
      }
      deps.push_back(std::move(*key_result));
    }
  }
  return deps;
}

} // namespace oxygen::content
