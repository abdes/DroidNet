//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Content/Import/Internal/LooseCookedWriter.h>
#include <Oxygen/Content/Import/LooseCookedLayout.h>
#include <Oxygen/Content/LooseCookedInspection.h>
#include <Oxygen/Data/AssetType.h>

namespace oxygen::content::testing {

namespace {

  using oxygen::content::LooseCookedInspection;
  using oxygen::content::import::LooseCookedLayout;
  using oxygen::content::import::LooseCookedWriter;
  using oxygen::data::AssetKey;
  using oxygen::data::AssetType;
  using oxygen::data::SourceKey;
  using oxygen::data::loose_cooked::FileKind;

  //=== Test Helpers
  //===--------------------------------------------------------//

  auto MakeTempCookedRoot(std::string_view suffix) -> std::filesystem::path
  {
    const auto root
      = std::filesystem::temp_directory_path() / "oxygen_content_tests";
    const auto cooked_root = root / std::filesystem::path(std::string(suffix));
    std::error_code ec;
    std::filesystem::remove_all(cooked_root, ec);
    return cooked_root;
  }

  auto MakeTestSourceKey(const uint8_t seed) -> SourceKey
  {
    std::array<uint8_t, 16> bytes {};
    for (uint8_t i = 0; i < 16; ++i) {
      bytes[i] = static_cast<uint8_t>(seed + i);
    }
    return SourceKey(bytes);
  }

  auto IsAllZerosDigest(const oxygen::base::Sha256Digest& digest) -> bool
  {
    return std::all_of(
      digest.begin(), digest.end(), [](const uint8_t b) { return b == 0; });
  }

  struct BadVirtualPathCase final {
    const char* case_name;
    std::string virtual_path;
  };

  //! Parameterized fixture for invalid virtual path strings.
  class BadVirtualPathTest
    : public ::testing::TestWithParam<BadVirtualPathCase> { };

  //=== LooseCookedWriter Tests
  //===--------------------------------------------//

  //! Test: Finish emits a valid loadable index
  /*!
   Scenario: Creates a writer with an explicit source key and no assets/files.
   Verifies that the index is loadable and contains the expected GUID.
  */
  NOLINT_TEST(LooseCookedWriterTest, Finish_EmptyContainer_WritesLoadableIndex)
  {
    // Arrange
    const auto cooked_root = MakeTempCookedRoot("loose_cooked_writer_empty");
    const auto source_key = MakeTestSourceKey(1);

    LooseCookedWriter writer(cooked_root);
    writer.SetSourceKey(source_key);

    // Act
    const auto result = writer.Finish();
    LooseCookedInspection inspection;
    inspection.LoadFromFile(cooked_root / "container.index.bin");

    // Assert
    EXPECT_EQ(result.source_key, source_key);
    EXPECT_EQ(inspection.Guid(), source_key);
    EXPECT_TRUE(inspection.Assets().empty());
    EXPECT_TRUE(inspection.Files().empty());
  }

  //! Test: Updating an existing key replaces its metadata
  /*!
   Scenario: Writes an asset descriptor, then reopens the same cooked root and
   writes the same AssetKey again with a new relpath and bytes.
   Verifies the index contains only one entry for that key.
  */
  NOLINT_TEST(LooseCookedWriterTest, WriteAssetDescriptor_SameKey_UpdatesEntry)
  {
    // Arrange
    const auto cooked_root = MakeTempCookedRoot("loose_cooked_writer_update");

    AssetKey key {};
    key.guid[0] = 0x11;

    const std::vector<std::byte> bytes0 = {
      std::byte { 0x01 },
      std::byte { 0x02 },
      std::byte { 0x03 },
    };

    {
      LooseCookedWriter writer(cooked_root);
      writer.WriteAssetDescriptor(key, AssetType::kMaterial,
        "/.cooked/Materials/"
          + LooseCookedLayout::MaterialDescriptorFileName("A"),
        "Materials/" + LooseCookedLayout::MaterialDescriptorFileName("A"),
        bytes0);
      (void)writer.Finish();
    }

    const std::vector<std::byte> bytes1 = {
      std::byte { 0x04 },
    };

    // Act
    {
      LooseCookedWriter writer(cooked_root);
      writer.WriteAssetDescriptor(key, AssetType::kMaterial,
        "/.cooked/Materials/"
          + LooseCookedLayout::MaterialDescriptorFileName("A"),
        "Materials/" + LooseCookedLayout::MaterialDescriptorFileName("A2"),
        bytes1);
      (void)writer.Finish();
    }

    LooseCookedInspection inspection;
    inspection.LoadFromFile(cooked_root / "container.index.bin");

    const auto assets = inspection.Assets();
    const auto it = std::find_if(assets.begin(), assets.end(),
      [&](const LooseCookedInspection::AssetEntry& e) { return e.key == key; });

    // Assert
    EXPECT_EQ(assets.size(), 1u);
    ASSERT_NE(it, assets.end());
    EXPECT_EQ(it->descriptor_relpath,
      "Materials/" + LooseCookedLayout::MaterialDescriptorFileName("A2"));
    EXPECT_EQ(it->descriptor_size, 1u);
  }

