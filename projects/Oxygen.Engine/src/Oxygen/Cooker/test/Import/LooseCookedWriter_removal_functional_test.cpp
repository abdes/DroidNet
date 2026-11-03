//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Internal/LooseCookedWriter.cpp (removals)

#include <algorithm>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "LooseCookedWriterTestSupport.h"

#include <Oxygen/Content/LooseCookedIndex.h>
#include <Oxygen/Cooker/Cooked/CookedContent.h>
#include <Oxygen/Cooker/Cooked/Validate.h>
#include <Oxygen/Cooker/Import/Internal/LooseCookedWriter.h>
#include <Oxygen/Cooker/testing/MaterialBuilder.h>
#include <Oxygen/Cooker/testing/MemoryFileSystem.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/LooseCookedIndexFormat.h>
#include <Oxygen/Testing/GTest.h>

namespace oxygen::content::testing {
namespace fixtures = oxygen::cooker::testing;

namespace {

  //! A material descriptor's file name.
  auto MaterialFile(const std::string_view name) -> std::string
  {
    return std::string(name) + ".omat";
  }
  using oxygen::content::cooked::CookedContent;
  using oxygen::content::import::LooseCookedWriter;
  using oxygen::content::lc::IntegrityCheck;
  using oxygen::cooker::testing::MemoryFileSystem;
  using oxygen::data::AssetType;
  using oxygen::data::loose_cooked::FileKind;

  auto VirtualPath(const std::string& name) -> std::string
  {
    return "/.cooked/Materials/" + MaterialFile(name);
  }

  auto DescriptorPath(const std::string& name) -> std::string
  {
    return "Materials/" + MaterialFile(name);
  }

  //! Cooks materials A and B into a fresh root.
  auto CookTwoMaterials(
    MemoryFileSystem& storage, const std::filesystem::path& cooked_root) -> void
  {
    LooseCookedWriter writer(cooked_root, storage);
    const auto bytes = fixtures::MaterialBuilder { "bytes" }.Bytes();
    writer.WriteAssetDescriptor(MakeFirstByteAssetKey(0x11),
      AssetType::kMaterial, VirtualPath("A"), DescriptorPath("A"), bytes, {},
      FromSource(VirtualPath("A") + ".json"));
    writer.WriteAssetDescriptor(MakeFirstByteAssetKey(0x22),
      AssetType::kMaterial, VirtualPath("B"), DescriptorPath("B"), bytes, {},
      FromSource(VirtualPath("B") + ".json"));
    static_cast<void>(writer.Finish());
  }

  auto HasMatchingContent(const MemoryFileSystem& storage,
    const std::filesystem::path& cooked_root) -> bool
  {
    try {
      cooked::Validate(
        CookedContent::Open(storage, cooked_root), IntegrityCheck::kFull);
      return true;
    } catch (const std::exception&) {
      return false;
    }
  }

  /*!
   Scenario: the source of one cooked material is deleted. Removing its asset
   leaves the other material in the index, deletes only the removed
   descriptor, and leaves a root whose content matches its index.
  */
  NOLINT_TEST(LooseCookedWriterRemovalTest,
    RemoveAssetDescriptorKeepsTheOtherAssetsAndDeletesItsFile)
  {
    auto storage = MemoryFileSystem {};
    const auto cooked_root = MemoryRoot() / "loose_cooked_writer_removal";
    CookTwoMaterials(storage, cooked_root);

    LooseCookedWriter writer(cooked_root, storage);
    writer.RemoveAssetDescriptor(VirtualPath("A"));
    const auto result = writer.Finish();

    EXPECT_EQ(result.removed_files, std::vector { DescriptorPath("A") });
    EXPECT_FALSE(Exists(storage, cooked_root / DescriptorPath("A")));
    EXPECT_TRUE(Exists(storage, cooked_root / DescriptorPath("B")));
    const auto content = CookedContent::Open(storage, cooked_root);
    ASSERT_EQ(content.Assets().size(), 1U);
    EXPECT_EQ(content.Assets().front().virtual_path, VirtualPath("B"));
    EXPECT_TRUE(HasMatchingContent(storage, cooked_root));
    EXPECT_NO_THROW(cooked::Validate(content));
  }

