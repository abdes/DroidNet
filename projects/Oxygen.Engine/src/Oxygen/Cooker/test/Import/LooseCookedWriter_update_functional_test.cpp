//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Internal/LooseCookedWriter.cpp

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "LooseCookedWriterTestSupport.h"

#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Cooker/Import/Internal/LooseCookedWriter.h>
#include <Oxygen/Cooker/Loose/Inspection.h>
#include <Oxygen/Cooker/test/Support/DescriptorFixtures.h>
#include <Oxygen/Cooker/test/Support/TempDir.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/LooseCookedIndexFormat.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Testing/GTest.h>

namespace oxygen::content::testing {
namespace fixtures = oxygen::content::test;

namespace {

  using oxygen::content::import::LooseCookedIdentityCollision;
  using oxygen::content::import::LooseCookedProducer;
  using oxygen::content::import::LooseCookedWriter;
  using oxygen::content::lc::Inspection;
  using oxygen::cooker::test::ScopedTempDir;
  using oxygen::data::AssetType;
  using oxygen::data::loose_cooked::FileKind;

  constexpr std::string_view kRed = "/.cooked/Materials/Red.omat";
  constexpr std::string_view kRedSource = "/.cooked/Materials/Red.omat.json";
  constexpr std::string_view kOtherSource = "/.cooked/Other/Red.omat.json";

  auto Material() -> std::vector<std::byte>
  {
    return fixtures::MaterialDescriptor(
      "bytes", fixtures::MaterialVariant::kPlain)
      .bytes;
  }

  //! The only asset of an index, copied out of its inspection.
  auto Asset(const Inspection& inspection) -> Inspection::AssetEntry
  {
    EXPECT_EQ(inspection.Assets().size(), 1U);
    return inspection.Assets().front();
  }

  auto Inspect(const std::filesystem::path& cooked_root) -> Inspection
  {
    Inspection inspection;
    inspection.LoadFromFile(cooked_root / "container.index.bin");
    return inspection;
  }

  //! Each entry records the source that produced it.
  NOLINT_TEST(LooseCookedWriterUpdateTest, IndexRecordsTheProducingSource)
  {
    const ScopedTempDir temp;
    const auto cooked_root = temp.Path() / "records_source";
    const auto bytes = Material();

    LooseCookedWriter writer(cooked_root);
    writer.WriteAssetDescriptor(MakeFirstByteAssetKey(0x11),
      AssetType::kMaterial, kRed, "Materials/Red.omat", bytes, {},
      FromSource(std::string(kRedSource)));
    (void)writer.Finish();

    EXPECT_EQ(Asset(Inspect(cooked_root)).source_path, kRedSource);
  }

  //! A source cooking its asset again replaces its entry.
  NOLINT_TEST(LooseCookedWriterUpdateTest, ASourceRecookingItsAssetReplacesIt)
  {
    const ScopedTempDir temp;
    const auto cooked_root = temp.Path() / "recook";
    const auto key = MakeFirstByteAssetKey(0x11);
    const auto first = fixtures::TexturedMaterialDescriptor("white",
      data::pak::core::kFallbackResourceIndex,
      fixtures::MaterialVariant::kWithShader);
    const auto second = fixtures::TexturedMaterialDescriptor(
      "error", data::pak::core::kErrorTextureResourceIndex);

    {
      LooseCookedWriter writer(cooked_root);
      writer.WriteAssetDescriptor(key, AssetType::kMaterial, kRed,
        "Materials/Red.omat", first.bytes, first.references,
        FromSource(std::string(kRedSource)));
      (void)writer.Finish();
    }
    {
      LooseCookedWriter writer(cooked_root);
      writer.WriteAssetDescriptor(key, AssetType::kMaterial, kRed,
        "Materials/Red2.omat", second.bytes, second.references,
        FromSource(std::string(kRedSource)));
      (void)writer.Finish();
    }

    const auto asset = Asset(Inspect(cooked_root));
    EXPECT_EQ(asset.descriptor_relpath, "Materials/Red2.omat");
    EXPECT_EQ(asset.references, second.references);
    EXPECT_EQ(asset.descriptor_sha256, base::ComputeSha256(second.bytes));
  }

