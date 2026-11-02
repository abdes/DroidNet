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
#include <optional>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Content/PakFile.h>
#include <Oxygen/Content/VirtualPath.h>
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
#include <Oxygen/Serio/MappedFile.h>
#include <Oxygen/Serio/MemoryStream.h>
#include <Oxygen/Serio/Reader.h>
#include <Oxygen/Serio/Types.h>

using oxygen::content::PakFile;
using oxygen::data::AssetKey;
using oxygen::data::CookedDefect;
using oxygen::data::CookedOpenError;
using oxygen::data::pak::core::AssetDirectoryEntry;
using oxygen::data::pak::core::kPakMetadataAlignment;
using oxygen::data::pak::core::kPakPayloadAlignment;
using oxygen::data::pak::core::PakBrowseIndexEntry;
using oxygen::data::pak::core::PakBrowseIndexHeader;
using oxygen::data::pak::core::PakBrowseIndexTextureEntry;
using oxygen::data::pak::core::PakFooter;
using oxygen::data::pak::core::PakHeader;
using oxygen::data::pak::core::ResourceRegion;
using oxygen::data::pak::core::ResourceTable;

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

// The `size` bytes of `bytes` at `offset`; `what` names them when they lie
// outside it.
auto Section(const Bytes bytes, const uint64_t offset, const uint64_t size,
  const std::string_view what) -> Bytes
{
  const auto in_bytes = offset <= bytes.size() && size <= bytes.size() - offset;
  if (!in_bytes) {
    throw CookedOpenError(
      CookedDefect::kOutOfBounds, std::string(what) + " is out of bounds");
  }
  return bytes.subspan(static_cast<size_t>(offset), static_cast<size_t>(size));
}

// Whether every reserved byte is zero, as the format requires.
template <class T> auto IsZero(const T& reserve) noexcept -> bool
{
  return oxygen::data::pak::core::IsZeroReserve(
    std::as_bytes(std::span { reserve }));
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
      CookedDefect::kTooSmall, "File too small to hold a pak header");
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
      CookedDefect::kTooSmall, "File too small to hold a pak");
  }
  const auto footer_result
    = oxygen::serio::ByteReader(bytes.last(sizeof(PakFooter)))
        .Read<PakFooter>();
  if (!footer_result) {
    LOG_F(
      ERROR, "Failed to read pak footer: {}", footer_result.error().message());
    throw CookedOpenError(
      CookedDefect::kTruncated, "Failed to read pak footer");
  }

  const auto footer = footer_result.value();
  LOG_F(INFO, "pak crc32        : {}", footer.pak_crc32);
  LOG_F(INFO, "directory offset : {}", footer.directory_offset);
  LOG_F(INFO, "directory size   : {}", footer.directory_size);
  LOG_F(INFO, "asset count      : {}", footer.asset_count);
  PakFile::CheckFooter(footer, bytes.size());
  return footer;
}

auto ReadAndIndexDirectory(const Bytes bytes, const PakFooter& footer,
  std::vector<AssetDirectoryEntry>& directory,
  std::unordered_map<AssetKey, size_t>& key_to_index) -> uint64_t
{
  const auto file_size = bytes.size();
  constexpr auto kEntrySize = sizeof(AssetDirectoryEntry);
  const auto sized_for_its_entries
    = footer.asset_count <= footer.directory_size / kEntrySize
    && footer.directory_size == footer.asset_count * kEntrySize;
  if (!sized_for_its_entries) {
    throw CookedOpenError(CookedDefect::kSizeMismatch,
      "PAK asset directory size does not match its asset count");
  }
  const auto body_end = file_size - sizeof(PakFooter);
  const auto in_body = footer.directory_offset <= body_end
    && footer.directory_size <= body_end - footer.directory_offset;
  if (!in_body) {
    throw CookedOpenError(
      CookedDefect::kOutOfBounds, "PAK asset directory is out of bounds");
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
      throw CookedOpenError(
        CookedDefect::kTruncated, "Failed to read asset directory entries");
    }
    const auto& entry = *entry_result;
    if (!IsZero(entry._reserved)) {
      throw CookedOpenError(CookedDefect::kReservedNotZero,
        "PAK directory entry reserved bytes are not zero");
    }
    const auto names_its_position
      = entry.entry_offset == footer.directory_offset + (i * kEntrySize);
    if (!names_its_position) {
      throw CookedOpenError(CookedDefect::kInconsistent,
        "PAK directory entry offset does not match its position");
    }
    const auto before_directory = entry.desc_offset <= footer.directory_offset
      && entry.desc_size <= footer.directory_offset - entry.desc_offset;
    if (!before_directory) {
      throw CookedOpenError(
        CookedDefect::kOverlap, "PAK descriptor overlaps the directory");
    }
    metadata_begin
      = std::max(metadata_begin, entry.desc_offset + entry.desc_size);
    directory.push_back(entry);
    if (!key_to_index.emplace(entry.asset_key, directory.size() - 1).second) {
      throw CookedOpenError(CookedDefect::kDuplicate,
        "PAK asset directory contains a duplicate key");
    }
  }
  return metadata_begin;
}

