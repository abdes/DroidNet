//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <filesystem>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Content/Internal/LooseCookedIndexImpl.h>
#include <Oxygen/Content/LooseCookedIndex.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/CookedOpenError.h>
#include <Oxygen/Data/LooseCookedIndexEncoder.h>
#include <Oxygen/Data/LooseCookedIndexFormat.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Data/SourceKey.h>
#include <Oxygen/Serio/AtomicFile.h>
#include <Oxygen/Serio/MemoryStream.h>
#include <Oxygen/Serio/Writer.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Testing/TempDirTest.h>

namespace {

using oxygen::content::internal::LooseCookedIndexImpl;
using oxygen::content::lc::IntegrityCheck;
using oxygen::content::lc::IntegrityFailure;
using oxygen::content::lc::LooseCookedIndex;
using oxygen::data::AssetKey;
using oxygen::data::AssetType;
using oxygen::data::CookedOpenError;
using oxygen::data::CookedOpenFailure;
using oxygen::data::loose_cooked::AssetEntry;
using oxygen::data::loose_cooked::FileKind;
using oxygen::data::loose_cooked::FileRecord;
using oxygen::data::loose_cooked::IndexContent;
using oxygen::data::loose_cooked::IndexedAsset;
using oxygen::data::loose_cooked::IndexedFile;
using oxygen::data::loose_cooked::IndexHeader;

using Bytes = std::vector<std::byte>;

constexpr auto kSource = std::string_view { "/Content/Ship/Ship.glb" };

//! A fixed UUIDv7 source identity.
auto TestSourceKey() -> oxygen::data::SourceKey
{
  auto bytes = std::array<uint8_t, 16> {};
  for (uint8_t i = 0; i < bytes.size(); ++i) {
    bytes.at(i) = static_cast<uint8_t>(i + 1);
  }
  bytes.at(6) = static_cast<uint8_t>((bytes.at(6) & 0x0FU) | 0x70U);
  bytes.at(8) = static_cast<uint8_t>((bytes.at(8) & 0x3FU) | 0x80U);
  return oxygen::data::SourceKey::FromBytes(bytes).value();
}

auto DigestOf(const std::string_view text) -> oxygen::base::Sha256Digest
{
  return oxygen::base::ComputeSha256(std::as_bytes(std::span(text)));
}

//! A material asset; its key is derived from `key_path`, which defaults to
//! the virtual path and must be canonical.
auto Material(const std::string_view virtual_path,
  const std::string_view relpath, const std::string_view key_path = {})
  -> IndexedAsset
{
  return {
    .key
    = AssetKey::FromVirtualPath(key_path.empty() ? virtual_path : key_path),
    .asset_type = AssetType::kMaterial,
    .virtual_path = std::string(virtual_path),
    .descriptor_relpath = std::string(relpath),
    .descriptor_size = relpath.size(),
    .descriptor_sha256 = DigestOf(relpath),
    .references = {},
    .source_path = std::string(kSource),
  };
}

auto File(const FileKind kind, const std::string_view relpath) -> IndexedFile
{
  return {
    .kind = kind,
    .relpath = std::string(relpath),
    .size = relpath.size(),
    .sha256 = DigestOf(relpath),
    .virtual_path = {},
    .source_path = {},
  };
}

auto Encode(const std::span<const IndexedAsset> assets,
  const std::span<const IndexedFile> files = {}) -> Bytes
{
  auto stream = oxygen::serio::MemoryStream {};
  auto writer = oxygen::serio::Writer(stream);
  const auto encoded = oxygen::data::loose_cooked::EncodeLooseCookedIndex(
    IndexContent {
      .source_key = TestSourceKey(),
      .content_version = 7,
      .assets = assets,
      .files = files,
    },
    writer);
  EXPECT_TRUE(encoded.has_value());
  const auto bytes = stream.Data();
  return { bytes.begin(), bytes.end() };
}

auto HeaderOf(const Bytes& bytes) -> IndexHeader
{
  auto header = IndexHeader {};
  std::memcpy(&header, bytes.data(), sizeof(header));
  return header;
}

//! Overwrites the bytes at `offset` with the object representation of
//! `value`, to forge what the encoder never writes.
template <typename T>
auto Patch(Bytes& bytes, const uint64_t offset, const T& value) -> void
{
  std::memcpy(bytes.data() + offset, &value, sizeof(T));
}

//! Replaces the string `valid` of the encoded string table with `broken`, of
//! the same length: the encoder refuses what the loader must refuse, so a
//! refusal test damages valid bytes.
auto ReplaceString(Bytes& bytes, const std::string_view valid,
  const std::string_view broken) -> void
{
  ASSERT_EQ(valid.size(), broken.size());
  auto whole = std::string(1, '\0') + std::string(valid) + '\0';
  const auto text = std::string_view(
    reinterpret_cast<const char*>(bytes.data()), bytes.size());
  const auto at = text.find(whole);
  ASSERT_NE(at, std::string_view::npos) << valid;
  std::memcpy(bytes.data() + at + 1, broken.data(), broken.size());
}

auto PatchHeader(Bytes& bytes, const std::function<void(IndexHeader&)>& edit)
  -> void
{
  auto header = HeaderOf(bytes);
  edit(header);
  Patch(bytes, 0, header);
}

auto AssetEntryOffset(const Bytes& bytes, const size_t index) -> uint64_t
{
  return HeaderOf(bytes).asset_entries_offset + (index * sizeof(AssetEntry));
}

auto FileRecordOffset(const Bytes& bytes, const size_t index) -> uint64_t
{
  return HeaderOf(bytes).file_records_offset + (index * sizeof(FileRecord));
}

//! The failure loading `bytes` raises; the test fails when they load.
auto RefusalOf(const Bytes& bytes) -> std::pair<CookedOpenFailure, std::string>
{
  try {
    static_cast<void>(LooseCookedIndexImpl::LoadFromBytes(bytes));
  } catch (const CookedOpenError& error) {
    return { error.Failure(), error.what() };
  }
  ADD_FAILURE() << "the index loaded";
  return {};
}

auto ExpectCorrupt(const Bytes& bytes, const std::string_view message) -> void
{
  const auto [failure, what] = RefusalOf(bytes);
  EXPECT_EQ(failure, CookedOpenFailure::kCorrupt) << what;
  EXPECT_THAT(what, testing::HasSubstr(std::string(message)));
}

//=== Encoding and loading ===------------------------------------------------//

//! Everything the encoder writes reads back through the loader.
NOLINT_TEST(LooseCookedIndexLoadTest, EncodedContentReadsBack)
{
  // Arrange
  auto references = oxygen::data::AssetReferences::Create(
    { { .kind = oxygen::data::ResourceKind::kTexture,
      .index = oxygen::data::pak::core::ResourceIndexT { 3U } } },
    { { .key = AssetKey::FromVirtualPath("/Content/Materials/Base.omat"),
      .kind = oxygen::data::KeyReferenceKind::kAsset,
      .expected_type = AssetType::kMaterial } });
  ASSERT_HAS_VALUE(references);
  auto wood = Material("/Content/Materials/Wood.omat", "Materials/Wood.omat");
  wood.references = std::move(*references);
  const auto assets = std::array {
    wood,
    Material("/Content/Materials/Base.omat", "Materials/Base.omat"),
  };
  auto sky = File(FileKind::kTextureDescriptor, "Textures/Sky.otex");
  sky.virtual_path = "/Content/Textures/Sky.otex";
  // A texture descriptor records the source that produced it.
  sky.source_path = std::string(kSource);
  const auto files = std::array {
    File(FileKind::kTexturesTable, "Resources/textures.table"),
    File(FileKind::kTexturesData, "Resources/textures.data"),
    sky,
  };

  // Act
  const auto index = LooseCookedIndexImpl::LoadFromBytes(Encode(assets, files));

  // Assert
  EXPECT_EQ(index.Guid(), TestSourceKey());
  EXPECT_EQ(index.ContentVersion(), 7U);
  ASSERT_EQ(index.GetAllAssetKeys().size(), 2U);
  for (const auto& asset : assets) {
    EXPECT_EQ(index.FindDescriptorRelPath(asset.key), asset.descriptor_relpath);
    EXPECT_EQ(index.FindVirtualPath(asset.key), asset.virtual_path);
    EXPECT_EQ(index.FindSourcePath(asset.key), kSource);
    EXPECT_EQ(index.FindDescriptorSize(asset.key), asset.descriptor_size);
    EXPECT_EQ(index.FindAssetType(asset.key),
      static_cast<uint8_t>(AssetType::kMaterial));
    EXPECT_EQ(index.FindAssetKeyByVirtualPath(asset.virtual_path), asset.key);
    const auto stored = index.FindAssetReferences(asset.key);
    ASSERT_HAS_VALUE(stored);
    EXPECT_EQ(*stored, asset.references);
  }
  EXPECT_EQ(
    index.FindFileRelPath(FileKind::kTexturesData), "Resources/textures.data");
  EXPECT_EQ(index.FindTextureDescriptorRelPath("/Content/Textures/Sky.otex"),
    "Textures/Sky.otex");
  const auto inventory = index.GetFileInventory();
  ASSERT_EQ(inventory.size(), assets.size() + files.size());
}

//! Equal content encodes to equal bytes, whatever order it is listed in.
NOLINT_TEST(LooseCookedIndexLoadTest, EncodingIgnoresListingOrder)
{
  // Arrange
  const auto wood
    = Material("/Content/Materials/Wood.omat", "Materials/Wood.omat");
  const auto base
    = Material("/Content/Materials/Base.omat", "Materials/Base.omat");
  const auto table = File(FileKind::kBuffersTable, "Resources/buffers.table");
  const auto data = File(FileKind::kBuffersData, "Resources/buffers.data");

  // Act
  const auto first
    = Encode(std::array { wood, base }, std::array { table, data });
  const auto second
    = Encode(std::array { base, wood }, std::array { data, table });

  // Assert
  EXPECT_EQ(first, second);
}

//! An index of nothing is a valid index.
NOLINT_TEST(LooseCookedIndexLoadTest, EmptyIndexLoads)
{
  // Act
  const auto index = LooseCookedIndexImpl::LoadFromBytes(Encode({}));

  // Assert
  EXPECT_TRUE(index.GetAllAssetKeys().empty());
  EXPECT_TRUE(index.GetFileInventory().empty());
}

//! Owned bytes load in place, like a mapped file.
NOLINT_TEST(LooseCookedIndexLoadTest, OwnedBytesLoad)
{
  // Arrange
  const auto wood
    = Material("/Content/Materials/Wood.omat", "Materials/Wood.omat");
  auto bytes = Encode(std::array { wood });

  // Act
  const auto index = LooseCookedIndex::LoadFromBytes(std::move(bytes));

  // Assert
  EXPECT_EQ(index.FindVirtualPath(wood.key), wood.virtual_path);
}

//=== Header refusals ===-----------------------------------------------------//

//! Bytes that are not an index are a wrong format, not a corrupt index.
NOLINT_TEST(LooseCookedIndexHeaderTest, ForeignBytesAreAWrongFormat)
{
  // Arrange
  auto wrong_magic = Encode({});
  wrong_magic.front() = std::byte { 'X' };
  const auto too_small = Bytes(sizeof(IndexHeader) - 1);

  // Act & Assert
  EXPECT_EQ(RefusalOf(wrong_magic).first, CookedOpenFailure::kWrongFormat);
  EXPECT_EQ(RefusalOf(too_small).first, CookedOpenFailure::kWrongFormat);
}

NOLINT_TEST(LooseCookedIndexHeaderTest, AnotherVersionIsUnsupported)
{
  // Arrange
  auto bytes = Encode({});
  PatchHeader(bytes, [](IndexHeader& header) -> void { header.version = 999; });

  // Act & Assert
  EXPECT_EQ(RefusalOf(bytes).first, CookedOpenFailure::kUnsupportedVersion);
}

NOLINT_TEST(LooseCookedIndexHeaderTest, MalformedHeadersAreCorrupt)
{
  using oxygen::data::loose_cooked::kHasFileRecords;
  using oxygen::data::loose_cooked::kHasVirtualPaths;

  struct Case {
    std::string_view message;
    std::function<void(IndexHeader&)> edit;
  };
  const auto cases = std::array {
    Case {
      .message = "String table must start after index header",
      .edit = [](IndexHeader& h) -> void { h.string_table_offset = 8; },
    },
    Case {
      .message = "Asset entries must start after the end of the string table",
      .edit = [](IndexHeader& h) -> void { --h.asset_entries_offset; },
    },
    Case {
      .message = "Unsupported IndexHeader flags",
      .edit = [](IndexHeader& h) -> void { h.flags |= 0x80U; },
    },
    Case {
      .message = "must declare virtual-path support",
      .edit = [](IndexHeader& h) -> void { h.flags = kHasFileRecords; },
    },
    Case {
      .message = "flags disallow file records",
      .edit = [](IndexHeader& h) -> void { h.flags = kHasVirtualPaths; },
    },
    Case {
      .message = "non-zero UUIDv7 source identity",
      .edit = [](IndexHeader& h) -> void { h.source_identity = {}; },
    },
    Case {
      .message = "Unexpected AssetEntry size",
      .edit = [](IndexHeader& h) -> void { ++h.asset_entry_size; },
    },
  };
  const auto files = std::array {
    File(FileKind::kBuffersTable, "Resources/buffers.table"),
    File(FileKind::kBuffersData, "Resources/buffers.data"),
  };
  for (const auto& test_case : cases) {
    // Arrange
    auto bytes = Encode({}, files);
    PatchHeader(bytes, test_case.edit);

    // Act & Assert
    ExpectCorrupt(bytes, test_case.message);
  }
}

//=== Asset entry refusals ===------------------------------------------------//

//! Virtual paths are canonical; a custom mount root is one.
NOLINT_TEST(LooseCookedIndexAssetTest, VirtualPathsMustBeCanonical)
{
  // Arrange
  const auto custom = Material("/Props/Wood.omat", "Wood.omat");
  const auto no_slash
    = Material("Content/Wood.omat", "Wood.omat", "/Content/Wood.omat");
  const auto dot_dot
    = Material("/Content/../Wood.omat", "Wood.omat", "/Content/Wood.omat");

  // Act & Assert
  EXPECT_NO_THROW(static_cast<void>(
    LooseCookedIndexImpl::LoadFromBytes(Encode(std::array { custom }))));
  ExpectCorrupt(
    Encode(std::array { no_slash }), "Virtual path is not canonical");
  ExpectCorrupt(
    Encode(std::array { dot_dot }), "Virtual path is not canonical");
}

NOLINT_TEST(LooseCookedIndexAssetTest, ContentPathsMustBeContainerRelative)
{
  struct Case {
    //! A valid path, encoded, then replaced by `relpath`.
    std::string_view valid;
    std::string_view relpath;
    std::string_view message;
  };
  const auto cases = std::array {
    Case { .valid = "Materials/Wood.omat",
      .relpath = "Materials\\Wood.omat",
      .message = "'/' as the separator" },
    Case { .valid = "AMaterials/Wood.omat",
      .relpath = "/Materials/Wood.omat",
      .message = "container-relative" },
    Case { .valid = "Materials/xx/Wood.omat",
      .relpath = "Materials/../Wood.omat",
      .message = "'..'" },
    Case { .valid = "container.index.bxn",
      .relpath = "container.index.bin",
      .message = "Reserved path" },
  };
  for (const auto& test_case : cases) {
    // Arrange
    auto bytes = Encode(
      std::array { Material("/Content/Materials/Wood.omat", test_case.valid) });
    ReplaceString(bytes, test_case.valid, test_case.relpath);

    // Act & Assert
    ExpectCorrupt(bytes, test_case.message);
  }
}

NOLINT_TEST(LooseCookedIndexAssetTest, IdentitiesListedTwiceAreCorrupt)
{
  // Arrange
  const auto assets = std::array {
    Material("/Content/Materials/A.omat", "Materials/A.omat"),
    Material("/Content/Materials/B.omat", "Materials/B.omat"),
  };
  const auto encoded = Encode(assets);
  auto first = AssetEntry {};
  std::memcpy(
    &first, encoded.data() + AssetEntryOffset(encoded, 0), sizeof(first));
  auto same_key = encoded;
  Patch(same_key,
    AssetEntryOffset(same_key, 1) + offsetof(AssetEntry, asset_key),
    first.asset_key);
  auto same_path = encoded;
  Patch(same_path,
    AssetEntryOffset(same_path, 1) + offsetof(AssetEntry, virtual_path_offset),
    first.virtual_path_offset);
  auto same_file = encoded;
  Patch(same_file,
    AssetEntryOffset(same_file, 1)
      + offsetof(AssetEntry, descriptor_relpath_offset),
    first.descriptor_relpath_offset);

  // Act & Assert
  ExpectCorrupt(same_key, "Duplicate AssetKey");
  ExpectCorrupt(same_path, "Duplicate virtual path");
  ExpectCorrupt(same_file, "Duplicate content path");
}

NOLINT_TEST(LooseCookedIndexAssetTest, DescriptorWithoutDigestIsCorrupt)
{
  // Arrange
  auto bytes = Encode(
    std::array { Material("/Content/Materials/A.omat", "Materials/A.omat") });
  Patch(bytes,
    AssetEntryOffset(bytes, 0) + offsetof(AssetEntry, descriptor_sha256),
    oxygen::base::Sha256Digest {});

  // Act & Assert
  ExpectCorrupt(bytes, "Missing descriptor SHA-256");
}

//=== File record refusals ===------------------------------------------------//

//! Each resource table comes with its data file, and each data file with its
//! table.
NOLINT_TEST(LooseCookedIndexFileTest, TablesAndDataFilesComeInPairs)
{
  struct Pair {
    FileKind table;
    FileKind data;
  };
  const auto pairs = std::array {
    Pair { .table = FileKind::kBuffersTable, .data = FileKind::kBuffersData },
    Pair { .table = FileKind::kTexturesTable, .data = FileKind::kTexturesData },
    Pair { .table = FileKind::kScriptsTable, .data = FileKind::kScriptsData },
    Pair { .table = FileKind::kPhysicsTable, .data = FileKind::kPhysicsData },
  };
  for (const auto& pair : pairs) {
    // Arrange
    const auto table = File(pair.table, "Resources/x.table");
    const auto data = File(pair.data, "Resources/x.data");

    // Act & Assert
    EXPECT_NO_THROW(static_cast<void>(LooseCookedIndexImpl::LoadFromBytes(
      Encode({}, std::array { table, data }))));
    ExpectCorrupt(Encode({}, std::array { table }), "must provide both");
    ExpectCorrupt(Encode({}, std::array { data }), "must provide both");
  }
}

NOLINT_TEST(LooseCookedIndexFileTest, MalformedRecordsAreCorrupt)
{
  // Records are in path order: the auxiliary file, then the table and data.
  const auto files = std::array {
    File(FileKind::kAuxiliary, "Ships/Hull.obuf"),
    File(FileKind::kBuffersTable, "buffers.table"),
    File(FileKind::kBuffersData, "buffers.data"),
  };
  struct Case {
    std::string_view message;
    std::function<void(Bytes&)> edit;
  };
  const auto kind_at = [](const Bytes& bytes) -> uint64_t {
    return FileRecordOffset(bytes, 0) + offsetof(FileRecord, kind);
  };
  const auto cases = std::array {
    Case {
      .message = "Unsupported FileKind",
      .edit = [&](Bytes& bytes) -> void {
        Patch(bytes, kind_at(bytes), uint16_t { 99 });
      },
    },
    Case {
      .message = "Duplicate FileKind record",
      .edit = [&](Bytes& bytes) -> void {
        Patch(bytes, kind_at(bytes), FileKind::kBuffersTable);
      },
    },
    Case {
      .message = "Missing file SHA-256",
      .edit = [](Bytes& bytes) -> void {
        Patch(bytes, FileRecordOffset(bytes, 0) + offsetof(FileRecord, sha256),
          oxygen::base::Sha256Digest {});
      },
    },
  };
  for (const auto& test_case : cases) {
    // Arrange
    auto bytes = Encode({}, files);
    test_case.edit(bytes);

    // Act & Assert
    ExpectCorrupt(bytes, test_case.message);
  }
}

//! A texture descriptor record keeps the virtual path it is addressed by.
NOLINT_TEST(LooseCookedIndexFileTest, TextureDescriptorKeepsItsVirtualPath)
{
  // Arrange
  auto sky = File(FileKind::kTextureDescriptor, "Textures/Sky.otex");
  sky.virtual_path = "/Content/Textures/Sky.otex";
  // A texture descriptor records the source that produced it.
  sky.source_path = std::string(kSource);

  // Act
  const auto inventory
    = LooseCookedIndexImpl::LoadFromBytes(Encode({}, std::array { sky }))
        .GetFileInventory();

  // Assert
  ASSERT_EQ(inventory.size(), 1U);
  EXPECT_EQ(inventory.front().kind, FileKind::kTextureDescriptor);
  EXPECT_EQ(inventory.front().virtual_path, "/Content/Textures/Sky.otex");
}

//! A texture descriptor needs a canonical virtual path of its own; no other
//! file record may carry one.
NOLINT_TEST(LooseCookedIndexFileTest, InvalidFileVirtualPathsAreCorrupt)
{
  // A texture descriptor and an auxiliary file, both valid; each case then
  // points one record's virtual path elsewhere in the string table.
  auto sky = File(FileKind::kTextureDescriptor, "Textures/Sky.otex");
  sky.virtual_path = "/Content/Textures/Sky.otex";
  sky.source_path = std::string(kSource);
  const auto hull = File(FileKind::kAuxiliary, "Ship/Hull.obuf");
  const auto valid = Encode({}, std::array { hull, sky });
  // Files are encoded in path order: the hull's record, then the sky's.
  constexpr auto kHull = size_t { 0 };
  constexpr auto kSky = size_t { 1 };
  const auto sky_record = [&valid] -> FileRecord {
    auto record = FileRecord {};
    std::memcpy(
      &record, valid.data() + FileRecordOffset(valid, kSky), sizeof(record));
    return record;
  }();
  const auto vpath_field = offsetof(FileRecord, virtual_path_offset);

  struct Case {
    std::string_view name;
    size_t record;
    uint32_t virtual_path_offset;
  };
  const auto cases = std::array {
    // The table's leading NUL: an empty virtual path.
    Case { .name = "texture without virtual path",
      .record = kSky,
      .virtual_path_offset = 0U },
    // Past the leading '/': `Content/Textures/Sky.otex`, not canonical.
    Case { .name = "texture with a relative virtual path",
      .record = kSky,
      .virtual_path_offset = sky_record.virtual_path_offset + 1U },
    Case { .name = "auxiliary file with a virtual path",
      .record = kHull,
      .virtual_path_offset = sky_record.virtual_path_offset },
  };
  for (const auto& test_case : cases) {
    // Arrange
    auto bytes = valid;
    Patch(bytes, FileRecordOffset(bytes, test_case.record) + vpath_field,
      test_case.virtual_path_offset);

    // Act & Assert
    const auto [failure, what] = RefusalOf(bytes);
    EXPECT_EQ(failure, CookedOpenFailure::kCorrupt)
      << test_case.name << ": " << what;
  }
}

//! A texture descriptor and an asset never share a virtual path.
NOLINT_TEST(LooseCookedIndexFileTest, TextureAndAssetShareNoVirtualPath)
{
  // Arrange
  const auto wood
    = Material("/Content/Materials/Wood.omat", "Materials/Wood.omat");
  auto sky = File(FileKind::kTextureDescriptor, "Textures/Sky.otex");
  sky.virtual_path = "/Content/Textures/Sky.otex";
  // A texture descriptor records the source that produced it.
  sky.source_path = std::string(kSource);
  auto bytes = Encode(std::array { wood }, std::array { sky });
  auto entry = AssetEntry {};
  std::memcpy(&entry, bytes.data() + AssetEntryOffset(bytes, 0), sizeof(entry));
  Patch(bytes,
    FileRecordOffset(bytes, 0) + offsetof(FileRecord, virtual_path_offset),
    entry.virtual_path_offset);

  // Act & Assert
  ExpectCorrupt(bytes, "Duplicate virtual path");
}

//=== Files on disk ===-------------------------------------------------------//

class LooseCookedIndexFileSystemTest : public oxygen::testing::TempDirTest {
protected:
  //! Publishes `bytes` as the file `relpath` of the cooked root.
  auto Publish(const std::string_view relpath,
    const std::span<const std::byte> bytes) const -> void
  {
    const auto path = TempPath(relpath);
    std::filesystem::create_directories(path.parent_path());
    ASSERT_HAS_VALUE(oxygen::serio::WriteFileAtomically(path, bytes));
  }

