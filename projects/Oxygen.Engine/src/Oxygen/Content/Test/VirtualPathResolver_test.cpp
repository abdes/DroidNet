//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <filesystem>
#include <fstream>
#include <string>

#include <Oxygen/Content/VirtualPathResolver.h>
#include <Oxygen/Data/LooseCookedIndexFormat.h>
#include <Oxygen/Testing/GTest.h>

namespace {

//! Test helper: write a minimal loose cooked index with one asset entry.
/*!
 Scenario: Creates a `container.index.bin` mapping the given virtual path to the
 provided `AssetKey`.
*/
auto WriteSingleAssetIndex(const std::filesystem::path& cooked_root,
  const oxygen::data::AssetKey& key, const std::string_view descriptor_relpath,
  const std::string_view virtual_path) -> void
{
  using oxygen::data::loose_cooked::v1::AssetEntry;
  using oxygen::data::loose_cooked::v1::FileRecord;
  using oxygen::data::loose_cooked::v1::IndexHeader;

  std::filesystem::create_directories(cooked_root);

  std::string strings;
  strings.push_back('\0');
  const auto off_desc = static_cast<uint32_t>(strings.size());
  strings += descriptor_relpath;
  strings.push_back('\0');
  const auto off_vpath = static_cast<uint32_t>(strings.size());
  strings += virtual_path;
  strings.push_back('\0');

  IndexHeader header {};
  header.version = 1;
  header.content_version = 0;
  header.flags = oxygen::data::loose_cooked::v1::kHasVirtualPaths
    | oxygen::data::loose_cooked::v1::kHasFileRecords;
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

  AssetEntry entry {};
  entry.asset_key = key;
  entry.descriptor_relpath_offset = off_desc;
  entry.virtual_path_offset = off_vpath;
  entry.asset_type = 0;
  entry.descriptor_size = 0;

  const auto index_path = cooked_root / "container.index.bin";
  std::ofstream out(index_path, std::ios::binary);
  out.write(reinterpret_cast<const char*>(&header), sizeof(header));
  out.write(strings.data(), static_cast<std::streamsize>(strings.size()));
  out.write(reinterpret_cast<const char*>(&entry), sizeof(entry));
}

//! Test: Resolver returns the AssetKey for a matching virtual path
/*!
 Scenario: Mounts a single cooked root and resolves a known virtual path.
*/
NOLINT_TEST(VirtualPathResolverTest, ResolveAssetKey_Found_ReturnsKey)
{
  // Arrange
  const auto root
    = std::filesystem::temp_directory_path() / "oxygen_vpath_resolver_test";
  const auto cooked_root = root / "root0";

  oxygen::data::AssetKey key {};
  key.guid[0] = 0x11;

  WriteSingleAssetIndex(cooked_root, key, "assets/A.bin", "/Content/A.bin");

  oxygen::content::VirtualPathResolver resolver;
  resolver.AddLooseCookedRoot(cooked_root);

  // Act
  const auto resolved = resolver.ResolveAssetKey("/Content/A.bin");

  // Assert
  EXPECT_TRUE(resolved.has_value());
  EXPECT_EQ(resolved->guid[0], 0x11);
}

//! Test: Resolver prefers the first mounted root
/*!
 Scenario: Two roots contain the same virtual path, mapping to different keys.
 Verifies that the first added root wins.
*/
NOLINT_TEST(VirtualPathResolverTest, ResolveAssetKey_DuplicatePath_FirstWins)
{
  // Arrange
  const auto root
    = std::filesystem::temp_directory_path() / "oxygen_vpath_resolver_test";
  const auto cooked_root0 = root / "root0";
  const auto cooked_root1 = root / "root1";

  oxygen::data::AssetKey key0 {};
  key0.guid[0] = 0x11;
  oxygen::data::AssetKey key1 {};
  key1.guid[0] = 0x22;

  WriteSingleAssetIndex(cooked_root0, key0, "assets/A0.bin", "/Content/A.bin");
  WriteSingleAssetIndex(cooked_root1, key1, "assets/A1.bin", "/Content/A.bin");

  oxygen::content::VirtualPathResolver resolver;
  resolver.AddLooseCookedRoot(cooked_root0);
  resolver.AddLooseCookedRoot(cooked_root1);

  // Act
  const auto resolved = resolver.ResolveAssetKey("/Content/A.bin");

  // Assert
  EXPECT_TRUE(resolved.has_value());
  EXPECT_EQ(resolved->guid[0], 0x11);
}

//! Test: Resolver returns nullopt when the virtual path is not found
/*!
 Scenario: Mounts a cooked root and queries an unknown virtual path.
*/
NOLINT_TEST(VirtualPathResolverTest, ResolveAssetKey_NotFound_ReturnsNullopt)
{
  // Arrange
  const auto root
    = std::filesystem::temp_directory_path() / "oxygen_vpath_resolver_test";
  const auto cooked_root = root / "root0";

  oxygen::data::AssetKey key {};
  key.guid[0] = 0x11;

  WriteSingleAssetIndex(cooked_root, key, "assets/A.bin", "/Content/A.bin");

  oxygen::content::VirtualPathResolver resolver;
  resolver.AddLooseCookedRoot(cooked_root);

  // Act
  const auto resolved = resolver.ResolveAssetKey("/Content/DoesNotExist.bin");

  // Assert
  EXPECT_FALSE(resolved.has_value());
}

//! Test: Resolver rejects non-canonical virtual paths
/*!
 Scenario: Attempts to resolve a virtual path missing the leading '/'.
 Verifies the resolver throws.
*/
NOLINT_TEST(VirtualPathResolverTest, ResolveAssetKey_InvalidVirtualPath_Throws)
{
  // Arrange
  oxygen::content::VirtualPathResolver resolver;

  // Act & Assert
  EXPECT_THROW(
    { (void)resolver.ResolveAssetKey("Content/A.bin"); },
    std::invalid_argument);
}

} // namespace
