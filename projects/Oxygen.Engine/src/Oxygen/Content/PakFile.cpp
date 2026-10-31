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
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Content/Internal/BlockingReads.h>
#include <Oxygen/Content/PakFile.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/CookedOpenError.h>
#include <Oxygen/Data/PakCatalog.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/PakFormatSerioLoaders.h> // IWYU pragma: keep
#include <Oxygen/Data/SourceKey.h>
#include <Oxygen/Serio/AnyDevice.h>
#include <Oxygen/Serio/ByteReader.h>
#include <Oxygen/Serio/Integrity.h>
#include <Oxygen/Serio/IoDriver.h>
#include <Oxygen/Serio/MappedFile.h>
#include <Oxygen/Serio/MemoryStream.h>
#include <Oxygen/Serio/Reader.h>
#include <Oxygen/Serio/Region.h>
#include <Oxygen/Serio/Types.h>

using oxygen::content::PakFile;
using oxygen::data::AssetKey;
using oxygen::data::CookedOpenError;
using oxygen::data::CookedOpenFailure;
using oxygen::data::pak::core::AssetDirectoryEntry;
using oxygen::data::pak::core::PakBrowseIndexEntry;
using oxygen::data::pak::core::PakBrowseIndexHeader;
using oxygen::data::pak::core::PakBrowseIndexTextureEntry;
using oxygen::data::pak::core::PakFooter;
using oxygen::data::pak::core::PakHeader;

