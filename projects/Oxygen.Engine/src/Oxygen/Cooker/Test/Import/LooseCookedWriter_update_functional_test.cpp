//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Internal/LooseCookedWriter.cpp

#include <algorithm>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <vector>

#include "LooseCookedWriterTestSupport.h"

#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Cooker/Import/Internal/LooseCookedWriter.h>
#include <Oxygen/Cooker/Loose/Inspection.h>
#include <Oxygen/Cooker/Loose/LooseCookedLayout.h>
#include <Oxygen/Cooker/Test/Support/DescriptorFixtures.h>
#include <Oxygen/Cooker/Test/Support/TempDir.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/LooseCookedIndexFormat.h>
#include <Oxygen/Testing/GTest.h>

// NOLINTBEGIN(*-magic-numbers)

namespace oxygen::content::testing {
namespace fixtures = oxygen::content::test;

namespace {

  using oxygen::content::import::LooseCookedLayout;
  using oxygen::content::import::LooseCookedWriter;
  using oxygen::cooker::test::ScopedTempDir;
  using CollisionPolicy
    = oxygen::content::import::LooseCookedWriter::CollisionPolicy;
  using oxygen::content::lc::Inspection;
  using oxygen::data::AssetKey;
  using oxygen::data::AssetType;
  using oxygen::data::loose_cooked::FileKind;

  NOLINT_TEST(
    LooseCookedWriterUpdateTest, WriteAssetDescriptorSameKeyUpdatesEntry)
  {
    const ScopedTempDir temp;
    const auto cooked_root = temp.Path() / "loose_cooked_writer_update";

    const auto key = MakeFirstByteAssetKey(0x11);

    const auto first = fixtures::TexturedMaterialDescriptor("white",
      data::pak::core::kFallbackResourceIndex,
      fixtures::MaterialVariant::kWithShader);

    {
      LooseCookedWriter writer(cooked_root);
      writer.WriteAssetDescriptor(key, AssetType::kMaterial,
        "/.cooked/Materials/"
          + LooseCookedLayout::MaterialDescriptorFileName("A"),
        "Materials/" + LooseCookedLayout::MaterialDescriptorFileName("A"),
        first.bytes, first.references);
      (void)writer.Finish();
    }

    const auto second = fixtures::TexturedMaterialDescriptor(
      "error", data::pak::core::kErrorTextureResourceIndex);

    {
      LooseCookedWriter writer(cooked_root);
      writer.WriteAssetDescriptor(key, AssetType::kMaterial,
        "/.cooked/Materials/"
          + LooseCookedLayout::MaterialDescriptorFileName("A"),
        "Materials/" + LooseCookedLayout::MaterialDescriptorFileName("A2"),
        second.bytes, second.references);
      (void)writer.Finish();
    }

    Inspection inspection;
    inspection.LoadFromFile(cooked_root / "container.index.bin");

    const auto assets = inspection.Assets();
    const auto it = std::ranges::find_if(
      assets, [&](const Inspection::AssetEntry& e) { return e.key == key; });

    EXPECT_EQ(assets.size(), 1U);
    ASSERT_NE(it, assets.end());
    EXPECT_EQ(it->descriptor_relpath,
      "Materials/" + LooseCookedLayout::MaterialDescriptorFileName("A2"));
    EXPECT_EQ(it->descriptor_size, second.bytes.size());
    EXPECT_EQ(it->references, second.references);
    EXPECT_EQ(it->descriptor_sha256, base::ComputeSha256(second.bytes));
  }

