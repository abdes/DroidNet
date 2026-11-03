//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Internal/LooseCookedWriter.cpp

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <latch>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>

#include "LooseCookedWriterTestSupport.h"

#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Cooker/Cooked/CookedContent.h>
#include <Oxygen/Cooker/Cooked/CookerStorage.h>
#include <Oxygen/Cooker/Import/Internal/LooseCookedWriter.h>
#include <Oxygen/Cooker/testing/MaterialBuilder.h>
#include <Oxygen/Cooker/testing/MemoryFileSystem.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/LooseCookedIndexEncoder.h>
#include <Oxygen/Data/LooseCookedIndexFormat.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Data/PakFormat_render.h>
#include <Oxygen/Serio/MemoryStream.h>
#include <Oxygen/Serio/Writer.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Testing/ScopedTempDir.h>

namespace oxygen::content::testing {
namespace fixtures = oxygen::cooker::testing;

namespace {

  using oxygen::content::cooked::CookedAsset;
  using oxygen::content::cooked::CookedContent;
  using oxygen::content::import::LooseCookedIdentityCollision;
  using oxygen::content::import::LooseCookedProducer;
  using oxygen::content::import::LooseCookedWriter;
  using oxygen::cooker::testing::MemoryFileSystem;
  using oxygen::data::AssetType;
  using oxygen::data::loose_cooked::FileKind;

  using oxygen::data::loose_cooked::kIndexFileName;
  constexpr std::string_view kRed = "/.cooked/Materials/Red.omat";
  constexpr std::string_view kRedSource = "/.cooked/Materials/Red.omat.json";
  constexpr std::string_view kOtherSource = "/.cooked/Other/Red.omat.json";

  //! The only asset of an index, copied out of its inspection.
  auto Asset(const CookedContent& content) -> CookedAsset
  {
    EXPECT_EQ(content.Assets().size(), 1U);
    return content.Assets().front();
  }

  auto Inspect(const MemoryFileSystem& storage,
    const std::filesystem::path& cooked_root) -> CookedContent
  {
    return CookedContent::Open(storage, cooked_root);
  }

  //! A buffer table of `rows` rows, the size `Open` accepts.
  auto BufferTable(const size_t rows) -> std::vector<std::byte>
  {
    return std::vector<std::byte>(
      rows * cooked::CookedResourceTable::RowSize(cooked::TableKind::kBuffer));
  }

  //! Each entry records the source that produced it.
  NOLINT_TEST(LooseCookedWriterUpdateTest, IndexRecordsTheProducingSource)
  {
    auto storage = MemoryFileSystem {};
    const auto cooked_root = MemoryRoot() / "records_source";
    const auto bytes = fixtures::MaterialBuilder { "bytes" }.Bytes();

    LooseCookedWriter writer(cooked_root, storage);
    writer.WriteAssetDescriptor(MakeFirstByteAssetKey(0x11),
      AssetType::kMaterial, kRed, "Materials/Red.omat", bytes, {},
      FromSource(std::string(kRedSource)));
    (void)writer.Finish();

    EXPECT_EQ(Asset(Inspect(storage, cooked_root)).source_path, kRedSource);
  }

