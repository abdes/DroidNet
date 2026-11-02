//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include <Oxygen/Cooker/Import/Internal/LooseCookedLayout.h>
#include <Oxygen/Cooker/testing/CookedBytes.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/LooseCookedIndexFormat.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Data/PakFormat_references.h>
#include <Oxygen/Data/SourceKey.h>
#include <Oxygen/Serio/ByteReader.h>

namespace oxygen::cooker::testing {

namespace {

  namespace core = data::pak::core;
  namespace lc = data::loose_cooked;

  //! Reads the record `T` at `offset` of `bytes`, lets `edit` change it and
  //! writes it back.
  template <class T, class Edit>
  auto EditRecord(std::vector<std::byte>& bytes, const size_t offset,
    const Edit& edit) -> void
  {
    const auto read = serio::CopyAt<T>(bytes, serio::ByteOffset { offset });
    if (!read) {
      throw std::invalid_argument("the artifact is too small for its record");
    }
    auto record = *read;
    edit(record);
    std::ranges::copy(std::bit_cast<std::array<std::byte, sizeof(T)>>(record),
      std::span { bytes }.subspan(offset).begin());
  }

  template <class Edit>
  auto EditHeader(std::vector<std::byte>& pak, const Edit& edit) -> void
  {
    EditRecord<core::PakHeader>(pak, 0U, edit);
  }

  auto FooterOffset(const std::vector<std::byte>& pak) -> size_t
  {
    if (pak.size() < sizeof(core::PakHeader) + sizeof(core::PakFooter)) {
      throw std::invalid_argument("the bytes are too small to be a pak");
    }
    return pak.size() - sizeof(core::PakFooter);
  }

  template <class Edit>
  auto EditFooter(std::vector<std::byte>& pak, const Edit& edit) -> void
  {
    EditRecord<core::PakFooter>(pak, FooterOffset(pak), edit);
  }

  auto ReadFooter(const std::vector<std::byte>& pak) -> core::PakFooter
  {
    const auto footer = serio::CopyAt<core::PakFooter>(
      pak, serio::ByteOffset { FooterOffset(pak) });
    if (!footer) {
      throw std::invalid_argument("the bytes are too small to be a pak");
    }
    return *footer;
  }

  //! The directory entry `index` of the pak.
  auto ReadDirectoryEntry(const std::vector<std::byte>& pak, const size_t index)
    -> core::AssetDirectoryEntry
  {
    const auto footer = ReadFooter(pak);
    if (index >= footer.asset_count) {
      throw std::invalid_argument("the pak holds no such asset");
    }
    const auto entry = serio::CopyAt<core::AssetDirectoryEntry>(pak,
      serio::ByteOffset { footer.directory_offset
        + (index * sizeof(core::AssetDirectoryEntry)) });
    if (!entry) {
      throw std::invalid_argument("the pak's directory is malformed");
    }
    return *entry;
  }

  //! Lets `edit` change the directory entry `index` of the pak.
  template <class Edit>
  auto EditDirectoryEntry(
    std::vector<std::byte>& pak, const size_t index, const Edit& edit) -> void
  {
    const auto footer = ReadFooter(pak);
    if (index >= footer.asset_count) {
      throw std::invalid_argument("the pak holds no such asset");
    }
    EditRecord<core::AssetDirectoryEntry>(pak,
      static_cast<size_t>(
        footer.directory_offset + (index * sizeof(core::AssetDirectoryEntry))),
      edit);
  }

  //! Where the browse index `browse` holds its first texture entry.
  auto FirstBrowseTexture(const std::vector<std::byte>& browse) -> size_t
  {
    const auto header = serio::CopyAt<core::PakBrowseIndexHeader>(browse);
    if (!header || header->texture_entry_count == 0U) {
      throw std::invalid_argument("the browse index holds no texture");
    }
    return sizeof(core::PakBrowseIndexHeader)
      + (size_t { header->entry_count } * sizeof(core::PakBrowseIndexEntry));
  }