  /*!
   Scenario: An existing loose index contains a non-canonical AssetKey for the
   same virtual path and descriptor relpath produced by a recook. Reopening the
   cooked root with the native virtual-path-derived key must replace the stale
   mapping instead of failing the index write.
  */
  NOLINT_TEST(LooseCookedWriterUpdateTest,
    WriteAssetDescriptorSameVirtualPathAndRelPathReplacesLoadedStaleKey)
  {
    const ScopedTempDir temp;
    const auto cooked_root
      = temp.Path() / "loose_cooked_writer_recook_legacy_key";

    const auto legacy_key = MakeFirstByteAssetKey(0x71);
    const auto native_key = MakeFirstByteAssetKey(0x72);
    const auto virtual_path = std::string("/.cooked/Materials/")
      + LooseCookedLayout::MaterialDescriptorFileName("A");
    const auto descriptor_relpath = std::string("Materials/")
      + LooseCookedLayout::MaterialDescriptorFileName("A");

    const auto first_bytes = fixtures::MaterialDescriptor(
      "first_bytes", fixtures::MaterialVariant::kPlain)
                               .bytes;
    const auto recook_bytes = fixtures::MaterialDescriptor(
      "recook_bytes", fixtures::MaterialVariant::kWithShader)
                                .bytes;

    {
      LooseCookedWriter writer(cooked_root);
      writer.WriteAssetDescriptor(legacy_key, AssetType::kMaterial,
        virtual_path, descriptor_relpath, first_bytes, {});
      (void)writer.Finish();
    }

    {
      LooseCookedWriter writer(cooked_root);
      writer.WriteAssetDescriptor(native_key, AssetType::kMaterial,
        virtual_path, descriptor_relpath, recook_bytes, {});
      const auto result = writer.Finish();
      EXPECT_EQ(result.collision_summary.asset_collisions, 1U);
      EXPECT_EQ(result.collision_summary.replaced_existing, 1U);
      EXPECT_EQ(result.collision_summary.rejected, 0U);
    }

    Inspection inspection;
    inspection.LoadFromFile(cooked_root / "container.index.bin");

    ASSERT_EQ(inspection.Assets().size(), 1U);
    EXPECT_EQ(inspection.Assets().front().key, native_key);
    EXPECT_EQ(inspection.Assets().front().virtual_path, virtual_path);
    EXPECT_EQ(
      inspection.Assets().front().descriptor_relpath, descriptor_relpath);
    EXPECT_EQ(inspection.Assets().front().descriptor_size, recook_bytes.size());
  }

  NOLINT_TEST(
    LooseCookedWriterUpdateTest, WriteAssetDescriptorDuplicateVirtualPathThrows)
  {
    const ScopedTempDir temp;
    const auto cooked_root = temp.Path() / "loose_cooked_writer_conflict";

    const auto key0 = MakeFirstByteAssetKey(0x11);
    const auto key1 = MakeFirstByteAssetKey(0x22);

    const auto bytes
      = fixtures::MaterialDescriptor("bytes", fixtures::MaterialVariant::kPlain)
          .bytes;

    LooseCookedWriter writer(cooked_root);
    writer.WriteAssetDescriptor(key0, AssetType::kMaterial,
      "/.cooked/Materials/"
        + LooseCookedLayout::MaterialDescriptorFileName("A"),
      "Materials/" + LooseCookedLayout::MaterialDescriptorFileName("A"), bytes,
      {});

    try {
      writer.WriteAssetDescriptor(key1, AssetType::kMaterial,
        "/.cooked/Materials/"
          + LooseCookedLayout::MaterialDescriptorFileName("A"),
        "Materials/" + LooseCookedLayout::MaterialDescriptorFileName("B"),
        bytes, {});
      FAIL() << "Expected virtual path collision.";
    } catch (const std::runtime_error& ex) {
      const std::string message = ex.what();
      EXPECT_THAT(
        message, ::testing::HasSubstr("Conflicting virtual path mapping"));
      EXPECT_THAT(message, ::testing::HasSubstr("WriteAssetDescriptor"));
      EXPECT_THAT(message, ::testing::HasSubstr("incoming_key="));
      EXPECT_THAT(message, ::testing::HasSubstr("existing_key="));
    }
  }

