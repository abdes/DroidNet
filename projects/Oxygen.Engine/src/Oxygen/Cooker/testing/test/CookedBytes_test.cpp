//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <Oxygen/Content/PakFile.h>
#include <Oxygen/Cooker/testing/CookedBytes.h>
#include <Oxygen/Cooker/testing/CookedPak.h>
#include <Oxygen/Cooker/testing/LooseCookedRoot.h>
#include <Oxygen/Cooker/testing/MaterialBuilder.h>
#include <Oxygen/Cooker/testing/Seeds.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/LooseCookedIndexFormat.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Serio/AnyDevice.h>
#include <Oxygen/Serio/ByteReader.h>
#include <Oxygen/Serio/MemoryFile.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using oxygen::content::PakFile;
using oxygen::cooker::testing::CookedBytes;
using oxygen::cooker::testing::CookedPak;
using oxygen::cooker::testing::EncodeBrowseIndex;
using oxygen::cooker::testing::LooseCookedFiles;
using oxygen::cooker::testing::LooseCookedRoot;
using oxygen::cooker::testing::MaterialBuilder;
using oxygen::cooker::testing::SeedBytes;
using oxygen::cooker::testing::SourceKeyOf;
namespace core = oxygen::data::pak::core;
namespace lc = oxygen::data::loose_cooked;

//! A root of a material that binds a texture, and the texture.
auto TexturedRoot() -> LooseCookedRoot
{
  auto game = LooseCookedRoot { "Game" };
  const auto texture = game.AddTexture(SeedBytes(0x5AU));
  auto wood = MaterialBuilder { "Wood" };
  wood.Texture(
    &oxygen::data::pak::render::MaterialAssetDesc::base_color_texture, texture);
  static_cast<void>(game.Material("Materials/Wood.omat", wood));
  return game;
}

//! A valid pak of a material that binds a texture, and the texture.
auto ValidPak() -> std::vector<std::byte>
{
  return CookedPak { TexturedRoot() }.Encode();
}

//! The bytes a defect may change: `size` bytes at `offset`.
struct Field final {
  size_t offset = 0;
  size_t size = 0;
};

//! The record `T` at `offset` of `bytes`, read independently of the builder.
template <class T>
auto RecordAt(const std::vector<std::byte>& bytes, const uint64_t offset) -> T
{
  const auto record
    = oxygen::serio::CopyAt<T>(bytes, oxygen::serio::ByteOffset { offset });
  if (!record) {
    throw std::logic_error("the bytes are too small for the record");
  }
  return *record;
}

auto FooterOffset(const std::vector<std::byte>& pak) -> size_t
{
  return pak.size() - sizeof(core::PakFooter);
}

auto FooterOf(const std::vector<std::byte>& pak) -> core::PakFooter
{
  return RecordAt<core::PakFooter>(pak, FooterOffset(pak));
}

//! The field at `field_offset`, of `size` bytes, of the pak footer.
auto FooterField(const std::vector<std::byte>& pak, const size_t field_offset,
  const size_t size) -> Field
{
  return { .offset = FooterOffset(pak) + field_offset, .size = size };
}

//! The field at `field_offset`, of `size` bytes, of the last texture row that
//! holds a payload.
auto LastTextureRowField(const std::vector<std::byte>& pak,
  const size_t field_offset, const size_t size) -> Field
{
  const auto table = FooterOf(pak).texture_table;
  for (auto row = uint64_t { table.count }; row-- > 0U;) {
    const auto offset
      = table.offset + (row * sizeof(core::TextureResourceDesc));
    if (RecordAt<core::TextureResourceDesc>(pak, offset).size_bytes != 0U) {
      return {
        .offset = static_cast<size_t>(offset) + field_offset,
        .size = size,
      };
    }
  }
  throw std::logic_error("the pak holds no texture payload");
}