namespace {

using Bytes = std::span<const std::byte>;

// Maps the pak file and throws with logging on error.
auto MapPakFile(const std::filesystem::path& path) -> oxygen::serio::MappedFile
{
  auto file = oxygen::serio::MappedFile::Open(path);
  if (!file) {
    LOG_F(ERROR, "Failed to open pak file '{}': {}", path.string(),
      file.error().message());
    throw std::system_error(file.error(), "open '" + path.string() + "'");
  }
  return std::move(file).value();
}

// Opens the pak file through `driver` for its payloads, when there is one.
auto OpenPayloads(
  const std::filesystem::path& path, oxygen::serio::IoDriver* driver)
  -> std::optional<oxygen::serio::AnyAsyncDevice>
{
  if (driver == nullptr) {
    return std::nullopt;
  }
  auto file = driver->Open(path, {});
  if (!file) {
    LOG_F(ERROR, "Failed to open pak file '{}' for payloads: {}", path.string(),
      file.error().message());
    throw std::system_error(file.error(), "open '" + path.string() + "'");
  }
  return oxygen::serio::AnyAsyncDevice(std::move(file).value());
}

// Payload reads of `device` itself, which complete at once.
auto ReadsOf(const oxygen::serio::AnyDevice& device)
  -> oxygen::serio::AnyAsyncDevice
{
  auto whole = oxygen::serio::AnyRegion::Whole(device);
  if (!whole) {
    throw std::system_error(whole.error(), "size of the pak device");
  }
  return oxygen::serio::AnyAsyncDevice(
    oxygen::content::internal::BlockingReads(*whole));
}

// The `size` bytes of `bytes` at `offset`; `what` names them when they lie
// outside it.
auto Section(const Bytes bytes, const uint64_t offset, const uint64_t size,
  const std::string_view what) -> Bytes
{
  if (offset > bytes.size() || size > bytes.size() - offset) {
    throw std::runtime_error(std::string(what) + " is out of bounds");
  }
  return bytes.subspan(static_cast<size_t>(offset), static_cast<size_t>(size));
}

struct ParsedPakMetadata final {
  PakHeader header {};
  oxygen::data::SourceKey source_key {};
  PakFooter footer {};
  std::vector<AssetDirectoryEntry> directory {}; // NOLINT
  std::unordered_map<AssetKey, size_t> key_to_index {}; // NOLINT
};

auto ReadPakHeader(const Bytes bytes, oxygen::data::SourceKey& source_key_out)
  -> PakHeader
{
  LOG_SCOPE_FUNCTION(1);

  const auto header_result = oxygen::serio::ByteReader(bytes).Read<PakHeader>();
  if (!header_result) {
    LOG_F(
      ERROR, "Failed to read pak header: {}", header_result.error().message());
    throw CookedOpenError(
      CookedOpenFailure::kWrongFormat, "File too small to hold a pak header");
  }

  const auto header = header_result.value();
  PakFile::CheckHeader(header);
  source_key_out
    = oxygen::data::SourceKey::FromBytes(header.source_identity).value();
  LOG_F(INFO, "format version  : {}", header.version);
  LOG_F(INFO, "content version : {}", header.content_version);
  LOG_F(INFO, "pak source_identity        : {}", source_key_out);
  return header;
}

auto ReadPakFooter(const Bytes bytes) -> PakFooter
{
  LOG_SCOPE_FUNCTION(1);

  if (bytes.size() < sizeof(PakHeader) + sizeof(PakFooter)) {
    LOG_F(ERROR, "Pak file is smaller than a header and a footer: {} bytes",
      bytes.size());
    throw CookedOpenError(
      CookedOpenFailure::kWrongFormat, "File too small to hold a pak");
  }
  const auto footer_result
    = oxygen::serio::ByteReader(bytes.last(sizeof(PakFooter)))
        .Read<PakFooter>();
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
  PakFile::CheckFooter(footer, bytes.size());
  return footer;
}

auto ReadPakDirectory(const Bytes bytes, const PakFooter& footer,
  std::vector<AssetDirectoryEntry>& directory,
  std::unordered_map<AssetKey, size_t>& key_to_index) -> void
{
  LOG_SCOPE_FUNCTION(1);

  const auto file_size = bytes.size();
  constexpr auto kEntrySize = sizeof(AssetDirectoryEntry);
  if (footer.asset_count > footer.directory_size / kEntrySize
    || footer.directory_size != footer.asset_count * kEntrySize
    || footer.directory_offset > file_size - sizeof(PakFooter)
    || footer.directory_size
      > file_size - sizeof(PakFooter) - footer.directory_offset) {
    throw std::runtime_error("PAK asset directory is out of bounds");
  }

  auto reader = oxygen::serio::ByteReader(Section(bytes,
    footer.directory_offset, footer.directory_size, "PAK asset directory"));
  directory.clear();
  key_to_index.clear();
  directory.reserve(footer.asset_count);

  uint64_t metadata_begin = sizeof(PakHeader);
  for (uint64_t i = 0; i < footer.asset_count; ++i) {
    const auto entry_result = reader.Read<AssetDirectoryEntry>();
    if (!entry_result) {
      LOG_F(ERROR, "Failed to read asset directory entry: {}",
        entry_result.error().message());
      throw std::runtime_error("Failed to read asset directory entries");
    }
    const auto& entry = *entry_result;
    if (entry.desc_offset > footer.directory_offset
      || entry.desc_size > footer.directory_offset - entry.desc_offset) {
      throw std::runtime_error("PAK descriptor overlaps the directory");
    }
    metadata_begin
      = std::max(metadata_begin, entry.desc_offset + entry.desc_size);
    directory.push_back(entry);
    if (!key_to_index.emplace(entry.asset_key, directory.size() - 1).second) {
      throw std::runtime_error("PAK asset directory contains a duplicate key");
    }
  }
  for (const auto& entry : directory) {
    const auto& references = entry.references;
    const auto size = oxygen::data::AssetReferences::EncodedSize(
      references.resource_count, references.key_count);
    if (size == 0U) {
      if (references.offset != 0U) {
        throw std::runtime_error(
          "Empty PAK reference inventory has a nonzero offset");
      }
      continue;
    }
    if (references.offset != metadata_begin
      || references.offset > footer.directory_offset
      || size > footer.directory_offset - references.offset) {
      throw std::runtime_error(
        "PAK reference inventories overlap or are out of bounds");
    }
    metadata_begin += size;
  }
}

auto LoadPakCatalog(const Bytes bytes, const ParsedPakMetadata& metadata)
  -> oxygen::data::PakCatalog
{
  const auto& footer = metadata.footer;
  const auto require_before_catalog
    = [&](const uint64_t offset, const uint64_t length) -> void {
    if (length != 0U
      && (offset > footer.catalog_offset
        || length > footer.catalog_offset - offset)) {
      throw std::runtime_error("PAK section overlaps the embedded catalog");
    }
  };
  require_before_catalog(footer.directory_offset, footer.directory_size);
  require_before_catalog(footer.browse_index_offset, footer.browse_index_size);
  for (const auto& region : {
         footer.texture_region,
         footer.buffer_region,
         footer.audio_region,
         footer.script_region,
         footer.physics_region,
       }) {
    require_before_catalog(region.offset, region.size);
    // Payload regions lie on payload boundaries, so every payload can be read
    // unbuffered (`kPakPayloadAlignment`).
    if (region.size != 0U
      && (region.offset % oxygen::data::pak::core::kPakPayloadAlignment != 0U
        || region.size % oxygen::data::pak::core::kPakPayloadAlignment != 0U)) {
      throw std::runtime_error(
        "PAK payload region does not lie on payload boundaries");
    }
  }
  for (const auto& table : {
         footer.texture_table,
         footer.buffer_table,
         footer.audio_table,
         footer.script_resource_table,
         footer.physics_resource_table,
       }) {
    require_before_catalog(
      table.offset, uint64_t { table.count } * table.entry_size);
  }
  auto catalog = oxygen::data::PakCatalog::Decode(Section(
    bytes, footer.catalog_offset, footer.catalog_size, "Embedded PAK catalog"));
  if (!catalog) {
    throw CookedOpenError(catalog.error());
  }
  if (catalog->source_key != metadata.source_key
    || catalog->content_version != metadata.header.content_version
    || catalog->entries.size() != metadata.directory.size()) {
    throw std::runtime_error(
      "Embedded PAK catalog disagrees with its container");
  }
  for (const auto& entry : catalog->entries) {
    const auto found = metadata.key_to_index.find(entry.asset_key);
    if (found == metadata.key_to_index.end()
      || metadata.directory.at(found->second).asset_type != entry.asset_type) {
      throw std::runtime_error(
        "Embedded PAK catalog disagrees with its directory");
    }
  }
  return std::move(*catalog);
}

// Whether a pak with no CRC32 (zero) is refused or passes unchecked.
enum class MissingCrc : uint8_t {
  kRefuse,
  kSkip,
};

// Checks the CRC32 `footer` names against all of `device`, read once in
// order, except the CRC field itself, which writers do not hash (that is not
// the same as hashing it as zeros). A zero CRC means the pak holds none.
auto CheckCrc32(const oxygen::serio::AnyDevice& device, const PakFooter& footer,
  const std::filesystem::path& path, const MissingCrc missing) -> void
{
  if (footer.pak_crc32 == 0) {
    if (missing == MissingCrc::kRefuse) {
      LOG_F(
        ERROR, "Verified open of a pak without CRC32 path={}", path.string());
      throw CookedOpenError(
        CookedOpenFailure::kUnverifiable, "Pak holds no CRC32 to verify");
    }
    LOG_F(
      INFO, "CRC32 validation skipped (pak_crc32=0) path={}", path.string());
    return;
  }
  const auto size = device.Size();
  if (!size) {
    throw std::system_error(size.error(), "size of '" + path.string() + "'");
  }
  const auto crc_field = oxygen::serio::ByteRange {
    .offset = oxygen::serio::ByteOffset { size->get() - sizeof(PakFooter)
      + offsetof(PakFooter, pak_crc32) },
    .count = oxygen::serio::CountOf(sizeof(footer.pak_crc32)),
  };
  const auto crc
    = oxygen::serio::ComputeCrc32(device, std::span { &crc_field, 1 });
  if (!crc) {
    throw std::system_error(
      crc.error(), "read '" + path.string() + "' for CRC32");
  }
  if (*crc != footer.pak_crc32) {
    LOG_F(ERROR, "CRC32 mismatch path={} expected=0x{:08x} actual=0x{:08x}",
      path.string(), footer.pak_crc32, *crc);
    throw CookedOpenError(CookedOpenFailure::kCorrupt, "Pak CRC32 mismatch");
  }
  LOG_F(INFO, "CRC32 OK path={} crc32=0x{:08x}", path.string(), *crc);
}

// Parses the header, the footer and the directory; `verify` checks the CRC32
// once the footer is known, before the directory is parsed.
template <class Verify>
auto LoadPakMetadata(const Bytes bytes, const Verify& verify)
  -> ParsedPakMetadata
{
  ParsedPakMetadata metadata {};
  metadata.header = ReadPakHeader(bytes, metadata.source_key);
  metadata.footer = ReadPakFooter(bytes);
  verify(metadata.footer);
  ReadPakDirectory(
    bytes, metadata.footer, metadata.directory, metadata.key_to_index);
  return metadata;
}

} // namespace

