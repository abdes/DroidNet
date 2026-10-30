//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstddef>
#include <cstring>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

#include <Oxygen/Content/test/Fixtures/LooseCookedTestWriter.h>
#include <Oxygen/Content/test/Fixtures/PakTestWriter.h>
#include <Oxygen/Cooker/Loose/LibraryFacts.h>
#include <Oxygen/Cooker/test/Support/MemoryFileSystem.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/CookedOpenError.h>
#include <Oxygen/Data/LooseCookedIndexFormat.h>
#include <Oxygen/Data/PakCatalog.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Data/PakFormat_scripting.h>
#include <Oxygen/Testing/GTest.h>

namespace {

namespace fs = std::filesystem;
using oxygen::content::lc::LibraryFacts;
using oxygen::content::lc::LibraryKind;
using oxygen::content::lc::ReadLibraryFacts;
using oxygen::content::testing::LooseCookedTestWriter;
using oxygen::content::testing::PakTestWriter;
using oxygen::cooker::test::MemoryFileSystem;
using oxygen::data::AssetKey;
using oxygen::data::AssetType;
using oxygen::data::CookedOpenFailure;
using oxygen::data::KeyReferenceKind;
using oxygen::data::PakRequirement;
using oxygen::data::TextureKeyFromVirtualPath;
using oxygen::data::pak::scripting::ScriptAssetDesc;

//! Libraries built by the loose cooked and pak test writers into memory,
//! under a root no test creates.
class LibraryFactsTest : public testing::Test {
protected:
  static auto Root() -> fs::path
  {
    return fs::current_path().root_path() / "OxygenLibraryFacts";
  }

  //! Adds a material at each virtual path.
  static auto AddMaterials(LooseCookedTestWriter& writer,
    const std::vector<std::string>& paths) -> void
  {
    const auto bytes = std::vector<std::byte>(8);
    for (const auto& path : paths) {
      writer.WriteAssetDescriptor(AssetKey::FromVirtualPath(path),
        AssetType::kMaterial, path, path.substr(1), bytes);
    }
  }

  //! Adds a script at `path`, with an external source or without.
  static auto AddScript(LooseCookedTestWriter& writer, const std::string& path,
    const bool external) -> void
  {
    auto desc = ScriptAssetDesc {};
    desc.external_source_path_offset = external ? 1U : 0U;
    auto bytes = std::vector<std::byte>(sizeof(desc));
    std::memcpy(bytes.data(), &desc, sizeof(desc));
    writer.WriteAssetDescriptor(AssetKey::FromVirtualPath(path),
      AssetType::kScript, path, path.substr(1), bytes);
  }

  //! The facts of the loose root `writer` built.
  auto ReadLoose(LooseCookedTestWriter& writer) -> LibraryFacts
  {
    static_cast<void>(writer.Finish());
    files_.AddFiles(Root() / "Loose", writer.Files());
    return ReadLibraryFacts(files_, Root() / "Loose");
  }

  //! The facts of the pak `bytes`.
  auto ReadPak(const std::vector<std::byte>& bytes) -> LibraryFacts
  {
    files_.AddFile(Root() / "Props.pak", bytes);
    return ReadLibraryFacts(files_, Root() / "Props.pak");
  }

