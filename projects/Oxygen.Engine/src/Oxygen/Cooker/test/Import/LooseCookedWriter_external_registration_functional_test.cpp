//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Internal/LooseCookedWriter.cpp

#include <array>
#include <cstddef>
#include <filesystem>
#include <span>
#include <stdexcept>
#include <vector>

#include "LooseCookedWriterTestSupport.h"

#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Cooker/Cooked/CookedContent.h>
#include <Oxygen/Cooker/Import/Internal/LooseCookedWriter.h>
#include <Oxygen/Cooker/testing/MaterialBuilder.h>
#include <Oxygen/Cooker/testing/MemoryFileSystem.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/LooseCookedIndexFormat.h>
#include <Oxygen/Testing/GTest.h>

namespace oxygen::content::testing {

namespace {

  using oxygen::content::cooked::CookedContent;
  using oxygen::content::import::LooseCookedWriter;
  using oxygen::cooker::testing::MemoryFileSystem;
  namespace fixtures = oxygen::cooker::testing;
  using oxygen::data::AssetType;
  using oxygen::data::loose_cooked::FileKind;

  //! A second writer of a root appends to a data file the first one
  //! published: the index records the grown file. (A root has one live writer,
  //! so no stale writer can publish over the append.)
  NOLINT_TEST(LooseCookedWriterExternalRegistrationTest,
    ExternalAppendRefreshesMetadataWithoutCollision)
  {
    auto storage = MemoryFileSystem {};
    const auto root = MemoryRoot() / "external_append_refresh";
    const auto initial = std::array { std::byte { 1U } };
    const auto table = std::vector<std::byte>(
      cooked::CookedResourceTable::RowSize(cooked::TableKind::kBuffer));
    {
      auto writer = LooseCookedWriter(root, storage);
      writer.WriteFile(FileKind::kBuffersData, "buffers.data", initial);
      writer.WriteFile(FileKind::kBuffersTable, "buffers.table", table);
      static_cast<void>(writer.Finish());
    }
    {
      auto refreshed_writer = LooseCookedWriter(root, storage);
      const auto appended = std::array { std::byte { 1U }, std::byte { 2U } };
      storage.AddFile(root / "buffers.data", appended);
      refreshed_writer.RegisterExternalDataFile(
        FileKind::kBuffersData, "buffers.data");
      static_cast<void>(refreshed_writer.Finish());
    }
    const auto content = CookedContent::Open(storage, root);
    const auto& data
      = content.ResourceTable(data::ResourceKind::kBuffer).DataStored();
    ASSERT_FALSE(data.file.empty());
    EXPECT_EQ(data.range.count.get(), 2U);
  }

  NOLINT_TEST(LooseCookedWriterExternalRegistrationTest,
    ExternalDescriptorRejectsMalformedBytesBeforeIndexPublication)
  {
    auto storage = MemoryFileSystem {};
    const auto root = MemoryRoot() / "external_descriptor_rejection";
    constexpr auto invalid = std::array { std::byte { 1U } };
    storage.AddFile(root / "invalid.omat", invalid);
    import::LooseCookedWriter writer(root, storage);
    EXPECT_THROW(
      writer.RegisterExternalAssetDescriptor(MakeFirstByteAssetKey(1U),
        data::AssetType::kMaterial, "/test/invalid.omat", "invalid.omat", {},
        FromSource("/test/invalid.omat.json")),
      std::runtime_error);
    EXPECT_FALSE(Exists(storage, root / "container.index.bin"));
    EXPECT_TRUE(writer.Finish().assets.empty());
  }

  //! The digest a file's producer computed as it wrote the file is the one
  //! the index records: `Finish` does not read the file again to hash it.
  NOLINT_TEST(LooseCookedWriterExternalRegistrationTest,
    ProducerDigestIsIndexedWithoutRehashing)
  {
    // Arrange
    auto storage = MemoryFileSystem {};
    const auto root = MemoryRoot() / "producer_digest";
    const auto bytes = std::array { std::byte { 1U }, std::byte { 2U } };
    storage.AddFile(root / "Ship/Hull.obuf", bytes);
    storage.AddFile(root / "Textures/Sky.otex", bytes);
    // Not the digest of `bytes`, so the index shows which one it recorded.
    const auto produced = import::LooseCookedFileDigest {
      .size = bytes.size(),
      .sha256 = base::ComputeSha256(std::as_bytes(std::span("produced"))),
    };
    auto writer = LooseCookedWriter(root, storage);
    writer.RegisterExternalAuxiliaryFile(
      "Ship/Hull.obuf", {}, FromSource("/Content/Ship.glb"), produced);
    writer.RegisterExternalTextureDescriptor("Textures/Sky.otex",
      "/Content/Textures/Sky.otex", FromSource("/Content/Ship.glb"), produced);

    // Act
    const auto result = writer.Finish();

    // Assert
    ASSERT_EQ(result.files.size(), 2U);
    for (const auto& file : result.files) {
      EXPECT_EQ(file.size, produced.size) << file.relpath;
      EXPECT_EQ(file.sha256, produced.sha256) << file.relpath;
    }
  }