auto ValidateReferenceInventories(const Bytes bytes, const PakFooter& footer,
  const std::span<const AssetDirectoryEntry> directory,
  const uint64_t metadata_begin) -> void
{
  const auto counts = oxygen::data::ResourceTableCounts {
    .buffers = footer.buffer_table.count,
    .textures = footer.texture_table.count,
    .scripts = footer.script_resource_table.count,
    .physics = footer.physics_resource_table.count,
  };
  // Where the next non-empty inventory must start.
  auto next_inventory = metadata_begin;
  for (const auto& entry : directory) {
    const auto& references = entry.references;
    const auto size = oxygen::data::AssetReferences::EncodedSize(
      references.resource_count, references.key_count);
    if (size == 0U) {
      const auto located_nowhere = references.offset == 0U;
      if (!located_nowhere) {
        throw CookedOpenError(CookedDefect::kInvalidReference,
          "Empty PAK reference inventory has a nonzero offset");
      }
      continue;
    }
    // Inventories follow the descriptors, back to back, in directory order.
    const auto follows_previous = references.offset == next_inventory;
    if (!follows_previous) {
      throw CookedOpenError(CookedDefect::kOverlap,
        "PAK reference inventories are not contiguous after the descriptors");
    }
    const auto before_directory = references.offset <= footer.directory_offset
      && size <= footer.directory_offset - references.offset;
    if (!before_directory) {
      throw CookedOpenError(CookedDefect::kOverlap,
        "PAK reference inventory overlaps the directory");
    }
    next_inventory += size;
    const auto decoded = oxygen::data::AssetReferences::Decode(
      bytes.subspan(
        static_cast<size_t>(references.offset), static_cast<size_t>(size)),
      references.resource_count, references.key_count);
    if (!decoded) {
      throw CookedOpenError(CookedDefect::kInvalidReference,
        "PAK reference inventory: " + decoded.error());
    }
    if (const auto bounded = decoded->ValidateResourceBounds(counts);
      !bounded) {
      throw CookedOpenError(CookedDefect::kInvalidReference,
        "PAK reference inventory: " + bounded.error());
    }
  }
}

auto ReadPakDirectory(const Bytes bytes, const PakFooter& footer,
  std::vector<AssetDirectoryEntry>& directory,
  std::unordered_map<AssetKey, size_t>& key_to_index) -> void
{
  LOG_SCOPE_FUNCTION(1);

  const auto metadata_begin
    = ReadAndIndexDirectory(bytes, footer, directory, key_to_index);
  ValidateReferenceInventories(bytes, footer, directory, metadata_begin);
}

// A section of a pak's body, between its header and its footer.
struct PakSection final {
  uint64_t offset = 0;
  uint64_t size = 0;
  uint64_t alignment = 1;
  std::string_view what;
};

// Checks that every non-empty section lies in the pak body, starts on its
// alignment and overlaps no other one.
auto CheckSections(std::vector<PakSection> sections, const uint64_t file_size)
  -> void
{
  const auto begin = uint64_t { sizeof(PakHeader) };
  const auto end = file_size - sizeof(PakFooter);
  std::erase_if(sections,
    [](const PakSection& section) -> bool { return section.size == 0U; });
  for (const auto& section : sections) {
    const auto in_body = section.offset >= begin && section.offset <= end
      && section.size <= end - section.offset;
    if (!in_body) {
      throw CookedOpenError(CookedDefect::kOutOfBounds,
        std::string(section.what) + " lies outside the pak body");
    }
    if (section.offset % section.alignment != 0U) {
      throw CookedOpenError(CookedDefect::kMisaligned,
        std::string(section.what) + " is not aligned");
    }
  }
  std::ranges::sort(sections, {}, &PakSection::offset);
  for (const auto& [lower, upper] : sections | std::views::adjacent<2>) {
    const auto disjoint = lower.offset + lower.size <= upper.offset;
    if (!disjoint) {
      throw CookedOpenError(CookedDefect::kOverlap,
        std::string(lower.what) + " overlaps " + std::string(upper.what));
    }
  }
}