  //! Lets `edit` change the last row of `table` that holds a payload.
  template <class Desc, class Edit>
  auto EditLastPayloadRow(std::vector<std::byte>& bytes,
    const core::ResourceTable& table, const Edit& edit) -> void
  {
    for (auto row = uint64_t { table.count }; row-- > 0U;) {
      const auto offset
        = static_cast<size_t>(table.offset + (row * sizeof(Desc)));
      const auto desc
        = serio::CopyAt<Desc>(bytes, serio::ByteOffset { offset });
      if (desc && desc->size_bytes != 0U) {
        EditRecord<Desc>(bytes, offset, edit);
        return;
      }
    }
    throw std::invalid_argument("the table holds no payload");
  }

  //! Lets `edit` change the last texture row of the pak that holds a payload.
  template <class Edit>
  auto EditLastTextureRow(std::vector<std::byte>& pak, const Edit& edit) -> void
  {
    EditLastPayloadRow<core::TextureResourceDesc>(
      pak, ReadFooter(pak).texture_table, edit);
  }

  //! Sets the kind byte of the first resource binding of the reference
  //! block `references` locates to a kind no format defines.
  auto BreakFirstBindingKind(std::vector<std::byte>& bytes,
    const core::AssetReferenceTable& references) -> bool
  {
    if (references.resource_count == 0U) {
      return false;
    }
    // A binding starts with its kind byte; zero is no `ResourceKind`.
    bytes.at(static_cast<size_t>(references.offset)) = std::byte { 0U };
    return true;
  }