  //! A descriptor its producer wrote is indexed with the producer's digest:
  //! the writer only measures the file, it does not read it back.
  NOLINT_TEST(LooseCookedWriterExternalRegistrationTest,
    ProducedDescriptorIsIndexedWithoutReadingIt)
  {
    // Arrange
    auto storage = MemoryFileSystem {};
    const auto root = MemoryRoot() / "produced_descriptor";
    const auto bytes = fixtures::MaterialBuilder { "Wood" }.Bytes();
    storage.AddFile(root / "Materials/Wood.omat", bytes);
    // Not the digest of `bytes`, so the index shows which one it recorded.
    const auto produced = import::LooseCookedFileDigest {
      .size = bytes.size(),
      .sha256 = base::ComputeSha256(std::as_bytes(std::span("produced"))),
    };
    auto writer = LooseCookedWriter(root, storage);

    // Act
    writer.RegisterProducedAssetDescriptor(MakeFirstByteAssetKey(1U),
      data::AssetType::kMaterial, "/Content/Materials/Wood.omat",
      "Materials/Wood.omat", produced, {},
      FromSource("/Content/Materials/Wood.omat.json"));
    const auto result = writer.Finish();

    // Assert
    ASSERT_EQ(result.assets.size(), 1U);
    EXPECT_EQ(result.assets.front().descriptor_size, produced.size);
    EXPECT_EQ(result.assets.front().descriptor_sha256, produced.sha256);
  }

  //! A produced descriptor whose file holds another size than its producer
  //! hashed is refused before anything is indexed.
  NOLINT_TEST(LooseCookedWriterExternalRegistrationTest,
    ProducedDescriptorOfAnotherSizeIsRefused)
  {
    auto storage = MemoryFileSystem {};
    const auto root = MemoryRoot() / "produced_size_mismatch";
    const auto bytes = fixtures::MaterialBuilder { "Wood" }.Bytes();
    storage.AddFile(root / "Materials/Wood.omat", bytes);
    auto writer = LooseCookedWriter(root, storage);

    EXPECT_THROW(writer.RegisterProducedAssetDescriptor(
                   MakeFirstByteAssetKey(1U), data::AssetType::kMaterial,
                   "/Content/Materials/Wood.omat", "Materials/Wood.omat",
                   import::LooseCookedFileDigest {
                     .size = bytes.size() + 1U,
                     .sha256 = base::ComputeSha256(bytes),
                   },
                   {}, FromSource("/Content/Materials/Wood.omat.json")),
      std::runtime_error);
    EXPECT_TRUE(writer.Finish().assets.empty());
  }

  //! A file whose size changed after its producer hashed it is refused when
  //! the index is published: its digest no longer describes it.
  NOLINT_TEST(LooseCookedWriterExternalRegistrationTest,
    ProducedFileChangedBeforeFinishIsRefused)
  {
    auto storage = MemoryFileSystem {};
    const auto root = MemoryRoot() / "produced_file_changed";
    const auto bytes = std::array { std::byte { 1U }, std::byte { 2U } };
    storage.AddFile(root / "Ship/Hull.obuf", bytes);
    auto writer = LooseCookedWriter(root, storage);
    writer.RegisterExternalAuxiliaryFile("Ship/Hull.obuf", {},
      FromSource("/Content/Ship.glb"),
      import::LooseCookedFileDigest {
        .size = bytes.size(),
        .sha256 = base::ComputeSha256(bytes),
      });
    storage.AddFile(root / "Ship/Hull.obuf",
      std::array { std::byte { 1U }, std::byte { 2U }, std::byte { 3U } });

    EXPECT_THROW(static_cast<void>(writer.Finish()), std::runtime_error);
    EXPECT_FALSE(Exists(storage, root / "container.index.bin"));
  }

