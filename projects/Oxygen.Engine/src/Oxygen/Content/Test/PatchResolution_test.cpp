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
#include <string_view>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Content/VirtualPathResolver.h>
#include <Oxygen/Data/LooseCookedIndexFormat.h>
#include <Oxygen/Data/PakCatalog.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/PatchManifest.h>
#include <Oxygen/Data/SourceKey.h>

namespace {

constexpr auto kBaseGuidSeed = uint8_t { 0x01U };
constexpr auto kAltGuidSeed = uint8_t { 0x31U };
constexpr auto kMissingGuidSeed = uint8_t { 0xaaU };
constexpr auto kAssetSeedA = uint8_t { 0x11U };
constexpr auto kAssetSeedB = uint8_t { 0x22U };

auto MakeAssetKey(const uint8_t seed) -> oxygen::data::AssetKey
{
  auto bytes = std::array<std::uint8_t, oxygen::data::AssetKey::kSizeBytes> {};
  bytes[0] = seed;
  return oxygen::data::AssetKey::FromBytes(bytes);
}

auto MakeSourceKey(const uint8_t seed) -> oxygen::data::SourceKey
{
  auto bytes = std::array<std::uint8_t, oxygen::data::SourceKey::kSizeBytes> {};
  bytes[0] = seed;
  return oxygen::data::SourceKey::FromBytes(bytes);
}

auto WriteSingleAssetIndex(const std::filesystem::path& cooked_root,
  const oxygen::data::AssetKey& key, const std::string_view descriptor_relpath,
  const std::string_view virtual_path, const uint8_t guid_seed) -> void
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

  for (size_t i = 0; i < sizeof(header.guid); ++i) {
    header.guid[i] = static_cast<uint8_t>(guid_seed + i);
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

NOLINT_TEST(PatchResolutionRuntimeTest, LastMountedWinsForVirtualPathLookup)
{
  constexpr auto kVirtualPath = "/.cooked/Asset.bin";
  const auto root = std::filesystem::temp_directory_path() / "oxygen_patch_rt";
  const auto root0 = root / "root0";
  const auto root1 = root / "root1";
  const auto key0 = MakeAssetKey(kAssetSeedA);
  const auto key1 = MakeAssetKey(kAssetSeedB);

  WriteSingleAssetIndex(root0, key0, "A0.bin", kVirtualPath, kBaseGuidSeed);
  WriteSingleAssetIndex(root1, key1, "A1.bin", kVirtualPath, kAltGuidSeed);

  oxygen::content::VirtualPathResolver resolver;
  resolver.AddLooseCookedRoot(root0);
  resolver.AddLooseCookedRoot(root1);

  const auto resolved = resolver.ResolveAssetKey(kVirtualPath);

  ASSERT_TRUE(resolved.has_value());
  EXPECT_EQ(*resolved, key1);
}

NOLINT_TEST(
  PatchResolutionRuntimeTest, CompatibilityMismatchRejectsPatchMounting)
{
  constexpr auto kVirtualPath = "/.cooked/Asset.bin";
  const auto root = std::filesystem::temp_directory_path() / "oxygen_patch_rt";
  const auto base_root = root / "base";
  const auto patch_pak = root / "patch.pak";
  const auto key = MakeAssetKey(kAssetSeedA);

  WriteSingleAssetIndex(
    base_root, key, "Base.bin", kVirtualPath, kBaseGuidSeed);
  WriteSingleAssetPakWithBrowseIndex(patch_pak, key, kVirtualPath);

  oxygen::data::PatchManifest manifest {};
  manifest.compatibility_policy_snapshot.require_exact_base_set = false;
  manifest.compatibility_policy_snapshot.require_content_version_match = false;
  manifest.compatibility_policy_snapshot.require_catalog_digest_match = false;
  manifest.compatibility_policy_snapshot.require_base_source_key_match = true;
  manifest.compatibility_envelope.required_base_source_keys.push_back(
    MakeSourceKey(kMissingGuidSeed));

  oxygen::content::VirtualPathResolver resolver;
  resolver.AddLooseCookedRoot(base_root);

  EXPECT_THROW(
    {
      resolver.AddPatchPakFile(
        patch_pak, manifest, std::span<const oxygen::data::PakCatalog> {});
    },
    std::runtime_error);
}

NOLINT_TEST(
  PatchResolutionRuntimeTest, TombstoneBlocksFallbackDeterministically)
{
  constexpr auto kVirtualPath = "/.cooked/Masked.bin";
  const auto root = std::filesystem::temp_directory_path() / "oxygen_patch_rt";
  const auto base_root = root / "base";
  const auto patch_pak = root / "patch.pak";
  const auto key = MakeAssetKey(kAssetSeedA);

  WriteSingleAssetIndex(
    base_root, key, "Base.bin", kVirtualPath, kBaseGuidSeed);
  WriteSingleAssetPakWithBrowseIndex(patch_pak, key, kVirtualPath);

  oxygen::data::PatchManifest manifest {};
  manifest.compatibility_policy_snapshot.require_exact_base_set = false;
  manifest.compatibility_policy_snapshot.require_content_version_match = false;
  manifest.compatibility_policy_snapshot.require_base_source_key_match = false;
  manifest.compatibility_policy_snapshot.require_catalog_digest_match = false;
  manifest.deleted.push_back(key);

  oxygen::content::VirtualPathResolver resolver;
  resolver.AddLooseCookedRoot(base_root);
  resolver.AddPatchPakFile(
    patch_pak, manifest, std::span<const oxygen::data::PakCatalog> {});

  const auto resolved = resolver.ResolveAssetKey(kVirtualPath);

  EXPECT_FALSE(resolved.has_value());
}

} // namespace
