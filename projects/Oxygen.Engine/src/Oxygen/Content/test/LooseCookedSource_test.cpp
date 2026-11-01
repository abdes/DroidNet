//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <bit>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Content/Internal/LooseCookedSource.h>
#include <Oxygen/Content/LooseCookedIndex.h>
#include <Oxygen/Cooker/testing/CookedBytes.h>
#include <Oxygen/Cooker/testing/Descriptors.h>
#include <Oxygen/Cooker/testing/LooseCookedRoot.h>
#include <Oxygen/Cooker/testing/Materialize.h>
#include <Oxygen/Cooker/testing/Seeds.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/BufferResource.h>
#include <Oxygen/Data/LooseCookedIndexFormat.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Data/PhysicsResource.h>
#include <Oxygen/Data/ScriptResource.h>
#include <Oxygen/Data/TextureResource.h>
#include <Oxygen/Serio/FileLock.h>
#include <Oxygen/Serio/IoDriver.h>
#include <Oxygen/Serio/NativeFile.h>
#include <Oxygen/Serio/Types.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Testing/TempDirTest.h>

using oxygen::content::internal::LooseCookedSource;
using oxygen::content::lc::LooseCookedIndex;
using oxygen::cooker::testing::CookedBytes;
using oxygen::cooker::testing::LooseCookedRoot;
using oxygen::cooker::testing::MaterialDescriptor;
using oxygen::cooker::testing::Materialize;
using oxygen::cooker::testing::SeedBytes;
using oxygen::data::AssetKey;
using oxygen::data::AssetType;
using oxygen::data::loose_cooked::FileKind;
using oxygen::data::pak::core::ResourceIndexT;

namespace {

constexpr auto kMount = "Content";
constexpr auto kUnsupportedIndexVersion = uint16_t { 999U };
constexpr auto kForgedDescriptorSize = uint64_t { 16U };
constexpr auto kForgedDigestByte = uint8_t { 0xABU };

//! The driver payload-reading mounts await through: it completes on the
//! awaiting thread, as the tests have no loop.
auto InlineDriver() -> std::shared_ptr<oxygen::serio::IoDriver>
{
  return std::make_shared<oxygen::serio::IoDriver>(
    oxygen::serio::IoDriver::CreateInline().value());
}

//! A key no root of these tests lists.
auto AbsentKey() -> AssetKey
{
  return AssetKey::FromVirtualPath("/Content/absent.omat");
}

//! A root of the mount with one buffer, so with a buffers table and data file.
auto RootWithBuffers() -> LooseCookedRoot
{
  auto root = LooseCookedRoot { kMount };
  static_cast<void>(root.AddBuffer(SeedBytes(1U)));
  return root;
}

//! A root of the mount with one material, `test.omat`.
auto RootWithMaterial() -> LooseCookedRoot
{
  auto root = LooseCookedRoot { kMount };
  static_cast<void>(root.Material("test.omat"));
  return root;
}

//! A root of the mount with one auxiliary member of one byte per relpath.
auto RootWithMembers(const std::vector<std::string>& relpaths)
  -> LooseCookedRoot
{
  auto root = LooseCookedRoot { kMount };
  for (const auto& relpath : relpaths) {
    root.AddFile(FileKind::kAuxiliary, relpath, SeedBytes(42U, 1U));
  }
  return root;
}

//! Changes the first byte of the file at `path`, keeping its size.
auto ChangeFirstByte(const std::filesystem::path& path) -> void
{
  constexpr auto mode = static_cast<std::ios::openmode>(
    static_cast<unsigned>(std::ios::in) | static_cast<unsigned>(std::ios::out)
    | static_cast<unsigned>(std::ios::binary));
  auto file = std::fstream(path, mode);
  auto first = char { 0 };
  file.get(first);
  file.seekp(0);
  const auto changed
    = static_cast<unsigned char>(static_cast<unsigned char>(first) ^ 1U);
  file.put(std::bit_cast<char>(changed));
}

class LooseCookedSourceTest : public oxygen::testing::TempDirTest {
protected:
  //! Puts `root` on disk, as the cooked root under test.
  auto Write(const LooseCookedRoot& root) const -> void
  {
    Materialize(root.Files(), CookedRoot());
  }