  //! Two writers of one cooked root that finish at once keep each other's
  //! records: each merges the published index under the root's index lock,
  //! which excludes writers in other threads and processes alike.
  NOLINT_TEST(LooseCookedWriterUpdateTest, ConcurrentWritersKeepBothRecords)
  {
    const auto temp = oxygen::testing::ScopedTempDir {};
    auto& storage = cooked::RealCookerStorage();
    const auto cooked_root = temp.Path() / "concurrent";
    const auto bytes = fixtures::MaterialBuilder { "bytes" }.Bytes();
    // Both writers read the root before either publishes: without the lock,
    // the second publication would drop the first one's record.
    LooseCookedWriter red(cooked_root, storage);
    LooseCookedWriter blue(cooked_root, storage);
    red.WriteAssetDescriptor(MakeFirstByteAssetKey(0x31), AssetType::kMaterial,
      "/.cooked/Materials/Red.omat", "Materials/Red.omat", bytes, {},
      FromSource("/.cooked/Materials/Red.omat.json"));
    blue.WriteAssetDescriptor(MakeFirstByteAssetKey(0x32), AssetType::kMaterial,
      "/.cooked/Materials/Blue.omat", "Materials/Blue.omat", bytes, {},
      FromSource("/.cooked/Materials/Blue.omat.json"));

    auto start = std::latch { 2 };
    auto finish = [&start](LooseCookedWriter& writer) -> void {
      start.arrive_and_wait();
      (void)writer.Finish();
    };
    auto red_thread = std::thread(finish, std::ref(red));
    auto blue_thread = std::thread(finish, std::ref(blue));
    red_thread.join();
    blue_thread.join();

    const auto content = CookedContent::Open(storage, cooked_root);
    EXPECT_EQ(content.Assets().size(), 2U);
  }

  //! A source cooking its asset again replaces its entry.
  NOLINT_TEST(LooseCookedWriterUpdateTest, ASourceRecookingItsAssetReplacesIt)
  {
    auto storage = MemoryFileSystem {};
    const auto cooked_root = MemoryRoot() / "recook";
    const auto key = MakeFirstByteAssetKey(0x11);
    auto first = fixtures::MaterialBuilder { "white" };
    first.Texture(&data::pak::render::MaterialAssetDesc::base_color_texture,
      data::pak::core::kFallbackResourceIndex);
    first.PixelShader();
    auto second = fixtures::MaterialBuilder { "error" };
    second.Texture(&data::pak::render::MaterialAssetDesc::base_color_texture,
      data::pak::core::kErrorTextureResourceIndex);

    {
      LooseCookedWriter writer(cooked_root, storage);
      writer.WriteAssetDescriptor(key, AssetType::kMaterial, kRed,
        "Materials/Red.omat", first.Bytes(), first.References(),
        FromSource(std::string(kRedSource)));
      (void)writer.Finish();
    }
    {
      LooseCookedWriter writer(cooked_root, storage);
      writer.WriteAssetDescriptor(key, AssetType::kMaterial, kRed,
        "Materials/Red2.omat", second.Bytes(), second.References(),
        FromSource(std::string(kRedSource)));
      (void)writer.Finish();
    }

    const auto content = Inspect(storage, cooked_root);
    const auto asset = Asset(content);
    EXPECT_EQ(content.DescriptorStored(asset.key)
                .file.lexically_relative(cooked_root)
                .generic_string(),
      "Materials/Red2.omat");
    EXPECT_EQ(asset.references, second.References());
    EXPECT_EQ(asset.descriptor_sha256, base::ComputeSha256(second.Bytes()));
    // The descriptor at its former path is listed no more, so it is deleted.
    EXPECT_FALSE(Exists(storage, cooked_root / "Materials/Red.omat"));
  }

  //! A source's recook under another key replaces its entry at the identity.
  NOLINT_TEST(
    LooseCookedWriterUpdateTest, ASourceRecookingUnderAnotherKeyReplacesIt)
  {
    auto storage = MemoryFileSystem {};
    const auto cooked_root = MemoryRoot() / "recook_key";
    const auto native_key = MakeFirstByteAssetKey(0x72);
    const auto bytes = fixtures::MaterialBuilder { "bytes" }.Bytes();

    {
      LooseCookedWriter writer(cooked_root, storage);
      writer.WriteAssetDescriptor(MakeFirstByteAssetKey(0x71),
        AssetType::kMaterial, kRed, "Materials/Red.omat", bytes, {},
        FromSource(std::string(kRedSource)));
      (void)writer.Finish();
    }
    {
      LooseCookedWriter writer(cooked_root, storage);
      writer.WriteAssetDescriptor(native_key, AssetType::kMaterial, kRed,
        "Materials/Red.omat", bytes, {}, FromSource(std::string(kRedSource)));
      (void)writer.Finish();
    }

    EXPECT_EQ(Asset(Inspect(storage, cooked_root)).key, native_key);
  }