//! The field at `field_offset`, of `size` bytes, of directory entry `entry`.
auto EntryField(const std::vector<std::byte>& pak, const size_t entry,
  const size_t field_offset, const size_t size) -> Field
{
  return {
    .offset = static_cast<size_t>(FooterOf(pak).directory_offset)
      + (entry * sizeof(core::AssetDirectoryEntry)) + field_offset,
    .size = size,
  };
}

//! The first byte of the first reference inventory that binds a resource.
auto FirstBindingField(const std::vector<std::byte>& pak) -> Field
{
  const auto footer = FooterOf(pak);
  for (uint64_t i = 0; i < footer.asset_count; ++i) {
    const auto entry = RecordAt<core::AssetDirectoryEntry>(
      pak, footer.directory_offset + (i * sizeof(core::AssetDirectoryEntry)));
    if (entry.references.resource_count != 0U) {
      return {
        .offset = static_cast<size_t>(entry.references.offset),
        .size = 1U,
      };
    }
  }
  throw std::logic_error("no asset of the pak binds a resource");
}

//! The browse index section, which holds every browse path.
auto BrowseSection(const std::vector<std::byte>& pak) -> Field
{
  const auto footer = FooterOf(pak);
  return {
    .offset = static_cast<size_t>(footer.browse_index_offset),
    .size = static_cast<size_t>(footer.browse_index_size),
  };
}

//! Whether `after` differs from `before`, and only within `field`.
auto ChangedOnlyWithin(const std::vector<std::byte>& before,
  const std::vector<std::byte>& after, const Field& field) -> bool
{
  if (before.size() != after.size()) {
    return false;
  }
  auto changed = false;
  for (size_t i = 0; i < before.size(); ++i) {
    if (before.at(i) == after.at(i)) {
      continue;
    }
    const auto in_field = i >= field.offset && i - field.offset < field.size;
    if (!in_field) {
      return false;
    }
    changed = true;
  }
  return changed;
}

struct PakEdit final {
  std::string name;
  std::function<void(std::vector<std::byte>&)> apply;
  //! The field of the valid pak the edit names.
  std::function<Field(const std::vector<std::byte>&)> field;
};

class CookedBytesPakTest : public ::testing::TestWithParam<PakEdit> { };

//! Each pak defect changes the field it names and nothing else, so a test of
//! the rule that field carries tests that rule alone.
NOLINT_TEST_P(CookedBytesPakTest, DefectChangesOnlyItsField)
{
  const auto valid = ValidPak();
  auto broken = valid;

  GetParam().apply(broken);

  EXPECT_TRUE(ChangedOnlyWithin(valid, broken, GetParam().field(valid)));
}