// The section of a resource table whose rows are `Desc`; a non-empty table
// of another row size is corrupt.
template <class Desc>
auto TableSection(const ResourceTable& table, const std::string_view what)
  -> PakSection
{
  const auto rows_have_their_size
    = table.count == 0U || table.entry_size == sizeof(Desc);
  if (!rows_have_their_size) {
    throw CookedOpenError(CookedDefect::kWrongRowSize,
      std::string(what) + " has the wrong entry size");
  }
  return {
    .offset = table.offset,
    .size = uint64_t { table.count } * table.entry_size,
    .alignment = kPakMetadataAlignment,
    .what = what,
  };
}

// Checks that each row of `table` places its payload in `region`, on a
// payload boundary, and holds the payload's digest when its format has one.
template <class Desc>
auto CheckRows(const Bytes bytes, const ResourceTable& table,
  const ResourceRegion& region, const std::string_view what) -> void
{
  for (uint64_t row = 0; row < table.count; ++row) {
    const auto desc = oxygen::serio::CopyAt<Desc>(
      bytes, oxygen::serio::ByteOffset { table.offset + (row * sizeof(Desc)) });
    if (!desc) {
      throw CookedOpenError(
        CookedDefect::kTruncated, std::string(what) + " row is truncated");
    }
    if constexpr (requires { desc->_reserved; }) {
      if (!IsZero(desc->_reserved)) {
        throw CookedOpenError(CookedDefect::kReservedNotZero,
          std::string(what) + " row reserved bytes are not zero");
      }
    }
    if constexpr (std::is_same_v<Desc,
                    oxygen::data::pak::core::TextureResourceDesc>) {
      if (!oxygen::data::pak::core::HasSupportedCompression(*desc)) {
        throw CookedOpenError(CookedDefect::kInvalidValue,
          std::string(what) + " row records a compression its format lacks");
      }
    }
    if (desc->size_bytes == 0U) {
      continue;
    }
    const auto offset = uint64_t { desc->data_offset };
    const auto in_region = offset >= region.offset
      && offset - region.offset <= region.size
      && desc->size_bytes <= region.size - (offset - region.offset);
    if (!in_region) {
      throw CookedOpenError(CookedDefect::kOutOfBounds,
        std::string(what) + " row lies outside its region");
    }
    if (offset % kPakPayloadAlignment != 0U) {
      throw CookedOpenError(CookedDefect::kMisaligned,
        std::string(what) + " row does not start on a payload boundary");
    }
    if constexpr (requires { desc->content_hash; }) {
      if (oxygen::base::IsAllZero(desc->content_hash)) {
        throw CookedOpenError(CookedDefect::kMissingDigest,
          std::string(what) + " row has no content digest");
      }
    }
  }
}

auto CheckFooterAlignment(const Bytes bytes) -> void
{
  if ((bytes.size() - sizeof(PakFooter)) % kPakMetadataAlignment != 0U) {
    throw CookedOpenError(
      CookedDefect::kMisaligned, "PAK footer is not aligned");
  }
}

auto MakeMetadataSections(const PakFooter& footer) -> std::vector<PakSection>
{
  namespace core = oxygen::data::pak::core;
  return {
    {
      .offset = footer.directory_offset,
      .size = footer.directory_size,
      .alignment = kPakMetadataAlignment,
      .what = "PAK asset directory",
    },
    {
      .offset = footer.browse_index_offset,
      .size = footer.browse_index_size,
      .alignment = kPakMetadataAlignment,
      .what = "Browse index",
    },
    {
      .offset = footer.catalog_offset,
      .size = footer.catalog_size,
      .alignment = 1,
      .what = "Embedded PAK catalog",
    },
    TableSection<core::TextureResourceDesc>(
      footer.texture_table, "PAK texture table"),
    TableSection<core::BufferResourceDesc>(
      footer.buffer_table, "PAK buffer table"),
    TableSection<oxygen::data::pak::audio::AudioResourceDesc>(
      footer.audio_table, "PAK audio table"),
    TableSection<oxygen::data::pak::scripting::ScriptResourceDesc>(
      footer.script_resource_table, "PAK script table"),
    TableSection<oxygen::data::pak::physics::PhysicsResourceDesc>(
      footer.physics_resource_table, "PAK physics table"),
  };
}