  //! Test: Conflicting virtual path mapping throws
  /*!
   Scenario: Writes two different AssetKeys with the same virtual path.
   Verifies the writer rejects the conflict.
  */
  NOLINT_TEST(
    LooseCookedWriterTest, WriteAssetDescriptor_DuplicateVirtualPath_Throws)
  {
    // Arrange
    const auto cooked_root = MakeTempCookedRoot("loose_cooked_writer_conflict");

    AssetKey key0 {};
    key0.guid[0] = 0x11;
    AssetKey key1 {};
    key1.guid[0] = 0x22;

    const std::vector<std::byte> bytes = {
      std::byte { 0x01 },
    };

    LooseCookedWriter writer(cooked_root);
    writer.WriteAssetDescriptor(key0, AssetType::kMaterial,
      "/.cooked/Materials/"
        + LooseCookedLayout::MaterialDescriptorFileName("A"),
      "Materials/" + LooseCookedLayout::MaterialDescriptorFileName("A"), bytes);

    // Act & Assert
    EXPECT_THROW(
      writer.WriteAssetDescriptor(key1, AssetType::kMaterial,
        "/.cooked/Materials/"
          + LooseCookedLayout::MaterialDescriptorFileName("A"),
        "Materials/" + LooseCookedLayout::MaterialDescriptorFileName("B"),
        bytes),
      std::runtime_error);
  }

  //! Test: Missing required file pair throws
  /*!
   Scenario: Writes only buffers.table without buffers.data.
   Verifies Finish rejects the invalid index state.
  */
  NOLINT_TEST(LooseCookedWriterTest, Finish_MissingBuffersPair_Throws)
  {
    // Arrange
    const auto cooked_root = MakeTempCookedRoot("loose_cooked_writer_pairs");

    const std::vector<std::byte> bytes = {
      std::byte { 0x10 },
    };

    LooseCookedWriter writer(cooked_root);
    writer.WriteFile(FileKind::kBuffersTable, "Resources/buffers.table", bytes);

    // Act & Assert
    EXPECT_THROW(
      { [[maybe_unused]] const auto ignored = writer.Finish(); },
      std::runtime_error);
  }

  //! Test: Missing required textures pair throws
  /*!
   Scenario: Writes only textures.table without textures.data.
   Verifies Finish rejects the invalid index state.
  */
  NOLINT_TEST(LooseCookedWriterTest, Finish_MissingTexturesPair_Throws)
  {
    // Arrange
    const auto cooked_root
      = MakeTempCookedRoot("loose_cooked_writer_textures_pairs");

    const std::vector<std::byte> bytes = {
      std::byte { 0x10 },
    };

    LooseCookedWriter writer(cooked_root);
    writer.WriteFile(
      FileKind::kTexturesTable, "Resources/textures.table", bytes);

    // Act & Assert
    EXPECT_THROW(
      { [[maybe_unused]] const auto ignored = writer.Finish(); },
      std::runtime_error);
  }

  //! Test: Existing GUID is preserved when not overridden
  /*!
   Scenario: Writes an index with an explicit source key.
   Reopens the same cooked root without calling SetSourceKey.
   Verifies the GUID remains unchanged (update semantics).
  */
  NOLINT_TEST(LooseCookedWriterTest, Finish_PreservesExistingSourceKey)
  {
    // Arrange
    const auto cooked_root
      = MakeTempCookedRoot("loose_cooked_writer_guid_preserve");
    const auto source_key = MakeTestSourceKey(7);

    {
      LooseCookedWriter writer(cooked_root);
      writer.SetSourceKey(source_key);
      (void)writer.Finish();
    }

    // Act
    LooseCookedWriter writer(cooked_root);
    const auto result = writer.Finish();

    LooseCookedInspection inspection;
    inspection.LoadFromFile(cooked_root / "container.index.bin");

    // Assert
    EXPECT_EQ(result.source_key, source_key);
    EXPECT_EQ(inspection.Guid(), source_key);
  }

