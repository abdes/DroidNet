//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Internal/LooseCookedWriter.cpp (removals)

#include <cstddef>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "LooseCookedWriterTestSupport.h"

#include <Oxygen/Content/LooseCookedIndex.h>
#include <Oxygen/Cooker/Import/Internal/LooseCookedWriter.h>
#include <Oxygen/Cooker/Loose/Inspection.h>
#include <Oxygen/Cooker/Loose/Validation.h>
#include <Oxygen/Cooker/test/Support/DescriptorFixtures.h>
#include <Oxygen/Cooker/test/Support/TempDir.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/LooseCookedIndexFormat.h>
#include <Oxygen/Testing/GTest.h>

namespace oxygen::content::testing {
namespace fixtures = oxygen::content::test;

namespace {

  //! A material descriptor's file name.
  auto MaterialFile(const std::string_view name) -> std::string
  {
    return std::string(name) + ".omat";
  }
  using oxygen::content::import::LooseCookedWriter;
  using oxygen::content::lc::Inspection;
  using oxygen::content::lc::IntegrityCheck;
  using oxygen::content::lc::LooseCookedIndex;
  using oxygen::cooker::test::ScopedTempDir;
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
  auto CookTwoMaterials(const std::filesystem::path& cooked_root) -> void
  {
    LooseCookedWriter writer(cooked_root);
    const auto bytes
      = fixtures::MaterialDescriptor("bytes", fixtures::MaterialVariant::kPlain)
          .bytes;
    writer.WriteAssetDescriptor(MakeFirstByteAssetKey(0x11),
      AssetType::kMaterial, VirtualPath("A"), DescriptorPath("A"), bytes, {});
    writer.WriteAssetDescriptor(MakeFirstByteAssetKey(0x22),
      AssetType::kMaterial, VirtualPath("B"), DescriptorPath("B"), bytes, {});
    static_cast<void>(writer.Finish());
  }

  auto HasMatchingContent(const std::filesystem::path& cooked_root) -> bool
  {
    return LooseCookedIndex::LoadFromRoot(cooked_root)
      .CheckContent(cooked_root, IntegrityCheck::kFull)
      .empty();
  }

  /*!
   Scenario: the source of one cooked material is deleted. Removing its asset
   leaves the other material in the index, deletes only the removed
   descriptor, and leaves a root whose content matches its index.
  */
  NOLINT_TEST(LooseCookedWriterRemovalTest,
    RemoveAssetDescriptorKeepsTheOtherAssetsAndDeletesItsFile)
  {
    const ScopedTempDir temp;
    const auto cooked_root = temp.Path() / "loose_cooked_writer_removal";
    CookTwoMaterials(cooked_root);

    LooseCookedWriter writer(cooked_root);
    writer.RemoveAssetDescriptor(VirtualPath("A"));
    const auto result = writer.Finish();

    EXPECT_EQ(result.removed_files, std::vector { DescriptorPath("A") });
    EXPECT_FALSE(std::filesystem::exists(cooked_root / DescriptorPath("A")));
    EXPECT_TRUE(std::filesystem::exists(cooked_root / DescriptorPath("B")));
    Inspection inspection;
    inspection.LoadFromFile(cooked_root / "container.index.bin");
    ASSERT_EQ(inspection.Assets().size(), 1U);
    EXPECT_EQ(inspection.Assets().front().virtual_path, VirtualPath("B"));
    EXPECT_TRUE(HasMatchingContent(cooked_root));
    EXPECT_NO_THROW(lc::ValidateRoot(cooked_root));
  }

  /*!
   Scenario: another writer publishes an asset after this writer opened the
   root. The removal applies to the index published at Finish, so it still
   removes that asset.
  */
  NOLINT_TEST(LooseCookedWriterRemovalTest,
    RemoveAssetDescriptorAppliesToTheIndexPublishedAtFinish)
  {
    const ScopedTempDir temp;
    const auto cooked_root = temp.Path() / "loose_cooked_writer_late_removal";

    LooseCookedWriter remover(cooked_root);
    remover.RemoveAssetDescriptor(VirtualPath("A"));
    CookTwoMaterials(cooked_root);
    static_cast<void>(remover.Finish());

    Inspection inspection;
    inspection.LoadFromFile(cooked_root / "container.index.bin");
    ASSERT_EQ(inspection.Assets().size(), 1U);
    EXPECT_EQ(inspection.Assets().front().virtual_path, VirtualPath("B"));
    EXPECT_FALSE(std::filesystem::exists(cooked_root / DescriptorPath("A")));
  }

  NOLINT_TEST(
    LooseCookedWriterRemovalTest, RemoveAssetDescriptorNotInTheIndexThrows)
  {
    const ScopedTempDir temp;
    const auto cooked_root = temp.Path() / "loose_cooked_writer_unknown";
    CookTwoMaterials(cooked_root);

    LooseCookedWriter writer(cooked_root);
    writer.RemoveAssetDescriptor(VirtualPath("C"));

    EXPECT_THROW(static_cast<void>(writer.Finish()), std::runtime_error);
    Inspection inspection;
    inspection.LoadFromFile(cooked_root / "container.index.bin");
    EXPECT_EQ(inspection.Assets().size(), 2U);
  }

  NOLINT_TEST(
    LooseCookedWriterRemovalTest, RemoveAssetDescriptorThisWriterWritesThrows)
  {
    const ScopedTempDir temp;
    const auto cooked_root = temp.Path() / "loose_cooked_writer_rewrite";
    CookTwoMaterials(cooked_root);

    LooseCookedWriter writer(cooked_root);
    writer.WriteAssetDescriptor(MakeFirstByteAssetKey(0x11),
      AssetType::kMaterial, VirtualPath("A"), DescriptorPath("A"),
      fixtures::MaterialDescriptor("again", fixtures::MaterialVariant::kPlain)
        .bytes,
      {});
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
    const ScopedTempDir temp;
    const auto cooked_root = temp.Path() / "loose_cooked_writer_auxiliary";
    CookTwoMaterials(cooked_root);
    {
      LooseCookedWriter writer(cooked_root);
      const std::vector bytes { std::byte { 0x01 } };
      writer.WriteFile(FileKind::kAuxiliary, "Aux/A.bin", bytes);
      writer.WriteFile(FileKind::kAuxiliary, "Aux/B.bin", bytes);
      static_cast<void>(writer.Finish());
    }

    LooseCookedWriter writer(cooked_root);
    writer.RemoveAuxiliaryFile("Aux/A.bin");
    const auto result = writer.Finish();

    EXPECT_EQ(result.removed_files, std::vector<std::string> { "Aux/A.bin" });
    EXPECT_FALSE(std::filesystem::exists(cooked_root / "Aux/A.bin"));
    EXPECT_TRUE(std::filesystem::exists(cooked_root / "Aux/B.bin"));
    EXPECT_TRUE(HasMatchingContent(cooked_root));
  }

  NOLINT_TEST(
    LooseCookedWriterRemovalTest, RemoveAuxiliaryFileNotInTheIndexThrows)
  {
    const ScopedTempDir temp;
    const auto cooked_root = temp.Path() / "loose_cooked_writer_no_auxiliary";
    CookTwoMaterials(cooked_root);

    LooseCookedWriter writer(cooked_root);
    writer.RemoveAuxiliaryFile("Aux/A.bin");

    EXPECT_THROW(static_cast<void>(writer.Finish()), std::runtime_error);
  }

} // namespace

} // namespace oxygen::content::testing