  /*!
   Scenario: a writer removes an asset and writes another file at the path of
   its descriptor. The new index lists the file, so it stays on disk: only a
   path the new index does not list, of any kind, is deleted.
  */
  NOLINT_TEST(LooseCookedWriterRemovalTest,
    APathTheNewIndexListsStaysWhenItsFormerAssetIsRemoved)
  {
    auto storage = MemoryFileSystem {};
    const auto cooked_root = MemoryRoot() / "loose_cooked_writer_reused_path";
    CookTwoMaterials(storage, cooked_root);
    const auto auxiliary = std::vector { std::byte { 0x5A } };

    LooseCookedWriter writer(cooked_root, storage);
    writer.RemoveAssetDescriptor(VirtualPath("A"));
    writer.WriteFile(FileKind::kAuxiliary, DescriptorPath("A"), auxiliary);
    const auto result = writer.Finish();

    EXPECT_TRUE(result.removed_files.empty());
    EXPECT_TRUE(Exists(storage, cooked_root / DescriptorPath("A")));
    EXPECT_TRUE(HasMatchingContent(storage, cooked_root));
  }

  /*!
   Scenario: a writer adds a file whose path differs from an indexed
   descriptor's only in case. A case-insensitive file system holds both in one
   file, so the index is not published and the root keeps its old index.
  */
  NOLINT_TEST(
    LooseCookedWriterRemovalTest, AFilePathDifferingOnlyInCaseIsRefused)
  {
    auto storage = MemoryFileSystem {};
    const auto cooked_root = MemoryRoot() / "loose_cooked_writer_case_path";
    CookTwoMaterials(storage, cooked_root);
    const auto published = storage.Bytes(cooked_root / "container.index.bin");
    const auto index
      = std::vector<std::byte>(published.begin(), published.end());

    LooseCookedWriter writer(cooked_root, storage);
    writer.WriteFile(FileKind::kAuxiliary, "Materials/a.omat",
      std::vector { std::byte { 0x5A } });

    EXPECT_THROW(
      static_cast<void>(writer.Finish()), import::LooseCookedIdentityCollision);
    EXPECT_TRUE(std::ranges::equal(
      storage.Bytes(cooked_root / "container.index.bin"), index));
  }

  /*!
   Scenario: every material of a folder is removed, as when its model folder is
   renamed only in case. The emptied folder goes too, so the next write
   creates it under its new name instead of reusing the old one.
  */
  NOLINT_TEST(LooseCookedWriterRemovalTest,
    RemovingTheLastAssetsOfAFolderDeletesTheFolder)
  {
    auto storage = MemoryFileSystem {};
    const auto cooked_root = MemoryRoot() / "loose_cooked_writer_empty_folder";
    CookTwoMaterials(storage, cooked_root);

    LooseCookedWriter writer(cooked_root, storage);
    writer.RemoveAssetDescriptor(VirtualPath("A"));
    writer.RemoveAssetDescriptor(VirtualPath("B"));
    static_cast<void>(writer.Finish());

    EXPECT_FALSE(Exists(storage, cooked_root / "Materials"));
    EXPECT_TRUE(Exists(storage, cooked_root / "container.index.bin"));
  }

  /*!
   Scenario: another writer publishes an asset after this writer opened the
   root. The removal applies to the index published at Finish, so it still
   removes that asset.
  */
  NOLINT_TEST(LooseCookedWriterRemovalTest,
    RemoveAssetDescriptorAppliesToTheIndexPublishedAtFinish)
  {
    auto storage = MemoryFileSystem {};
    const auto cooked_root = MemoryRoot() / "loose_cooked_writer_late_removal";

    LooseCookedWriter remover(cooked_root, storage);
    remover.RemoveAssetDescriptor(VirtualPath("A"));
    CookTwoMaterials(storage, cooked_root);
    static_cast<void>(remover.Finish());

    const auto content = CookedContent::Open(storage, cooked_root);
    ASSERT_EQ(content.Assets().size(), 1U);
    EXPECT_EQ(content.Assets().front().virtual_path, VirtualPath("B"));
    EXPECT_FALSE(Exists(storage, cooked_root / DescriptorPath("A")));
  }