  NOLINT_TEST(LooseCookedWriterUpdateTest, WriteFileSameKindUpdatesEntry)
  {
    const ScopedTempDir temp;
    const auto cooked_root = temp.Path() / "loose_cooked_writer_file_update";

    const std::vector<std::byte> bytes0 = {
      std::byte { 0x01 },
    };
    const std::vector<std::byte> bytes1 = {
      std::byte { 0xAA },
      std::byte { 0xBB },
    };

    {
      LooseCookedWriter writer(cooked_root);
      writer.WriteFile(
        FileKind::kBuffersTable, "Resources/buffers.table", bytes0);
      writer.WriteFile(
        FileKind::kBuffersData, "Resources/buffers.data", bytes0);
      (void)writer.Finish();
    }

    {
      LooseCookedWriter writer(cooked_root);
      writer.WriteFile(
        FileKind::kBuffersData, "Resources/buffers_v2.data", bytes1);
      (void)writer.Finish();
    }

    Inspection inspection;
    inspection.LoadFromFile(cooked_root / "container.index.bin");

    const auto files = inspection.Files();
    const auto it = std::find_if(
      files.begin(), files.end(), [](const Inspection::FileEntry& e) {
        return e.kind == FileKind::kBuffersData;
      });

    EXPECT_EQ(files.size(), 2U);
    ASSERT_NE(it, files.end());
    EXPECT_EQ(it->relpath, "Resources/buffers_v2.data");
    EXPECT_EQ(it->size, 2U);
  }

  //! Duplicate writes in one session overwrite by key/kind.
  /*!
   Scenario: Writes the same AssetKey and the same FileKind twice within a
   single writer session. Verifies current overwrite semantics keep one entry
   per key/kind with latest metadata.
  */
  NOLINT_TEST(LooseCookedWriterUpdateTest,
    SameSessionDuplicateWritesKeepLatestMetadataPerKeyAndKind)
  {
    const ScopedTempDir temp;
    const auto cooked_root
      = temp.Path() / "loose_cooked_writer_same_session_overwrite";

    const auto key = MakeFirstByteAssetKey(0x61);

    const auto desc_a = fixtures::MaterialDescriptor(
      "desc_a", fixtures::MaterialVariant::kPlain)
                          .bytes;
    const auto desc_b = fixtures::MaterialDescriptor(
      "desc_b", fixtures::MaterialVariant::kWithShader)
                          .bytes;
    const std::vector<std::byte> file_a = {
      std::byte { 0x02 },
    };
    const std::vector<std::byte> file_b = {
      std::byte { 0x10 },
      std::byte { 0x20 },
      std::byte { 0x30 },
    };

    LooseCookedWriter writer(cooked_root);
    writer.WriteAssetDescriptor(key, AssetType::kMaterial,
      "/.cooked/Materials/"
        + LooseCookedLayout::MaterialDescriptorFileName("A"),
      "Materials/" + LooseCookedLayout::MaterialDescriptorFileName("A"), desc_a,
      {});
    writer.WriteAssetDescriptor(key, AssetType::kMaterial,
      "/.cooked/Materials/"
        + LooseCookedLayout::MaterialDescriptorFileName("A"),
      "Materials/" + LooseCookedLayout::MaterialDescriptorFileName("B"), desc_b,
      {});

    writer.WriteFile(
      FileKind::kBuffersData, "Resources/buffers_a.data", file_a);
    writer.WriteFile(
      FileKind::kBuffersData, "Resources/buffers_b.data", file_b);
    writer.WriteFile(
      FileKind::kBuffersTable, "Resources/buffers.table", file_a);

    (void)writer.Finish();

    Inspection inspection;
    inspection.LoadFromFile(cooked_root / "container.index.bin");

    ASSERT_EQ(inspection.Assets().size(), 1U);
    EXPECT_EQ(inspection.Assets().front().descriptor_relpath,
      "Materials/" + LooseCookedLayout::MaterialDescriptorFileName("B"));
    EXPECT_EQ(inspection.Assets().front().descriptor_size, desc_b.size());

    const auto files = inspection.Files();
    ASSERT_EQ(files.size(), 2U);
    const auto it
      = std::ranges::find_if(files, [](const Inspection::FileEntry& e) {
          return e.kind == FileKind::kBuffersData;
        });
    ASSERT_NE(it, files.end());
    EXPECT_EQ(it->relpath, "Resources/buffers_b.data");
    EXPECT_EQ(it->size, 3U);
  }

