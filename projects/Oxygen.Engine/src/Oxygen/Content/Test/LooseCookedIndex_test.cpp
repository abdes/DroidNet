//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <filesystem>
#include <fstream>

#include "./AssetLoader_test.h"

#include <Oxygen/Content/Import/LooseCookedLayout.h>
#include <Oxygen/Data/LooseCookedIndexFormat.h>

using oxygen::content::testing::AssetLoaderBasicTest;

namespace {

using oxygen::content::import::LooseCookedLayout;

//! Fixture for AssetLoader dependency tests
class LooseCookedIndexTest : public AssetLoaderBasicTest {
protected:
  auto SetUp() -> void override
  {
    AssetLoaderBasicTest::SetUp();
    asset_loader_->SetVerifyContentHashes(true);
  }
};

auto FillTestGuid(oxygen::data::loose_cooked::IndexHeader& header) -> void
{
  for (uint8_t i = 0; i < 16; ++i) {
    header.guid[i] = static_cast<uint8_t>(i + 1);
  }
}

//! Test: Descriptor SHA-256 verification uses the standard digest
/*!
 Scenario: Writes a descriptor file containing "abc" and records the known
 SHA-256 digest in the index. Verifies that mounting succeeds.
*/
NOLINT_TEST_F(
  LooseCookedIndexTest, AddLooseCookedRoot_DescriptorShaMatches_Abc_Succeeds)
{
  using oxygen::data::loose_cooked::AssetEntry;
  using oxygen::data::loose_cooked::FileRecord;
  using oxygen::data::loose_cooked::IndexHeader;

  // Arrange
  const auto cooked_root = temp_dir_ / "loose_cooked_root";
  std::filesystem::create_directories(cooked_root);
  const auto index_path = cooked_root / "container.index.bin";

  {
    std::ofstream out(cooked_root / "Abc.bin", std::ios::binary);
    out.write("abc", 3);
  }

  std::string strings;
  strings.push_back('\0');
  const auto off_desc = static_cast<uint32_t>(strings.size());
  strings += "Abc.bin";
  strings.push_back('\0');
  const auto off_vpath = static_cast<uint32_t>(strings.size());
  strings += "/.cooked/Abc.bin";
  strings.push_back('\0');

  IndexHeader header {};
  FillTestGuid(header);
  header.version = 1;
  header.content_version = 0;
  header.flags = oxygen::data::loose_cooked::kHasVirtualPaths
    | oxygen::data::loose_cooked::kHasFileRecords;
  header.string_table_offset = sizeof(IndexHeader);
  header.string_table_size = static_cast<uint64_t>(strings.size());
  header.asset_entries_offset
    = header.string_table_offset + header.string_table_size;
  header.asset_count = 1;
  header.asset_entry_size = sizeof(AssetEntry);
  header.file_records_offset
    = header.asset_entries_offset + sizeof(AssetEntry) * header.asset_count;
  header.file_record_count = 0;
  header.file_record_size = sizeof(FileRecord);

  oxygen::data::AssetKey key {};
  AssetEntry entry {};
  entry.asset_key = key;
  entry.descriptor_relpath_offset = off_desc;
  entry.virtual_path_offset = off_vpath;
  entry.asset_type = 0;
  entry.descriptor_size = 3;

  // Known SHA-256("abc")
  const std::array<uint8_t, 32> expected = {
    0xba,
    0x78,
    0x16,
    0xbf,
    0x8f,
    0x01,
    0xcf,
    0xea,
    0x41,
    0x41,
    0x40,
    0xde,
    0x5d,
    0xae,
    0x22,
    0x23,
    0xb0,
    0x03,
    0x61,
    0xa3,
    0x96,
    0x17,
    0x7a,
    0x9c,
    0xb4,
    0x10,
    0xff,
    0x61,
    0xf2,
    0x00,
    0x15,
    0xad,
  };
  std::copy_n(
    expected.begin(), expected.size(), std::begin(entry.descriptor_sha256));

  {
    std::ofstream out(index_path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(&header), sizeof(header));
    out.write(strings.data(), static_cast<std::streamsize>(strings.size()));
    out.write(reinterpret_cast<const char*>(&entry), sizeof(entry));
  }

  // Act & Assert
  EXPECT_NO_THROW({ asset_loader_->AddLooseCookedRoot(cooked_root); });
}

//! Test: Minimal loose cooked index parses successfully
/*!
 Scenario: Writes the smallest valid `container.index.bin` (empty asset list)
 and verifies that `AssetLoader::AddLooseCookedRoot(...)` accepts it.
*/
NOLINT_TEST_F(LooseCookedIndexTest, AddLooseCookedRoot_MinimalIndex_Succeeds)
{
  using oxygen::data::loose_cooked::AssetEntry;
  using oxygen::data::loose_cooked::FileRecord;
  using oxygen::data::loose_cooked::IndexHeader;

  // Arrange
  const auto cooked_root = temp_dir_ / "loose_cooked_root";
  std::filesystem::create_directories(cooked_root);
  const auto index_path = cooked_root / "container.index.bin";

  IndexHeader header {};
  FillTestGuid(header);
  header.version = 1;
  header.content_version = 0;
  header.flags = oxygen::data::loose_cooked::kHasVirtualPaths
    | oxygen::data::loose_cooked::kHasFileRecords;
  header.string_table_offset = sizeof(IndexHeader);
  header.string_table_size = 1; // "\0"
  header.asset_entries_offset
    = header.string_table_offset + header.string_table_size;
  header.asset_count = 0;
  header.asset_entry_size = sizeof(AssetEntry);
  header.file_records_offset = header.asset_entries_offset;
  header.file_record_count = 0;
  header.file_record_size = sizeof(FileRecord);

  {
    std::ofstream out(index_path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(&header), sizeof(header));
    const char zero = 0;
    out.write(&zero, 1);
  }

  // Act
  EXPECT_NO_THROW({ asset_loader_->AddLooseCookedRoot(cooked_root); });
}

//! Test: Schema version mismatch rejects the index
/*!
 Scenario: Writes a well-formed file with an unsupported schema version and
 verifies that mounting fails.
*/
NOLINT_TEST_F(
  LooseCookedIndexTest, AddLooseCookedRoot_UnsupportedVersion_Throws)
{
  using oxygen::data::loose_cooked::AssetEntry;
  using oxygen::data::loose_cooked::FileRecord;
  using oxygen::data::loose_cooked::IndexHeader;

  // Arrange
  const auto cooked_root = temp_dir_ / "loose_cooked_root";
  std::filesystem::create_directories(cooked_root);
  const auto index_path = cooked_root / "container.index.bin";

  IndexHeader header {};
  FillTestGuid(header);
  header.version = 999;
  header.content_version = 0;
  header.flags = oxygen::data::loose_cooked::kHasVirtualPaths
    | oxygen::data::loose_cooked::kHasFileRecords;
  header.string_table_offset = sizeof(IndexHeader);
  header.string_table_size = 1;
  header.asset_entries_offset
    = header.string_table_offset + header.string_table_size;
  header.asset_count = 0;
  header.asset_entry_size = sizeof(AssetEntry);
  header.file_records_offset = header.asset_entries_offset;
  header.file_record_count = 0;
  header.file_record_size = sizeof(FileRecord);

  {
    std::ofstream out(index_path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(&header), sizeof(header));
    const char zero = 0;
    out.write(&zero, 1);
  }

  // Act & Assert
  EXPECT_THROW(
    { asset_loader_->AddLooseCookedRoot(cooked_root); }, std::exception);
}

//! Test: Index rejects a string table that overlaps the header
/*!
 Scenario: Writes an index whose string table offset points into the header.
 Verifies that mounting fails.
*/
NOLINT_TEST_F(
  LooseCookedIndexTest, AddLooseCookedRoot_StringTableBeforeHeader_Throws)
{
  using oxygen::data::loose_cooked::AssetEntry;
  using oxygen::data::loose_cooked::FileRecord;
  using oxygen::data::loose_cooked::IndexHeader;

  // Arrange
  const auto cooked_root = temp_dir_ / "loose_cooked_root";
  std::filesystem::create_directories(cooked_root);
  const auto index_path = cooked_root / "container.index.bin";

  IndexHeader header {};
  FillTestGuid(header);
  header.version = 1;
  header.content_version = 0;
  header.flags = oxygen::data::loose_cooked::kHasVirtualPaths
    | oxygen::data::loose_cooked::kHasFileRecords;
  header.string_table_offset = 0; // Invalid: overlaps header
  header.string_table_size = 1; // "\0"
  header.asset_entries_offset = sizeof(IndexHeader);
  header.asset_count = 0;
  header.asset_entry_size = sizeof(AssetEntry);
  header.file_records_offset = header.asset_entries_offset;
  header.file_record_count = 0;
  header.file_record_size = sizeof(FileRecord);

  {
    std::ofstream out(index_path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(&header), sizeof(header));
    const char zero = 0;
    out.write(&zero, 1);
  }

  // Act & Assert
  EXPECT_THROW(
    { asset_loader_->AddLooseCookedRoot(cooked_root); }, std::exception);
}

//! Test: Index rejects asset entries that overlap the string table
/*!
 Scenario: Writes an index where asset entries begin before the end of the
 string table. Verifies that mounting fails.
*/
NOLINT_TEST_F(LooseCookedIndexTest,
  AddLooseCookedRoot_AssetEntriesOverlapStringTable_Throws)
{
  using oxygen::data::loose_cooked::AssetEntry;
  using oxygen::data::loose_cooked::FileRecord;
  using oxygen::data::loose_cooked::IndexHeader;

  // Arrange
  const auto cooked_root = temp_dir_ / "loose_cooked_root";
  std::filesystem::create_directories(cooked_root);
  const auto index_path = cooked_root / "container.index.bin";

  std::string strings;
  strings.push_back('\0');
  strings += "A.bin";
  strings.push_back('\0');

  IndexHeader header {};
  FillTestGuid(header);
  header.version = 1;
  header.content_version = 0;
  header.flags = oxygen::data::loose_cooked::kHasVirtualPaths
    | oxygen::data::loose_cooked::kHasFileRecords;
  header.string_table_offset = sizeof(IndexHeader);
  header.string_table_size = static_cast<uint64_t>(strings.size());
  header.asset_entries_offset = header.string_table_offset; // Invalid overlap
  header.asset_count = 0;
  header.asset_entry_size = sizeof(AssetEntry);
  header.file_records_offset = header.asset_entries_offset;
  header.file_record_count = 0;
  header.file_record_size = sizeof(FileRecord);

  {
    std::ofstream out(index_path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(&header), sizeof(header));
    out.write(strings.data(), static_cast<std::streamsize>(strings.size()));
  }

  // Act & Assert
  EXPECT_THROW(
    { asset_loader_->AddLooseCookedRoot(cooked_root); }, std::exception);
}

//! Test: Virtual paths must start with '/'
/*!
 Scenario: Writes an index with a virtual path that does not start with '/'.
 Verifies that mounting fails.
*/
NOLINT_TEST_F(LooseCookedIndexTest,
  AddLooseCookedRoot_VirtualPathMissingLeadingSlash_Throws)
{
  using oxygen::data::loose_cooked::AssetEntry;
  using oxygen::data::loose_cooked::FileRecord;
  using oxygen::data::loose_cooked::IndexHeader;

  // Arrange
  const auto cooked_root = temp_dir_ / "loose_cooked_root";
  std::filesystem::create_directories(cooked_root);
  const auto index_path = cooked_root / "container.index.bin";

  std::string strings;
  strings.push_back('\0');
  const auto off_desc = static_cast<uint32_t>(strings.size());
  strings += "A.bin";
  strings.push_back('\0');
  const auto off_vpath = static_cast<uint32_t>(strings.size());
  strings += "Content/A.bin"; // Invalid: missing leading '/'
  strings.push_back('\0');

  IndexHeader header {};
  FillTestGuid(header);
  header.version = 1;
  header.content_version = 0;
  header.flags = oxygen::data::loose_cooked::kHasVirtualPaths
    | oxygen::data::loose_cooked::kHasFileRecords;
  header.string_table_offset = sizeof(IndexHeader);
  header.string_table_size = static_cast<uint64_t>(strings.size());
  header.asset_entries_offset
    = header.string_table_offset + header.string_table_size;
  header.asset_count = 1;
  header.asset_entry_size = sizeof(AssetEntry);
  header.file_records_offset
    = header.asset_entries_offset + sizeof(AssetEntry) * header.asset_count;
  header.file_record_count = 0;
  header.file_record_size = sizeof(FileRecord);

  oxygen::data::AssetKey key {};
  AssetEntry entry {};
  entry.asset_key = key;
  entry.descriptor_relpath_offset = off_desc;
  entry.virtual_path_offset = off_vpath;
  entry.asset_type = 0;
  entry.descriptor_size = 0;

  {
    std::ofstream out(index_path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(&header), sizeof(header));
    out.write(strings.data(), static_cast<std::streamsize>(strings.size()));
    out.write(reinterpret_cast<const char*>(&entry), sizeof(entry));
  }

  // Act & Assert
  EXPECT_THROW(
    { asset_loader_->AddLooseCookedRoot(cooked_root); }, std::exception);
}

//! Test: Virtual paths must not contain '..'
/*!
 Scenario: Writes an index with a virtual path containing a '..' segment.
 Verifies that mounting fails.
*/
NOLINT_TEST_F(
  LooseCookedIndexTest, AddLooseCookedRoot_VirtualPathContainsDotDot_Throws)
{
  using oxygen::data::loose_cooked::AssetEntry;
  using oxygen::data::loose_cooked::FileRecord;
  using oxygen::data::loose_cooked::IndexHeader;

  // Arrange
  const auto cooked_root = temp_dir_ / "loose_cooked_root";
  std::filesystem::create_directories(cooked_root);
  const auto index_path = cooked_root / "container.index.bin";

  std::string strings;
  strings.push_back('\0');
  const auto off_desc = static_cast<uint32_t>(strings.size());
  strings += "A.bin";
  strings.push_back('\0');
  const auto off_vpath = static_cast<uint32_t>(strings.size());
  strings += "/.cooked/../A.bin"; // Invalid: contains '..'
  strings.push_back('\0');

  IndexHeader header {};
  FillTestGuid(header);
  header.version = 1;
  header.content_version = 0;
  header.flags = oxygen::data::loose_cooked::kHasVirtualPaths
    | oxygen::data::loose_cooked::kHasFileRecords;
  header.string_table_offset = sizeof(IndexHeader);
  header.string_table_size = static_cast<uint64_t>(strings.size());
  header.asset_entries_offset
    = header.string_table_offset + header.string_table_size;
  header.asset_count = 1;
  header.asset_entry_size = sizeof(AssetEntry);
  header.file_records_offset
    = header.asset_entries_offset + sizeof(AssetEntry) * header.asset_count;
  header.file_record_count = 0;
  header.file_record_size = sizeof(FileRecord);

  oxygen::data::AssetKey key {};
  AssetEntry entry {};
  entry.asset_key = key;
  entry.descriptor_relpath_offset = off_desc;
  entry.virtual_path_offset = off_vpath;
  entry.asset_type = 0;
  entry.descriptor_size = 0;

  {
    std::ofstream out(index_path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(&header), sizeof(header));
    out.write(strings.data(), static_cast<std::streamsize>(strings.size()));
    out.write(reinterpret_cast<const char*>(&entry), sizeof(entry));
  }

  // Act & Assert
  EXPECT_THROW(
    { asset_loader_->AddLooseCookedRoot(cooked_root); }, std::exception);
}

//! Test: Descriptor relpaths must not contain '\\'
/*!
 Scenario: Writes an index with a descriptor relpath using Windows-style
 separators. Verifies that mounting fails.
*/
NOLINT_TEST_F(
  LooseCookedIndexTest, AddLooseCookedRoot_RelPathContainsBackslash_Throws)
{
  using oxygen::data::loose_cooked::AssetEntry;
  using oxygen::data::loose_cooked::FileRecord;
  using oxygen::data::loose_cooked::IndexHeader;

  // Arrange
  const auto cooked_root = temp_dir_ / "loose_cooked_root";
  std::filesystem::create_directories(cooked_root);
  const auto index_path = cooked_root / "container.index.bin";

  std::string strings;
  strings.push_back('\0');
  const auto off_desc = static_cast<uint32_t>(strings.size());
  strings += "Materials\\A.bin"; // Invalid: contains '\\'
  strings.push_back('\0');
  const auto off_vpath = static_cast<uint32_t>(strings.size());
  strings += "/.cooked/A.bin";
  strings.push_back('\0');

  IndexHeader header {};
  FillTestGuid(header);
  header.version = 1;
  header.content_version = 0;
  header.flags = oxygen::data::loose_cooked::kHasVirtualPaths
    | oxygen::data::loose_cooked::kHasFileRecords;
  header.string_table_offset = sizeof(IndexHeader);
  header.string_table_size = static_cast<uint64_t>(strings.size());
  header.asset_entries_offset
    = header.string_table_offset + header.string_table_size;
  header.asset_count = 1;
  header.asset_entry_size = sizeof(AssetEntry);
  header.file_records_offset
    = header.asset_entries_offset + sizeof(AssetEntry) * header.asset_count;
  header.file_record_count = 0;
  header.file_record_size = sizeof(FileRecord);

  oxygen::data::AssetKey key {};
  AssetEntry entry {};
  entry.asset_key = key;
  entry.descriptor_relpath_offset = off_desc;
  entry.virtual_path_offset = off_vpath;
  entry.asset_type = 0;
  entry.descriptor_size = 0;

  {
    std::ofstream out(index_path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(&header), sizeof(header));
    out.write(strings.data(), static_cast<std::streamsize>(strings.size()));
    out.write(reinterpret_cast<const char*>(&entry), sizeof(entry));
  }

  // Act & Assert
  EXPECT_THROW(
    { asset_loader_->AddLooseCookedRoot(cooked_root); }, std::exception);
}

//! Test: Unknown file kinds are rejected
/*!
 Scenario: Writes a valid index with a single file record whose kind is
 `FileKind::kUnknown`. Verifies that mounting fails.
*/
NOLINT_TEST_F(LooseCookedIndexTest, AddLooseCookedRoot_UnknownFileKind_Throws)
{
  using oxygen::data::loose_cooked::AssetEntry;
  using oxygen::data::loose_cooked::FileKind;
  using oxygen::data::loose_cooked::FileRecord;
  using oxygen::data::loose_cooked::IndexHeader;

  // Arrange
  const auto cooked_root = temp_dir_ / "loose_cooked_root";
  std::filesystem::create_directories(cooked_root);
  const auto index_path = cooked_root / "container.index.bin";

  std::string strings;
  strings.push_back('\0');
  const auto off_path = static_cast<uint32_t>(strings.size());
  strings += "Resources/unknown.bin";
  strings.push_back('\0');

  IndexHeader header {};
  FillTestGuid(header);
  header.version = 1;
  header.content_version = 0;
  header.flags = oxygen::data::loose_cooked::kHasVirtualPaths
    | oxygen::data::loose_cooked::kHasFileRecords;
  header.string_table_offset = sizeof(IndexHeader);
  header.string_table_size = static_cast<uint64_t>(strings.size());
  header.asset_entries_offset
    = header.string_table_offset + header.string_table_size;
  header.asset_count = 0;
  header.asset_entry_size = sizeof(AssetEntry);
  header.file_records_offset = header.asset_entries_offset;
  header.file_record_count = 1;
  header.file_record_size = sizeof(FileRecord);

  FileRecord record {};
  record.kind = FileKind::kUnknown;
  record.relpath_offset = off_path;
  record.size = 0;

  {
    std::ofstream out(index_path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(&header), sizeof(header));
    out.write(strings.data(), static_cast<std::streamsize>(strings.size()));
    out.write(reinterpret_cast<const char*>(&record), sizeof(record));
  }

  // Act & Assert
  EXPECT_THROW(
    { asset_loader_->AddLooseCookedRoot(cooked_root); }, std::exception);
}

//! Test: Duplicate file-kind records are rejected
/*!
 Scenario: Writes a valid index with two file records of the same kind and
 verifies that mounting fails.
*/
NOLINT_TEST_F(LooseCookedIndexTest, AddLooseCookedRoot_DuplicateFileKind_Throws)
{
  using oxygen::data::loose_cooked::AssetEntry;
  using oxygen::data::loose_cooked::FileKind;
  using oxygen::data::loose_cooked::FileRecord;
  using oxygen::data::loose_cooked::IndexHeader;

  // Arrange
  const auto cooked_root = temp_dir_ / "loose_cooked_root";
  std::filesystem::create_directories(cooked_root);
  const auto index_path = cooked_root / "container.index.bin";

  std::string strings;
  strings.push_back('\0');
  const auto off_a = static_cast<uint32_t>(strings.size());
  strings += "Resources/textures.table";
  strings.push_back('\0');

  IndexHeader header {};
  FillTestGuid(header);
  header.version = 1;
  header.content_version = 0;
  header.flags = oxygen::data::loose_cooked::kHasVirtualPaths
    | oxygen::data::loose_cooked::kHasFileRecords;
  header.string_table_offset = sizeof(IndexHeader);
  header.string_table_size = static_cast<uint64_t>(strings.size());
  header.asset_entries_offset
    = header.string_table_offset + header.string_table_size;
  header.asset_count = 0;
  header.asset_entry_size = sizeof(AssetEntry);
  header.file_records_offset = header.asset_entries_offset;
  header.file_record_count = 2;
  header.file_record_size = sizeof(FileRecord);

  FileRecord r0 {};
  r0.kind = FileKind::kTexturesTable;
  r0.relpath_offset = off_a;
  r0.size = 0;

  FileRecord r1 {};
  r1.kind = FileKind::kTexturesTable;
  r1.relpath_offset = off_a;
  r1.size = 0;

  {
    std::ofstream out(index_path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(&header), sizeof(header));
    out.write(strings.data(), static_cast<std::streamsize>(strings.size()));
    out.write(reinterpret_cast<const char*>(&r0), sizeof(r0));
    out.write(reinterpret_cast<const char*>(&r1), sizeof(r1));
  }

  // Act & Assert
  EXPECT_THROW(
    { asset_loader_->AddLooseCookedRoot(cooked_root); }, std::exception);
}

//! Test: Duplicate AssetKey entries are rejected
/*!
 Scenario: Writes an index with two asset entries that share the same AssetKey.
 Verifies that mounting fails.
*/
NOLINT_TEST_F(LooseCookedIndexTest, AddLooseCookedRoot_DuplicateAssetKey_Throws)
{
  using oxygen::data::loose_cooked::AssetEntry;
  using oxygen::data::loose_cooked::FileRecord;
  using oxygen::data::loose_cooked::IndexHeader;

  // Arrange
  const auto cooked_root = temp_dir_ / "loose_cooked_root";
  std::filesystem::create_directories(cooked_root);
  const auto index_path = cooked_root / "container.index.bin";

  std::string strings;
  strings.push_back('\0');
  const auto off_desc = static_cast<uint32_t>(strings.size());
  strings += "A.bin";
  strings.push_back('\0');
  const auto off_vpath_a = static_cast<uint32_t>(strings.size());
  strings += "/.cooked/A.bin";
  strings.push_back('\0');
  const auto off_desc_b = static_cast<uint32_t>(strings.size());
  strings += "B.bin";
  strings.push_back('\0');
  const auto off_vpath_b = static_cast<uint32_t>(strings.size());
  strings += "/.cooked/B.bin";
  strings.push_back('\0');

  IndexHeader header {};
  FillTestGuid(header);
  header.version = 1;
  header.content_version = 0;
  header.flags = oxygen::data::loose_cooked::kHasVirtualPaths
    | oxygen::data::loose_cooked::kHasFileRecords;
  header.string_table_offset = sizeof(IndexHeader);
  header.string_table_size = static_cast<uint64_t>(strings.size());
  header.asset_entries_offset
    = header.string_table_offset + header.string_table_size;
  header.asset_count = 2;
  header.asset_entry_size = sizeof(AssetEntry);
  header.file_records_offset
    = header.asset_entries_offset + sizeof(AssetEntry) * header.asset_count;
  header.file_record_count = 0;
  header.file_record_size = sizeof(FileRecord);

  oxygen::data::AssetKey key {};

  AssetEntry a {};
  a.asset_key = key;
  a.descriptor_relpath_offset = off_desc;
  a.virtual_path_offset = off_vpath_a;
  a.asset_type = 0;
  a.descriptor_size = 0;

  AssetEntry b {};
  b.asset_key = key;
  b.descriptor_relpath_offset = off_desc_b;
  b.virtual_path_offset = off_vpath_b;
  b.asset_type = 0;
  b.descriptor_size = 0;

  {
    std::ofstream out(index_path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(&header), sizeof(header));
    out.write(strings.data(), static_cast<std::streamsize>(strings.size()));
    out.write(reinterpret_cast<const char*>(&a), sizeof(a));
    out.write(reinterpret_cast<const char*>(&b), sizeof(b));
  }

  // Act & Assert
  EXPECT_THROW(
    { asset_loader_->AddLooseCookedRoot(cooked_root); }, std::exception);
}

//! Test: Duplicate virtual path strings are rejected
/*!
 Scenario: Writes an index with two different virtual-path string-table offsets
 that contain the same virtual path text. Verifies that mounting fails.
*/
NOLINT_TEST_F(
  LooseCookedIndexTest, AddLooseCookedRoot_DuplicateVirtualPathString_Throws)
{
  using oxygen::data::loose_cooked::AssetEntry;
  using oxygen::data::loose_cooked::FileRecord;
  using oxygen::data::loose_cooked::IndexHeader;

  // Arrange
  const auto cooked_root = temp_dir_ / "loose_cooked_root";
  std::filesystem::create_directories(cooked_root);
  const auto index_path = cooked_root / "container.index.bin";

  std::string strings;
  strings.push_back('\0');
  const auto off_desc_a = static_cast<uint32_t>(strings.size());
  strings += "A.bin";
  strings.push_back('\0');
  const auto off_desc_b = static_cast<uint32_t>(strings.size());
  strings += "B.bin";
  strings.push_back('\0');

  const auto off_vpath_1 = static_cast<uint32_t>(strings.size());
  strings += "/.cooked/Same.bin";
  strings.push_back('\0');
  const auto off_vpath_2 = static_cast<uint32_t>(strings.size());
  strings += "/.cooked/Same.bin";
  strings.push_back('\0');

  IndexHeader header {};
  FillTestGuid(header);
  header.version = 1;
  header.content_version = 0;
  header.flags = oxygen::data::loose_cooked::kHasVirtualPaths
    | oxygen::data::loose_cooked::kHasFileRecords;
  header.string_table_offset = sizeof(IndexHeader);
  header.string_table_size = static_cast<uint64_t>(strings.size());
  header.asset_entries_offset
    = header.string_table_offset + header.string_table_size;
  header.asset_count = 2;
  header.asset_entry_size = sizeof(AssetEntry);
  header.file_records_offset
    = header.asset_entries_offset + sizeof(AssetEntry) * header.asset_count;
  header.file_record_count = 0;
  header.file_record_size = sizeof(FileRecord);

  oxygen::data::AssetKey key_a {};
  key_a.guid[0] = 0x11;
  oxygen::data::AssetKey key_b {};
  key_b.guid[0] = 0x22;

  AssetEntry a {};
  a.asset_key = key_a;
  a.descriptor_relpath_offset = off_desc_a;
  a.virtual_path_offset = off_vpath_1;
  a.asset_type = 0;
  a.descriptor_size = 0;

  AssetEntry b {};
  b.asset_key = key_b;
  b.descriptor_relpath_offset = off_desc_b;
  b.virtual_path_offset = off_vpath_2;
  b.asset_type = 0;
  b.descriptor_size = 0;

  {
    std::ofstream out(index_path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(&header), sizeof(header));
    out.write(strings.data(), static_cast<std::streamsize>(strings.size()));
    out.write(reinterpret_cast<const char*>(&a), sizeof(a));
    out.write(reinterpret_cast<const char*>(&b), sizeof(b));
  }

  // Act & Assert
  EXPECT_THROW(
    { asset_loader_->AddLooseCookedRoot(cooked_root); }, std::exception);
}

//! Test: Table/data pairs must be complete when present
/*!
 Scenario: Writes a loose cooked root where textures.table is indexed, but
 textures.data is missing. Verifies that mounting fails.
*/
NOLINT_TEST_F(LooseCookedIndexTest, AddLooseCookedRoot_TableWithoutData_Throws)
{
  using oxygen::data::AssetType;
  using oxygen::data::loose_cooked::AssetEntry;
  using oxygen::data::loose_cooked::FileKind;
  using oxygen::data::loose_cooked::FileRecord;
  using oxygen::data::loose_cooked::IndexHeader;
  using oxygen::data::pak::MaterialAssetDesc;

  const LooseCookedLayout layout {};
  using oxygen::data::pak::TextureResourceDesc;

  // Arrange
  const auto cooked_root = temp_dir_ / "loose_cooked_root";
  std::filesystem::create_directories(cooked_root / layout.materials_subdir);
  std::filesystem::create_directories(cooked_root / layout.resources_dir);
  const auto index_path = cooked_root / "container.index.bin";

  // Write a minimal textures.table (fallback + one record) but do not write
  // textures.data.
  TextureResourceDesc fallback_desc {};
  TextureResourceDesc test_desc {};
  {
    std::ofstream out(
      cooked_root / layout.resources_dir / layout.textures_table_file_name,
      std::ios::binary);
    out.write(reinterpret_cast<const char*>(&fallback_desc),
      static_cast<std::streamsize>(sizeof(fallback_desc)));
    out.write(reinterpret_cast<const char*>(&test_desc),
      static_cast<std::streamsize>(sizeof(test_desc)));
  }

  MaterialAssetDesc material_desc {};
  material_desc.header.asset_type = static_cast<uint8_t>(AssetType::kMaterial);
  material_desc.header.version = oxygen::data::pak::v2::kMaterialAssetVersion;
  {
    const auto material_file
      = LooseCookedLayout::MaterialDescriptorFileName("TestMaterial");
    std::ofstream out(
      cooked_root / layout.materials_subdir / material_file, std::ios::binary);
    out.write(reinterpret_cast<const char*>(&material_desc),
      static_cast<std::streamsize>(sizeof(material_desc)));
  }

  // Build index string table.
  std::string strings;
  strings.push_back('\0');
  const auto off_desc = static_cast<uint32_t>(strings.size());
  strings += std::string(layout.materials_subdir) + "/"
    + LooseCookedLayout::MaterialDescriptorFileName("TestMaterial");
  strings.push_back('\0');
  const auto off_vpath = static_cast<uint32_t>(strings.size());
  strings += layout.MaterialVirtualPath("TestMaterial");
  strings.push_back('\0');
  const auto off_tex_table = static_cast<uint32_t>(strings.size());
  strings += layout.TexturesTableRelPath();
  strings.push_back('\0');

  IndexHeader header {};
  FillTestGuid(header);
  header.version = 1;
  header.content_version = 0;
  header.flags = oxygen::data::loose_cooked::kHasVirtualPaths
    | oxygen::data::loose_cooked::kHasFileRecords;
  header.string_table_offset = sizeof(IndexHeader);
  header.string_table_size = static_cast<uint64_t>(strings.size());
  header.asset_entries_offset
    = header.string_table_offset + header.string_table_size;
  header.asset_count = 1;
  header.asset_entry_size = sizeof(AssetEntry);
  header.file_records_offset
    = header.asset_entries_offset + sizeof(AssetEntry) * header.asset_count;
  header.file_record_count = 1;
  header.file_record_size = sizeof(FileRecord);

  oxygen::data::AssetKey key {};
  AssetEntry asset_entry {};
  asset_entry.asset_key = key;
  asset_entry.descriptor_relpath_offset = off_desc;
  asset_entry.virtual_path_offset = off_vpath;
  asset_entry.asset_type = static_cast<uint8_t>(AssetType::kMaterial);
  asset_entry.descriptor_size = sizeof(MaterialAssetDesc);

  FileRecord tex_table_record {};
  tex_table_record.kind = FileKind::kTexturesTable;
  tex_table_record.relpath_offset = off_tex_table;
  tex_table_record.size = sizeof(TextureResourceDesc) * 2;

  {
    std::ofstream out(index_path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(&header), sizeof(header));
    out.write(strings.data(), static_cast<std::streamsize>(strings.size()));
    out.write(reinterpret_cast<const char*>(&asset_entry), sizeof(asset_entry));
    out.write(reinterpret_cast<const char*>(&tex_table_record),
      sizeof(tex_table_record));
  }

  // Act & Assert
  EXPECT_THROW(
    { asset_loader_->AddLooseCookedRoot(cooked_root); }, std::exception);
}

//! Test: Table/data pairs must be complete (data without table)
/*!
 Scenario: Writes a loose cooked root where textures.data is indexed, but
 textures.table is missing. Verifies that mounting fails.
*/
NOLINT_TEST_F(LooseCookedIndexTest, AddLooseCookedRoot_DataWithoutTable_Throws)
{
  using oxygen::data::loose_cooked::AssetEntry;
  using oxygen::data::loose_cooked::FileKind;
  using oxygen::data::loose_cooked::FileRecord;
  using oxygen::data::loose_cooked::IndexHeader;

  // Arrange
  const auto cooked_root = temp_dir_ / "loose_cooked_root";
  std::filesystem::create_directories(cooked_root);
  const auto index_path = cooked_root / "container.index.bin";

  std::string strings;
  strings.push_back('\0');
  const auto off_tex_data = static_cast<uint32_t>(strings.size());
  strings += "Resources/textures.data";
  strings.push_back('\0');

  IndexHeader header {};
  FillTestGuid(header);
  header.version = 1;
  header.content_version = 0;
  header.flags = oxygen::data::loose_cooked::kHasVirtualPaths
    | oxygen::data::loose_cooked::kHasFileRecords;
  header.string_table_offset = sizeof(IndexHeader);
  header.string_table_size = static_cast<uint64_t>(strings.size());
  header.asset_entries_offset
    = header.string_table_offset + header.string_table_size;
  header.asset_count = 0;
  header.asset_entry_size = sizeof(AssetEntry);
  header.file_records_offset = header.asset_entries_offset;
  header.file_record_count = 1;
  header.file_record_size = sizeof(FileRecord);

  FileRecord tex_data_record {};
  tex_data_record.kind = FileKind::kTexturesData;
  tex_data_record.relpath_offset = off_tex_data;
  tex_data_record.size = 0;

  {
    std::ofstream out(index_path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(&header), sizeof(header));
    out.write(strings.data(), static_cast<std::streamsize>(strings.size()));
    out.write(
      reinterpret_cast<const char*>(&tex_data_record), sizeof(tex_data_record));
  }

  // Act & Assert
  EXPECT_THROW(
    { asset_loader_->AddLooseCookedRoot(cooked_root); }, std::exception);
}

//! Test: File-record legacy SHA bytes are ignored
/*!
 Scenario: The loose cooked index v1 used to store file-level SHA-256 digests
 in file records. That field has been removed and replaced with reserved bytes.

 Writes textures.table and textures.data with correct sizes, and fills the
 reserved bytes of the textures.table FileRecord with non-zero data to emulate
 a legacy SHA-256 field.

 Verifies that mounting succeeds.
*/
NOLINT_TEST_F(
  LooseCookedIndexTest, AddLooseCookedRoot_FileRecordLegacyShaBytes_Ignored)
{
  using oxygen::data::loose_cooked::AssetEntry;
  using oxygen::data::loose_cooked::FileKind;
  using oxygen::data::loose_cooked::FileRecord;
  using oxygen::data::loose_cooked::IndexHeader;
  using oxygen::data::pak::TextureResourceDesc;

  const LooseCookedLayout layout {};

  // Arrange
  const auto cooked_root = temp_dir_ / "loose_cooked_root";
  std::filesystem::create_directories(cooked_root / layout.resources_dir);
  const auto index_path = cooked_root / "container.index.bin";

  // Write minimal resources.
  TextureResourceDesc fallback_desc {};
  TextureResourceDesc test_desc {};
  {
    std::ofstream out(
      cooked_root / layout.resources_dir / layout.textures_table_file_name,
      std::ios::binary);
    out.write(reinterpret_cast<const char*>(&fallback_desc),
      static_cast<std::streamsize>(sizeof(fallback_desc)));
    out.write(reinterpret_cast<const char*>(&test_desc),
      static_cast<std::streamsize>(sizeof(test_desc)));
  }
  {
    std::ofstream out(
      cooked_root / layout.resources_dir / layout.textures_data_file_name,
      std::ios::binary);
  }

  std::string strings;
  strings.push_back('\0');
  const auto off_table = static_cast<uint32_t>(strings.size());
  strings += layout.TexturesTableRelPath();
  strings.push_back('\0');
  const auto off_data = static_cast<uint32_t>(strings.size());
  strings += layout.TexturesDataRelPath();
  strings.push_back('\0');

  IndexHeader header {};
  FillTestGuid(header);
  header.version = 1;
  header.content_version = 0;
  header.flags = oxygen::data::loose_cooked::kHasVirtualPaths
    | oxygen::data::loose_cooked::kHasFileRecords;
  header.string_table_offset = sizeof(IndexHeader);
  header.string_table_size = static_cast<uint64_t>(strings.size());
  header.asset_entries_offset
    = header.string_table_offset + header.string_table_size;
  header.asset_count = 0;
  header.asset_entry_size = sizeof(AssetEntry);
  header.file_records_offset = header.asset_entries_offset;
  header.file_record_count = 2;
  header.file_record_size = sizeof(FileRecord);

  FileRecord table_record {};
  table_record.kind = FileKind::kTexturesTable;
  table_record.relpath_offset = off_table;
  table_record.size = sizeof(TextureResourceDesc) * 2;
  std::ranges::fill(table_record.reserved1, static_cast<uint8_t>(0xAB));

  FileRecord data_record {};
  data_record.kind = FileKind::kTexturesData;
  data_record.relpath_offset = off_data;
  data_record.size = 0;

  {
    std::ofstream out(index_path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(&header), sizeof(header));
    out.write(strings.data(), static_cast<std::streamsize>(strings.size()));
    out.write(
      reinterpret_cast<const char*>(&table_record), sizeof(table_record));
    out.write(reinterpret_cast<const char*>(&data_record), sizeof(data_record));
  }

  // Act & Assert
  EXPECT_NO_THROW({ asset_loader_->AddLooseCookedRoot(cooked_root); });
}

//! Test: Descriptor SHA-256 mismatch rejects the root
/*!
 Scenario: Writes a valid descriptor file with correct size, but provides a
 non-zero, incorrect descriptor SHA-256 in the index. Verifies mounting fails.
*/
NOLINT_TEST_F(
  LooseCookedIndexTest, AddLooseCookedRoot_DescriptorShaMismatch_Throws)
{
  using oxygen::data::AssetType;
  using oxygen::data::loose_cooked::AssetEntry;
  using oxygen::data::loose_cooked::FileRecord;
  using oxygen::data::loose_cooked::IndexHeader;
  using oxygen::data::pak::MaterialAssetDesc;

  const LooseCookedLayout layout {};

  // Arrange
  const auto cooked_root = temp_dir_ / "loose_cooked_root";
  std::filesystem::create_directories(cooked_root / layout.materials_subdir);
  const auto index_path = cooked_root / "container.index.bin";

  MaterialAssetDesc material_desc {};
  material_desc.header.asset_type = static_cast<uint8_t>(AssetType::kMaterial);
  material_desc.header.version = oxygen::data::pak::v2::kMaterialAssetVersion;
  {
    const auto material_file
      = LooseCookedLayout::MaterialDescriptorFileName("TestMaterial");
    std::ofstream out(
      cooked_root / layout.materials_subdir / material_file, std::ios::binary);
    out.write(reinterpret_cast<const char*>(&material_desc),
      static_cast<std::streamsize>(sizeof(material_desc)));
  }

  std::string strings;
  strings.push_back('\0');
  const auto off_desc = static_cast<uint32_t>(strings.size());
  strings += std::string(layout.materials_subdir) + "/"
    + LooseCookedLayout::MaterialDescriptorFileName("TestMaterial");
  strings.push_back('\0');
  const auto off_vpath = static_cast<uint32_t>(strings.size());
  strings += layout.MaterialVirtualPath("TestMaterial");
  strings.push_back('\0');

  IndexHeader header {};
  FillTestGuid(header);
  header.version = 1;
  header.content_version = 0;
  header.flags = oxygen::data::loose_cooked::kHasVirtualPaths
    | oxygen::data::loose_cooked::kHasFileRecords;
  header.string_table_offset = sizeof(IndexHeader);
  header.string_table_size = static_cast<uint64_t>(strings.size());
  header.asset_entries_offset
    = header.string_table_offset + header.string_table_size;
  header.asset_count = 1;
  header.asset_entry_size = sizeof(AssetEntry);
  header.file_records_offset
    = header.asset_entries_offset + sizeof(AssetEntry) * header.asset_count;
  header.file_record_count = 0;
  header.file_record_size = sizeof(FileRecord);

  oxygen::data::AssetKey key {};
  AssetEntry entry {};
  entry.asset_key = key;
  entry.descriptor_relpath_offset = off_desc;
  entry.virtual_path_offset = off_vpath;
  entry.asset_type = static_cast<uint8_t>(AssetType::kMaterial);
  entry.descriptor_size = sizeof(MaterialAssetDesc);
  entry.descriptor_sha256[0] = 0x01; // Non-zero, intentionally incorrect

  {
    std::ofstream out(index_path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(&header), sizeof(header));
    out.write(strings.data(), static_cast<std::streamsize>(strings.size()));
    out.write(reinterpret_cast<const char*>(&entry), sizeof(entry));
  }

  // Act & Assert
  EXPECT_THROW(
    { asset_loader_->AddLooseCookedRoot(cooked_root); }, std::exception);
}

//! Test: Unknown header flags are rejected
/*!
 Scenario: Writes a minimal index with an unknown flags bit set. Verifies that
 mounting fails.
*/
NOLINT_TEST_F(LooseCookedIndexTest, AddLooseCookedRoot_UnknownFlags_Throws)
{
  using oxygen::data::loose_cooked::AssetEntry;
  using oxygen::data::loose_cooked::FileRecord;
  using oxygen::data::loose_cooked::IndexHeader;

  // Arrange
  const auto cooked_root = temp_dir_ / "loose_cooked_root";
  std::filesystem::create_directories(cooked_root);
  const auto index_path = cooked_root / "container.index.bin";

  IndexHeader header {};
  FillTestGuid(header);
  header.version = 1;
  header.content_version = 0;
  header.flags = 0x80000000u;
  header.string_table_offset = sizeof(IndexHeader);
  header.string_table_size = 1; // "\0"
  header.asset_entries_offset
    = header.string_table_offset + header.string_table_size;
  header.asset_count = 0;
  header.asset_entry_size = sizeof(AssetEntry);
  header.file_records_offset = header.asset_entries_offset;
  header.file_record_count = 0;
  header.file_record_size = sizeof(FileRecord);

  {
    std::ofstream out(index_path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(&header), sizeof(header));
    const char zero = 0;
    out.write(&zero, 1);
  }

  // Act & Assert
  EXPECT_THROW(
    { asset_loader_->AddLooseCookedRoot(cooked_root); }, std::exception);
}

//! Test: Non-zero flags must declare virtual-path support
/*!
 Scenario: Writes a minimal index with non-zero flags that omit
 `kHasVirtualPaths`. Verifies that mounting fails.
*/
NOLINT_TEST_F(
  LooseCookedIndexTest, AddLooseCookedRoot_FlagsMissingVirtualPaths_Throws)
{
  using oxygen::data::loose_cooked::AssetEntry;
  using oxygen::data::loose_cooked::FileRecord;
  using oxygen::data::loose_cooked::IndexHeader;

  // Arrange
  const auto cooked_root = temp_dir_ / "loose_cooked_root";
  std::filesystem::create_directories(cooked_root);
  const auto index_path = cooked_root / "container.index.bin";

  IndexHeader header {};
  FillTestGuid(header);
  header.version = 1;
  header.content_version = 0;
  header.flags = oxygen::data::loose_cooked::kHasFileRecords;
  header.string_table_offset = sizeof(IndexHeader);
  header.string_table_size = 1;
  header.asset_entries_offset
    = header.string_table_offset + header.string_table_size;
  header.asset_count = 0;
  header.asset_entry_size = sizeof(AssetEntry);
  header.file_records_offset = header.asset_entries_offset;
  header.file_record_count = 0;
  header.file_record_size = sizeof(FileRecord);

  {
    std::ofstream out(index_path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(&header), sizeof(header));
    const char zero = 0;
    out.write(&zero, 1);
  }

  // Act & Assert
  EXPECT_THROW(
    { asset_loader_->AddLooseCookedRoot(cooked_root); }, std::exception);
}

//! Test: File records are disallowed unless declared by flags
/*!
 Scenario: Writes an index with file records but omits `kHasFileRecords`.
 Verifies that mounting fails.
*/
NOLINT_TEST_F(
  LooseCookedIndexTest, AddLooseCookedRoot_FileRecordsWithoutFlag_Throws)
{
  using oxygen::data::loose_cooked::AssetEntry;
  using oxygen::data::loose_cooked::FileKind;
  using oxygen::data::loose_cooked::FileRecord;
  using oxygen::data::loose_cooked::IndexHeader;

  // Arrange
  const auto cooked_root = temp_dir_ / "loose_cooked_root";
  std::filesystem::create_directories(cooked_root);
  const auto index_path = cooked_root / "container.index.bin";

  std::string strings;
  strings.push_back('\0');
  const auto off_file = static_cast<uint32_t>(strings.size());
  strings += "Resources/textures.table";
  strings.push_back('\0');

  IndexHeader header {};
  FillTestGuid(header);
  header.version = 1;
  header.content_version = 0;
  header.flags = oxygen::data::loose_cooked::kHasVirtualPaths;
  header.string_table_offset = sizeof(IndexHeader);
  header.string_table_size = static_cast<uint64_t>(strings.size());
  header.asset_entries_offset
    = header.string_table_offset + header.string_table_size;
  header.asset_count = 0;
  header.asset_entry_size = sizeof(AssetEntry);
  header.file_records_offset = header.asset_entries_offset;
  header.file_record_count = 1;
  header.file_record_size = sizeof(FileRecord);

  FileRecord record {};
  record.kind = FileKind::kTexturesTable;
  record.relpath_offset = off_file;
  record.size = 0;

  {
    std::ofstream out(index_path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(&header), sizeof(header));
    out.write(strings.data(), static_cast<std::streamsize>(strings.size()));
    out.write(reinterpret_cast<const char*>(&record), sizeof(record));
  }

  // Act & Assert
  EXPECT_THROW(
    { asset_loader_->AddLooseCookedRoot(cooked_root); }, std::exception);
}

//! Test: Verify that an index without a GUID (all zeros) is rejected.
NOLINT_TEST_F(LooseCookedIndexTest, AddLooseCookedRoot_NoGuid_Throws)
{
  using oxygen::data::loose_cooked::FileRecord;
  using oxygen::data::loose_cooked::IndexHeader;

  const auto cooked_root = temp_dir_ / "loose_cooked_no_guid";
  std::filesystem::create_directories(cooked_root);
  const auto index_path = cooked_root / "container.index.bin";

  IndexHeader header {};
  header.version = 1;
  header.content_version = 0;
  header.flags = 0;
  header.string_table_offset = sizeof(IndexHeader);
  header.string_table_size = 1; // "\0"
  header.asset_entries_offset = sizeof(IndexHeader) + 1;
  header.asset_count = 0;
  header.file_records_offset = header.asset_entries_offset;
  header.file_record_count = 0;
  header.file_record_size = sizeof(FileRecord);

  // Leave GUID as zeros (default initialization)

  {
    std::ofstream out(index_path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(&header), sizeof(header));
    const char zero = 0;
    out.write(&zero, 1);
  }

  EXPECT_THROW(
    { asset_loader_->AddLooseCookedRoot(cooked_root); }, std::exception);
}

} // namespace