  //! Test: Existing content version is preserved when not overridden
  /*!
   Scenario: Writes an index with an explicit content version.
   Reopens the same cooked root without calling SetContentVersion.
   Verifies the version remains unchanged.
  */
  NOLINT_TEST(LooseCookedWriterTest, Finish_PreservesExistingContentVersion)
  {
    // Arrange
    const auto cooked_root
      = MakeTempCookedRoot("loose_cooked_writer_version_preserve");

    {
      LooseCookedWriter writer(cooked_root);
      writer.SetContentVersion(123);
      (void)writer.Finish();
    }

    // Act
    LooseCookedWriter writer(cooked_root);
    const auto result = writer.Finish();

    // Assert
    EXPECT_EQ(result.content_version, 123);
  }

  //! Test: Updating a file kind replaces its record
  /*!
   Scenario: Writes a buffers.table + buffers.data pair.
   Reopens the same cooked root and writes buffers.data again with a new
   relpath. Verifies there is still exactly one buffers.data record and it was
   updated.
  */
  NOLINT_TEST(LooseCookedWriterTest, WriteFile_SameKind_UpdatesEntry)
  {
    // Arrange
    const auto cooked_root
      = MakeTempCookedRoot("loose_cooked_writer_file_update");

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

    // Act
    {
      LooseCookedWriter writer(cooked_root);
      writer.WriteFile(
        FileKind::kBuffersData, "Resources/buffers_v2.data", bytes1);
      (void)writer.Finish();
    }

    LooseCookedInspection inspection;
    inspection.LoadFromFile(cooked_root / "container.index.bin");

    const auto files = inspection.Files();
    const auto it = std::find_if(files.begin(), files.end(),
      [](const LooseCookedInspection::FileEntry& e) {
        return e.kind == FileKind::kBuffersData;
      });

    // Assert
    EXPECT_EQ(files.size(), 2u);
    ASSERT_NE(it, files.end());
    EXPECT_EQ(it->relpath, "Resources/buffers_v2.data");
    EXPECT_EQ(it->size, 2u);
  }

  //! Test: Writing a new key merges with existing assets
  /*!
   Scenario: Writes one asset, finishes, then reopens the same cooked root
   and writes a second asset with a different key.
   Verifies both assets are present in the merged index.
  */
  NOLINT_TEST(LooseCookedWriterTest, Finish_MergesNewAsset_WithExistingAssets)
  {
    // Arrange
    const auto cooked_root
      = MakeTempCookedRoot("loose_cooked_writer_merge_assets");

    AssetKey key0 {};
    key0.guid[0] = 0x10;
    AssetKey key1 {};
    key1.guid[0] = 0x20;

    const std::vector<std::byte> bytes = {
      std::byte { 0x01 },
    };

    {
      LooseCookedWriter writer(cooked_root);
      writer.WriteAssetDescriptor(key0, AssetType::kMaterial,
        "/.cooked/Materials/"
          + LooseCookedLayout::MaterialDescriptorFileName("A"),
        "Materials/" + LooseCookedLayout::MaterialDescriptorFileName("A"),
        bytes);
      (void)writer.Finish();
    }

    // Act
    {
      LooseCookedWriter writer(cooked_root);
      writer.WriteAssetDescriptor(key1, AssetType::kMaterial,
        "/.cooked/Materials/"
          + LooseCookedLayout::MaterialDescriptorFileName("B"),
        "Materials/" + LooseCookedLayout::MaterialDescriptorFileName("B"),
        bytes);
      (void)writer.Finish();
    }

    LooseCookedInspection inspection;
    inspection.LoadFromFile(cooked_root / "container.index.bin");

    // Assert
    EXPECT_EQ(inspection.Assets().size(), 2u);
  }

