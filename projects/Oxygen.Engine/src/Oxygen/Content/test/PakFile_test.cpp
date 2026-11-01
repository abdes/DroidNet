//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//
// Covers: Content/PakFile.h

#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <system_error>
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
using oxygen::cooker::testing::LooseCookedRoot;
using oxygen::cooker::testing::Materialize;
using oxygen::cooker::testing::SeedBytes;
using oxygen::cooker::testing::SourceKeyOf;
using oxygen::data::CookedOpenError;
using oxygen::data::CookedOpenFailure;
using oxygen::serio::testing::BytesEqual;

constexpr auto kMount = "Tests";
constexpr auto kMaterialLeaf = "A.omat";
constexpr auto kMaterialPath = "/Tests/A.omat";
constexpr auto kUnknownFormatVersion = uint16_t { 99U };
constexpr auto kOtherIdentitySeed = uint8_t { 0xEEU };

//! Opens the pak `bytes` hold, from memory.
auto Open(const std::vector<std::byte>& bytes) -> PakFile
{
  return { oxygen::serio::AnyDevice(
             oxygen::serio::MemoryFile::FromBytes(bytes).value()),
    "test.pak" };
}

//! The failure the pak `bytes` hold is refused with.
auto OpenFailure(const std::vector<std::byte>& bytes) -> CookedOpenFailure
{
  try {
    std::ignore = Open(bytes);
  } catch (const CookedOpenError& error) {
    return error.Failure();
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

  NOLINT_EXPECT_THROW(PakFile(dir.Path() / "missing.pak"), std::system_error);
}

NOLINT_TEST(PakFileTest, RefusesADeviceWithoutAView)
{
  auto device = oxygen::serio::AnyDevice(
    oxygen::serio::testing::FaultInjectingDevice(oxygen::serio::MemoryFile {}));

  NOLINT_EXPECT_THROW(
    PakFile(std::move(device), "test.pak"), std::invalid_argument);
}

NOLINT_TEST(PakFileTest, RefusesTooFewBytesForAHeader)
{
  EXPECT_EQ(
    OpenFailure(std::vector<std::byte>(4)), CookedOpenFailure::kWrongFormat);
}

NOLINT_TEST(PakFileTest, RefusesAnotherHeaderMagic)
{
  auto bytes = ValidPak();
  CookedBytes::BreakPakMagic(bytes);

  EXPECT_EQ(OpenFailure(bytes), CookedOpenFailure::kWrongFormat);
}

NOLINT_TEST(PakFileTest, RefusesAnotherFormatVersion)
{
  auto bytes = ValidPak();
  CookedBytes::SetPakVersion(bytes, kUnknownFormatVersion);

  EXPECT_EQ(OpenFailure(bytes), CookedOpenFailure::kUnsupportedVersion);
}

NOLINT_TEST(PakFileTest, RefusesAnotherFooterMagic)
{
  auto bytes = ValidPak();
  CookedBytes::BreakFooterMagic(bytes);

  EXPECT_EQ(OpenFailure(bytes), CookedOpenFailure::kCorrupt);
}

NOLINT_TEST(PakFileTest, RefusesAMissingCatalog)
{
  auto bytes = ValidPak();
  CookedBytes::DropCatalog(bytes);

  EXPECT_EQ(OpenFailure(bytes), CookedOpenFailure::kCorrupt);
}

NOLINT_TEST(PakFileTest, RefusesADirectoryOutsideThePak)
{
  auto bytes = ValidPak();
  CookedBytes::MoveDirectoryOutside(bytes);

  EXPECT_EQ(OpenFailure(bytes), CookedOpenFailure::kCorrupt);
}

NOLINT_TEST(PakFileTest, RefusesACatalogOfAnotherIdentity)
{
  auto bytes = ValidPak();
  CookedBytes::SetHeaderIdentity(bytes, SourceKeyOf(kOtherIdentitySeed));

  EXPECT_EQ(OpenFailure(bytes), CookedOpenFailure::kCorrupt);
}

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

  NOLINT_EXPECT_THROW(pak.ValidateCrc32Integrity(), std::runtime_error);
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
