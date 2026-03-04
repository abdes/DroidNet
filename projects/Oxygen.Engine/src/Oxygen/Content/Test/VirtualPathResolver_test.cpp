//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <vector>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Content/VirtualPathResolver.h>
#include <Oxygen/Data/LooseCookedIndexFormat.h>
#include <Oxygen/Data/PakFormat.h>

namespace {

auto MakeAssetKey(const std::uint8_t seed) -> oxygen::data::AssetKey
{
  auto bytes = std::array<std::uint8_t, oxygen::data::AssetKey::kSizeBytes> {};
  bytes[0] = seed;
  return oxygen::data::AssetKey::FromBytes(bytes);
}

//! Test helper: write a minimal loose cooked index with one asset entry.
/*!
 Scenario: Creates a `container.index.bin` mapping the given virtual path to
 the provided `AssetKey`.
*/
auto WriteSingleAssetIndex(const std::filesystem::path& cooked_root,
  const oxygen::data::AssetKey& key, const std::string_view descriptor_relpath,
  const std::string_view virtual_path) -> void
{
  using oxygen::data::loose_cooked::AssetEntry;
  using oxygen::data::loose_cooked::FileRecord;
  using oxygen::data::loose_cooked::IndexHeader;

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
  header.flags = oxygen::data::loose_cooked::kHasVirtualPaths
    | oxygen::data::loose_cooked::kHasFileRecords;

  // The runtime loader rejects indexes with an all-zero GUID.
  // For these tests we only need a valid (non-zero) value.
  for (size_t i = 0; i < sizeof(header.guid); ++i) {
    header.guid[i] = static_cast<uint8_t>(i + 1);
  }

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

//! Test helper: write a minimal pak with an embedded browse index.
/*!
 Scenario: Creates a `.pak` file whose footer references an embedded browse
 index mapping the given virtual path to the provided AssetKey.
*/
auto WriteSingleAssetPakWithBrowseIndex(const std::filesystem::path& pak_path,
  const oxygen::data::AssetKey& key, const std::string_view virtual_path)
  -> void
{
  using oxygen::data::pak::core::AssetDirectoryEntry;
  using oxygen::data::pak::core::PakBrowseIndexEntry;
  using oxygen::data::pak::core::PakBrowseIndexHeader;
  using oxygen::data::pak::core::PakFooter;
  using oxygen::data::pak::core::PakHeader;

  PakHeader header {};

  std::string strings;
  const auto off_vpath = static_cast<uint32_t>(strings.size());
  strings += virtual_path;

  PakBrowseIndexHeader bheader {};
  bheader.version = 1;
  bheader.entry_count = 1;
  bheader.string_table_size = static_cast<uint32_t>(strings.size());

  PakBrowseIndexEntry bentry {};
  bentry.asset_key = key;
  bentry.virtual_path_offset = off_vpath;
  bentry.virtual_path_length = static_cast<uint32_t>(strings.size());

  AssetDirectoryEntry dir {};
  dir.asset_key = key;
  dir.asset_type = oxygen::data::AssetType::kUnknown;

  const uint64_t directory_offset = sizeof(PakHeader);
  const uint64_t browse_offset = directory_offset + sizeof(AssetDirectoryEntry);
  const uint64_t browse_size = sizeof(PakBrowseIndexHeader)
    + sizeof(PakBrowseIndexEntry) + strings.size();

  dir.entry_offset = directory_offset;
  dir.desc_offset = 0;
  dir.desc_size = 0;

  PakFooter footer {};
  footer.directory_offset = directory_offset;
  footer.directory_size = sizeof(AssetDirectoryEntry);
  footer.asset_count = 1;

  footer.browse_index_offset = browse_offset;
  footer.browse_index_size = browse_size;

  std::filesystem::create_directories(pak_path.parent_path());
  std::ofstream out(pak_path, std::ios::binary);
  out.write(reinterpret_cast<const char*>(&header), sizeof(header));
  out.write(reinterpret_cast<const char*>(&dir), sizeof(dir));
  out.write(reinterpret_cast<const char*>(&bheader), sizeof(bheader));
  out.write(reinterpret_cast<const char*>(&bentry), sizeof(bentry));
  out.write(strings.data(), static_cast<std::streamsize>(strings.size()));
  out.write(reinterpret_cast<const char*>(&footer), sizeof(footer));
}

//! Test: Resolver returns the AssetKey for a matching virtual path
/*!
 Scenario: Mounts a single cooked root and resolves a known virtual path.
*/
NOLINT_TEST(VirtualPathResolverTest, ResolveAssetKeyFoundReturnsKey)
{
  // Arrange
  const auto root
    = std::filesystem::temp_directory_path() / "oxygen_vpath_resolver_test";
  const auto cooked_root = root / "root0";

  const auto key = MakeAssetKey(0x11U);

  WriteSingleAssetIndex(cooked_root, key, "A.bin", "/.cooked/A.bin");

  oxygen::content::VirtualPathResolver resolver;
  resolver.AddLooseCookedRoot(cooked_root);

  // Act
  const auto resolved = resolver.ResolveAssetKey("/.cooked/A.bin");

  // Assert
  EXPECT_TRUE(resolved.has_value());
  EXPECT_EQ(*resolved, key);
}

//! Test: Resolver prefers the last mounted root
/*!
 Scenario: Two roots contain the same virtual path, mapping to different
 * keys.
 Verifies that the last added root wins.
*/
NOLINT_TEST(VirtualPathResolverTest, ResolveAssetKeyDuplicatePathLastWins)
{
  // Arrange
  const auto root
    = std::filesystem::temp_directory_path() / "oxygen_vpath_resolver_test";
  const auto cooked_root0 = root / "root0";
  const auto cooked_root1 = root / "root1";

  const auto key0 = MakeAssetKey(0x11U);
  const auto key1 = MakeAssetKey(0x22U);

  WriteSingleAssetIndex(cooked_root0, key0, "A0.bin", "/.cooked/A.bin");
  WriteSingleAssetIndex(cooked_root1, key1, "A1.bin", "/.cooked/A.bin");

  oxygen::content::VirtualPathResolver resolver;
  resolver.AddLooseCookedRoot(cooked_root0);
  resolver.AddLooseCookedRoot(cooked_root1);

  // Act
  const auto resolved = resolver.ResolveAssetKey("/.cooked/A.bin");

  // Assert
  EXPECT_TRUE(resolved.has_value());
  EXPECT_EQ(*resolved, key1);
}

//! Test: Resolver returns nullopt when the virtual path is not found
/*!
 Scenario: Mounts a cooked root and queries an unknown virtual path.
*/
NOLINT_TEST(VirtualPathResolverTest, ResolveAssetKeyNotFoundReturnsNullopt)
{
  // Arrange
  const auto root
    = std::filesystem::temp_directory_path() / "oxygen_vpath_resolver_test";
  const auto cooked_root = root / "root0";

  const auto key = MakeAssetKey(0x11U);

  WriteSingleAssetIndex(cooked_root, key, "A.bin", "/.cooked/A.bin");

  oxygen::content::VirtualPathResolver resolver;
  resolver.AddLooseCookedRoot(cooked_root);

  // Act
  const auto resolved = resolver.ResolveAssetKey("/.cooked/DoesNotExist.bin");

  // Assert
  EXPECT_FALSE(resolved.has_value());
}

//! Test: Resolver rejects non-canonical virtual paths
/*!
 Scenario: Attempts to resolve a virtual path missing the leading '/'.
 Verifies the resolver throws.
*/
NOLINT_TEST(VirtualPathResolverTest, ResolveAssetKeyInvalidVirtualPathThrows)
{
  // Arrange
  oxygen::content::VirtualPathResolver resolver;

  // Act & Assert
  EXPECT_THROW(
    { (void)resolver.ResolveAssetKey(".cooked/A.bin"); },
    std::invalid_argument);
}

//! Test: Resolver accepts canonical paths from the phase-1 spec and rejects
//! invalid forms.
/*!
 Scenario: No mounts are needed; we validate canonical path parsing
 * behavior.
*/
NOLINT_TEST(VirtualPathResolverTest, CanonicalPathValidationMatrixFromSpec)
{
  oxygen::content::VirtualPathResolver resolver;

  constexpr auto kValidPaths = std::array<std::string_view, 9> {
    "/Game/Physics/Materials/Rubber.opmat", "/Game/Physics/Materials/Ice.opmat",
    "/Game/Physics/Shapes/BoulderConvexHull.ocshape",
    "/Game/Physics/Vehicles/Wheeled/SportsCar.opscene",
    "/Game/World/Scenes/Showcase.oscene", "/Game/World/Scenes/Showcase.opscene",
    "/Engine/Physics/Materials/Default.opmat",
    "/Pak/DLC01/Game/Physics/Materials/Lava.opmat",
    "/.cooked/Physics/Materials/Rubber.opmat"
  };

  for (const auto path : kValidPaths) {
    EXPECT_NO_THROW({
      const auto resolved = resolver.ResolveAssetKey(path);
      EXPECT_FALSE(resolved.has_value());
    }) << path;
  }

  constexpr auto kInvalidPaths
    = std::array<std::string_view, 12> { "/game/Physics/Materials/Rubber.opmat",
        "Physics/Materials/Rubber.opmat",
        "/Game/Physics/Materials/Rubber.opmat/",
        "/Game/Physics//Materials/Rubber.opmat",
        "/Game/Physics/Materials/../Rubber.opmat",
        "/Game/Physics/Materials/my rubber.opmat",
        "/Game/Physics.Materials/Rubber.opmat",
        "/Game/Physics/Materials/Rubber", "/Game/Physics/Materials/.Rubber",
        "/Pak/DLC.01/Game/Physics/Materials/Lava.opmat",
        "/Custom/Physics/Materials/Rubber.opmat", "/Pak" };

  for (const auto path : kInvalidPaths) {
    EXPECT_THROW(
      { (void)resolver.ResolveAssetKey(path); }, std::invalid_argument)
      << path;
  }
}

//! Test: Resolver can resolve virtual paths using mounted pak browse index.
/*!
 Scenario: Creates a `.pak` with an embedded browse index and resolves a known
 virtual path.
*/
NOLINT_TEST(
  VirtualPathResolverTest, ResolveAssetKeyPakBrowseIndexFoundReturnsKey)
{
  // Arrange
  const auto root
    = std::filesystem::temp_directory_path() / "oxygen_vpath_resolver_test";
  const auto pak_path = root / "mounted.pak";

  const auto key = MakeAssetKey(0x33U);

  WriteSingleAssetPakWithBrowseIndex(pak_path, key, "/.cooked/Pak.bin");

  oxygen::content::VirtualPathResolver resolver;
  resolver.AddPakFile(pak_path);

  // Act
  const auto resolved = resolver.ResolveAssetKey("/.cooked/Pak.bin");

  // Assert
  EXPECT_TRUE(resolved.has_value());
  EXPECT_EQ(*resolved, key);
}

//! Test: Patch tombstones block virtual-path fallthrough deterministically.
/*!
 Scenario: Base root and patch pak both map the same virtual path/key, and
 * the
 patch manifest tombstones that key. Resolution must return `nullopt`.
*/
NOLINT_TEST(VirtualPathResolverTest, PatchTombstoneBlocksVirtualPathLookup)
{
  using oxygen::data::PatchManifest;
  constexpr auto kPatchSeed = uint8_t { 0x66U };

  const auto root
    = std::filesystem::temp_directory_path() / "oxygen_vpath_resolver_test";
  const auto cooked_root = root / "base";
  const auto patch_pak_path = root / "patch.pak";
  const auto key = MakeAssetKey(kPatchSeed);
  constexpr auto kVirtualPath = "/.cooked/Masked.bin";

  WriteSingleAssetIndex(cooked_root, key, "Masked.bin", kVirtualPath);
  WriteSingleAssetPakWithBrowseIndex(patch_pak_path, key, kVirtualPath);

  PatchManifest manifest {};
  manifest.compatibility_policy_snapshot.require_exact_base_set = false;
  manifest.compatibility_policy_snapshot.require_content_version_match = false;
  manifest.compatibility_policy_snapshot.require_base_source_key_match = false;
  manifest.compatibility_policy_snapshot.require_catalog_digest_match = false;
  manifest.deleted.push_back(key);

  oxygen::content::VirtualPathResolver resolver;
  resolver.AddLooseCookedRoot(cooked_root);
  resolver.AddPatchPakFile(
    patch_pak_path, manifest, std::span<const oxygen::data::PakCatalog> {});

  const auto resolved = resolver.ResolveAssetKey(kVirtualPath);

  EXPECT_FALSE(resolved.has_value());
}

} // namespace