INSTANTIATE_TEST_SUITE_P(Defects, CookedBytesPakTest,
  ::testing::Values(
    PakEdit {
      .name = "PakVersion",
      .apply = [](std::vector<std::byte>& bytes) -> void {
        CookedBytes::SetPakVersion(
          bytes, static_cast<uint16_t>(core::kCurrentPakFormatVersion + 1U));
      },
      .field = [](const std::vector<std::byte>&) -> Field {
        return { .offset = offsetof(core::PakHeader, version),
          .size = sizeof(uint16_t) };
      },
    },
    PakEdit {
      .name = "HeaderMagic",
      .apply = &CookedBytes::BreakPakMagic,
      .field = [](const std::vector<std::byte>&) -> Field {
        return { .offset = offsetof(core::PakHeader, magic), .size = 8U };
      },
    },
    PakEdit {
      .name = "HeaderIdentity",
      .apply = [](std::vector<std::byte>& bytes) -> void {
        CookedBytes::SetHeaderIdentity(bytes, SourceKeyOf(0xEEU));
      },
      .field = [](const std::vector<std::byte>&) -> Field {
        return { .offset = offsetof(core::PakHeader, source_identity),
          .size = 16U };
      },
    },
    PakEdit {
      .name = "HeaderReserved",
      .apply = &CookedBytes::SetHeaderReserved,
      .field = [](const std::vector<std::byte>&) -> Field {
        return { .offset = offsetof(core::PakHeader, _reserved),
          .size = sizeof(core::PakHeader::_reserved) };
      },
    },
    PakEdit {
      .name = "FooterMagic",
      .apply = &CookedBytes::BreakFooterMagic,
      .field = [](const std::vector<std::byte>& bytes) -> Field {
        return FooterField(bytes, offsetof(core::PakFooter, footer_magic), 8U);
      },
    },
    PakEdit {
      .name = "FooterReserved",
      .apply = &CookedBytes::SetFooterReserved,
      .field = [](const std::vector<std::byte>& bytes) -> Field {
        return FooterField(bytes, offsetof(core::PakFooter, _reserved),
          sizeof(core::PakFooter::_reserved));
      },
    },
    PakEdit {
      .name = "NoBrowseIndex",
      .apply = &CookedBytes::DropBrowseIndex,
      .field = [](const std::vector<std::byte>& bytes) -> Field {
        return FooterField(bytes,
          offsetof(core::PakFooter, browse_index_offset),
          2U * sizeof(uint64_t));
      },
    },
    PakEdit {
      .name = "NoCatalog",
      .apply = &CookedBytes::DropCatalog,
      .field = [](const std::vector<std::byte>& bytes) -> Field {
        return FooterField(bytes, offsetof(core::PakFooter, catalog_offset),
          2U * sizeof(uint64_t));
      },
    },
    PakEdit {
      .name = "CatalogOverrun",
      .apply = &CookedBytes::OverrunCatalog,
      .field = [](const std::vector<std::byte>& bytes) -> Field {
        return FooterField(bytes, offsetof(core::PakFooter, catalog_offset),
          2U * sizeof(uint64_t));
      },
    },
    PakEdit {
      .name = "DirectoryOutside",
      .apply = &CookedBytes::MoveDirectoryOutside,
      .field = [](const std::vector<std::byte>& bytes) -> Field {
        return FooterField(
          bytes, offsetof(core::PakFooter, directory_offset), sizeof(uint64_t));
      },
    },
    PakEdit {
      .name = "MisalignedTextureRegion",
      .apply = &CookedBytes::MisalignTextureRegion,
      .field = [](const std::vector<std::byte>& bytes) -> Field {
        return FooterField(
          bytes, offsetof(core::PakFooter, texture_region), sizeof(uint64_t));
      },
    },
    PakEdit {
      .name = "OverlappingRegions",
      .apply = &CookedBytes::OverlapAudioRegionWithTextures,
      .field = [](const std::vector<std::byte>& bytes) -> Field {
        return FooterField(bytes, offsetof(core::PakFooter, audio_region),
          sizeof(core::ResourceRegion));
      },
    },
    PakEdit {
      .name = "TableEntrySize",
      .apply = &CookedBytes::GrowTextureEntrySize,
      .field = [](const std::vector<std::byte>& bytes) -> Field {
        return FooterField(bytes,
          offsetof(core::PakFooter, texture_table)
            + offsetof(core::ResourceTable, entry_size),
          sizeof(uint32_t));
      },
    },
    PakEdit {
      .name = "Crc",
      .apply = &CookedBytes::CorruptCrc,
      .field = [](const std::vector<std::byte>& bytes) -> Field {
        return FooterField(
          bytes, offsetof(core::PakFooter, pak_crc32), sizeof(uint32_t));
      },
    },
    PakEdit {
      .name = "DirectoryEntryReserved",
      .apply = &CookedBytes::SetDirectoryEntryReserved,
      .field = [](const std::vector<std::byte>& bytes) -> Field {
        return { .offset = static_cast<size_t>(FooterOf(bytes).directory_offset)
            + offsetof(core::AssetDirectoryEntry, _reserved),
          .size = sizeof(core::AssetDirectoryEntry::_reserved) };
      },
    },
    PakEdit {
      .name = "DirectoryEntryOffset",
      .apply = [](std::vector<std::byte>& bytes) -> void {
        CookedBytes::SetDirectoryEntryOffset(bytes, 0U, 0U);
      },
      .field = [](const std::vector<std::byte>& bytes) -> Field {
        return EntryField(bytes, 0U,
          offsetof(core::AssetDirectoryEntry, entry_offset), sizeof(uint64_t));
      },
    },
    PakEdit {
      .name = "DescriptorOntoDirectory",
      .apply = &CookedBytes::MoveDescriptorOntoDirectory,
      .field = [](const std::vector<std::byte>& bytes) -> Field {
        return EntryField(bytes, 0U,
          offsetof(core::AssetDirectoryEntry, desc_offset), sizeof(uint64_t));
      },
    },
    PakEdit {
      .name = "DisplacedReferenceInventory",
      .apply = &CookedBytes::DisplaceReferenceInventory,
      .field = [](const std::vector<std::byte>& bytes) -> Field {
        return EntryField(bytes, 0U,
          offsetof(core::AssetDirectoryEntry, references)
            + offsetof(core::AssetReferenceTable, offset),
          sizeof(uint64_t));
      },
    },
    PakEdit {
      .name = "ReferenceKind",
      .apply = &CookedBytes::BreakReferenceKind,
      .field = &FirstBindingField,
    },
    PakEdit {
      .name = "TextureDigest",
      .apply = &CookedBytes::ClearTextureDigest,
      .field = [](const std::vector<std::byte>& bytes) -> Field {
        return LastTextureRowField(bytes,
          offsetof(core::TextureResourceDesc, content_hash),
          sizeof(core::ContentHashDigest));
      },
    },
    PakEdit {
      .name = "TexturePayloadOutsideRegion",
      .apply = &CookedBytes::MoveTexturePayloadOutsideRegion,
      .field = [](const std::vector<std::byte>& bytes) -> Field {
        return LastTextureRowField(bytes,
          offsetof(core::TextureResourceDesc, data_offset), sizeof(uint64_t));
      },
    },
    PakEdit {
      .name = "TexturePayloadOffBoundary",
      .apply = &CookedBytes::MisalignTexturePayload,
      .field = [](const std::vector<std::byte>& bytes) -> Field {
        return LastTextureRowField(bytes,
          offsetof(core::TextureResourceDesc, data_offset), sizeof(uint64_t));
      },
    },
    PakEdit {
      .name = "TextureCompression",
      .apply = [](std::vector<std::byte>& bytes) -> void {
        CookedBytes::SetTextureCompression(bytes, 0xEEU);
      },
      .field = [](const std::vector<std::byte>& bytes) -> Field {
        return LastTextureRowField(bytes,
          offsetof(core::TextureResourceDesc, compression_type),
          sizeof(uint8_t));
      },
    },
    PakEdit {
      .name = "BrowsePathUtf8",
      .apply = &CookedBytes::BreakBrowsePathUtf8,
      .field = &BrowseSection,
    },
    PakEdit {
      .name = "BrowsePathDotSegment",
      .apply = &CookedBytes::BreakBrowsePathDotSegment,
      .field = &BrowseSection,
    },
    PakEdit {
      .name = "BrowseMount",
      .apply = &CookedBytes::ChangeBrowseMount,
      .field = &BrowseSection,
    },
    PakEdit {
      .name = "BrowseAssetKey",
      .apply = [](std::vector<std::byte>& bytes) -> void {
        CookedBytes::SetBrowseAssetKey(
          bytes, oxygen::data::AssetKey::FromVirtualPath("/Game/Absent.omat"));
      },
      .field = [](const std::vector<std::byte>& bytes) -> Field {
        return { .offset = BrowseSection(bytes).offset
            + sizeof(core::PakBrowseIndexHeader)
            + offsetof(core::PakBrowseIndexEntry, asset_key),
          .size = oxygen::data::AssetKey::kSizeBytes };
      },
    }),
  [](const ::testing::TestParamInfo<PakEdit>& info) -> std::string {
    return info.param.name;
  });