auto AppendResourceRegions(
  std::vector<PakSection>& sections, const PakFooter& footer) -> void
{
  for (const auto& [region, what] : {
         std::pair { footer.texture_region, "PAK texture region" },
         std::pair { footer.buffer_region, "PAK buffer region" },
         std::pair { footer.audio_region, "PAK audio region" },
         std::pair { footer.script_region, "PAK script region" },
         std::pair { footer.physics_region, "PAK physics region" },
       }) {
    // A region is padded to whole payload boundaries.
    if (region.size % kPakPayloadAlignment != 0U) {
      throw CookedOpenError(CookedDefect::kMisaligned,
        std::string(what) + " is not padded to a payload boundary");
    }
    sections.push_back({
      .offset = region.offset,
      .size = region.size,
      .alignment = kPakPayloadAlignment,
      .what = what,
    });
  }
}

auto AppendAssetSections(std::vector<PakSection>& sections,
  const std::span<const AssetDirectoryEntry> directory) -> void
{
  for (const auto& entry : directory) {
    sections.push_back({
      .offset = entry.desc_offset,
      .size = entry.desc_size,
      .alignment = kPakMetadataAlignment,
      .what = "PAK asset descriptor",
    });
    sections.push_back({
      .offset = entry.references.offset,
      .size = oxygen::data::AssetReferences::EncodedSize(
        entry.references.resource_count, entry.references.key_count),
      .alignment = 1,
      .what = "PAK reference inventory",
    });
  }
}

auto CheckResourceRows(const Bytes bytes, const PakFooter& footer) -> void
{
  namespace core = oxygen::data::pak::core;
  CheckRows<core::TextureResourceDesc>(
    bytes, footer.texture_table, footer.texture_region, "PAK texture table");
  CheckRows<core::BufferResourceDesc>(
    bytes, footer.buffer_table, footer.buffer_region, "PAK buffer table");
  CheckRows<oxygen::data::pak::audio::AudioResourceDesc>(
    bytes, footer.audio_table, footer.audio_region, "PAK audio table");
  CheckRows<oxygen::data::pak::scripting::ScriptResourceDesc>(bytes,
    footer.script_resource_table, footer.script_region, "PAK script table");
  CheckRows<oxygen::data::pak::physics::PhysicsResourceDesc>(bytes,
    footer.physics_resource_table, footer.physics_region, "PAK physics table");
}

// Checks the pak body before anything in it is exposed: every section in
// bounds, aligned and disjoint, every table of its row size and every row's
// payload in its region.
auto CheckPakLayout(const Bytes bytes, const ParsedPakMetadata& metadata)
  -> void
{
  const auto& footer = metadata.footer;
  CheckFooterAlignment(bytes);
  auto sections = MakeMetadataSections(footer);
  AppendResourceRegions(sections, footer);
  AppendAssetSections(sections, metadata.directory);
  CheckSections(std::move(sections), bytes.size());
  CheckResourceRows(bytes, footer);
}

// Every browse path lies in the pak's own mount, and every browse asset is
// one its directory holds.
auto CheckBrowseContents(const PakFile::BrowseContents& contents,
  const std::string_view mount,
  const std::unordered_map<AssetKey, size_t>& key_to_index) -> void
{
  const auto in_mount = [mount](const std::string_view path) -> bool {
    const auto root = oxygen::content::MountRootOf(path);
    return !root.empty() && root.substr(1) == mount;
  };
  for (const auto& entry : contents.entries) {
    if (!in_mount(entry.virtual_path)) {
      throw CookedOpenError(CookedDefect::kInconsistent,
        "Browse index path lies outside the pak's mount");
    }
    if (!key_to_index.contains(entry.asset_key)) {
      throw CookedOpenError(CookedDefect::kInconsistent,
        "Browse index names an asset the pak does not hold");
    }
  }
  for (const auto& texture : contents.textures) {
    if (!in_mount(texture.virtual_path)) {
      throw CookedOpenError(CookedDefect::kInconsistent,
        "Browse index path lies outside the pak's mount");
    }
  }
}