  NOLINT_TEST(
    LooseCookedWriterRemovalTest, RemoveAssetDescriptorNotInTheIndexThrows)
  {
    auto storage = MemoryFileSystem {};
    const auto cooked_root = MemoryRoot() / "loose_cooked_writer_unknown";
    CookTwoMaterials(storage, cooked_root);

    LooseCookedWriter writer(cooked_root, storage);
    writer.RemoveAssetDescriptor(VirtualPath("C"));

    EXPECT_THROW(static_cast<void>(writer.Finish()), std::runtime_error);
    const auto content = CookedContent::Open(storage, cooked_root);
    EXPECT_EQ(content.Assets().size(), 2U);
  }

  NOLINT_TEST(
    LooseCookedWriterRemovalTest, RemoveAssetDescriptorThisWriterWritesThrows)
  {
    auto storage = MemoryFileSystem {};
    const auto cooked_root = MemoryRoot() / "loose_cooked_writer_rewrite";
    CookTwoMaterials(storage, cooked_root);

    LooseCookedWriter writer(cooked_root, storage);
    writer.WriteAssetDescriptor(MakeFirstByteAssetKey(0x11),
      AssetType::kMaterial, VirtualPath("A"), DescriptorPath("A"),
      fixtures::MaterialBuilder { "again" }.Bytes(), {},
      FromSource(VirtualPath("A") + ".json"));
    writer.RemoveAssetDescriptor(VirtualPath("A"));

    EXPECT_THROW(static_cast<void>(writer.Finish()), std::runtime_error);
  }

  /*!
   Scenario: a removed import emitted an auxiliary file. Removing it drops its
   record and the file, and keeps the root's other records.
  */
  NOLINT_TEST(
    LooseCookedWriterRemovalTest, RemoveAuxiliaryFileDropsItsRecordAndDeletesIt)
  {
    auto storage = MemoryFileSystem {};
    const auto cooked_root = MemoryRoot() / "loose_cooked_writer_auxiliary";
    CookTwoMaterials(storage, cooked_root);
    {
      LooseCookedWriter writer(cooked_root, storage);
      const std::vector bytes { std::byte { 0x01 } };
      writer.WriteFile(FileKind::kAuxiliary, "Aux/A.bin", bytes);
      writer.WriteFile(FileKind::kAuxiliary, "Aux/B.bin", bytes);
      static_cast<void>(writer.Finish());
    }

    LooseCookedWriter writer(cooked_root, storage);
    writer.RemoveAuxiliaryFile("Aux/A.bin");
    const auto result = writer.Finish();

    EXPECT_EQ(result.removed_files, std::vector<std::string> { "Aux/A.bin" });
    EXPECT_FALSE(Exists(storage, cooked_root / "Aux/A.bin"));
    EXPECT_TRUE(Exists(storage, cooked_root / "Aux/B.bin"));
    EXPECT_TRUE(HasMatchingContent(storage, cooked_root));
  }

  NOLINT_TEST(
    LooseCookedWriterRemovalTest, RemoveAuxiliaryFileNotInTheIndexThrows)
  {
    auto storage = MemoryFileSystem {};
    const auto cooked_root = MemoryRoot() / "loose_cooked_writer_no_auxiliary";
    CookTwoMaterials(storage, cooked_root);

    LooseCookedWriter writer(cooked_root, storage);
    writer.RemoveAuxiliaryFile("Aux/A.bin");

    EXPECT_THROW(static_cast<void>(writer.Finish()), std::runtime_error);
  }

} // namespace

} // namespace oxygen::content::testing