  //! A source's recook under another key replaces its entry at the identity.
  NOLINT_TEST(
    LooseCookedWriterUpdateTest, ASourceRecookingUnderAnotherKeyReplacesIt)
  {
    const ScopedTempDir temp;
    const auto cooked_root = temp.Path() / "recook_key";
    const auto native_key = MakeFirstByteAssetKey(0x72);
    const auto bytes = Material();

    {
      LooseCookedWriter writer(cooked_root);
      writer.WriteAssetDescriptor(MakeFirstByteAssetKey(0x71),
        AssetType::kMaterial, kRed, "Materials/Red.omat", bytes, {},
        FromSource(std::string(kRedSource)));
      (void)writer.Finish();
    }
    {
      LooseCookedWriter writer(cooked_root);
      writer.WriteAssetDescriptor(native_key, AssetType::kMaterial, kRed,
        "Materials/Red.omat", bytes, {}, FromSource(std::string(kRedSource)));
      (void)writer.Finish();
    }

    EXPECT_EQ(Asset(Inspect(cooked_root)).key, native_key);
  }

  //! Another source producing an identity is refused, in one cook or later.
  NOLINT_TEST(LooseCookedWriterUpdateTest, AnotherSourceOfAnIdentityIsRefused)
  {
    const ScopedTempDir temp;
    const auto cooked_root = temp.Path() / "other_source";
    const auto key = MakeFirstByteAssetKey(0x11);
    const auto bytes = Material();

    {
      LooseCookedWriter writer(cooked_root);
      writer.WriteAssetDescriptor(key, AssetType::kMaterial, kRed,
        "Materials/Red.omat", bytes, {}, FromSource(std::string(kRedSource)));
      EXPECT_THROW(writer.WriteAssetDescriptor(key, AssetType::kMaterial, kRed,
                     "Materials/Red.omat", bytes, {},
                     FromSource(std::string(kOtherSource))),
        LooseCookedIdentityCollision);
      (void)writer.Finish();
    }

    LooseCookedWriter writer(cooked_root);
    EXPECT_THROW(
      writer.WriteAssetDescriptor(key, AssetType::kMaterial, kRed,
        "Materials/Red.omat", bytes, {}, FromSource(std::string(kOtherSource))),
      LooseCookedIdentityCollision);
    EXPECT_EQ(Asset(Inspect(cooked_root)).source_path, kRedSource);
  }

  //! Identities differing only in case are one file on a case-insensitive file
  //! system, so the second is refused whoever produces it.
  NOLINT_TEST(
    LooseCookedWriterUpdateTest, AnIdentityDifferingOnlyInCaseIsRefused)
  {
    const ScopedTempDir temp;
    const auto cooked_root = temp.Path() / "case_only";
    const auto bytes = Material();

    {
      LooseCookedWriter writer(cooked_root);
      writer.WriteAssetDescriptor(MakeFirstByteAssetKey(0x11),
        AssetType::kMaterial, kRed, "Materials/Red.omat", bytes, {},
        FromSource(std::string(kRedSource)));
      (void)writer.Finish();
    }

    LooseCookedWriter writer(cooked_root);
    EXPECT_THROW(writer.WriteAssetDescriptor(MakeFirstByteAssetKey(0x12),
                   AssetType::kMaterial, "/.cooked/Materials/red.omat",
                   "Materials/red2.omat", bytes, {},
                   FromSource("/.cooked/Materials/red.omat.json")),
      LooseCookedIdentityCollision);
  }

  //! A removed source's identity is free for another source.
  NOLINT_TEST(
    LooseCookedWriterUpdateTest, ARemovedSourcesIdentityIsFreeForAnother)
  {
    const ScopedTempDir temp;
    const auto cooked_root = temp.Path() / "removed_source";
    const auto key = MakeFirstByteAssetKey(0x11);
    const auto bytes = Material();

    {
      LooseCookedWriter writer(cooked_root);
      writer.WriteAssetDescriptor(key, AssetType::kMaterial, kRed,
        "Materials/Red.omat", bytes, {}, FromSource(std::string(kRedSource)));
      (void)writer.Finish();
    }
    {
      LooseCookedWriter remover(cooked_root);
      remover.RemoveAssetDescriptor(kRed);
      (void)remover.Finish();
    }
    {
      LooseCookedWriter writer(cooked_root);
      writer.WriteAssetDescriptor(key, AssetType::kMaterial, kRed,
        "Materials/Red.omat", bytes, {}, FromSource(std::string(kOtherSource)));
      (void)writer.Finish();
    }

    EXPECT_EQ(Asset(Inspect(cooked_root)).source_path, kOtherSource);
  }