  //! Test: Disabling SHA-256 emits zero hashes
  /*!
   Scenario: Disables hashing, writes an asset descriptor, finishes.
   Verifies the emitted descriptor SHA-256 (if present) is all-zero.
  */
  NOLINT_TEST(
    LooseCookedWriterTest, Finish_ComputeSha256Disabled_EmitsZeroHashes)
  {
    // Arrange
    const auto cooked_root = MakeTempCookedRoot("loose_cooked_writer_no_sha");

    AssetKey key {};
    key.guid[0] = 0x33;

    const std::vector<std::byte> bytes = {
      std::byte { 0xDE },
      std::byte { 0xAD },
      std::byte { 0xBE },
      std::byte { 0xEF },
    };

    LooseCookedWriter writer(cooked_root);
    writer.SetComputeSha256(false);
    writer.WriteAssetDescriptor(key, AssetType::kMaterial,
      "/.cooked/Materials/"
        + LooseCookedLayout::MaterialDescriptorFileName("A"),
      "Materials/" + LooseCookedLayout::MaterialDescriptorFileName("A"), bytes);

    // Act
    (void)writer.Finish();

    LooseCookedInspection inspection;
    inspection.LoadFromFile(cooked_root / "container.index.bin");

    // Assert
    ASSERT_EQ(inspection.Assets().size(), 1u);
    const auto& asset = inspection.Assets().front();
    if (asset.descriptor_sha256.has_value()) {
      EXPECT_TRUE(IsAllZerosDigest(*asset.descriptor_sha256));
    }
  }

  //! Test: Invalid virtual path strings throw
  /*!
   Scenario: Attempts to write an asset descriptor using known-invalid virtual
   path strings.
   Verifies validation rejects each case.
  */
  NOLINT_TEST_P(BadVirtualPathTest, WriteAssetDescriptor_Throws)
  {
    // Arrange
    const auto suffix
      = std::string("loose_cooked_writer_bad_vpath_") + GetParam().case_name;
    const auto cooked_root = MakeTempCookedRoot(suffix);

    AssetKey key {};
    key.guid[0] = 0x44;

    const std::vector<std::byte> bytes = {
      std::byte { 0x01 },
    };

    LooseCookedWriter writer(cooked_root);

    // Act & Assert
    EXPECT_THROW(
      writer.WriteAssetDescriptor(key, AssetType::kMaterial,
        std::string_view(GetParam().virtual_path),
        "Materials/" + LooseCookedLayout::MaterialDescriptorFileName("A"),
        bytes),
      std::runtime_error);
  }

  INSTANTIATE_TEST_SUITE_P(BadVirtualPaths, BadVirtualPathTest,
    ::testing::Values(
      BadVirtualPathCase {
        .case_name = "MissingLeadingSlash",
        .virtual_path = std::string(".cooked/")
          + LooseCookedLayout::MaterialDescriptorFileName("A"),
      },
      BadVirtualPathCase {
        .case_name = "DotSegments",
        .virtual_path = std::string("/.cooked/../")
          + LooseCookedLayout::MaterialDescriptorFileName("A"),
      },
      BadVirtualPathCase {
        .case_name = "Backslashes",
        .virtual_path = std::string("\\\\.cooked\\\\")
          + LooseCookedLayout::MaterialDescriptorFileName("A"),
      },
      BadVirtualPathCase {
        .case_name = "DoubleSlash",
        .virtual_path = std::string("/.cooked//")
          + LooseCookedLayout::MaterialDescriptorFileName("A"),
      },
      BadVirtualPathCase {
        .case_name = "TrailingSlash",
        .virtual_path = std::string("/.cooked/")
          + LooseCookedLayout::MaterialDescriptorFileName("A") + "/",
      }),
    [](const ::testing::TestParamInfo<BadVirtualPathCase>& info) {
      return std::string(info.param.case_name);
    });

  //! Test: Descriptor relpath must be container-relative and sanitized
  /*!
   Scenario: Attempts to write an asset with an absolute descriptor path.
   Verifies validation rejects it.
  */
  NOLINT_TEST(
    LooseCookedWriterTest, WriteAssetDescriptor_AbsoluteRelPath_Throws)
  {
    // Arrange
    const auto cooked_root
      = MakeTempCookedRoot("loose_cooked_writer_bad_rel_abs");

    AssetKey key {};
    key.guid[0] = 0x46;

    const std::vector<std::byte> bytes = {
      std::byte { 0x01 },
    };

    LooseCookedWriter writer(cooked_root);

    // Act & Assert
    EXPECT_THROW(
      writer.WriteAssetDescriptor(key, AssetType::kMaterial,
        "/.cooked/Materials/"
          + LooseCookedLayout::MaterialDescriptorFileName("A"),
        "/Materials/" + LooseCookedLayout::MaterialDescriptorFileName("A"),
        bytes),
      std::runtime_error);
  }