  auto PublishText(
    const std::string_view relpath, const std::string_view text) const -> void
  {
    Publish(relpath, std::as_bytes(std::span(text)));
  }

  [[nodiscard]] auto IndexPath() const -> std::filesystem::path
  {
    return TempPath("container.index.bin");
  }
};

//! A loaded index maps its file and keeps the version it mapped while the
//! cooker publishes another.
NOLINT_TEST_F(LooseCookedIndexFileSystemTest, MappedIndexSurvivesRepublication)
{
  // Arrange
  const auto wood
    = Material("/Content/Materials/Wood.omat", "Materials/Wood.omat");
  const auto base
    = Material("/Content/Materials/Base.omat", "Materials/Base.omat");
  Publish("container.index.bin", Encode(std::array { wood }));
  const auto index = LooseCookedIndexImpl::LoadFromFile(IndexPath());

  // Act
  Publish("container.index.bin", Encode(std::array { base }));

  // Assert
  EXPECT_EQ(index.FindVirtualPath(wood.key), wood.virtual_path);
  EXPECT_FALSE(index.FindVirtualPath(base.key).has_value());
  EXPECT_EQ(
    LooseCookedIndexImpl::LoadFromFile(IndexPath()).FindVirtualPath(base.key),
    base.virtual_path);
}

//! Index loads wait for an in-process publication to finish.
/*!
 Holds the exclusive index file lock through a differently spelled path while
 the index is absent, as during a replace. A concurrent load must wait and
 read the republished index instead of failing on the missing file.
*/
NOLINT_TEST_F(
  LooseCookedIndexFileSystemTest, LoadFromRootWaitsForExclusivePublication)
{
  // Arrange
  Publish("container.index.bin", Encode({}));
  auto publication = LooseCookedIndexImpl::LockFileExclusive(
    TempPath("./container.index.bin"));
  std::filesystem::remove(IndexPath());

  // Act
  auto load_error = std::string {};
  auto reader = std::thread([this, &load_error] -> void {
    try {
      static_cast<void>(LooseCookedIndex::LoadFromRoot(TempDir()));
    } catch (const std::exception& ex) {
      load_error = ex.what();
    }
  });
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  Publish("container.index.bin", Encode({}));
  publication.unlock();
  reader.join();

  // Assert
  EXPECT_TRUE(load_error.empty()) << load_error;
}

//! A full check hashes each descriptor against its indexed digest.
NOLINT_TEST_F(LooseCookedIndexFileSystemTest, DescriptorDigestsAreChecked)
{
  // Arrange
  const auto wood
    = Material("/Content/Materials/Wood.omat", "Materials/Wood.omat");
  PublishText(wood.descriptor_relpath, wood.descriptor_relpath);
  auto forged = wood;
  forged.descriptor_sha256 = DigestOf("forged");
  Publish("container.index.bin", Encode(std::array { wood }));
  const auto valid = LooseCookedIndexImpl::LoadFromFile(IndexPath());
  Publish("container.index.bin", Encode(std::array { forged }));
  const auto mismatched = LooseCookedIndexImpl::LoadFromFile(IndexPath());

  // Act
  const auto valid_issues
    = valid.CheckContent(TempDir(), IntegrityCheck::kFull);
  const auto issues = mismatched.CheckContent(TempDir(), IntegrityCheck::kFull);
  const auto metadata_issues
    = mismatched.CheckContent(TempDir(), IntegrityCheck::kMetadata);

  // Assert
  EXPECT_TRUE(valid_issues.empty());
  ASSERT_EQ(issues.size(), 1U);
  EXPECT_EQ(issues.front().relative_path, wood.descriptor_relpath);
  EXPECT_EQ(issues.front().reason, IntegrityFailure::kDigestMismatch);
  EXPECT_TRUE(metadata_issues.empty());
  EXPECT_THROW(mismatched.ValidateContent(TempDir(), IntegrityCheck::kFull),
    std::runtime_error);
}

} // namespace