auto PakFile::DecodeBrowseIndex(const std::span<const std::byte> bytes,
  const uint32_t texture_count) -> BrowseContents
{
  const auto corrupt = [](const std::string& message) -> CookedOpenError {
    return { CookedOpenFailure::kCorrupt, "Browse index: " + message };
  };
  serio::ReadOnlyMemoryStream stream(bytes);
  serio::Reader reader(stream);
  const auto packed = reader.ScopedAlignment(1);
  const auto header = reader.Read<PakBrowseIndexHeader>();
  if (!header) {
    throw corrupt("truncated header");
  }
  constexpr std::array<char, 8> kBrowseMagic
    = { 'O', 'X', 'P', 'A', 'K', 'B', 'I', 'X' };
  if (!std::ranges::equal(std::span { header->magic }, kBrowseMagic)) {
    throw corrupt("magic mismatch");
  }
  if (header->version != oxygen::data::pak::core::kBrowseIndexVersion) {
    throw CookedOpenError(CookedOpenFailure::kUnsupportedVersion,
      "Unsupported browse index version; recook content");
  }
  const uint64_t expected_size = sizeof(PakBrowseIndexHeader)
    + (uint64_t { header->entry_count } * sizeof(PakBrowseIndexEntry))
    + (uint64_t { header->texture_entry_count }
      * sizeof(PakBrowseIndexTextureEntry))
    + header->string_table_size;
  if (expected_size > bytes.size()) {
    throw corrupt("truncated payload");
  }

  std::vector<PakBrowseIndexEntry> entries(header->entry_count);
  for (auto& entry : entries) {
    auto read = reader.Read<PakBrowseIndexEntry>();
    if (!read) {
      throw corrupt("truncated entry");
    }
    entry = *read;
  }
  std::vector<PakBrowseIndexTextureEntry> texture_entries(
    header->texture_entry_count);
  for (auto& entry : texture_entries) {
    auto read = reader.Read<PakBrowseIndexTextureEntry>();
    if (!read) {
      throw corrupt("truncated texture entry");
    }
    entry = *read;
  }
  const auto strings = bytes.subspan(
    static_cast<size_t>(expected_size - header->string_table_size),
    static_cast<size_t>(header->string_table_size));
  const auto read_path
    = [&](const uint64_t offset, const uint64_t length) -> std::string {
    if (offset > strings.size() || length > strings.size() - offset) {
      throw corrupt("string reference out of bounds");
    }
    auto path = std::string(static_cast<size_t>(length), char {});
    std::ranges::transform(
      strings.subspan(static_cast<size_t>(offset), static_cast<size_t>(length)),
      path.begin(),
      [](const std::byte byte) -> char { return std::to_integer<char>(byte); });
    if (path.empty() || path.front() != '/') {
      throw corrupt("virtual path is not canonical");
    }
    return path;
  };

  auto contents = BrowseContents {};
  auto paths = std::unordered_set<std::string> {};
  const auto claim = [&paths, &corrupt](const std::string& path) -> void {
    if (!paths.insert(path).second) {
      throw corrupt("duplicate virtual path '" + path + "'");
    }
  };
  contents.entries.reserve(entries.size());
  for (const auto& entry : entries) {
    auto path = read_path(entry.virtual_path_offset, entry.virtual_path_length);
    claim(path);
    contents.entries.push_back({
      .virtual_path = std::move(path),
      .asset_key = entry.asset_key,
    });
  }
  contents.textures.reserve(texture_entries.size());
  for (const auto& entry : texture_entries) {
    if (entry.texture_index == oxygen::data::pak::core::kNoResourceIndex
      || entry.texture_index.get() >= texture_count) {
      throw corrupt("texture outside the texture table");
    }
    auto path = read_path(entry.virtual_path_offset, entry.virtual_path_length);
    claim(path);
    contents.textures.push_back({
      .virtual_path = std::move(path),
      .texture_index = entry.texture_index,
    });
  }
  return contents;
}

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