  //! Where the first browse path of the pak starts in its bytes.
  auto FirstBrowsePath(const std::vector<std::byte>& pak) -> size_t
  {
    const auto browse_offset
      = static_cast<size_t>(ReadFooter(pak).browse_index_offset);
    const auto header = serio::CopyAt<core::PakBrowseIndexHeader>(
      pak, serio::ByteOffset { browse_offset });
    if (!header || header->entry_count == 0U) {
      throw std::invalid_argument("the pak browses no asset");
    }
    const auto entry_offset
      = browse_offset + sizeof(core::PakBrowseIndexHeader);
    const auto entry = serio::CopyAt<core::PakBrowseIndexEntry>(
      pak, serio::ByteOffset { entry_offset });
    if (!entry || entry->virtual_path_length < 2U) {
      throw std::invalid_argument("the pak's browse index is malformed");
    }
    const auto strings = entry_offset
      + (size_t { header->entry_count } * sizeof(core::PakBrowseIndexEntry))
      + (size_t { header->texture_entry_count }
        * sizeof(core::PakBrowseIndexTextureEntry));
    return strings + entry->virtual_path_offset;
  }

} // namespace

auto CookedBytes::SetPakVersion(
  std::vector<std::byte>& pak, const uint16_t version) -> void
{
  EditHeader(pak,
    [version](core::PakHeader& header) -> void { header.version = version; });
}

auto CookedBytes::BreakPakMagic(std::vector<std::byte>& pak) -> void
{
  EditHeader(
    pak, [](core::PakHeader& header) -> void { header.magic[0] = 'X'; });
}

auto CookedBytes::BreakFooterMagic(std::vector<std::byte>& pak) -> void
{
  EditFooter(
    pak, [](core::PakFooter& footer) -> void { footer.footer_magic[0] = 'X'; });
}

auto CookedBytes::SetHeaderIdentity(
  std::vector<std::byte>& pak, const data::SourceKey& key) -> void
{
  EditHeader(pak, [&key](core::PakHeader& header) -> void {
    const auto bytes = data::as_bytes(key);
    std::memcpy(header.source_identity.data(), bytes.data(), bytes.size());
  });
}

auto CookedBytes::DropBrowseIndex(std::vector<std::byte>& pak) -> void
{
  EditFooter(pak, [](core::PakFooter& footer) -> void {
    footer.browse_index_offset = 0U;
    footer.browse_index_size = 0U;
  });
}

auto CookedBytes::OverrunCatalog(std::vector<std::byte>& pak) -> void
{
  const auto footer_offset = FooterOffset(pak);
  EditFooter(pak, [footer_offset](core::PakFooter& footer) -> void {
    footer.catalog_size = footer_offset - footer.catalog_offset + 1U;
  });
}

auto CookedBytes::DropCatalog(std::vector<std::byte>& pak) -> void
{
  EditFooter(
    pak, [](core::PakFooter& footer) -> void { footer.catalog_size = 0U; });
}

auto CookedBytes::MoveDirectoryOutside(std::vector<std::byte>& pak) -> void
{
  const auto size = pak.size();
  EditFooter(pak, [size](core::PakFooter& footer) -> void {
    footer.directory_offset = size + 1U;
  });
}

auto CookedBytes::MisalignTextureRegion(std::vector<std::byte>& pak) -> void
{
  EditFooter(pak, [](core::PakFooter& footer) -> void {
    if (footer.texture_region.size == 0U) {
      throw std::invalid_argument("the pak holds no texture payloads");
    }
    ++footer.texture_region.offset;
  });
}

auto CookedBytes::SetHeaderReserved(std::vector<std::byte>& pak) -> void
{
  EditHeader(pak, [](core::PakHeader& header) -> void {
    std::span { header._reserved }.front() = 1U;
  });
}

auto CookedBytes::SetFooterReserved(std::vector<std::byte>& pak) -> void
{
  EditFooter(pak, [](core::PakFooter& footer) -> void {
    std::span { footer._reserved }.front() = 1U;
  });
}

auto CookedBytes::SetDirectoryEntryReserved(std::vector<std::byte>& pak) -> void
{
  EditDirectoryEntry(pak, 0U, [](core::AssetDirectoryEntry& entry) -> void {
    std::span { entry._reserved }.front() = 1U;
  });
}

auto CookedBytes::SetDirectoryEntryOffset(std::vector<std::byte>& pak,
  const size_t entry, const uint64_t offset) -> void
{
  EditDirectoryEntry(
    pak, entry, [offset](core::AssetDirectoryEntry& record) -> void {
      record.entry_offset = offset;
    });
}

auto CookedBytes::MoveDescriptorOntoDirectory(std::vector<std::byte>& pak)
  -> void
{
  const auto directory_offset = ReadFooter(pak).directory_offset;
  EditDirectoryEntry(
    pak, 0U, [directory_offset](core::AssetDirectoryEntry& record) -> void {
      record.desc_offset = directory_offset;
    });
}

auto CookedBytes::DisplaceReferenceInventory(std::vector<std::byte>& pak)
  -> void
{
  EditDirectoryEntry(pak, 0U, [](core::AssetDirectoryEntry& record) -> void {
    if (record.references.resource_count == 0U
      && record.references.key_count == 0U) {
      throw std::invalid_argument("the first asset holds no references");
    }
    ++record.references.offset;
  });
}

auto CookedBytes::ShareReferenceInventory(std::vector<std::byte>& pak) -> void
{
  const auto first = ReadDirectoryEntry(pak, 0U).references;
  if (first.resource_count == 0U && first.key_count == 0U) {
    throw std::invalid_argument("the first asset holds no references");
  }
  EditDirectoryEntry(
    pak, 1U, [&first](core::AssetDirectoryEntry& record) -> void {
      if (record.references.resource_count == 0U
        && record.references.key_count == 0U) {
        throw std::invalid_argument("the second asset holds no references");
      }
      record.references.offset = first.offset;
    });
}

auto CookedBytes::LocateEmptyReferenceInventory(std::vector<std::byte>& pak)
  -> void
{
  EditDirectoryEntry(pak, 0U, [](core::AssetDirectoryEntry& record) -> void {
    if (record.references.resource_count != 0U
      || record.references.key_count != 0U) {
      throw std::invalid_argument("the first asset holds references");
    }
    record.references.offset = 1U;
  });
}

auto CookedBytes::SetBrowseTexturePathOffset(
  std::vector<std::byte>& browse, const uint32_t offset) -> void
{
  EditRecord<core::PakBrowseIndexTextureEntry>(browse,
    FirstBrowseTexture(browse),
    [offset](core::PakBrowseIndexTextureEntry& entry) -> void {
      entry.virtual_path_offset = offset;
    });
}

auto CookedBytes::AliasBrowseTextureToFirstEntry(std::vector<std::byte>& browse)
  -> void
{
  const auto header = serio::CopyAt<core::PakBrowseIndexHeader>(browse);
  if (!header || header->entry_count == 0U) {
    throw std::invalid_argument("the browse index holds no asset");
  }
  const auto asset = serio::CopyAt<core::PakBrowseIndexEntry>(
    browse, serio::ByteOffset { sizeof(core::PakBrowseIndexHeader) });
  if (!asset) {
    throw std::invalid_argument("the browse index is malformed");
  }
  EditRecord<core::PakBrowseIndexTextureEntry>(browse,
    FirstBrowseTexture(browse),
    [&asset](core::PakBrowseIndexTextureEntry& entry) -> void {
      entry.virtual_path_offset = asset->virtual_path_offset;
      entry.virtual_path_length = asset->virtual_path_length;
    });
}

auto CookedBytes::OverlapAudioRegionWithTextures(std::vector<std::byte>& pak)
  -> void
{
  EditFooter(pak, [](core::PakFooter& footer) -> void {
    if (footer.texture_region.size == 0U) {
      throw std::invalid_argument("the pak holds no texture payloads");
    }
    footer.audio_region = footer.texture_region;
  });
}

auto CookedBytes::GrowTextureEntrySize(std::vector<std::byte>& pak) -> void
{
  EditFooter(pak, [](core::PakFooter& footer) -> void {
    if (footer.texture_table.count == 0U) {
      throw std::invalid_argument("the pak holds no texture table");
    }
    ++footer.texture_table.entry_size;
  });
}

auto CookedBytes::BreakReferenceKind(std::vector<std::byte>& pak) -> void
{
  const auto footer = ReadFooter(pak);
  for (uint64_t i = 0; i < footer.asset_count; ++i) {
    const auto offset
      = footer.directory_offset + (i * sizeof(core::AssetDirectoryEntry));
    const auto entry = serio::CopyAt<core::AssetDirectoryEntry>(
      pak, serio::ByteOffset { offset });
    if (entry && BreakFirstBindingKind(pak, entry->references)) {
      return;
    }
  }
  throw std::invalid_argument("no asset of the pak binds a resource");
}

auto CookedBytes::ClearTextureDigest(std::vector<std::byte>& pak) -> void
{
  EditLastTextureRow(
    pak, [](core::TextureResourceDesc& row) -> void { row.content_hash = {}; });
}

auto CookedBytes::MoveTexturePayloadOutsideRegion(std::vector<std::byte>& pak)
  -> void
{
  const auto region = ReadFooter(pak).texture_region;
  EditLastTextureRow(pak, [region](core::TextureResourceDesc& row) -> void {
    row.data_offset = region.offset + region.size;
  });
}

auto CookedBytes::MisalignTexturePayload(std::vector<std::byte>& pak) -> void
{
  EditLastTextureRow(
    pak, [](core::TextureResourceDesc& row) -> void { ++row.data_offset; });
}

auto CookedBytes::BreakBrowsePathUtf8(std::vector<std::byte>& pak) -> void
{
  // A lone 0xFF starts no UTF-8 sequence.
  pak.at(FirstBrowsePath(pak) + 1U) = std::byte { 0xFFU };
}

auto CookedBytes::BreakBrowsePathDotSegment(std::vector<std::byte>& pak) -> void
{
  const auto path = FirstBrowsePath(pak);
  // The path is `/<Mount>/<Segment>/...`; skip the mount to the segment.
  auto segment = path + 1U;
  while (pak.at(segment) != std::byte { '/' }) {
    ++segment;
  }
  ++segment;
  pak.at(segment) = std::byte { '.' };
  pak.at(segment + 1U) = std::byte { '.' };
  pak.at(segment + 2U) = std::byte { '/' };
}

auto CookedBytes::SetTextureCompression(
  std::vector<std::byte>& pak, const uint8_t compression) -> void
{
  EditLastTextureRow(
    pak, [compression](core::TextureResourceDesc& row) -> void {
      row.compression_type = compression;
    });
}

auto CookedBytes::ChangeBrowseMount(std::vector<std::byte>& pak) -> void
{
  // The mount starts after the path's leading '/'.
  auto& first = pak.at(FirstBrowsePath(pak) + 1U);
  first = first == std::byte { 'Z' } ? std::byte { 'Y' } : std::byte { 'Z' };
}

auto CookedBytes::SetBrowseAssetKey(
  std::vector<std::byte>& pak, const data::AssetKey& key) -> void
{
  const auto browse_offset
    = static_cast<size_t>(ReadFooter(pak).browse_index_offset);
  EditRecord<core::PakBrowseIndexEntry>(pak,
    browse_offset + sizeof(core::PakBrowseIndexHeader),
    [&key](
      core::PakBrowseIndexEntry& entry) -> void { entry.asset_key = key; });
}

auto CookedBytes::SetCrc(std::vector<std::byte>& pak, const uint32_t crc)
  -> void
{
  EditFooter(
    pak, [crc](core::PakFooter& footer) -> void { footer.pak_crc32 = crc; });
}

auto CookedBytes::CorruptCrc(std::vector<std::byte>& pak) -> void
{
  EditFooter(pak, [](core::PakFooter& footer) -> void {
    footer.pak_crc32 = ~footer.pak_crc32;
  });
}

auto CookedBytes::CrcFieldOffset(const std::vector<std::byte>& pak) -> size_t
{
  return FooterOffset(pak) + offsetof(core::PakFooter, pak_crc32);
}

auto CookedBytes::SetIndexVersion(
  LooseCookedFiles& files, const uint16_t version) -> void
{
  EditRecord<lc::IndexHeader>(Index(files), 0U,
    [version](lc::IndexHeader& header) -> void { header.version = version; });
}

auto CookedBytes::BreakIndexMagic(LooseCookedFiles& files) -> void
{
  EditRecord<lc::IndexHeader>(Index(files), 0U,
    [](lc::IndexHeader& header) -> void { header.magic.front() = 'X'; });
}

auto CookedBytes::SetIndexReserved(LooseCookedFiles& files) -> void
{
  EditRecord<lc::IndexHeader>(
    Index(files), 0U, [](lc::IndexHeader& header) -> void {
      std::span { header._reserved }.front() = 1U;
    });
}

auto CookedBytes::BreakIndexReferenceKind(LooseCookedFiles& files) -> void
{
  auto& index = Index(files);
  const auto header = serio::CopyAt<lc::IndexHeader>(index);
  if (!header) {
    throw std::invalid_argument("the index is too small for its header");
  }
  for (uint32_t i = 0; i < header->asset_count; ++i) {
    const auto offset = header->asset_entries_offset
      + (uint64_t { i } * sizeof(lc::AssetEntry));
    const auto entry
      = serio::CopyAt<lc::AssetEntry>(index, serio::ByteOffset { offset });
    if (entry && BreakFirstBindingKind(index, entry->references)) {
      return;
    }
  }
  throw std::invalid_argument("no indexed asset binds a resource");
}

auto CookedBytes::GivePartialRow(
  LooseCookedFiles& files, const lc::FileKind table) -> void
{
  auto& index = Index(files);
  const auto header = serio::CopyAt<lc::IndexHeader>(index);
  if (!header) {
    throw std::invalid_argument("the index is too small for its header");
  }
  for (uint32_t i = 0; i < header->file_record_count; ++i) {
    const auto offset = static_cast<size_t>(
      header->file_records_offset + (uint64_t { i } * sizeof(lc::FileRecord)));
    const auto record
      = serio::CopyAt<lc::FileRecord>(index, serio::ByteOffset { offset });
    if (record && record->kind == table) {
      EditRecord<lc::FileRecord>(
        index, offset, [](lc::FileRecord& file) -> void { ++file.size; });
      return;
    }
  }
  throw std::invalid_argument("the index records no such table");
}

auto CookedBytes::Index(LooseCookedFiles& files) -> std::vector<std::byte>&
{
  const auto it = files.find(
    std::string(content::import::internal::loose_layout::kIndexFileName));
  if (it == files.end()) {
    throw std::invalid_argument("the loose cooked root has no index");
  }
  return it->second;
}

} // namespace oxygen::cooker::testing