  NOLINT_TEST(
    LooseCookedWriterUpdateTest, CollisionPolicyErrorRejectsFileOverwrite)
  {
    const ScopedTempDir temp;
    const auto cooked_root
      = temp.Path() / "loose_cooked_writer_collision_error";

    const std::vector<std::byte> file_a = { std::byte { 0x01 } };
    const std::vector<std::byte> file_b
      = { std::byte { 0x02 }, std::byte { 0x03 } };

    LooseCookedWriter writer(cooked_root);
    writer.SetCollisionPolicy(CollisionPolicy::kError);
    writer.WriteFile(
      FileKind::kBuffersData, "Resources/buffers_a.data", file_a);
    EXPECT_THROW(writer.WriteFile(
                   FileKind::kBuffersData, "Resources/buffers_b.data", file_b),
      std::runtime_error);
    writer.WriteFile(
      FileKind::kBuffersTable, "Resources/buffers.table", file_a);
    const auto result = writer.Finish();
    EXPECT_EQ(result.collision_summary.file_collisions, 1U);
    EXPECT_EQ(result.collision_summary.kept_existing, 0U);
    EXPECT_EQ(result.collision_summary.replaced_existing, 0U);
    EXPECT_EQ(result.collision_summary.rejected, 1U);
  }

  NOLINT_TEST(
    LooseCookedWriterUpdateTest, CollisionPolicyKeepExistingPreservesFirst)
  {
    const ScopedTempDir temp;
    const auto cooked_root = temp.Path() / "loose_cooked_writer_collision_keep";

    const std::vector<std::byte> bytes_a = { std::byte { 0x01 } };
    const std::vector<std::byte> bytes_b
      = { std::byte { 0x10 }, std::byte { 0x20 } };

    LooseCookedWriter writer(cooked_root);
    writer.SetCollisionPolicy(CollisionPolicy::kWarnKeepExisting);
    writer.WriteFile(
      FileKind::kBuffersData, "Resources/buffers_a.data", bytes_a);
    writer.WriteFile(
      FileKind::kBuffersData, "Resources/buffers_b.data", bytes_b);
    writer.WriteFile(
      FileKind::kBuffersTable, "Resources/buffers.table", bytes_a);
    const auto result = writer.Finish();
    EXPECT_EQ(result.collision_summary.file_collisions, 1U);
    EXPECT_EQ(result.collision_summary.kept_existing, 1U);
    EXPECT_EQ(result.collision_summary.replaced_existing, 0U);
    EXPECT_EQ(result.collision_summary.rejected, 0U);

    Inspection inspection;
    inspection.LoadFromFile(cooked_root / "container.index.bin");
    const auto files = inspection.Files();
    const auto it
      = std::ranges::find_if(files, [](const Inspection::FileEntry& e) {
          return e.kind == FileKind::kBuffersData;
        });
    ASSERT_NE(it, files.end());
    EXPECT_EQ(it->relpath, "Resources/buffers_a.data");
    EXPECT_EQ(it->size, 1U);
  }