PakFile::PakFile(const std::filesystem::path& path,
  const Verification verification, std::shared_ptr<serio::IoDriver> driver)
  : PakFile(serio::AnyDevice(MapPakFile(path)), path, verification, driver,
      OpenPayloads(path, driver.get()))
{
}

PakFile::PakFile(serio::AnyDevice device, std::filesystem::path name,
  const Verification verification)
  : PakFile(
      std::move(device), std::move(name), verification, nullptr, std::nullopt)
{
}

PakFile::PakFile(serio::AnyDevice device, std::filesystem::path name,
  const Verification verification, std::shared_ptr<serio::IoDriver> driver,
  std::optional<serio::AnyAsyncDevice> payloads)
  : path_(std::move(name))
  , driver_(std::move(driver))
  , device_(std::move(device))
  , payloads_(payloads ? std::move(*payloads) : ReadsOf(device_))
{
  LOG_SCOPE_FUNCTION(1);
  LOG_F(INFO, "file : {}", path_.string());

  const auto view = device_.Contiguous();
  if (!view) {
    throw std::invalid_argument("A pak is read from a contiguous device");
  }
  bytes_ = *view;

  // Every refusal of the file's bytes is typed: what is not a wrong format
  // or version is a malformed pak.
  try {
    const auto bytes = bytes_;
    auto metadata
      = LoadPakMetadata(bytes, [&](const PakFooter& footer) -> void {
          if (verification == Verification::kCrc32) {
            CheckCrc32(device_, footer, path_, MissingCrc::kRefuse);
          }
        });
    catalog_ = LoadPakCatalog(bytes, metadata);
    header_ = metadata.header;
    source_key_ = metadata.source_key;
    footer_ = metadata.footer;
    directory_ = std::move(metadata.directory);
    key_to_index_ = std::move(metadata.key_to_index);
    InitializeResourceTablesFromFooter();

    // Every pak holds its browse index; the catalog check bounds it.
    if (footer_.browse_index_size == 0U) {
      throw CookedOpenError(
        CookedOpenFailure::kCorrupt, "The pak holds no browse index");
    }
    auto browse
      = DecodeBrowseIndex(Section(bytes, footer_.browse_index_offset,
                            footer_.browse_index_size, "Browse index"),
        footer_.texture_table.count);
    browse_index_ = std::move(browse.entries);
    browse_textures_ = std::move(browse.textures);
    for (const auto& entry : browse_index_) {
      browse_vpath_to_key_.emplace(entry.virtual_path, entry.asset_key);
    }
  } catch (const CookedOpenError&) {
    throw;
  } catch (const std::system_error&) {
    throw;
  } catch (const std::runtime_error& ex) {
    throw CookedOpenError(CookedOpenFailure::kCorrupt, ex.what());
  }
}