auto LoadPakCatalog(const Bytes bytes, const ParsedPakMetadata& metadata)
  -> oxygen::data::PakCatalog
{
  const auto& footer = metadata.footer;
  auto catalog = oxygen::data::PakCatalog::Decode(Section(
    bytes, footer.catalog_offset, footer.catalog_size, "Embedded PAK catalog"));
  if (!catalog) {
    throw CookedOpenError(catalog.error());
  }
  const auto describes_its_container
    = catalog->source_key == metadata.source_key
    && catalog->content_version == metadata.header.content_version
    && catalog->entries.size() == metadata.directory.size();
  if (!describes_its_container) {
    throw CookedOpenError(CookedDefect::kInconsistent,
      "Embedded PAK catalog disagrees with its container");
  }
  for (const auto& entry : catalog->entries) {
    const auto found = metadata.key_to_index.find(entry.asset_key);
    const auto in_directory = found != metadata.key_to_index.end()
      && metadata.directory.at(found->second).asset_type == entry.asset_type;
    if (!in_directory) {
      throw CookedOpenError(CookedDefect::kInconsistent,
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
// order. A zero CRC means the pak holds none.
auto VerifyDeviceCrc32(const oxygen::serio::AnyDevice& device,
  const PakFooter& footer, const std::filesystem::path& path,
  const MissingCrc missing) -> void
{
  const auto size = device.Size();
  if (!size) {
    throw std::system_error(size.error(), "size of '" + path.string() + "'");
  }
  const auto check = oxygen::content::PakFile::CrcCheckOf(footer, size->get());
  if (!check) {
    if (missing == MissingCrc::kRefuse) {
      LOG_F(
        ERROR, "Verified open of a pak without CRC32 path={}", path.string());
      throw CookedOpenError(
        CookedDefect::kNoIntegrityData, "Pak holds no CRC32 to verify");
    }
    LOG_F(
      INFO, "CRC32 validation skipped (pak_crc32=0) path={}", path.string());
    return;
  }
  const auto crc
    = oxygen::serio::ComputeCrc32(device, std::span { &check->crc_field, 1 });
  if (!crc) {
    throw std::system_error(
      crc.error(), "read '" + path.string() + "' for CRC32");
  }
  oxygen::content::PakFile::CheckCrc32(*check, *crc, path);
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
  CheckPakLayout(bytes, metadata);
  return metadata;
}

} // namespace

namespace {

using BrowseReader = oxygen::serio::Reader<oxygen::serio::ReadOnlyMemoryStream>;

auto BrowseError(const CookedDefect defect, const std::string& message)
  -> CookedOpenError
{
  return { defect, "Browse index: " + message };
}

struct BrowseHeader final {
  PakBrowseIndexHeader fields {};
  //! The size of the browse index the header declares.
  uint64_t size = 0;
};

auto ReadBrowseHeader(BrowseReader& reader, const size_t byte_count)
  -> BrowseHeader
{
  const auto header = reader.Read<PakBrowseIndexHeader>();
  if (!header) {
    throw BrowseError(CookedDefect::kTruncated, "truncated header");
  }
  constexpr std::array<char, 8> kBrowseMagic
    = { 'O', 'X', 'P', 'A', 'K', 'B', 'I', 'X' };
  if (!std::ranges::equal(std::span { header->magic }, kBrowseMagic)) {
    // The footer names bytes that are no browse index.
    throw BrowseError(CookedDefect::kMissingPart, "magic mismatch");
  }
  if (header->version != oxygen::data::pak::core::kBrowseIndexVersion) {
    throw BrowseError(
      CookedDefect::kUnsupportedVersion, "unsupported version; recook content");
  }
  const uint64_t expected_size = sizeof(PakBrowseIndexHeader)
    + (uint64_t { header->entry_count } * sizeof(PakBrowseIndexEntry))
    + (uint64_t { header->texture_entry_count }
      * sizeof(PakBrowseIndexTextureEntry))
    + header->string_table_size;
  if (expected_size > byte_count) {
    throw BrowseError(CookedDefect::kTruncated, "truncated payload");
  }
  // The footer records the browse index's exact size: unused bytes after it
  // make the pak non-canonical.
  const auto fills_its_section = expected_size == byte_count;
  if (!fills_its_section) {
    throw BrowseError(
      CookedDefect::kSizeMismatch, "bytes after the declared payload");
  }
  return { .fields = *header, .size = expected_size };
}

auto ReadBrowseEntries(BrowseReader& reader, const uint32_t count)
  -> std::vector<PakBrowseIndexEntry>
{
  std::vector<PakBrowseIndexEntry> entries(count);
  for (auto& entry : entries) {
    auto read = reader.Read<PakBrowseIndexEntry>();
    if (!read) {
      throw BrowseError(CookedDefect::kTruncated, "truncated entry");
    }
    entry = *read;
  }
  return entries;
}

auto ReadBrowseTextureEntries(BrowseReader& reader, const uint32_t count)
  -> std::vector<PakBrowseIndexTextureEntry>
{
  std::vector<PakBrowseIndexTextureEntry> texture_entries(count);
  for (auto& entry : texture_entries) {
    auto read = reader.Read<PakBrowseIndexTextureEntry>();
    if (!read) {
      throw BrowseError(CookedDefect::kTruncated, "truncated texture entry");
    }
    entry = *read;
  }
  return texture_entries;
}

auto BrowseStrings(const Bytes bytes, const BrowseHeader& header) -> Bytes
{
  return bytes.subspan(
    static_cast<size_t>(header.size - header.fields.string_table_size),
    static_cast<size_t>(header.fields.string_table_size));
}

auto BuildBrowseContents(const std::span<const PakBrowseIndexEntry> entries,
  const std::span<const PakBrowseIndexTextureEntry> texture_entries,
  const Bytes strings, const uint32_t texture_count) -> PakFile::BrowseContents
{
  const auto read_path
    = [&](const uint64_t offset, const uint64_t length) -> std::string {
    if (offset > strings.size() || length > strings.size() - offset) {
      throw BrowseError(
        CookedDefect::kOutOfBounds, "string reference out of bounds");
    }
    auto path = std::string(static_cast<size_t>(length), char {});
    std::ranges::transform(
      strings.subspan(static_cast<size_t>(offset), static_cast<size_t>(length)),
      path.begin(),
      [](const std::byte byte) -> char { return std::to_integer<char>(byte); });
    if (oxygen::content::ValidateVirtualPath(path).has_value()) {
      throw BrowseError(
        CookedDefect::kInvalidPath, "virtual path is not canonical");
    }
    return path;
  };

  auto contents = PakFile::BrowseContents {};
  auto paths = std::unordered_set<std::string> {};
  const auto claim = [&paths](const std::string& path) -> void {
    if (!paths.insert(path).second) {
      throw BrowseError(
        CookedDefect::kDuplicate, "duplicate virtual path '" + path + "'");
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
      throw BrowseError(
        CookedDefect::kOutOfBounds, "texture outside the texture table");
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

} // namespace

auto PakFile::DecodeBrowseIndex(const std::span<const std::byte> bytes,
  const uint32_t texture_count) -> BrowseContents
{
  serio::ReadOnlyMemoryStream stream(bytes);
  serio::Reader reader(stream);
  const auto packed = reader.ScopedAlignment(1);

  const auto header = ReadBrowseHeader(reader, bytes.size());
  const auto entries = ReadBrowseEntries(reader, header.fields.entry_count);
  const auto textures
    = ReadBrowseTextureEntries(reader, header.fields.texture_entry_count);
  const auto strings = BrowseStrings(bytes, header);
  return BuildBrowseContents(entries, textures, strings, texture_count);
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

PakFile::PakFile(
  const std::filesystem::path& path, const Verification verification)
  : PakFile(serio::AnyDevice(MapPakFile(path)), path, verification)
{
}

PakFile::PakFile(serio::AnyDevice device, std::filesystem::path name,
  const Verification verification)
  : path_(std::move(name))
  , device_(std::move(device))
{
  LOG_SCOPE_FUNCTION(1);
  LOG_F(INFO, "file : {}", path_.string());

  const auto view = device_.Contiguous();
  if (!view) {
    throw std::invalid_argument("A pak is read from a contiguous device");
  }
  bytes_ = *view;

  // Every refusal of the file's bytes throws `CookedOpenError` where its rule
  // is checked; I/O failures throw `std::system_error`.
  const auto bytes = bytes_;
  auto metadata = LoadPakMetadata(bytes, [&](const PakFooter& footer) -> void {
    if (verification == Verification::kCrc32) {
      VerifyDeviceCrc32(device_, footer, path_, MissingCrc::kRefuse);
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
      CookedDefect::kMissingPart, "The pak holds no browse index");
  }
  auto browse = DecodeBrowseIndex(Section(bytes, footer_.browse_index_offset,
                                    footer_.browse_index_size, "Browse index"),
    footer_.texture_table.count);
  CheckBrowseContents(browse, catalog_.mount, key_to_index_);
  browse_index_ = std::move(browse.entries);
  browse_textures_ = std::move(browse.textures);
  for (const auto& entry : browse_index_) {
    browse_vpath_to_key_.emplace(entry.virtual_path, entry.asset_key);
  }
}

auto PakFile::CheckHeader(const PakHeader& header) -> void
{
  if (!std::ranges::equal(
        std::span { header.magic }, oxygen::data::pak::core::kPakHeaderMagic)) {
    LOG_F(ERROR, "Invalid pak file header magic");
    throw CookedOpenError(
      CookedDefect::kWrongMagic, "Invalid pak file header magic");
  }
  if (header.version != oxygen::data::pak::core::kCurrentPakFormatVersion) {
    const auto msg = std::string("Unsupported PAK format version: ")
      + std::to_string(header.version) + " (this build loads v"
      + std::to_string(oxygen::data::pak::core::kCurrentPakFormatVersion)
      + " only).";
    LOG_F(ERROR, "{}", msg);
    throw CookedOpenError(CookedDefect::kUnsupportedVersion, msg);
  }
  if (!oxygen::data::SourceKey::FromBytes(header.source_identity).has_value()) {
    LOG_F(
      ERROR, "Invalid pak source_identity: expected canonical UUIDv7 bytes");
    throw CookedOpenError(
      CookedDefect::kInvalidIdentity, "Invalid pak file source identity");
  }
  if (!IsZero(header._reserved)) {
    LOG_F(ERROR, "Pak header reserved bytes are not zero");
    throw CookedOpenError(
      CookedDefect::kReservedNotZero, "Pak header reserved bytes are not zero");
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
    // A pak whose last bytes are no footer was cut short or appended to.
    throw CookedOpenError(
      CookedDefect::kTruncated, "Invalid pak file footer magic");
  }
  if (!IsZero(footer._reserved)) {
    LOG_F(ERROR, "Pak footer reserved bytes are not zero");
    throw CookedOpenError(
      CookedDefect::kReservedNotZero, "Pak footer reserved bytes are not zero");
  }
  if (file_size < sizeof(PakHeader) + sizeof(PakFooter)) {
    throw CookedOpenError(
      CookedDefect::kTooSmall, "File too small to hold a pak");
  }
  if (footer.catalog_size == 0U) {
    throw CookedOpenError(
      CookedDefect::kMissingPart, "The pak holds no embedded catalog");
  }
  const auto body_end = file_size - sizeof(PakFooter);
  const auto catalog_in_body = footer.catalog_offset >= sizeof(PakHeader)
    && footer.catalog_offset <= body_end
    && footer.catalog_size <= body_end - footer.catalog_offset;
  if (!catalog_in_body) {
    throw CookedOpenError(
      CookedDefect::kOutOfBounds, "Embedded PAK catalog is out of bounds");
  }
}

auto PakFile::CrcCheckOf(const PakFooter& footer,
  const uint64_t file_size) noexcept -> std::optional<CrcCheck>
{
  if (footer.pak_crc32 == 0 || file_size < sizeof(PakFooter)) {
    return std::nullopt;
  }
  return CrcCheck {
    .expected = footer.pak_crc32,
    .crc_field = serio::ByteRange {
      .offset = serio::ByteOffset { file_size - sizeof(PakFooter)
        + offsetof(PakFooter, pak_crc32) },
      .count = serio::CountOf(sizeof(footer.pak_crc32)),
    },
  };
}

auto PakFile::CheckCrc32(const CrcCheck& check, const uint32_t actual,
  const std::filesystem::path& path) -> void
{
  if (actual != check.expected) {
    LOG_F(ERROR, "CRC32 mismatch path={} expected=0x{:08x} actual=0x{:08x}",
      path.string(), check.expected, actual);
    throw CookedOpenError(CookedDefect::kDigestMismatch, "Pak CRC32 mismatch");
  }
  LOG_F(INFO, "CRC32 OK path={} crc32=0x{:08x}", path.string(), actual);
}

auto PakFile::ValidateCrc32Integrity() const -> void
{
  VerifyDeviceCrc32(device_, footer_, path_, MissingCrc::kSkip);
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
