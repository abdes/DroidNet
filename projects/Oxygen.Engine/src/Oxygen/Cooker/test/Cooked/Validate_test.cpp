//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Cooked/Validate.cpp

#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <Oxygen/Content/LooseCookedIndex.h>
#include <Oxygen/Cooker/Cooked/CookedContent.h>
#include <Oxygen/Cooker/Cooked/Validate.h>
#include <Oxygen/Cooker/testing/CookedBytes.h>
#include <Oxygen/Cooker/testing/DescriptorDefects.h>
#include <Oxygen/Cooker/testing/LooseCookedRoot.h>
#include <Oxygen/Cooker/testing/MaterialBuilder.h>
#include <Oxygen/Cooker/testing/PakWorkspace.h>
#include <Oxygen/Cooker/testing/Seeds.h>
#include <Oxygen/Core/Meta/Data/ResourceIndex.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/CookedFormatError.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Data/PakFormat_render.h>
#include <Oxygen/Serio/ByteReader.h>
#include <Oxygen/Testing/GTest.h>

namespace {

namespace cooked = oxygen::content::cooked;
namespace core = oxygen::data::pak::core;
namespace data = oxygen::data;
namespace fixtures = oxygen::cooker::testing;
namespace fs = std::filesystem;
namespace serio = oxygen::serio;

using cooked::CookedContent;
using cooked::Validate;
using oxygen::ResourceIndexT;
using oxygen::content::lc::IntegrityCheck;
using oxygen::cooker::testing::CookedBytes;
using oxygen::cooker::testing::LooseCookedRoot;
using oxygen::cooker::testing::PakWorkspace;
using oxygen::cooker::testing::SeedBytes;

//=== Builders ===------------------------------------------------------------//

//! A valid library: a material that binds a texture the library holds.
auto ValidLibrary() -> LooseCookedRoot
{
  auto library = LooseCookedRoot { "Game" };
  const auto texture = library.AddTexture(SeedBytes(1U));
  auto wood = fixtures::MaterialBuilder { "Wood" };
  wood.Texture(
    &data::pak::render::MaterialAssetDesc::base_color_texture, texture);
  static_cast<void>(library.Add({
    .type = data::AssetType::kMaterial,
    .leaf = "Materials/Wood.omat",
    .descriptor = wood.Bytes(),
    .references = wood.References(),
  }));
  return library;
}

//! A library holding only the material `descriptor`, of `type`, binding
//! `references`.
auto LibraryOf(std::vector<std::byte> descriptor,
  const data::AssetType type = data::AssetType::kMaterial,
  data::AssetReferences references = {}) -> LooseCookedRoot
{
  auto library = LooseCookedRoot { "Game" };
  static_cast<void>(library.Add({
    .type = type,
    .leaf = "Materials/Wood.omat",
    .descriptor = std::move(descriptor),
    .references = std::move(references),
  }));
  return library;
}

//! The material descriptor `Wood` with its header version changed by `delta`.
auto WoodWithVersionShifted(const int delta) -> std::vector<std::byte>
{
  auto bytes = fixtures::MaterialBuilder { "Wood" }.Bytes();
  const auto read = serio::CopyAt<core::AssetHeader>(std::span { bytes });
  EXPECT_TRUE(read.has_value());
  const auto header = read.value_or(core::AssetHeader {});
  fixtures::DescriptorDefects::SetVersion(
    bytes, static_cast<uint8_t>(header.version + delta));
  return bytes;
}

//=== Fixture ===-------------------------------------------------------------//

//! Validates cooked content opened from an in-memory workspace.
class ValidateTest : public ::testing::Test {
protected:
  //! The loose root of `library`, opened.
  auto OpenLoose(const LooseCookedRoot& library) -> CookedContent
  {
    return CookedContent::Open(workspace_.Storage(), workspace_.Add(library));
  }

  //! The pak of `library`, opened.
  auto OpenPak(const LooseCookedRoot& library) -> CookedContent
  {
    const auto request = workspace_.FullRequest({ workspace_.Add(library) });
    static_cast<void>(workspace_.PackOrThrow(request));
    return CookedContent::Open(
      workspace_.Storage(), PakWorkspace::PakSource(request.output_pak_path));
  }

