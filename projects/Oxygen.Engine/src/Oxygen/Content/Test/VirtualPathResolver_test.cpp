//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Content/VirtualPathResolver.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/LooseCookedIndexFormat.h>
#include <Oxygen/Data/PakCatalog.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/PatchManifest.h>
#include <Oxygen/Testing/GTest.h>

namespace {

auto MakeAssetKey(const std::uint8_t seed) -> oxygen::data::AssetKey
{
  auto bytes = std::array<std::uint8_t, oxygen::data::AssetKey::kSizeBytes> {};
  bytes.at(0) = seed;
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
  header.version = oxygen::data::loose_cooked::kIndexVersion;
  header.content_version = 0;
  header.flags = oxygen::data::loose_cooked::kHasVirtualPaths
    | oxygen::data::loose_cooked::kHasFileRecords;

  // The runtime loader rejects indexes with an all-zero GUID.
  // For these tests we only need a valid (non-zero) value.
  for (size_t i = 0; i < sizeof(header.source_identity); ++i) {
    header.source_identity.at(i) = static_cast<uint8_t>(i + 1);
  }
  header.source_identity.at(6)
    = static_cast<uint8_t>((header.source_identity.at(6) & 0x0FU) | 0x70U);
  header.source_identity.at(8)
    = static_cast<uint8_t>((header.source_identity.at(8) & 0x3FU) | 0x80U);

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
  std::ranges::copy(oxygen::base::ComputeSha256({}), entry.descriptor_sha256);

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
  if (!resolved.has_value()) {
    FAIL() << "Expected a resolved asset key";
  }
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
  if (!resolved.has_value()) {
    FAIL() << "Expected a resolved asset key";
  }
  EXPECT_EQ(*resolved, key1);
  resolver.AddLooseCookedRoot(cooked_root0);
  EXPECT_EQ(resolver.ResolveAssetKey("/.cooked/A.bin"), key0);
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

  constexpr auto kValidPaths = std::array<std::string_view, 11> {
    "/Game/Physics/Materials/Rubber.opmat", "/Game/Physics/Materials/Ice.opmat",
    "/game/Physics/Materials/Rubber.opmat",
    "/Game/Physics/Shapes/BoulderConvexHull.ocshape",
    "/Game/Physics/Vehicles/Wheeled/SportsCar.opscene",
    "/Game/World/Scenes/Showcase.oscene", "/Game/World/Scenes/Showcase.opscene",
    "/Engine/Physics/Materials/Default.opmat",
    "/Pak/DLC01/Game/Physics/Materials/Lava.opmat",
    "/.cooked/Physics/Materials/Rubber.opmat",
    "/Custom/Physics/Materials/Rubber.opmat"
  };

  for (const auto path : kValidPaths) {
    EXPECT_NO_THROW({
      const auto resolved = resolver.ResolveAssetKey(path);
      EXPECT_FALSE(resolved.has_value());
    }) << path;
  }

  constexpr auto kInvalidPaths = std::array<std::string_view, 11> {
    "/9game/Physics/Materials/Rubber.opmat", "Physics/Materials/Rubber.opmat",
    "/Game/Physics/Materials/Rubber.opmat/",
    "/Game/Physics//Materials/Rubber.opmat",
    "/Game/Physics/Materials/../Rubber.opmat",
    "/Game/Physics/Materials/my rubber.opmat",
    "/Game/Physics.Materials/Rubber.opmat", "/Game/Physics/Materials/Rubber",
    "/Game/Physics/Materials/.Rubber",
    "/Pak/DLC.01/Game/Physics/Materials/Lava.opmat", "/Pak"
  };

  for (const auto path : kInvalidPaths) {
    EXPECT_THROW(
      { (void)resolver.ResolveAssetKey(path); }, std::invalid_argument)
      << path;
  }
}

} // namespace