  //! Another source producing an identity is refused, in one cook or later.
  NOLINT_TEST(LooseCookedWriterUpdateTest, AnotherSourceOfAnIdentityIsRefused)
  {
    auto storage = MemoryFileSystem {};
    const auto cooked_root = MemoryRoot() / "other_source";
    const auto key = MakeFirstByteAssetKey(0x11);
    const auto bytes = fixtures::MaterialBuilder { "bytes" }.Bytes();

    {
      LooseCookedWriter writer(cooked_root, storage);
      writer.WriteAssetDescriptor(key, AssetType::kMaterial, kRed,
        "Materials/Red.omat", bytes, {}, FromSource(std::string(kRedSource)));
      EXPECT_THROW(writer.WriteAssetDescriptor(key, AssetType::kMaterial, kRed,
                     "Materials/Red.omat", bytes, {},
                     FromSource(std::string(kOtherSource))),
        LooseCookedIdentityCollision);
      (void)writer.Finish();
    }

    LooseCookedWriter writer(cooked_root, storage);
    EXPECT_THROW(
      writer.WriteAssetDescriptor(key, AssetType::kMaterial, kRed,
        "Materials/Red.omat", bytes, {}, FromSource(std::string(kOtherSource))),
      LooseCookedIdentityCollision);
    EXPECT_EQ(Asset(Inspect(storage, cooked_root)).source_path, kRedSource);
  }

  //! Identities differing only in case are one file on a case-insensitive file
  //! system, so the second is refused whoever produces it.
  NOLINT_TEST(
    LooseCookedWriterUpdateTest, AnIdentityDifferingOnlyInCaseIsRefused)
  {
    auto storage = MemoryFileSystem {};
    const auto cooked_root = MemoryRoot() / "case_only";
    const auto bytes = fixtures::MaterialBuilder { "bytes" }.Bytes();

    {
      LooseCookedWriter writer(cooked_root, storage);
      writer.WriteAssetDescriptor(MakeFirstByteAssetKey(0x11),
        AssetType::kMaterial, kRed, "Materials/Red.omat", bytes, {},
        FromSource(std::string(kRedSource)));
      (void)writer.Finish();
    }

    LooseCookedWriter writer(cooked_root, storage);
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
    auto storage = MemoryFileSystem {};
    const auto cooked_root = MemoryRoot() / "removed_source";
    const auto key = MakeFirstByteAssetKey(0x11);
    const auto bytes = fixtures::MaterialBuilder { "bytes" }.Bytes();

    {
      LooseCookedWriter writer(cooked_root, storage);
      writer.WriteAssetDescriptor(key, AssetType::kMaterial, kRed,
        "Materials/Red.omat", bytes, {}, FromSource(std::string(kRedSource)));
      (void)writer.Finish();
    }
    {
      LooseCookedWriter remover(cooked_root, storage);
      remover.RemoveAssetDescriptor(kRed);
      (void)remover.Finish();
    }
    {
      LooseCookedWriter writer(cooked_root, storage);
      writer.WriteAssetDescriptor(key, AssetType::kMaterial, kRed,
        "Materials/Red.omat", bytes, {}, FromSource(std::string(kOtherSource)));
      (void)writer.Finish();
    }

    EXPECT_EQ(Asset(Inspect(storage, cooked_root)).source_path, kOtherSource);
  }