  NOLINT_TEST(
    LooseCookedWriterUpdateTest, CollisionPolicyReplaceOverwritesEntry)
  {
    const ScopedTempDir temp;
    const auto cooked_root
      = temp.Path() / "loose_cooked_writer_collision_replace";

    const std::vector<std::byte> bytes_a = { std::byte { 0x01 } };
    const std::vector<std::byte> bytes_b
      = { std::byte { 0x10 }, std::byte { 0x20 } };

    LooseCookedWriter writer(cooked_root);
    writer.SetCollisionPolicy(CollisionPolicy::kWarnReplace);
    writer.WriteFile(
      FileKind::kBuffersData, "Resources/buffers_a.data", bytes_a);
    writer.WriteFile(
      FileKind::kBuffersData, "Resources/buffers_b.data", bytes_b);
    writer.WriteFile(
      FileKind::kBuffersTable, "Resources/buffers.table", bytes_a);
    const auto result = writer.Finish();
    EXPECT_EQ(result.collision_summary.file_collisions, 1U);
    EXPECT_EQ(result.collision_summary.kept_existing, 0U);
    EXPECT_EQ(result.collision_summary.replaced_existing, 1U);
    EXPECT_EQ(result.collision_summary.rejected, 0U);

    Inspection inspection;
    inspection.LoadFromFile(cooked_root / "container.index.bin");
    const auto files = inspection.Files();
    const auto it
      = std::ranges::find_if(files, [](const Inspection::FileEntry& e) {
          return e.kind == FileKind::kBuffersData;
        });
    ASSERT_NE(it, files.end());
    EXPECT_EQ(it->relpath, "Resources/buffers_b.data");
    EXPECT_EQ(it->size, 2U);
  }

  NOLINT_TEST(
    LooseCookedWriterUpdateTest, FinishMergesNewAssetWithExistingAssets)
  {
    const ScopedTempDir temp;
    const auto cooked_root = temp.Path() / "loose_cooked_writer_merge_assets";

    const auto key0 = MakeFirstByteAssetKey(0x10);
    const auto key1 = MakeFirstByteAssetKey(0x20);

    const auto bytes
      = fixtures::MaterialDescriptor("bytes", fixtures::MaterialVariant::kPlain)
          .bytes;

    {
      LooseCookedWriter writer(cooked_root);
      writer.WriteAssetDescriptor(key0, AssetType::kMaterial,
        "/.cooked/Materials/"
          + LooseCookedLayout::MaterialDescriptorFileName("A"),
        "Materials/" + LooseCookedLayout::MaterialDescriptorFileName("A"),
        bytes, {});
      (void)writer.Finish();
    }

    {
      LooseCookedWriter writer(cooked_root);
      writer.WriteAssetDescriptor(key1, AssetType::kMaterial,
        "/.cooked/Materials/"
          + LooseCookedLayout::MaterialDescriptorFileName("B"),
        "Materials/" + LooseCookedLayout::MaterialDescriptorFileName("B"),
        bytes, {});
      (void)writer.Finish();
    }

    Inspection inspection;
    inspection.LoadFromFile(cooked_root / "container.index.bin");

    EXPECT_EQ(inspection.Assets().size(), 2U);
  }

  //! Every finalized descriptor has its exact content digest.
  NOLINT_TEST(LooseCookedWriterUpdateTest, FinishAlwaysEmitsDescriptorDigest)
  {
    const ScopedTempDir temp;
    const auto cooked_root = temp.Path() / "loose_cooked_writer_no_sha";

    const auto key = MakeFirstByteAssetKey(0x33);

    const auto bytes
      = fixtures::MaterialDescriptor("bytes", fixtures::MaterialVariant::kPlain)
          .bytes;

    LooseCookedWriter writer(cooked_root);
    writer.WriteAssetDescriptor(key, AssetType::kMaterial,
      "/.cooked/Materials/"
        + LooseCookedLayout::MaterialDescriptorFileName("A"),
      "Materials/" + LooseCookedLayout::MaterialDescriptorFileName("A"), bytes,
      {});

    (void)writer.Finish();

    Inspection inspection;
    inspection.LoadFromFile(cooked_root / "container.index.bin");

    ASSERT_EQ(inspection.Assets().size(), 1U);
    const auto& asset = inspection.Assets().front();
    ASSERT_TRUE(asset.descriptor_sha256.has_value())
      << "Expected descriptor sha256 to be present";
    EXPECT_EQ(*asset.descriptor_sha256, oxygen::base::ComputeSha256(bytes));
  }
} // namespace

} // namespace oxygen::content::testing

// NOLINTEND(*-magic-numbers)