  PakWorkspace workspace_;
};

//=== Valid content ===-------------------------------------------------------//

NOLINT_TEST_F(ValidateTest, ValidLooseRootPasses)
{
  // Arrange
  const auto content = OpenLoose(ValidLibrary());

  // Act / Assert
  EXPECT_NO_THROW(Validate(content));
  EXPECT_NO_THROW(Validate(content, IntegrityCheck::kMetadata));
}

NOLINT_TEST_F(ValidateTest, PakOfAValidRootPasses)
{
  // Arrange
  const auto content = OpenPak(ValidLibrary());

  // Act / Assert
  EXPECT_NO_THROW(Validate(content));
  EXPECT_NO_THROW(Validate(content, IntegrityCheck::kMetadata));
}

NOLINT_TEST_F(ValidateTest, SourceHoldingNothingPasses)
{
  // Arrange
  const auto content = OpenLoose(LooseCookedRoot { "Game" });

  // Act / Assert
  EXPECT_NO_THROW(Validate(content));
}

//=== Invalid content ===-----------------------------------------------------//

//! The defect `Validate` reports for `content`, if it refuses it.
auto ValidateDefect(const CookedContent& content,
  const IntegrityCheck check = IntegrityCheck::kFull)
  -> std::optional<data::CookedDefect>
{
  try {
    Validate(content, check);
  } catch (const data::CookedFormatError& error) {
    return error.Defect();
  }
  return std::nullopt;
}

//! A root whose index is of another version cannot be opened to validate.
NOLINT_TEST_F(ValidateTest, IndexOfAnotherVersionCannotBeOpened)
{
  // Arrange
  auto files = LooseCookedRoot { "Game" }.Files();
  CookedBytes::SetIndexVersion(files, 999U);
  workspace_.Storage().AddFiles("/cooked/Future", files);

  // Act / Assert
  EXPECT_THROW(static_cast<void>(CookedContent::Open(
                 workspace_.Storage(), fs::path("/cooked/Future"))),
    data::CookedFormatError);
}

NOLINT_TEST_F(ValidateTest, DescriptorOfAnOlderVersionIsRefused)
{
  // Arrange
  const auto content = OpenLoose(LibraryOf(WoodWithVersionShifted(-1)));

  // Act / Assert
  EXPECT_EQ(ValidateDefect(content), data::CookedDefect::kUnsupportedVersion);
}

NOLINT_TEST_F(ValidateTest, DescriptorOfANewerVersionIsRefused)
{
  // Arrange
  const auto content = OpenLoose(LibraryOf(WoodWithVersionShifted(1)));

  // Act / Assert
  EXPECT_EQ(ValidateDefect(content), data::CookedDefect::kUnsupportedVersion);
}

//! The type the index records must be the type the descriptor states.
NOLINT_TEST_F(ValidateTest, DescriptorOfAnotherTypeThanTheIndexIsRefused)
{
  // Arrange
  const auto content = OpenLoose(LibraryOf(
    fixtures::MaterialBuilder { "Wood" }.Bytes(), data::AssetType::kGeometry));

  // Act / Assert
  EXPECT_EQ(ValidateDefect(content), data::CookedDefect::kInconsistent);
}

NOLINT_TEST_F(ValidateTest, DescriptorSmallerThanItsHeaderIsRefused)
{
  // Arrange
  const auto content = OpenLoose(
    LibraryOf(std::vector<std::byte>(sizeof(core::AssetHeader) - 1U)));

  // Act / Assert
  EXPECT_EQ(ValidateDefect(content), data::CookedDefect::kTooSmall);
}

//! Bytes after the descriptor's last record are not part of any asset.
NOLINT_TEST_F(ValidateTest, DescriptorWithTrailingBytesIsRefused)
{
  // Arrange
  auto bytes = fixtures::MaterialBuilder { "Wood" }.Bytes();
  bytes.push_back(std::byte { 0U });
  const auto content = OpenLoose(LibraryOf(std::move(bytes)));

  // Act / Assert
  EXPECT_EQ(ValidateDefect(content), data::CookedDefect::kSizeMismatch);
}

//! A reference past the end of the tables the root holds is refused.
NOLINT_TEST_F(ValidateTest, ReferenceBeyondTheTablesIsRefused)
{
  // Arrange
  auto wood = fixtures::MaterialBuilder { "Wood" };
  wood.Texture(&data::pak::render::MaterialAssetDesc::base_color_texture,
    ResourceIndexT { 5U });
  const auto content = OpenLoose(
    LibraryOf(wood.Bytes(), data::AssetType::kMaterial, wood.References()));

  // Act / Assert
  EXPECT_EQ(ValidateDefect(content), data::CookedDefect::kInvalidReference);
}

//! A digest the index records wrongly shows only in the full check.
NOLINT_TEST_F(ValidateTest, ForgedDescriptorDigestFailsTheFullCheckOnly)
{
  // Arrange
  auto library = LooseCookedRoot { "Game" };
  static_cast<void>(library.Add({
    .type = data::AssetType::kMaterial,
    .leaf = "Materials/Wood.omat",
    .descriptor = fixtures::MaterialBuilder { "Wood" }.Bytes(),
    .recorded_sha256 = std::array<uint8_t, 32> { 0x01U },
  }));
  const auto content = OpenLoose(library);

  // Act / Assert
  EXPECT_NO_THROW(Validate(content, IntegrityCheck::kMetadata));
  EXPECT_EQ(ValidateDefect(content, IntegrityCheck::kFull),
    data::CookedDefect::kDigestMismatch);
}

//! A pak whose bytes changed fails the full check, which reads its CRC.
NOLINT_TEST_F(ValidateTest, DamagedPakFailsTheFullCheck)
{
  // Arrange
  const auto library = ValidLibrary();
  const auto request = workspace_.FullRequest({ workspace_.Add(library) });
  static_cast<void>(workspace_.PackOrThrow(request));
  const auto source = PakWorkspace::PakSource(request.output_pak_path);
  const auto key = library.KeyOf("Materials/Wood.omat");
  const auto stored
    = CookedContent::Open(workspace_.Storage(), source).DescriptorStored(key);
  const auto current = workspace_.Storage().Bytes(source.path);
  auto bytes = std::vector<std::byte>(current.begin(), current.end());
  bytes.at(static_cast<size_t>(stored.range.offset.get()))
    ^= std::byte { 0xFFU };
  workspace_.Storage().AddFile(source.path, bytes);
  const auto content = CookedContent::Open(workspace_.Storage(), source);

  // Act / Assert
  EXPECT_THROW(Validate(content, IntegrityCheck::kFull), std::exception);
}

} // namespace