  //! A sidecar completes an asset, which keeps its producer.
  NOLINT_TEST(LooseCookedWriterUpdateTest, ASidecarKeepsTheProducerOfItsTarget)
  {
    auto storage = MemoryFileSystem {};
    const auto cooked_root = MemoryRoot() / "sidecar";
    const auto key = MakeFirstByteAssetKey(0x11);
    const auto bytes = fixtures::MaterialBuilder { "bytes" }.Bytes();

    {
      LooseCookedWriter writer(cooked_root, storage);
      writer.WriteAssetDescriptor(key, AssetType::kMaterial, kRed,
        "Materials/Red.omat", bytes, {}, FromSource(std::string(kRedSource)));
      (void)writer.Finish();
    }
    {
      LooseCookedWriter writer(cooked_root, storage);
      writer.WriteAssetDescriptor(key, AssetType::kMaterial, kRed,
        "Materials/Red.omat", bytes, {},
        LooseCookedProducer { .source = {}, .completes = std::string(kRed) });
      (void)writer.Finish();
    }

    EXPECT_EQ(Asset(Inspect(storage, cooked_root)).source_path, kRedSource);
    LooseCookedWriter writer(cooked_root, storage);
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
    auto storage = MemoryFileSystem {};
    const auto cooked_root = MemoryRoot() / "file_kind";
    const std::vector<std::byte> bytes0 = { std::byte { 0x01 } };
    const auto table = BufferTable(1U);
    const std::vector<std::byte> bytes1
      = { std::byte { 0xAA }, std::byte { 0xBB } };

    {
      LooseCookedWriter writer(cooked_root, storage);
      writer.WriteFile(
        FileKind::kBuffersTable, "Resources/buffers.table", table);
      writer.WriteFile(
        FileKind::kBuffersData, "Resources/buffers.data", bytes0);
      (void)writer.Finish();
    }

    LooseCookedWriter writer(cooked_root, storage);
    writer.WriteFile(FileKind::kBuffersData, "Resources/buffers.data", bytes1);
    EXPECT_THROW(writer.WriteFile(
                   FileKind::kBuffersData, "Resources/buffers_v2.data", bytes1),
      std::runtime_error);
    (void)writer.Finish();

    const auto content = Inspect(storage, cooked_root);
    const auto& data
      = content.ResourceTable(data::ResourceKind::kBuffer).DataStored();
    ASSERT_FALSE(data.file.empty());
    EXPECT_EQ(data.file.lexically_relative(cooked_root).generic_string(),
      "Resources/buffers.data");
    EXPECT_EQ(data.range.count.get(), 2U);
  }

  //! A data file written whole is indexed with no digest, and its table with
  //! the digest of its bytes.
  NOLINT_TEST(LooseCookedWriterUpdateTest, WrittenDataFileHasNoDigest)
  {
    auto storage = MemoryFileSystem {};
    const auto cooked_root = MemoryRoot() / "data_digest";
    const std::vector<std::byte> bytes = { std::byte { 0x01 } };
    const auto table = BufferTable(1U);
    LooseCookedWriter writer(cooked_root, storage);
    writer.WriteFile(FileKind::kBuffersTable, "Resources/buffers.table", table);
    writer.WriteFile(FileKind::kBuffersData, "Resources/buffers.data", bytes);

    const auto result = writer.Finish();

    ASSERT_EQ(result.files.size(), 2U);
    for (const auto& file : result.files) {
      if (file.kind == FileKind::kBuffersData) {
        EXPECT_TRUE(base::IsAllZero(file.sha256));
      } else {
        EXPECT_EQ(file.sha256, base::ComputeSha256(table));
      }
    }
  }

  NOLINT_TEST(
    LooseCookedWriterUpdateTest, FinishMergesNewAssetWithExistingAssets)
  {
    auto storage = MemoryFileSystem {};
    const auto cooked_root = MemoryRoot() / "merge_assets";
    const auto bytes = fixtures::MaterialBuilder { "bytes" }.Bytes();

    {
      LooseCookedWriter writer(cooked_root, storage);
      writer.WriteAssetDescriptor(MakeFirstByteAssetKey(0x10),
        AssetType::kMaterial, "/.cooked/Materials/A.omat", "Materials/A.omat",
        bytes, {}, FromSource("/.cooked/Materials/A.omat.json"));
      (void)writer.Finish();
    }
    {
      LooseCookedWriter writer(cooked_root, storage);
      writer.WriteAssetDescriptor(MakeFirstByteAssetKey(0x20),
        AssetType::kMaterial, "/.cooked/Materials/B.omat", "Materials/B.omat",
        bytes, {}, FromSource("/.cooked/Materials/B.omat.json"));
      (void)writer.Finish();
    }

    EXPECT_EQ(Inspect(storage, cooked_root).Assets().size(), 2U);
  }