//! A stale CRC opens, and fails only the integrity check.
NOLINT_TEST(CookedBytesTest, CorruptCrcFailsOnlyTheIntegrityCheck)
{
  auto bytes = ValidPak();

  CookedBytes::CorruptCrc(bytes);

  auto memory = oxygen::serio::MemoryFile::FromBytes(bytes);
  ASSERT_HAS_VALUE(memory);
  const auto pak
    = PakFile(oxygen::serio::AnyDevice(std::move(*memory)), "defect.pak");
  EXPECT_THROW(pak.ValidateCrc32Integrity(), std::exception);
}

//! The CRC field lies in the footer, at the field the footer declares.
NOLINT_TEST(CookedBytesTest, CrcFieldIsTheFootersField)
{
  const auto bytes = ValidPak();

  EXPECT_EQ(CookedBytes::CrcFieldOffset(bytes),
    bytes.size() - sizeof(core::PakFooter)
      + offsetof(core::PakFooter, pak_crc32));
}

//! The reference inventory offset of directory entry `entry`.
auto InventoryOffsetField(const std::vector<std::byte>& pak, const size_t entry)
  -> Field
{
  return EntryField(pak, entry,
    offsetof(core::AssetDirectoryEntry, references)
      + offsetof(core::AssetReferenceTable, offset),
    sizeof(uint64_t));
}

