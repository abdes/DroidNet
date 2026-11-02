//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//
// Covers: Content/PakFile.h

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include <Oxygen/Base/Crc32.h>
#include <Oxygen/Content/PakFile.h>
#include <Oxygen/Cooker/testing/CookedBytes.h>
#include <Oxygen/Cooker/testing/CookedPak.h>
#include <Oxygen/Cooker/testing/LooseCookedRoot.h>
#include <Oxygen/Cooker/testing/MaterialBuilder.h>
#include <Oxygen/Cooker/testing/Materialize.h>
#include <Oxygen/Cooker/testing/Seeds.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/BufferResource.h>
#include <Oxygen/Data/CookedOpenError.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/PakFormat_render.h>
#include <Oxygen/Data/PhysicsResource.h>
#include <Oxygen/Data/ScriptResource.h>
#include <Oxygen/Data/TextureResource.h>
#include <Oxygen/Serio/AnyDevice.h>
#include <Oxygen/Serio/MemoryFile.h>
#include <Oxygen/Serio/Testing/FaultInjectingDevice.h>
#include <Oxygen/Serio/Testing/TestBytes.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Testing/ScopedTempDir.h>

namespace {

using oxygen::content::PakFile;
using oxygen::cooker::testing::CookedBytes;
using oxygen::cooker::testing::CookedPak;
using oxygen::cooker::testing::EncodeBrowseIndex;
using oxygen::cooker::testing::LooseCookedRoot;
using oxygen::cooker::testing::Materialize;
using oxygen::cooker::testing::SeedBytes;
using oxygen::cooker::testing::SourceKeyOf;
using oxygen::data::CookedDefect;
using oxygen::data::CookedOpenError;
using oxygen::serio::testing::BytesEqual;

constexpr auto kMount = "Tests";
constexpr auto kMaterialLeaf = "A.omat";
constexpr auto kMaterialPath = "/Tests/A.omat";
constexpr auto kTexturePath = "/Tests/T.otex";
constexpr auto kUnknownFormatVersion = uint16_t { 99U };
constexpr auto kOtherIdentitySeed = uint8_t { 0xEEU };

//! Opens the pak `bytes` hold, from memory.
auto Open(const std::vector<std::byte>& bytes) -> PakFile
{
  return { oxygen::serio::AnyDevice(
             oxygen::serio::MemoryFile::FromBytes(bytes).value()),
    "test.pak" };
}

//! The rule the pak `bytes` hold is refused for.
auto OpenDefect(const std::vector<std::byte>& bytes) -> CookedDefect
{
  try {
    std::ignore = Open(bytes);
  } catch (const CookedOpenError& error) {
    return error.Defect();
  }
  ADD_FAILURE() << "the pak was accepted";
  return {};
}

//! The CRC32 of `bytes` except the footer's CRC field, computed here
//! independently of Serio.
auto PlainCrc32(const std::vector<std::byte>& bytes) -> uint32_t
{
  const auto field = CookedBytes::CrcFieldOffset(bytes);
  auto crc = oxygen::base::Crc32 {};
  crc.Update(std::span(bytes).first(field));
  crc.Update(std::span(bytes).subspan(field + sizeof(uint32_t)));
  return crc.Finalize();
}

//! A root of the mount with one material, at `/Tests/A.omat`.
auto OneMaterial() -> LooseCookedRoot
{
  auto root = LooseCookedRoot { kMount };
  static_cast<void>(root.Material(kMaterialLeaf));
  return root;
}

//! The bytes of a valid pak listing one material.
auto ValidPak() -> std::vector<std::byte>
{
  return CookedPak { OneMaterial() }.Encode();
}

//! The bytes of a valid pak of a material that binds a texture, and the
//! texture: every section of a pak, and a reference inventory.
auto TexturedPak() -> std::vector<std::byte>
{
  auto root = LooseCookedRoot { kMount };
  const auto texture = root.AddTexture(SeedBytes(0x5AU));
  auto material = oxygen::cooker::testing::MaterialBuilder { "A" };
  material.Texture(
    &oxygen::data::pak::render::MaterialAssetDesc::base_color_texture, texture);
  static_cast<void>(root.Material(kMaterialLeaf, material));
  return CookedPak { root }.Encode();
}

//! A browse index of one asset, at `kMaterialPath`, and one texture, of row
//! one, at `kTexturePath`, encoded by the product.
auto BrowseIndex(const uint32_t texture_index = 1U) -> std::vector<std::byte>
{
  return EncodeBrowseIndex({
    .entries = { {
      .asset_key = oxygen::data::AssetKey::FromVirtualPath(kMaterialPath),
      .virtual_path = kMaterialPath,
    }, },
    .textures = { {
      .texture_index = texture_index,
      .virtual_path = kTexturePath,
    }, },
  });
}

//! The rule decoding the browse index `bytes`, of a pak of `textures`
//! texture rows, is refused for.
auto BrowseDefect(const std::vector<std::byte>& bytes, const uint32_t textures)
  -> CookedDefect
{
  try {
    std::ignore = PakFile::DecodeBrowseIndex(bytes, textures);
  } catch (const CookedOpenError& error) {
    return error.Defect();
  }
  ADD_FAILURE() << "the browse index was accepted";
  return {};
}

//! A root of the mount with two materials that bind the same texture, so
//! both hold a reference inventory.
auto TwoTexturedMaterials() -> LooseCookedRoot
{
  auto root = LooseCookedRoot { kMount };
  const auto texture = root.AddTexture(SeedBytes(0x5AU));
  auto material = oxygen::cooker::testing::MaterialBuilder { "A" };
  material.Texture(
    &oxygen::data::pak::render::MaterialAssetDesc::base_color_texture, texture);
  static_cast<void>(root.Material(kMaterialLeaf, material));
  static_cast<void>(root.Material("B.omat", material));
  return root;
}

NOLINT_TEST(PakFileTest, OpensItsMetadata)
{
  constexpr auto kContentVersion = uint16_t { 42U };
  const auto root = OneMaterial();
  const auto bytes
    = CookedPak { root }.ContentVersion(kContentVersion).Encode();

  const auto pak = Open(bytes);

  EXPECT_EQ(pak.FilePath(), "test.pak");
  EXPECT_EQ(
    pak.FormatVersion(), oxygen::data::pak::core::kCurrentPakFormatVersion);
  EXPECT_EQ(pak.ContentVersion(), kContentVersion);
  ASSERT_EQ(pak.Directory().size(), 1U);
  EXPECT_EQ(pak.Directory().front().asset_key, root.KeyOf(kMaterialLeaf));
  EXPECT_EQ(
    pak.ResolveAssetKeyByVirtualPath(kMaterialPath), root.KeyOf(kMaterialLeaf));
  EXPECT_EQ(pak.Catalog().entries.size(), 1U);
}

//! A pak opens from a file as it does from memory.
NOLINT_TEST(PakFileTest, OpensAFile)
{
  const auto dir = oxygen::testing::ScopedTempDir {};
  const auto path = dir.Path() / "test.pak";
  Materialize(ValidPak(), path);

  const auto pak = PakFile(path);

  EXPECT_EQ(pak.FilePath(), path);
  EXPECT_EQ(pak.Directory().size(), 1U);
}

NOLINT_TEST(PakFileTest, MissingFileThrows)
{
  const auto dir = oxygen::testing::ScopedTempDir {};

  try {
    static_cast<void>(PakFile(dir.Path() / "missing.pak"));
    FAIL() << "A missing pak was opened";
  } catch (const oxygen::data::CookedOpenError& error) {
    EXPECT_EQ(error.Defect(), oxygen::data::CookedDefect::kAbsent);
  }
}

NOLINT_TEST(PakFileTest, RefusesADeviceWithoutAView)
{
  auto device = oxygen::serio::AnyDevice(
    oxygen::serio::testing::FaultInjectingDevice(oxygen::serio::MemoryFile {}));

  NOLINT_EXPECT_THROW(
    PakFile(std::move(device), "test.pak"), std::invalid_argument);
}

//! Bytes too few to hold a pak header are no pak.
NOLINT_TEST(PakFileTest, RefusesTooFewBytesForAHeader)
{
  EXPECT_EQ(OpenDefect(std::vector<std::byte>(4)), CookedDefect::kTooSmall);
}

//! Every directory entry records its own position, not another row's.
NOLINT_TEST(PakFileTest, RefusesAnIncorrectDirectoryEntryOffset)
{
  // Arrange
  auto root = OneMaterial();
  static_cast<void>(root.Material("B.omat"));
  const auto valid = CookedPak { root }.Encode();
  const auto directory_offset = Open(valid).Footer().directory_offset;
  constexpr auto kEntrySize
    = sizeof(oxygen::data::pak::core::AssetDirectoryEntry);
  // Zero, the first row's position, one byte past the second's, and a value
  // whose position arithmetic would wrap.
  const auto offsets = std::array {
    uint64_t { 0U },
    directory_offset,
    directory_offset + kEntrySize + 1U,
    std::numeric_limits<uint64_t>::max(),
  };

  // Act & Assert
  for (const auto offset : offsets) {
    auto bytes = valid;
    CookedBytes::SetDirectoryEntryOffset(bytes, 1U, offset);
    EXPECT_EQ(OpenDefect(bytes), CookedDefect::kInconsistent) << offset;
  }
}

//! Directory checks precede reference decoding when both are malformed.
NOLINT_TEST(PakFileTest, DescriptorOverlapPrecedesInvalidReference)
{
  // Arrange
  auto bytes = TexturedPak();
  CookedBytes::BreakReferenceKind(bytes);
  CookedBytes::MoveDescriptorOntoDirectory(bytes);

  // Act & Assert
  EXPECT_EQ(OpenDefect(bytes), CookedDefect::kOverlap);
}

//! Table row sizes are validated before resource region alignment.
NOLINT_TEST(PakFileTest, TableRowSizePrecedesRegionAlignment)
{
  // Arrange
  auto bytes = TexturedPak();
  CookedBytes::GrowTextureEntrySize(bytes);
  CookedBytes::MisalignTextureRegion(bytes);

  // Act & Assert
  EXPECT_EQ(OpenDefect(bytes), CookedDefect::kWrongRowSize);
}

//! Reference inventories start immediately after the final descriptor.
NOLINT_TEST(PakFileTest, RefusesADisplacedReferenceInventory)
{
  // Arrange
  auto bytes = TexturedPak();
  CookedBytes::DisplaceReferenceInventory(bytes);

  // Act & Assert
  EXPECT_EQ(OpenDefect(bytes), CookedDefect::kOverlap);
}

//! Inventories of successive assets cannot name the same block.
NOLINT_TEST(PakFileTest, RefusesOverlappingReferenceInventories)
{
  // Arrange
  auto bytes = CookedPak { TwoTexturedMaterials() }.Encode();
  CookedBytes::ShareReferenceInventory(bytes);

  // Act & Assert
  EXPECT_EQ(OpenDefect(bytes), CookedDefect::kOverlap);
}

//! An empty reference inventory names no bytes of the pak.
NOLINT_TEST(PakFileTest, RefusesALocatedEmptyReferenceInventory)
{
  // Arrange
  auto bytes = ValidPak();
  CookedBytes::LocateEmptyReferenceInventory(bytes);

  // Act & Assert
  EXPECT_EQ(OpenDefect(bytes), CookedDefect::kInvalidReference);
}

//! Asset and texture browse paths share one namespace.
NOLINT_TEST(PakFileTest, BrowseRefusesAnAssetTexturePathCollision)
{
  // Arrange
  auto bytes = BrowseIndex();
  CookedBytes::AliasBrowseTextureToFirstEntry(bytes);

  // Act & Assert
  EXPECT_EQ(BrowseDefect(bytes, 2U), CookedDefect::kDuplicate);
}

//! The texture index is checked before the texture's path: a texture
//! outside the table is refused although its path lies outside the strings.
NOLINT_TEST(PakFileTest, BrowseTextureIndexPrecedesItsPath)
{
  // Arrange
  auto bytes = BrowseIndex(/*texture_index=*/2U);
  CookedBytes::SetBrowseTexturePathOffset(
    bytes, std::numeric_limits<uint32_t>::max());

  // Act & Assert
  EXPECT_EQ(BrowseDefect(bytes, 2U), CookedDefect::kOutOfBounds);
}

//! The product's browse index decodes to its entries.
NOLINT_TEST(PakFileTest, BrowseDecodesItsEntries)
{
  // Act
  const auto browse = PakFile::DecodeBrowseIndex(BrowseIndex(), 2U);

  // Assert
  ASSERT_EQ(browse.entries.size(), 1U);
  ASSERT_EQ(browse.textures.size(), 1U);
  EXPECT_EQ(browse.entries.front().virtual_path, kMaterialPath);
  EXPECT_EQ(browse.textures.front().virtual_path, kTexturePath);
}

//! A browse index holds exactly the payload its header declares: a byte
//! after it is a size mismatch.
NOLINT_TEST(PakFileTest, BrowseRefusesTrailingBytes)
{
  // Arrange
  auto bytes = BrowseIndex();
  bytes.push_back(std::byte { 0x7F });

  // Act & Assert
  EXPECT_EQ(BrowseDefect(bytes, 2U), CookedDefect::kSizeMismatch);
}

struct PakRefusal final {
  std::string name;
  std::function<void(std::vector<std::byte>&)> apply;
  CookedDefect defect;
};

class PakFileRefusalTest : public ::testing::TestWithParam<PakRefusal> { };

//! A pak that breaks one rule of the format is refused at open, before
//! anything in it is exposed, naming that rule. CRC verification is off, so
//! each rule is checked on its own.
NOLINT_TEST_P(PakFileRefusalTest, RefusesTheBrokenRule)
{
  auto bytes = TexturedPak();

  GetParam().apply(bytes);

  EXPECT_EQ(OpenDefect(bytes), GetParam().defect);
}

INSTANTIATE_TEST_SUITE_P(Rules, PakFileRefusalTest,
  ::testing::Values(
    PakRefusal {
      .name = "HeaderMagic",
      .apply = &CookedBytes::BreakPakMagic,
      .defect = CookedDefect::kWrongMagic,
    },
    PakRefusal {
      .name = "FormatVersion",
      .apply = [](std::vector<std::byte>& bytes) -> void {
        CookedBytes::SetPakVersion(bytes, kUnknownFormatVersion);
      },
      .defect = CookedDefect::kUnsupportedVersion,
    },
    PakRefusal {
      .name = "FooterMagic",
      .apply = &CookedBytes::BreakFooterMagic,
      .defect = CookedDefect::kTruncated,
    },
    PakRefusal {
      .name = "HeaderReserved",
      .apply = &CookedBytes::SetHeaderReserved,
      .defect = CookedDefect::kReservedNotZero,
    },
    PakRefusal {
      .name = "FooterReserved",
      .apply = &CookedBytes::SetFooterReserved,
      .defect = CookedDefect::kReservedNotZero,
    },
    PakRefusal {
      .name = "DirectoryEntryReserved",
      .apply = &CookedBytes::SetDirectoryEntryReserved,
      .defect = CookedDefect::kReservedNotZero,
    },
    PakRefusal {
      .name = "NoCatalog",
      .apply = &CookedBytes::DropCatalog,
      .defect = CookedDefect::kMissingPart,
    },
    PakRefusal {
      .name = "CatalogOutsideTheBody",
      .apply = &CookedBytes::OverrunCatalog,
      .defect = CookedDefect::kOutOfBounds,
    },
    PakRefusal {
      .name = "CatalogOfAnotherIdentity",
      .apply = [](std::vector<std::byte>& bytes) -> void {
        CookedBytes::SetHeaderIdentity(bytes, SourceKeyOf(kOtherIdentitySeed));
      },
      .defect = CookedDefect::kInconsistent,
    },
    PakRefusal {
      .name = "DirectoryOutsideThePak",
      .apply = &CookedBytes::MoveDirectoryOutside,
      .defect = CookedDefect::kOutOfBounds,
    },
    PakRefusal {
      .name = "NoBrowseIndex",
      .apply = &CookedBytes::DropBrowseIndex,
      .defect = CookedDefect::kMissingPart,
    },
    PakRefusal {
      .name = "OverlappingRegions",
      .apply = &CookedBytes::OverlapAudioRegionWithTextures,
      .defect = CookedDefect::kOverlap,
    },
    PakRefusal {
      .name = "MisalignedRegion",
      .apply = &CookedBytes::MisalignTextureRegion,
      .defect = CookedDefect::kMisaligned,
    },
    PakRefusal {
      .name = "TableOfAnotherRowSize",
      .apply = &CookedBytes::GrowTextureEntrySize,
      .defect = CookedDefect::kWrongRowSize,
    },
    PakRefusal {
      .name = "UnknownBindingKind",
      .apply = &CookedBytes::BreakReferenceKind,
      .defect = CookedDefect::kInvalidReference,
    },
    PakRefusal {
      .name = "RowWithoutDigest",
      .apply = &CookedBytes::ClearTextureDigest,
      .defect = CookedDefect::kMissingDigest,
    },
    PakRefusal {
      .name = "PayloadOutsideItsRegion",
      .apply = &CookedBytes::MoveTexturePayloadOutsideRegion,
      .defect = CookedDefect::kOutOfBounds,
    },
    PakRefusal {
      .name = "PayloadOffItsBoundary",
      .apply = &CookedBytes::MisalignTexturePayload,
      .defect = CookedDefect::kMisaligned,
    },
    PakRefusal {
      .name = "UnknownCompression",
      .apply = [](std::vector<std::byte>& bytes) -> void {
        CookedBytes::SetTextureCompression(bytes, 0xEEU);
      },
      .defect = CookedDefect::kInvalidValue,
    },
    PakRefusal {
      .name = "BrowsePathNotUtf8",
      .apply = &CookedBytes::BreakBrowsePathUtf8,
      .defect = CookedDefect::kInvalidPath,
    },
    PakRefusal {
      .name = "BrowsePathDotSegment",
      .apply = &CookedBytes::BreakBrowsePathDotSegment,
      .defect = CookedDefect::kInvalidPath,
    },
    PakRefusal {
      .name = "BrowsePathOfAnotherMount",
      .apply = &CookedBytes::ChangeBrowseMount,
      .defect = CookedDefect::kInconsistent,
    },
    PakRefusal {
      .name = "BrowseAssetNotInThePak",
      .apply = [](std::vector<std::byte>& bytes) -> void {
        CookedBytes::SetBrowseAssetKey(
          bytes, oxygen::data::AssetKey::FromVirtualPath("/Tests/Absent.omat"));
      },
      .defect = CookedDefect::kInconsistent,
    }),
  [](const ::testing::TestParamInfo<PakRefusal>& info) -> std::string {
    return info.param.name;
  });

//! The CRC covers the whole pak except its own field: a CRC computed here,
//! skipping that field, is accepted.
NOLINT_TEST(PakFileTest, Crc32ValidationPassesWhenItMatches)
{
  auto bytes = ValidPak();
  CookedBytes::CorruptCrc(bytes);
  CookedBytes::SetCrc(bytes, PlainCrc32(bytes));

  const auto pak = Open(bytes);

  NOLINT_EXPECT_NO_THROW(pak.ValidateCrc32Integrity());
}

NOLINT_TEST(PakFileTest, Crc32ValidationFailsWhenAByteChanges)
{
  auto bytes = ValidPak();
  // The first descriptor byte, which opening the pak does not parse.
  const auto descriptor_at
    = static_cast<size_t>(Open(bytes).Directory().front().desc_offset);
  bytes.at(descriptor_at) ^= std::byte { 1 };

  const auto pak = Open(bytes);

  try {
    pak.ValidateCrc32Integrity();
    ADD_FAILURE() << "the CRC32 validated";
  } catch (const CookedOpenError& error) {
    EXPECT_EQ(error.Defect(), CookedDefect::kDigestMismatch);
  }
}

NOLINT_TEST(PakFileTest, Crc32ValidationSkippedWhenZero)
{
  auto bytes = ValidPak();
  CookedBytes::SetCrc(bytes, 0U);

  const auto pak = Open(bytes);

  NOLINT_EXPECT_NO_THROW(pak.ValidateCrc32Integrity());
}

//! A descriptor is a view of the entry's bytes in the pak.
NOLINT_TEST(PakFileTest, DescriptorBytesViewTheEntry)
{
  const auto bytes = ValidPak();
  const auto pak = Open(bytes);
  const auto entry = pak.Directory().front();

  const auto descriptor = pak.DescriptorBytes(entry);

  EXPECT_THAT(descriptor,
    BytesEqual(std::span(bytes).subspan(
      static_cast<size_t>(entry.desc_offset), entry.desc_size)));
}

NOLINT_TEST(PakFileTest, DescriptorBytesRefuseAnEntryOutsideThePak)
{
  const auto bytes = ValidPak();
  const auto pak = Open(bytes);
  auto outside = pak.Directory().front();
  outside.desc_offset = bytes.size();

  NOLINT_EXPECT_THROW(
    std::ignore = pak.DescriptorBytes(outside), std::out_of_range);
}

//! A pak whose material references a texture holds a texture table, and no
//! other.
NOLINT_TEST(PakFileTest, ExposesOnlyTheTablesItHolds)
{
  constexpr auto kTexturePayloadSeed = uint8_t { 7U };
  auto root = LooseCookedRoot { kMount };
  const auto wood = root.AddTexture(SeedBytes(kTexturePayloadSeed));
  auto material = oxygen::cooker::testing::MaterialBuilder { "A" };
  material.Texture(
    &oxygen::data::pak::render::MaterialAssetDesc::base_color_texture, wood);
  static_cast<void>(root.Material(kMaterialLeaf, material));
  const auto bytes = CookedPak { root }.Encode();

  const auto pak = Open(bytes);

  EXPECT_TRUE(pak.HasTableOf<oxygen::data::TextureResource>());
  EXPECT_FALSE(pak.HasTableOf<oxygen::data::BufferResource>());
  EXPECT_FALSE(pak.HasTableOf<oxygen::data::ScriptResource>());
  EXPECT_FALSE(pak.HasTableOf<oxygen::data::PhysicsResource>());
  EXPECT_NE(pak.GetResourceTable<oxygen::data::TextureResource>(), nullptr);
}

//! A pak with no resources has no table to hand out.
NOLINT_TEST(PakFileTest, RefusesATableItDoesNotHold)
{
  const auto pak = Open(ValidPak());

  EXPECT_FALSE(pak.HasTableOf<oxygen::data::TextureResource>());
  EXPECT_FALSE(pak.HasTableOf<oxygen::data::BufferResource>());
  NOLINT_EXPECT_THROW(std::ignore = pak.TexturesTable(), std::runtime_error);
}

} // namespace