auto PakFile::CheckHeader(const PakHeader& header) -> void
{
  if (!std::ranges::equal(
        std::span { header.magic }, oxygen::data::pak::core::kPakHeaderMagic)) {
    LOG_F(ERROR, "Invalid pak file header magic");
    throw CookedOpenError(
      CookedOpenFailure::kWrongFormat, "Invalid pak file header magic");
  }
  if (header.version != oxygen::data::pak::core::kCurrentPakFormatVersion) {
    const auto msg = std::string("Unsupported PAK format version: ")
      + std::to_string(header.version) + " (this build loads v"
      + std::to_string(oxygen::data::pak::core::kCurrentPakFormatVersion)
      + " only).";
    LOG_F(ERROR, "{}", msg);
    throw CookedOpenError(CookedOpenFailure::kUnsupportedVersion, msg);
  }
  if (!oxygen::data::SourceKey::FromBytes(header.source_identity).has_value()) {
    LOG_F(
      ERROR, "Invalid pak source_identity: expected canonical UUIDv7 bytes");
    throw CookedOpenError(
      CookedOpenFailure::kCorrupt, "Invalid pak file source identity");
  }
}

auto PakFile::CheckFooter(const PakFooter& footer, const uint64_t file_size)
  -> void
{
  const auto footer_magic = std::as_bytes(std::span { footer.footer_magic });
  const auto expected_footer_magic
    = std::as_bytes(std::span { oxygen::data::pak::core::kPakFooterMagic });
  if (!std::ranges::equal(footer_magic, expected_footer_magic)) {
    LOG_F(ERROR, "Invalid pak file footer magic");
    throw CookedOpenError(
      CookedOpenFailure::kCorrupt, "Invalid pak file footer magic");
  }
  if (file_size < sizeof(PakHeader) + sizeof(PakFooter)
    || footer.catalog_offset < sizeof(PakHeader) || footer.catalog_size == 0U
    || footer.catalog_offset > file_size - sizeof(PakFooter)
    || footer.catalog_size
      > file_size - sizeof(PakFooter) - footer.catalog_offset) {
    throw CookedOpenError(CookedOpenFailure::kCorrupt,
      "Missing or invalid embedded PAK catalog; recook content");
  }
}