//! Sharing an inventory changes only the second asset's inventory offset.
NOLINT_TEST(CookedBytesTest, ShareReferenceInventoryChangesOnlyItsField)
{
  auto game = TexturedRoot();
  auto stone = MaterialBuilder { "Stone" };
  stone.Texture(
    &oxygen::data::pak::render::MaterialAssetDesc::base_color_texture,
    game.AddTexture(SeedBytes(0x6BU)));
  static_cast<void>(game.Material("Materials/Stone.omat", stone));
  const auto valid = CookedPak { game }.Encode();
  auto broken = valid;

  CookedBytes::ShareReferenceInventory(broken);

  EXPECT_TRUE(
    ChangedOnlyWithin(valid, broken, InventoryOffsetField(valid, 1U)));
}

//! Locating an empty inventory changes only its offset.
NOLINT_TEST(CookedBytesTest, LocateEmptyReferenceInventoryChangesOnlyItsField)
{
  auto game = LooseCookedRoot { "Game" };
  static_cast<void>(game.Material("Materials/Plain.omat"));
  const auto valid = CookedPak { game }.Encode();
  auto broken = valid;

  CookedBytes::LocateEmptyReferenceInventory(broken);

  EXPECT_TRUE(
    ChangedOnlyWithin(valid, broken, InventoryOffsetField(valid, 0U)));
}

//=== Browse indexes ===----------------------------------------------------//

//! A browse index of one asset and one texture, encoded by the product.
auto ValidBrowseIndex() -> std::vector<std::byte>
{
  return EncodeBrowseIndex({
    .entries = { {
      .asset_key = oxygen::data::AssetKey::FromVirtualPath("/Game/A.omat"),
      .virtual_path = "/Game/A.omat",
    }, },
    .textures = { {
      .texture_index = 1U,
      .virtual_path = "/Game/T.otex",
    }, },
  });
}

//! The path locator, offset then length, of the first browse texture entry.
auto BrowseTexturePathField(const size_t size) -> Field
{
  return {
    .offset = sizeof(core::PakBrowseIndexHeader)
      + sizeof(core::PakBrowseIndexEntry)
      + offsetof(core::PakBrowseIndexTextureEntry, virtual_path_offset),
    .size = size,
  };
}

//! Setting a texture's path offset changes only that offset.
NOLINT_TEST(CookedBytesTest, SetBrowseTexturePathOffsetChangesOnlyItsField)
{
  const auto valid = ValidBrowseIndex();
  auto broken = valid;

  CookedBytes::SetBrowseTexturePathOffset(broken, 0xFFFFU);

  EXPECT_TRUE(
    ChangedOnlyWithin(valid, broken, BrowseTexturePathField(sizeof(uint32_t))));
}