  auto WriteValidEmptyIndex() const -> void
  {
    Write(LooseCookedRoot { kMount });
  }

  [[nodiscard]] auto CookedRoot() const -> const std::filesystem::path&
  {
    return TempDir();
  }
};

TEST_F(LooseCookedSourceTest, ConstructorMissingIndexFileThrows)
{
  EXPECT_THROW(
    {
      LooseCookedSource source(CookedRoot(),
        LooseCookedSource::OpenMode::kValidateMetadata,
        oxygen::serio::FileLock {}, InlineDriver());
    },
    std::runtime_error);
}

TEST_F(LooseCookedSourceTest, ConstructorValidEmptyIndexInitializes)
{
  WriteValidEmptyIndex();
  EXPECT_NO_THROW({
    LooseCookedSource source(CookedRoot(),
      LooseCookedSource::OpenMode::kValidateMetadata,
      oxygen::serio::FileLock {}, InlineDriver());
  });
}

TEST_F(LooseCookedSourceTest, ConstructorFileMissingThrows)
{
  Write(RootWithBuffers());

  // Remove the data file, but retain the index entry.
  std::filesystem::remove(CookedRoot() / "buffers.data");

  EXPECT_THROW(
    {
      LooseCookedSource source(CookedRoot(),
        LooseCookedSource::OpenMode::kValidateMetadata,
        oxygen::serio::FileLock {}, InlineDriver());
    },
    std::runtime_error);
}

//! A mounted root stays importable: an import opens a data file the mount
//! holds to append to it, sharing only reads, as the importer's writer does.
TEST_F(LooseCookedSourceTest, MountedDataFileAcceptsAnAppendingWriter)
{
  Write(RootWithBuffers());
  const auto source = LooseCookedSource(CookedRoot(),
    LooseCookedSource::OpenMode::kValidateMetadata, oxygen::serio::FileLock {},
    InlineDriver());
  ASSERT_TRUE(source.PayloadRegion(oxygen::data::BufferResource::ClassTypeId())
      .has_value());

  auto writer = oxygen::serio::NativeFile::Open(CookedRoot() / "buffers.data",
    {
      .access = oxygen::serio::Access::kReadWrite,
      .sharing = oxygen::serio::Sharing::kRead,
    });
  ASSERT_HAS_VALUE(writer);
  const auto size = writer->Size();
  ASSERT_HAS_VALUE(size);
  const auto appended = SeedBytes(2U);
  const auto written
    = writer->WriteAt(oxygen::serio::ByteOffset { size->get() }, appended);
  ASSERT_HAS_VALUE(written);
}

TEST_F(LooseCookedSourceTest, ConstructorFileSizeMismatchThrows)
{
  Write(RootWithBuffers());

  // Corrupt the size of the file on disk to be 2
  const auto table_path = CookedRoot() / "buffers.table";
  std::filesystem::resize_file(table_path, 2);

  EXPECT_THROW(
    {
      LooseCookedSource source(CookedRoot(),
        LooseCookedSource::OpenMode::kValidateMetadata,
        oxygen::serio::FileLock {}, InlineDriver());
    },
    std::runtime_error);
}

TEST_F(LooseCookedSourceTest, ConstructorDescriptorMissingThrows)
{
  Write(RootWithMaterial());

  std::filesystem::remove(CookedRoot() / "Descriptors/test.omat");

  EXPECT_THROW(
    {
      LooseCookedSource source(CookedRoot(),
        LooseCookedSource::OpenMode::kValidateMetadata,
        oxygen::serio::FileLock {}, InlineDriver());
    },
    std::runtime_error);
}

TEST_F(LooseCookedSourceTest, ConstructorDescriptorSha256MismatchThrows)
{
  Write(RootWithMaterial());

  // Malform the file data to break its SHA256 checksum, preserving size
  ChangeFirstByte(CookedRoot() / "Descriptors/test.omat");

  // Without verification, loads fine despite invalid hash mismatch
  EXPECT_NO_THROW({
    LooseCookedSource source(CookedRoot(),
      LooseCookedSource::OpenMode::kValidateMetadata,
      oxygen::serio::FileLock {}, InlineDriver());
  });

  // With verification enabled, throws
  EXPECT_THROW(
    {
      LooseCookedSource source(CookedRoot(),
        LooseCookedSource::OpenMode::kVerifyContent, oxygen::serio::FileLock {},
        InlineDriver());
    },
    std::runtime_error);
}

TEST_F(LooseCookedSourceTest, HoldsNoResourcesWhenFilesOmitted)
{
  WriteValidEmptyIndex();
  {
    LooseCookedSource source(CookedRoot(),
      LooseCookedSource::OpenMode::kValidateMetadata,
      oxygen::serio::FileLock {}, InlineDriver());

    for (const auto type : {
           oxygen::data::BufferResource::ClassTypeId(),
           oxygen::data::TextureResource::ClassTypeId(),
           oxygen::data::ScriptResource::ClassTypeId(),
           oxygen::data::PhysicsResource::ClassTypeId(),
         }) {
      EXPECT_FALSE(source.PayloadRegion(type).has_value()) << type;
      EXPECT_FALSE(
        source.ResourceDescriptorBytes(type, ResourceIndexT { 0 }).has_value())
        << type;
    }
    EXPECT_EQ(source.CreateAssetDescriptorReader(AbsentKey()), nullptr);
  }
}

TEST_F(LooseCookedSourceTest, FullIntegrityChecksEveryAuxiliaryFile)
{
  Write(RootWithMembers({ "Resources/first.obuf", "Resources/second.obuf" }));
  EXPECT_NO_THROW({
    LooseCookedSource source(CookedRoot(),
      LooseCookedSource::OpenMode::kVerifyContent, oxygen::serio::FileLock {},
      InlineDriver());
  });

  ChangeFirstByte(CookedRoot() / "Resources/second.obuf");
  EXPECT_NO_THROW({
    LooseCookedSource source(CookedRoot(),
      LooseCookedSource::OpenMode::kValidateMetadata,
      oxygen::serio::FileLock {}, InlineDriver());
  });
  EXPECT_THROW(
    {
      LooseCookedSource source(CookedRoot(),
        LooseCookedSource::OpenMode::kVerifyContent, oxygen::serio::FileLock {},
        InlineDriver());
    },
    std::runtime_error);
}

TEST_F(LooseCookedSourceTest, MetadataAdmissionRejectsExtraAndMissingMembers)
{
  Write(RootWithMembers({ "Resources/first.obuf" }));
  const auto extra = CookedRoot() / "stray.bin";
  {
    std::ofstream output(extra);
    output.put('!');
  }
  EXPECT_THROW(
    {
      LooseCookedSource source(CookedRoot(),
        LooseCookedSource::OpenMode::kValidateMetadata,
        oxygen::serio::FileLock {}, InlineDriver());
    },
    std::runtime_error);
  std::filesystem::remove(extra);
  std::filesystem::remove(CookedRoot() / "Resources/first.obuf");
  EXPECT_THROW(
    {
      LooseCookedSource source(CookedRoot(),
        LooseCookedSource::OpenMode::kValidateMetadata,
        oxygen::serio::FileLock {}, InlineDriver());
    },
    std::runtime_error);
}

TEST_F(LooseCookedSourceTest, NestedContainersAreNotMembershipExceptions)
{
  WriteValidEmptyIndex();
  Materialize(LooseCookedRoot { kMount }.Files(), CookedRoot() / "nested");
  EXPECT_THROW(
    {
      LooseCookedSource source(CookedRoot(),
        LooseCookedSource::OpenMode::kValidateMetadata,
        oxygen::serio::FileLock {}, InlineDriver());
    },
    std::runtime_error);
}

TEST_F(LooseCookedSourceTest, GenerationMarkerIsTheOnlyNonContentMember)
{
  WriteValidEmptyIndex();
  {
    std::ofstream marker(CookedRoot() / ".generation.lock");
  }
  EXPECT_NO_THROW({
    LooseCookedSource source(CookedRoot(),
      LooseCookedSource::OpenMode::kVerifyContent, oxygen::serio::FileLock {},
      InlineDriver());
  });
}

TEST_F(LooseCookedSourceTest, FullIntegritySupportsLongMemberPaths)
{
  const auto relative
    = std::string(120, 'a') + "/" + std::string(120, 'b') + ".obuf";
  Write(RootWithMembers({ relative }));
  EXPECT_GT((CookedRoot() / relative).native().size(), 260U);
  EXPECT_NO_THROW({
    LooseCookedSource source(CookedRoot(),
      LooseCookedSource::OpenMode::kVerifyContent, oxygen::serio::FileLock {},
      InlineDriver());
  });
}

//! A descriptor size and digest the index records instead of the file's
//! round-trip through the index.
TEST_F(LooseCookedSourceTest, IndexKeepsForgedDescriptorSizeAndDigest)
{
  // Arrange
  auto forged_sha = oxygen::base::Sha256Digest {};
  forged_sha.fill(kForgedDigestByte);
  auto root = LooseCookedRoot { kMount };
  const auto key = root.Add({
    .type = AssetType::kMaterial,
    .leaf = "a.omat",
    .descriptor = MaterialDescriptor("a").bytes,
    .recorded_size = kForgedDescriptorSize,
    .recorded_sha256 = forged_sha,
  });
  Write(root);

  // Act
  const auto index = LooseCookedIndex::LoadFromRoot(CookedRoot());

  // Assert
  EXPECT_EQ(index.FindDescriptorSize(key),
    std::optional<std::uint64_t> { kForgedDescriptorSize });
  const auto digest = index.FindDescriptorSha256(key);
  ASSERT_HAS_VALUE(digest);
  EXPECT_TRUE(std::ranges::equal(*digest, forged_sha));
}

//! An unsupported index version must be rejected by the reader.
TEST_F(LooseCookedSourceTest, IndexOfAnUnsupportedVersionIsRejected)
{
  // Arrange
  auto files = RootWithMaterial().Files();
  CookedBytes::SetIndexVersion(files, kUnsupportedIndexVersion);
  Materialize(files, CookedRoot());

  // Act + Assert
  EXPECT_THROW(
    { static_cast<void>(LooseCookedIndex::LoadFromRoot(CookedRoot())); },
    std::runtime_error);
}

//! A mode that reads payloads has no way to await them without a driver.
TEST_F(LooseCookedSourceTest, PayloadReadingModesWithoutADriverThrow)
{
  // Arrange
  Write(RootWithBuffers());

  // Act + Assert
  EXPECT_THROW(
    {
      LooseCookedSource source(
        CookedRoot(), LooseCookedSource::OpenMode::kValidateMetadata);
    },
    std::invalid_argument);
  EXPECT_THROW(
    {
      LooseCookedSource source(
        CookedRoot(), LooseCookedSource::OpenMode::kVerifyContent);
    },
    std::invalid_argument);
}

//! An index-only mount reads no payloads, so it needs no driver.
TEST_F(LooseCookedSourceTest, IndexOnlyModeNeedsNoDriver)
{
  // Arrange
  Write(RootWithBuffers());

  // Act
  const auto source
    = LooseCookedSource(CookedRoot(), LooseCookedSource::OpenMode::kIndexOnly);

  // Assert
  EXPECT_EQ(source.CookedRoot(), CookedRoot());
}

} // namespace