  //! Test: File relpath must not use backslashes
  /*!
   Scenario: Attempts to write a file with Windows-style separators.
   Verifies validation rejects it.
  */
  NOLINT_TEST(LooseCookedWriterTest, WriteFile_BackslashesInRelPath_Throws)
  {
    // Arrange
    const auto cooked_root
      = MakeTempCookedRoot("loose_cooked_writer_bad_file_backslashes");

    const std::vector<std::byte> bytes = {
      std::byte { 0x10 },
    };

    LooseCookedWriter writer(cooked_root);

    // Act & Assert
    EXPECT_THROW(writer.WriteFile(
                   FileKind::kBuffersTable, "Resources\\buffers.table", bytes),
      std::runtime_error);
  }

  //! Test: Virtual path conflict is detected across runs
  /*!
    Scenario: Writes an asset with virtual path '/.cooked/Materials/A.omat'.
   Reopens the same cooked root and writes a different key with the same
   virtual path.
   Verifies the second write throws to prevent ambiguous virtual path mapping.
  */
  NOLINT_TEST(LooseCookedWriterTest,
    WriteAssetDescriptor_DuplicateVirtualPathAcrossRuns_Throws)
  {
    // Arrange
    const auto cooked_root
      = MakeTempCookedRoot("loose_cooked_writer_conflict_across_runs");

    AssetKey key0 {};
    key0.guid[0] = 0x50;
    AssetKey key1 {};
    key1.guid[0] = 0x51;

    const std::vector<std::byte> bytes = {
      std::byte { 0x01 },
    };

    {
      LooseCookedWriter writer(cooked_root);
      writer.WriteAssetDescriptor(key0, AssetType::kMaterial,
        "/.cooked/Materials/"
          + LooseCookedLayout::MaterialDescriptorFileName("A"),
        "Materials/" + LooseCookedLayout::MaterialDescriptorFileName("A"),
        bytes);
      (void)writer.Finish();
    }

    // Act & Assert
    LooseCookedWriter writer(cooked_root);
    EXPECT_THROW(
      writer.WriteAssetDescriptor(key1, AssetType::kMaterial,
        "/.cooked/Materials/"
          + LooseCookedLayout::MaterialDescriptorFileName("A"),
        "Materials/" + LooseCookedLayout::MaterialDescriptorFileName("B"),
        bytes),
      std::runtime_error);
  }

  //! Test: Descriptor relpath must not contain '..'
  /*!
   Scenario: Attempts to write an asset with directory traversal in relpath.
   Verifies validation rejects it.
  */
  NOLINT_TEST(LooseCookedWriterTest, WriteAssetDescriptor_RelPathDotDot_Throws)
  {
    // Arrange
    const auto cooked_root
      = MakeTempCookedRoot("loose_cooked_writer_bad_rel_dotdot");

    AssetKey key {};
    key.guid[0] = 0x63;

    const std::vector<std::byte> bytes = {
      std::byte { 0x01 },
    };

    LooseCookedWriter writer(cooked_root);

    // Act & Assert
    EXPECT_THROW(writer.WriteAssetDescriptor(key, AssetType::kMaterial,
                   "/.cooked/Materials/"
                     + LooseCookedLayout::MaterialDescriptorFileName("A"),
                   "Materials/../materials/"
                     + LooseCookedLayout::MaterialDescriptorFileName("A"),
                   bytes),
      std::runtime_error);
  }

  //! Test: Descriptor relpath must not contain ':'
  /*!
   Scenario: Attempts to write an asset with a drive-letter style relpath.
   Verifies validation rejects it.
  */
  NOLINT_TEST(
    LooseCookedWriterTest, WriteAssetDescriptor_RelPathContainsColon_Throws)
  {
    // Arrange
    const auto cooked_root
      = MakeTempCookedRoot("loose_cooked_writer_bad_rel_colon");

    AssetKey key {};
    key.guid[0] = 0x64;

    const std::vector<std::byte> bytes = {
      std::byte { 0x01 },
    };

    LooseCookedWriter writer(cooked_root);

    // Act & Assert
    EXPECT_THROW(
      writer.WriteAssetDescriptor(key, AssetType::kMaterial,
        "/.cooked/Materials/"
          + LooseCookedLayout::MaterialDescriptorFileName("A"),
        "C:/Materials/" + LooseCookedLayout::MaterialDescriptorFileName("A"),
        bytes),
      std::runtime_error);
  }

} // namespace

} // namespace oxygen::content::testing