  //! A file that holds another size than its producer hashed is refused when
  //! it is registered.
  NOLINT_TEST(LooseCookedWriterExternalRegistrationTest,
    ProducedFileOfAnotherSizeIsRefused)
  {
    auto storage = MemoryFileSystem {};
    const auto root = MemoryRoot() / "produced_file_size_mismatch";
    const auto bytes = std::array { std::byte { 1U }, std::byte { 2U } };
    storage.AddFile(root / "Ship/Hull.obuf", bytes);
    auto writer = LooseCookedWriter(root, storage);

    EXPECT_THROW(writer.RegisterExternalAuxiliaryFile("Ship/Hull.obuf", {},
                   FromSource("/Content/Ship.glb"),
                   import::LooseCookedFileDigest {
                     .size = bytes.size() + 1U,
                     .sha256 = base::ComputeSha256(bytes),
                   }),
      std::runtime_error);
  }

  //! A resource data file, which every import appends to, is indexed with no
  //! digest: `Finish` does not hash it.
  NOLINT_TEST(LooseCookedWriterExternalRegistrationTest,
    ResourceDataFileIsIndexedWithoutDigest)
  {
    // Arrange
    auto storage = MemoryFileSystem {};
    const auto root = MemoryRoot() / "data_without_digest";
    const auto bytes = std::array { std::byte { 7U } };
    const auto table = std::vector<std::byte>(
      cooked::CookedResourceTable::RowSize(cooked::TableKind::kBuffer));
    storage.AddFile(root / "buffers.data", bytes);
    storage.AddFile(root / "buffers.table", table);
    auto writer = LooseCookedWriter(root, storage);
    writer.RegisterExternalDataFile(FileKind::kBuffersData, "buffers.data");
    writer.RegisterExternalFile(FileKind::kBuffersTable, "buffers.table",
      import::LooseCookedFileDigest {
        .size = table.size(),
        .sha256 = base::ComputeSha256(table),
      });

    // Act
    const auto result = writer.Finish();

    // Assert
    ASSERT_EQ(result.files.size(), 2U);
    for (const auto& file : result.files) {
      if (file.relpath == "buffers.data") {
        EXPECT_TRUE(base::IsAllZero(file.sha256));
      } else {
        EXPECT_FALSE(base::IsAllZero(file.sha256)) << file.relpath;
      }
    }
  }

  //! A resource data file has no digest to register, and a file with one is
  //! not a resource data file.
  NOLINT_TEST(
    LooseCookedWriterExternalRegistrationTest, DataFileKindsAndDigestsDoNotMix)
  {
    // Arrange
    auto storage = MemoryFileSystem {};
    const auto root = MemoryRoot() / "data_kind_mismatch";
    const auto bytes = std::array { std::byte { 7U } };
    storage.AddFile(root / "buffers.data", bytes);
    storage.AddFile(root / "buffers.table", bytes);
    auto writer = LooseCookedWriter(root, storage);

    // Act & Assert
    EXPECT_THROW(
      writer.RegisterExternalFile(FileKind::kBuffersData, "buffers.data",
        import::LooseCookedFileDigest {
          .size = bytes.size(),
          .sha256 = base::ComputeSha256(bytes),
        }),
      std::runtime_error);
    EXPECT_THROW(
      writer.RegisterExternalDataFile(FileKind::kBuffersTable, "buffers.table"),
      std::runtime_error);
  }

  //! A producer digest that does not describe the file's bytes is refused:
  //! at registration, and when the file changes before `Finish`.
  NOLINT_TEST(LooseCookedWriterExternalRegistrationTest,
    ProducerDigestOfAnotherSizeIsRefused)
  {
    // Arrange
    auto storage = MemoryFileSystem {};
    const auto root = MemoryRoot() / "stale_digest";
    const auto bytes = std::array { std::byte { 1U }, std::byte { 2U } };
    storage.AddFile(root / "Ship/Hull.obuf", bytes);
    const auto produced = import::LooseCookedFileDigest {
      .size = bytes.size(),
      .sha256 = base::ComputeSha256(bytes),
    };
    auto wrong_size = produced;
    ++wrong_size.size;
    auto writer = LooseCookedWriter(root, storage);

    // Act & Assert
    EXPECT_THROW(writer.RegisterExternalAuxiliaryFile("Ship/Hull.obuf", {},
                   FromSource("/Content/Ship.glb"), wrong_size),
      std::runtime_error);
    writer.RegisterExternalAuxiliaryFile(
      "Ship/Hull.obuf", {}, FromSource("/Content/Ship.glb"), produced);
    storage.AddFile(root / "Ship/Hull.obuf", std::array { std::byte { 1U } });
    EXPECT_THROW(static_cast<void>(writer.Finish()), std::runtime_error);
  }
} // namespace

} // namespace oxygen::content::testing