  MemoryFileSystem files_;
};

//! A loose root's roots are the mounts of its virtual paths; it provides its
//! assets and textures and requires nothing.
NOLINT_TEST_F(LibraryFactsTest, LooseRootStatesItsRoots)
{
  auto writer = LooseCookedTestWriter {};
  AddMaterials(writer,
    { "/Props/Materials/Red.omat", "/Props/Crate.omat", "/Vehicles/Car.omat" });
  writer.WriteFile(oxygen::data::loose_cooked::FileKind::kTextureDescriptor,
    "Props/Wood.otex", std::vector<std::byte>(8), "/Props/Wood.otex");

  const auto facts = ReadLoose(writer);

  EXPECT_EQ(facts.kind, LibraryKind::kLoose);
  EXPECT_FALSE(facts.unreadable.has_value());
  EXPECT_EQ(facts.roots, (std::vector<std::string> { "Props", "Vehicles" }));
  EXPECT_TRUE(facts.external_scripts.empty());
  EXPECT_EQ(facts.assets.size(), 3U);
  EXPECT_EQ(facts.assets.at(AssetKey::FromVirtualPath("/Props/Crate.omat")),
    AssetType::kMaterial);
  EXPECT_THAT(facts.textures,
    ::testing::ElementsAre(TextureKeyFromVirtualPath("/Props/Wood.otex")));
  EXPECT_TRUE(facts.requirements.empty());
}

//! Only a script whose descriptor names an external source is reported.
NOLINT_TEST_F(LibraryFactsTest, LooseRootStatesExternalScripts)
{
  auto writer = LooseCookedTestWriter {};
  AddScript(writer, "/Props/Scripts/Spin.oscript", true);
  AddScript(writer, "/Props/Scripts/Embedded.oscript", false);

  const auto facts = ReadLoose(writer);

  EXPECT_EQ(facts.external_scripts,
    (std::vector<std::string> { "/Props/Scripts/Spin.oscript" }));
}

//! A folder without an index is not a loose root.
NOLINT_TEST_F(LibraryFactsTest, FolderWithoutIndexIsWrongFormat)
{
  files_.AddFolder(Root() / "Empty");

  const auto facts = ReadLibraryFacts(files_, Root() / "Empty");

  EXPECT_EQ(facts.kind, LibraryKind::kLoose);
  ASSERT_HAS_VALUE(facts.unreadable);
  EXPECT_EQ(facts.unreadable->Failure(), CookedOpenFailure::kWrongFormat);
}

//! An index of another version is refused as unsupported, never misread.
NOLINT_TEST_F(LibraryFactsTest, IndexOfAnotherVersionIsUnsupported)
{
  auto writer = LooseCookedTestWriter {};
  writer.SetIndexVersion(oxygen::data::loose_cooked::kIndexVersion + 1);
  AddMaterials(writer, { "/Props/Crate.omat" });

  const auto facts = ReadLoose(writer);

  ASSERT_HAS_VALUE(facts.unreadable);
  EXPECT_EQ(
    facts.unreadable->Failure(), CookedOpenFailure::kUnsupportedVersion);
  EXPECT_TRUE(facts.roots.empty());
}

//! A truncated index is corrupt.
NOLINT_TEST_F(LibraryFactsTest, TruncatedIndexIsCorrupt)
{
  auto writer = LooseCookedTestWriter {};
  AddMaterials(writer, { "/Props/Crate.omat" });
  static_cast<void>(writer.Finish());
  auto files = writer.Files();
  files.at("container.index.bin").pop_back();
  files_.AddFiles(Root() / "Loose", files);

  const auto facts = ReadLibraryFacts(files_, Root() / "Loose");

  ASSERT_HAS_VALUE(facts.unreadable);
  EXPECT_EQ(facts.unreadable->Failure(), CookedOpenFailure::kCorrupt);
}

//! A pak states its library as its one root, the assets its catalog lists,
//! what it deletes and what it requires of other libraries.
NOLINT_TEST_F(LibraryFactsTest, PakStatesItsCatalog)
{
  const auto chair = AssetKey::FromVirtualPath("/Props/Chair.ogeo");
  const auto removed = AssetKey::FromVirtualPath("/Props/Old.ogeo");
  const auto requirement = PakRequirement {
    .kind = KeyReferenceKind::kAsset,
    .key = AssetKey::FromVirtualPath("/Engine/Default.omat"),
    .type = AssetType::kMaterial,
  };
  auto config = PakTestWriter::Config {};
  config.library = "Props";
  config.assets = {
    {
      .key = chair,
      .type = AssetType::kGeometry,
      .virtual_path = "/Props/Chair.ogeo",
    },
  };
  config.deleted = { removed };
  config.dependencies = {
    {
      .library = "Engine",
      .requirements = { requirement },
    },
  };

  const auto facts = ReadPak(PakTestWriter::Encode(config));

  EXPECT_EQ(facts.kind, LibraryKind::kPak);
  EXPECT_FALSE(facts.unreadable.has_value());
  EXPECT_THAT(facts.roots, ::testing::ElementsAre("Props"));
  EXPECT_TRUE(facts.external_scripts.empty());
  EXPECT_THAT(facts.assets,
    ::testing::ElementsAre(::testing::Pair(chair, AssetType::kGeometry)));
  EXPECT_THAT(facts.deleted, ::testing::ElementsAre(removed));
  ASSERT_EQ(facts.requirements.size(), 1U);
  EXPECT_EQ(facts.requirements.front().library, "Engine");
  EXPECT_THAT(facts.requirements.front().requirements,
    ::testing::ElementsAre(requirement));
}

//! A pak whose browse index is gone is corrupt: every pak holds one.
NOLINT_TEST_F(LibraryFactsTest, PakWithoutBrowseIndexIsCorrupt)
{
  auto bytes = PakTestWriter::Encode(PakTestWriter::Config {});
  auto footer = oxygen::data::pak::core::PakFooter {};
  const auto tail = std::span(bytes).last(sizeof(footer));
  std::memcpy(&footer, tail.data(), sizeof(footer));
  footer.browse_index_size = 0U;
  std::memcpy(tail.data(), &footer, sizeof(footer));

  const auto facts = ReadPak(bytes);

  ASSERT_HAS_VALUE(facts.unreadable);
  EXPECT_EQ(facts.unreadable->Failure(), CookedOpenFailure::kCorrupt);
}

//! A pak of another format version is refused as unsupported.
NOLINT_TEST_F(LibraryFactsTest, PakOfAnotherVersionIsUnsupported)
{
  auto config = PakTestWriter::Config {};
  ++config.header.version;

  const auto facts = ReadPak(PakTestWriter::Encode(config));

  ASSERT_HAS_VALUE(facts.unreadable);
  EXPECT_EQ(
    facts.unreadable->Failure(), CookedOpenFailure::kUnsupportedVersion);
}

//! A file with another magic, or too small to hold a pak, is not a pak.
NOLINT_TEST_F(LibraryFactsTest, OtherFilesAreWrongFormat)
{
  auto config = PakTestWriter::Config {};
  std::span(config.header.magic).front() = 'X';

  const auto magic = ReadPak(PakTestWriter::Encode(config));
  const auto small = ReadPak(std::vector<std::byte>(16));

  ASSERT_HAS_VALUE(magic.unreadable);
  EXPECT_EQ(magic.unreadable->Failure(), CookedOpenFailure::kWrongFormat);
  ASSERT_HAS_VALUE(small.unreadable);
  EXPECT_EQ(small.unreadable->Failure(), CookedOpenFailure::kWrongFormat);
}

//! A pak whose footer is damaged is corrupt.
NOLINT_TEST_F(LibraryFactsTest, DamagedPakIsCorrupt)
{
  auto bytes = PakTestWriter::Encode(PakTestWriter::Config {});
  bytes.back() ^= std::byte { 0xFF };

  const auto facts = ReadPak(bytes);

  ASSERT_HAS_VALUE(facts.unreadable);
  EXPECT_EQ(facts.unreadable->Failure(), CookedOpenFailure::kCorrupt);
}

} // namespace