auto PakFile::ValidateCrc32Integrity() const -> void
{
  CheckCrc32(device_, footer_, path_, MissingCrc::kSkip);
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

auto PakFile::BrowseTextures() const noexcept
  -> std::span<const BrowseTextureEntry>
{
  return browse_textures_;
}

auto PakFile::ResolveTextureIndexByVirtualPath(
  const std::string_view virtual_path) const noexcept
  -> std::optional<data::pak::core::ResourceIndexT>
{
  const auto it = std::ranges::find(
    browse_textures_, virtual_path, &BrowseTextureEntry::virtual_path);
  if (it == browse_textures_.end()) {
    return std::nullopt;
  }
  return it->texture_index;
}

/*!
  Looks up an asset directory entry by its key.
  @param key Asset key to search for.
  @return Optional directory entry if found, `std::nullopt` otherwise.
*/
auto PakFile::FindEntry(const AssetKey& key) const noexcept
  -> std::optional<AssetDirectoryEntry>
{
  const auto it = key_to_index_.find(key);
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
  return directory_;
}

auto PakFile::DescriptorBytes(const AssetDirectoryEntry& entry) const
  -> std::span<const std::byte>
{
  const auto bytes = bytes_;
  if (entry.desc_offset > bytes.size()
    || entry.desc_size > bytes.size() - entry.desc_offset) {
    throw std::out_of_range("PAK asset descriptor is out of bounds");
  }
  return bytes.subspan(static_cast<size_t>(entry.desc_offset),
    static_cast<size_t>(entry.desc_size));
}

auto PakFile::ReadAssetReferences(const data::AssetKey& key) const
  -> data::AssetReferences
{
  const auto entry = FindEntry(key);
  if (!entry) {
    throw std::out_of_range("Asset reference inventory is not in this PAK");
  }
  const auto& table = entry->references;
  const auto byte_count
    = data::AssetReferences::EncodedSize(table.resource_count, table.key_count);
  if (byte_count == 0U) {
    if (table.offset != 0U) {
      throw std::runtime_error(
        "Empty PAK asset reference block has nonzero offset");
    }
    return {};
  }
  auto decoded = data::AssetReferences::Decode(
    Section(bytes_, table.offset, byte_count, "PAK asset reference block"),
    table.resource_count, table.key_count);
  if (!decoded) {
    throw std::runtime_error(decoded.error());
  }
  return std::move(*decoded);
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

auto PakFile::Guid() const noexcept -> data::SourceKey { return source_key_; }

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