  //! Every finalized descriptor has its exact content digest.
  NOLINT_TEST(LooseCookedWriterUpdateTest, FinishAlwaysEmitsDescriptorDigest)
  {
    auto storage = MemoryFileSystem {};
    const auto cooked_root = MemoryRoot() / "digest";
    const auto bytes = fixtures::MaterialBuilder { "bytes" }.Bytes();

    LooseCookedWriter writer(cooked_root, storage);
    writer.WriteAssetDescriptor(MakeFirstByteAssetKey(0x33),
      AssetType::kMaterial, kRed, "Materials/Red.omat", bytes, {},
      FromSource(std::string(kRedSource)));
    (void)writer.Finish();

    const auto asset = Asset(Inspect(storage, cooked_root));
    EXPECT_EQ(asset.descriptor_sha256, oxygen::base::ComputeSha256(bytes));
  }

  //! The published index is the encoding of what `Finish` reports, streamed
  //! into a publication of the storage.
  NOLINT_TEST(LooseCookedWriterUpdateTest, FinishPublishesTheEncodedIndex)
  {
    // Arrange
    auto storage = MemoryFileSystem {};
    const auto cooked_root = MemoryRoot() / "encoded";
    const auto bytes = fixtures::MaterialBuilder { "bytes" }.Bytes();
    LooseCookedWriter writer(cooked_root, storage);
    writer.WriteAssetDescriptor(MakeFirstByteAssetKey(0x44),
      AssetType::kMaterial, kRed, "Materials/Red.omat", bytes, {},
      FromSource(std::string(kRedSource)));

    // Act
    const auto result = writer.Finish();

    // Assert
    auto expected = serio::MemoryStream {};
    auto encoder = serio::Writer(expected);
    const auto encoded = data::loose_cooked::EncodeLooseCookedIndex(
      {
        .source_key = result.source_key,
        .content_version = result.content_version,
        .assets = result.assets,
        .files = result.files,
      },
      encoder);
    ASSERT_TRUE(encoded.has_value());
    const auto published
      = storage.Bytes(cooked_root / std::string(kIndexFileName));
    EXPECT_TRUE(std::ranges::equal(published, expected.Data()));
  }

  //! An index that cannot be published fails `Finish` and leaves the
  //! published index as it was.
  NOLINT_TEST(LooseCookedWriterUpdateTest, FailedPublicationKeepsTheIndex)
  {
    // Arrange
    auto storage = MemoryFileSystem {};
    const auto cooked_root = MemoryRoot() / "unpublishable";
    const auto index_path = cooked_root / std::string(kIndexFileName);
    const auto bytes = fixtures::MaterialBuilder { "bytes" }.Bytes();
    {
      LooseCookedWriter writer(cooked_root, storage);
      writer.WriteAssetDescriptor(MakeFirstByteAssetKey(0x55),
        AssetType::kMaterial, kRed, "Materials/Red.omat", bytes, {},
        FromSource(std::string(kRedSource)));
      (void)writer.Finish();
    }
    const auto before = std::vector<std::byte>(
      storage.Bytes(index_path).begin(), storage.Bytes(index_path).end());
    storage.FailPublishing(index_path);
    LooseCookedWriter writer(cooked_root, storage);
    writer.RemoveAssetDescriptor(kRed);

    // Act & Assert
    EXPECT_THROW((void)writer.Finish(), std::system_error);
    EXPECT_TRUE(std::ranges::equal(storage.Bytes(index_path), before));
    EXPECT_TRUE(Exists(storage, cooked_root / "Materials/Red.omat"));
  }

} // namespace

} // namespace oxygen::content::testing