  //! A sidecar completes an asset, which keeps its producer.
  NOLINT_TEST(LooseCookedWriterUpdateTest, ASidecarKeepsTheProducerOfItsTarget)
  {
    const ScopedTempDir temp;
    const auto cooked_root = temp.Path() / "sidecar";
    const auto key = MakeFirstByteAssetKey(0x11);
    const auto bytes = Material();

    {
      LooseCookedWriter writer(cooked_root);
      writer.WriteAssetDescriptor(key, AssetType::kMaterial, kRed,
        "Materials/Red.omat", bytes, {}, FromSource(std::string(kRedSource)));
      (void)writer.Finish();
    }
    {
      LooseCookedWriter writer(cooked_root);
      writer.WriteAssetDescriptor(key, AssetType::kMaterial, kRed,
        "Materials/Red.omat", bytes, {},
        LooseCookedProducer { .source = {}, .completes = std::string(kRed) });
      (void)writer.Finish();
    }

    EXPECT_EQ(Asset(Inspect(cooked_root)).source_path, kRedSource);
    LooseCookedWriter writer(cooked_root);
    EXPECT_THROW(writer.WriteAssetDescriptor(MakeFirstByteAssetKey(0x12),
                   AssetType::kMaterial, "/.cooked/Materials/Blue.omat",
                   "Materials/Blue.omat", bytes, {},
                   LooseCookedProducer { .source = {},
                     .completes = "/.cooked/Scenes/None.oscene" }),
      std::runtime_error);
  }

  //! A table or data file is written again at its path; another path for its
  //! kind is refused.
  NOLINT_TEST(LooseCookedWriterUpdateTest, AFileKindHasOnePath)
  {
    const ScopedTempDir temp;
    const auto cooked_root = temp.Path() / "file_kind";
    const std::vector<std::byte> bytes0 = { std::byte { 0x01 } };
    const std::vector<std::byte> bytes1
      = { std::byte { 0xAA }, std::byte { 0xBB } };

    {
      LooseCookedWriter writer(cooked_root);
      writer.WriteFile(
        FileKind::kBuffersTable, "Resources/buffers.table", bytes0);
      writer.WriteFile(
        FileKind::kBuffersData, "Resources/buffers.data", bytes0);
      (void)writer.Finish();
    }

    LooseCookedWriter writer(cooked_root);
    writer.WriteFile(FileKind::kBuffersData, "Resources/buffers.data", bytes1);
    EXPECT_THROW(writer.WriteFile(
                   FileKind::kBuffersData, "Resources/buffers_v2.data", bytes1),
      std::runtime_error);
    (void)writer.Finish();

    const auto inspection = Inspect(cooked_root);
    const auto files = inspection.Files();
    const auto data = std::ranges::find(
      files, FileKind::kBuffersData, &Inspection::FileEntry::kind);
    ASSERT_NE(data, files.end());
    EXPECT_EQ(data->relpath, "Resources/buffers.data");
    EXPECT_EQ(data->size, 2U);
  }

  NOLINT_TEST(
    LooseCookedWriterUpdateTest, FinishMergesNewAssetWithExistingAssets)
  {
    const ScopedTempDir temp;
    const auto cooked_root = temp.Path() / "merge_assets";
    const auto bytes = Material();

    {
      LooseCookedWriter writer(cooked_root);
      writer.WriteAssetDescriptor(MakeFirstByteAssetKey(0x10),
        AssetType::kMaterial, "/.cooked/Materials/A.omat", "Materials/A.omat",
        bytes, {}, FromSource("/.cooked/Materials/A.omat.json"));
      (void)writer.Finish();
    }
    {
      LooseCookedWriter writer(cooked_root);
      writer.WriteAssetDescriptor(MakeFirstByteAssetKey(0x20),
        AssetType::kMaterial, "/.cooked/Materials/B.omat", "Materials/B.omat",
        bytes, {}, FromSource("/.cooked/Materials/B.omat.json"));
      (void)writer.Finish();
    }

    EXPECT_EQ(Inspect(cooked_root).Assets().size(), 2U);
  }

  //! Every finalized descriptor has its exact content digest.
  NOLINT_TEST(LooseCookedWriterUpdateTest, FinishAlwaysEmitsDescriptorDigest)
  {
    const ScopedTempDir temp;
    const auto cooked_root = temp.Path() / "digest";
    const auto bytes = Material();

    LooseCookedWriter writer(cooked_root);
    writer.WriteAssetDescriptor(MakeFirstByteAssetKey(0x33),
      AssetType::kMaterial, kRed, "Materials/Red.omat", bytes, {},
      FromSource(std::string(kRedSource)));
    (void)writer.Finish();

    const auto asset = Asset(Inspect(cooked_root));
    ASSERT_HAS_VALUE(asset.descriptor_sha256);
    EXPECT_EQ(*asset.descriptor_sha256, oxygen::base::ComputeSha256(bytes));
  }

} // namespace

} // namespace oxygen::content::testing
