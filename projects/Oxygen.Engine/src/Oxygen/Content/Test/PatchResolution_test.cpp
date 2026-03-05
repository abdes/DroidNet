//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Content/Internal/PatchResolutionPolicy.h>
#include <Oxygen/Content/VirtualPathResolver.h>
#include <Oxygen/Data/LooseCookedIndexFormat.h>
#include <Oxygen/Data/PakCatalog.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/PatchManifest.h>
#include <Oxygen/Data/SourceKey.h>

namespace {
namespace data = oxygen::data;

constexpr auto kBaseGuidSeed = uint8_t { 0x01U };
constexpr auto kAltGuidSeed = uint8_t { 0x31U };
constexpr auto kMissingGuidSeed = uint8_t { 0xaaU };
constexpr auto kAssetSeedA = uint8_t { 0x11U };
constexpr auto kAssetSeedB = uint8_t { 0x22U };
constexpr auto kAssetSeedC = uint8_t { 0x33U };

auto MakeAssetKey(const uint8_t seed) -> data::AssetKey
{
  auto bytes = std::array<std::uint8_t, data::AssetKey::kSizeBytes> {};
  bytes[0] = seed;
  return data::AssetKey::FromBytes(bytes);
}

auto MakeSourceKey(const uint8_t seed) -> data::SourceKey
{
  auto bytes = std::array<std::uint8_t, data::SourceKey::kSizeBytes> {};
  bytes[0] = seed;
  return data::SourceKey::FromBytes(bytes);
}

auto MakeCatalogDigest(const uint8_t seed) -> std::array<uint8_t, 32>
{
  auto digest = std::array<uint8_t, 32> {};
  digest[0] = seed;
  return digest;
}

struct SourceResolutionState final {
  std::unordered_set<data::AssetKey> assets;
  std::unordered_set<data::AssetKey> tombstones;
  std::unordered_map<std::string, data::AssetKey> virtual_path_to_asset;
};

auto FindState(
  const std::unordered_map<uint16_t, SourceResolutionState>& states,
  const uint16_t source_id) -> const SourceResolutionState*
{
  if (const auto it = states.find(source_id); it != states.end()) {
    return &it->second;
  }
  return nullptr;
}

auto MakeResolutionCallbacks(
  const std::unordered_map<uint16_t, SourceResolutionState>& states)
  -> oxygen::content::internal::VirtualPathResolutionCallbacks
{
  namespace policy = oxygen::content::internal;

  return policy::VirtualPathResolutionCallbacks {
    .key_resolution = policy::KeyResolutionCallbacks {
      .source_has_asset
      = [&states](const uint16_t source_id, const data::AssetKey& key) -> bool {
        if (const auto* state = FindState(states, source_id);
          state != nullptr) {
          return state->assets.contains(key);
        }
        return false;
      },
      .source_tombstones_asset
      = [&states](const uint16_t source_id, const data::AssetKey& key) -> bool {
        if (const auto* state = FindState(states, source_id);
          state != nullptr) {
          return state->tombstones.contains(key);
        }
        return false;
      },
    },
    .resolve_virtual_path
    = [&states](const uint16_t source_id,
        const std::string_view virtual_path) -> std::optional<data::AssetKey> {
      if (const auto* state = FindState(states, source_id); state != nullptr) {
        if (const auto it
          = state->virtual_path_to_asset.find(std::string { virtual_path });
          it != state->virtual_path_to_asset.end()) {
          return it->second;
        }
      }
      return std::nullopt;
    },
  };
}

auto CountCompatibilityCode(
  const std::vector<oxygen::content::internal::PatchCompatibilityDiagnostic>&
    diagnostics,
  const oxygen::content::internal::PatchCompatibilityCode code) -> size_t
{
  return static_cast<size_t>(
    std::count_if(diagnostics.begin(), diagnostics.end(),
      [code](
        const oxygen::content::internal::PatchCompatibilityDiagnostic& item) {
        return item.code == code;
      }));
}

NOLINT_TEST(PatchResolutionPolicyTest,
  MultiMountVirtualPathCollisionsEmitMaskedSourceDiagnostics)
{
  namespace policy = oxygen::content::internal;

  constexpr auto kVirtualPath = "/.cooked/collision.bin";
  constexpr auto kSourceA = uint16_t { 10U };
  constexpr auto kSourceB = uint16_t { 20U };
  constexpr auto kSourceC = uint16_t { 30U };
  constexpr auto kSourceD = uint16_t { 40U };

  const auto key_a = MakeAssetKey(kAssetSeedA);
  const auto key_b = MakeAssetKey(kAssetSeedB);
  const auto key_c = MakeAssetKey(kAssetSeedC);

  const auto source_ids = std::array<uint16_t, 4> {
    kSourceA,
    kSourceB,
    kSourceC,
    kSourceD,
  };

  auto states = std::unordered_map<uint16_t, SourceResolutionState> {};
  states[kSourceA].assets.insert(key_a);
  states[kSourceA].virtual_path_to_asset.emplace(kVirtualPath, key_a);

  states[kSourceB].assets.insert(key_c);
  states[kSourceB].virtual_path_to_asset.emplace(kVirtualPath, key_c);

  states[kSourceC].assets.insert(key_b);
  states[kSourceC].virtual_path_to_asset.emplace(kVirtualPath, key_b);

  states[kSourceD].assets.insert(key_a);
  states[kSourceD].virtual_path_to_asset.emplace(kVirtualPath, key_a);

  const auto callbacks = MakeResolutionCallbacks(states);
  const auto result = policy::ResolveVirtualPathByPrecedence(
    source_ids, kVirtualPath, callbacks);

  ASSERT_TRUE(result.asset_key.has_value());
  EXPECT_EQ(*result.asset_key, key_a);
  EXPECT_EQ(result.key_result.status, policy::KeyResolutionStatus::kFound);
  ASSERT_TRUE(result.key_result.source_id.has_value());
  EXPECT_EQ(*result.key_result.source_id, kSourceD);

  ASSERT_EQ(result.collisions.size(), 2U);
  EXPECT_EQ(result.collisions[0].winner_source_id, kSourceD);
  EXPECT_EQ(result.collisions[0].masked_source_id, kSourceC);
  EXPECT_EQ(result.collisions[0].winner_key, key_a);
  EXPECT_EQ(result.collisions[0].masked_key, key_b);

  EXPECT_EQ(result.collisions[1].winner_source_id, kSourceD);
  EXPECT_EQ(result.collisions[1].masked_source_id, kSourceB);
  EXPECT_EQ(result.collisions[1].winner_key, key_a);
  EXPECT_EQ(result.collisions[1].masked_key, key_c);
}

NOLINT_TEST(PatchResolutionPolicyTest,
  TombstoneInHigherPrecedenceMountMasksWinnerAndPreservesCollisionDiagnostics)
{
  namespace policy = oxygen::content::internal;

  constexpr auto kVirtualPath = "/.cooked/masked.bin";
  constexpr auto kSourceBase = uint16_t { 11U };
  constexpr auto kSourceMid = uint16_t { 21U };
  constexpr auto kSourceWinner = uint16_t { 31U };
  constexpr auto kSourceTombstone = uint16_t { 41U };

  const auto winner_key = MakeAssetKey(kAssetSeedA);
  const auto masked_key = MakeAssetKey(kAssetSeedB);

  const auto source_ids = std::array<uint16_t, 4> {
    kSourceBase,
    kSourceMid,
    kSourceWinner,
    kSourceTombstone,
  };

  auto states = std::unordered_map<uint16_t, SourceResolutionState> {};
  states[kSourceWinner].assets.insert(winner_key);
  states[kSourceWinner].virtual_path_to_asset.emplace(kVirtualPath, winner_key);

  states[kSourceMid].assets.insert(masked_key);
  states[kSourceMid].virtual_path_to_asset.emplace(kVirtualPath, masked_key);

  states[kSourceBase].assets.insert(winner_key);
  states[kSourceBase].virtual_path_to_asset.emplace(kVirtualPath, winner_key);

  states[kSourceTombstone].tombstones.insert(winner_key);

  const auto callbacks = MakeResolutionCallbacks(states);
  const auto result = policy::ResolveVirtualPathByPrecedence(
    source_ids, kVirtualPath, callbacks);

  EXPECT_FALSE(result.asset_key.has_value());
  EXPECT_EQ(result.key_result.status, policy::KeyResolutionStatus::kTombstoned);
  ASSERT_TRUE(result.key_result.source_id.has_value());
  EXPECT_EQ(*result.key_result.source_id, kSourceTombstone);

  ASSERT_EQ(result.collisions.size(), 1U);
  EXPECT_EQ(result.collisions[0].winner_source_id, kSourceWinner);
  EXPECT_EQ(result.collisions[0].masked_source_id, kSourceMid);
  EXPECT_EQ(result.collisions[0].winner_key, winner_key);
  EXPECT_EQ(result.collisions[0].masked_key, masked_key);
}

NOLINT_TEST(PatchResolutionPolicyTest,
  CompatibilityDiagnosticsEmitCompleteMissingAndUnexpectedSetsAcrossChains)
{
  namespace policy = oxygen::content::internal;

  const auto source_a = MakeSourceKey(0x01U);
  const auto source_b = MakeSourceKey(0x02U);
  const auto source_c = MakeSourceKey(0x03U);
  const auto required_source = MakeSourceKey(0x04U);

  const auto digest_a = MakeCatalogDigest(0x21U);
  const auto digest_b = MakeCatalogDigest(0x22U);
  const auto required_digest = MakeCatalogDigest(0x24U);

  const auto mounted_source_keys = std::array {
    source_a,
    source_b,
    source_c,
  };

  const auto mounted_catalogs = std::array {
    data::PakCatalog {
      .source_key = source_a,
      .content_version = 5U,
      .catalog_digest = digest_a,
      .entries = {},
    },
    data::PakCatalog {
      .source_key = source_b,
      .content_version = 9U,
      .catalog_digest = digest_b,
      .entries = {},
    },
  };

  auto manifest = data::PatchManifest {};
  manifest.compatibility_policy_snapshot.require_exact_base_set = true;
  manifest.compatibility_policy_snapshot.require_base_source_key_match = true;
  manifest.compatibility_policy_snapshot.require_content_version_match = true;
  manifest.compatibility_policy_snapshot.require_catalog_digest_match = true;
  manifest.compatibility_envelope.required_base_source_keys = {
    source_a,
    required_source,
  };
  manifest.compatibility_envelope.required_base_content_versions = {
    5U,
    11U,
  };
  manifest.compatibility_envelope.required_base_catalog_digests = {
    digest_a,
    required_digest,
  };

  const auto result = policy::ValidatePatchCompatibility(
    mounted_source_keys, mounted_catalogs, manifest);

  EXPECT_FALSE(result.compatible);
  EXPECT_EQ(result.diagnostics.size(), 7U);
  EXPECT_EQ(CountCompatibilityCode(result.diagnostics,
              policy::PatchCompatibilityCode::kMissingBaseSourceKey),
    1U);
  EXPECT_EQ(CountCompatibilityCode(result.diagnostics,
              policy::PatchCompatibilityCode::kUnexpectedBaseSourceKey),
    2U);
  EXPECT_EQ(CountCompatibilityCode(result.diagnostics,
              policy::PatchCompatibilityCode::kMissingBaseContentVersion),
    1U);
  EXPECT_EQ(CountCompatibilityCode(result.diagnostics,
              policy::PatchCompatibilityCode::kUnexpectedBaseContentVersion),
    1U);
  EXPECT_EQ(CountCompatibilityCode(result.diagnostics,
              policy::PatchCompatibilityCode::kMissingBaseCatalogDigest),
    1U);
  EXPECT_EQ(CountCompatibilityCode(result.diagnostics,
              policy::PatchCompatibilityCode::kUnexpectedBaseCatalogDigest),
    1U);

  for (const auto& diagnostic : result.diagnostics) {
    EXPECT_FALSE(diagnostic.message.empty());
  }
}

auto WriteSingleAssetIndex(const std::filesystem::path& cooked_root,
  const data::AssetKey& key, const std::string_view descriptor_relpath,
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

  for (size_t i = 0; i < sizeof(header.source_identity); ++i) {
    header.source_identity[i] = static_cast<uint8_t>(guid_seed + i);
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
  const data::AssetKey& key, const std::string_view virtual_path) -> void
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

class PatchResolutionRuntimeTest : public testing::Test {
protected:
  void SetUp() override
  {
    static auto counter = std::atomic_uint64_t { 0U };
    const auto id = ++counter;
    root_ = std::filesystem::temp_directory_path() / "oxygen_patch_resolution"
      / std::to_string(id);
    std::filesystem::create_directories(root_);
  }

  void TearDown() override { std::filesystem::remove_all(root_); }

  [[nodiscard]] auto RootPath() const -> const std::filesystem::path&
  {
    return root_;
  }

private:
  std::filesystem::path root_ {};
};

NOLINT_TEST_F(PatchResolutionRuntimeTest, LastMountedWinsForVirtualPathLookup)
{
  constexpr auto kVirtualPath = "/.cooked/Asset.bin";
  const auto root0 = RootPath() / "root0";
  const auto root1 = RootPath() / "root1";
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

NOLINT_TEST_F(
  PatchResolutionRuntimeTest, CompatibilityMismatchRejectsPatchMounting)
{
  constexpr auto kVirtualPath = "/.cooked/Asset.bin";
  const auto base_root = RootPath() / "base";
  const auto patch_pak = RootPath() / "patch.pak";
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

NOLINT_TEST_F(
  PatchResolutionRuntimeTest, TombstoneBlocksFallbackDeterministically)
{
  constexpr auto kVirtualPath = "/.cooked/Masked.bin";
  const auto base_root = RootPath() / "base";
  const auto patch_pak = RootPath() / "patch.pak";
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