//! Aliasing a texture to the first asset changes only its path locator.
NOLINT_TEST(CookedBytesTest, AliasBrowseTextureChangesOnlyItsPathLocator)
{
  const auto valid = ValidBrowseIndex();
  auto broken = valid;

  CookedBytes::AliasBrowseTextureToFirstEntry(broken);

  EXPECT_TRUE(ChangedOnlyWithin(
    valid, broken, BrowseTexturePathField(2U * sizeof(uint32_t))));
}

//=== Loose cooked roots ===------------------------------------------------//

//! The field at `field_offset`, of `size` bytes, of the index header.
auto IndexHeaderField(const size_t field_offset, const size_t size) -> Field
{
  return { .offset = field_offset, .size = size };
}

struct IndexEdit final {
  std::string name;
  std::function<void(LooseCookedFiles&)> apply;
  //! The field of the valid index the edit names.
  std::function<Field(const std::vector<std::byte>&)> field;
};

class CookedBytesIndexTest : public ::testing::TestWithParam<IndexEdit> { };

//! Each index defect changes the field it names and nothing else, in the
//! index alone.
NOLINT_TEST_P(CookedBytesIndexTest, DefectChangesOnlyItsField)
{
  const auto valid = TexturedRoot().Files();
  auto broken = valid;

  GetParam().apply(broken);

  const auto index_name = std::string(lc::kIndexFileName);
  const auto& valid_index = valid.at(index_name);
  EXPECT_TRUE(ChangedOnlyWithin(
    valid_index, broken.at(index_name), GetParam().field(valid_index)));
  for (const auto& [relpath, file] : valid) {
    if (relpath != index_name) {
      EXPECT_EQ(broken.at(relpath), file) << relpath;
    }
  }
}

INSTANTIATE_TEST_SUITE_P(Defects, CookedBytesIndexTest,
  ::testing::Values(IndexEdit {
                      .name = "IndexVersion",
                      .apply = [](LooseCookedFiles& files) -> void {
                        CookedBytes::SetIndexVersion(
                          files, static_cast<uint16_t>(lc::kIndexVersion + 1U));
                      },
                      .field = [](const std::vector<std::byte>&) -> Field {
                        return IndexHeaderField(
                          offsetof(lc::IndexHeader, version), sizeof(uint16_t));
                      },
                    },
    IndexEdit {
      .name = "IndexMagic",
      .apply = &CookedBytes::BreakIndexMagic,
      .field = [](const std::vector<std::byte>&) -> Field {
        return IndexHeaderField(offsetof(lc::IndexHeader, magic), 8U);
      },
    },
    IndexEdit {
      .name = "IndexReserved",
      .apply = &CookedBytes::SetIndexReserved,
      .field = [](const std::vector<std::byte>&) -> Field {
        return IndexHeaderField(offsetof(lc::IndexHeader, _reserved),
          sizeof(lc::IndexHeader::_reserved));
      },
    },
    IndexEdit {
      .name = "IndexReferenceKind",
      .apply = &CookedBytes::BreakIndexReferenceKind,
      .field = [](const std::vector<std::byte>& index) -> Field {
        // The reference blocks are the tail after the file records.
        const auto header = RecordAt<lc::IndexHeader>(index, 0U);
        const auto tail = header.file_records_offset
          + (uint64_t { header.file_record_count } * sizeof(lc::FileRecord));
        return { .offset = static_cast<size_t>(tail),
          .size = index.size() - static_cast<size_t>(tail) };
      },
    },
    IndexEdit {
      .name = "PartialRow",
      .apply = [](LooseCookedFiles& files) -> void {
        CookedBytes::GivePartialRow(files, lc::FileKind::kTexturesTable);
      },
      .field = [](const std::vector<std::byte>& index) -> Field {
        const auto header = RecordAt<lc::IndexHeader>(index, 0U);
        return { .offset = static_cast<size_t>(header.file_records_offset),
          .size = header.file_record_count * sizeof(lc::FileRecord) };
      },
    }),
  [](const ::testing::TestParamInfo<IndexEdit>& info) -> std::string {
    return info.param.name;
  });

} // namespace
