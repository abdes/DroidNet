//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Base/Span.h>
#include <Oxygen/Content/Internal/PatchResolutionPolicy.h>
#include <Oxygen/Content/VirtualPathResolver.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/LooseCookedIndexFormat.h>
#include <Oxygen/Data/PakCatalog.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/PatchManifest.h>
#include <Oxygen/Data/SourceKey.h>
#include <Oxygen/Data/SourceOrigin.h>
#include <Oxygen/Testing/GTest.h>

namespace {
namespace data = oxygen::data;

constexpr auto kBaseGuidSeed = uint8_t { 0x01U };
constexpr auto kAltGuidSeed = uint8_t { 0x31U };
constexpr auto kAssetSeedA = uint8_t { 0x11U };
constexpr auto kAssetSeedB = uint8_t { 0x22U };
constexpr auto kAssetSeedC = uint8_t { 0x33U };

auto MakeAssetKey(const uint8_t seed) -> data::AssetKey
{
  auto bytes = std::array<std::uint8_t, data::AssetKey::kSizeBytes> {};
  bytes.at(0) = seed;
  return data::AssetKey::FromBytes(bytes);
}

struct SourceResolutionState final {
  std::unordered_set<data::AssetKey> assets;
  std::unordered_set<data::AssetKey> tombstones;
  std::unordered_map<std::string, data::AssetKey> virtual_path_to_asset;
};

auto FindState(
  const std::unordered_map<data::SourceInstanceId, SourceResolutionState>&
    states,
  const data::SourceInstanceId source_id) -> const SourceResolutionState*
{
  if (const auto it = states.find(source_id); it != states.end()) {
    return &it->second;
  }
  return nullptr;
}

auto MakeResolutionCallbacks(
  const std::unordered_map<data::SourceInstanceId, SourceResolutionState>&
    states) -> oxygen::content::internal::VirtualPathResolutionCallbacks
{
  namespace policy = oxygen::content::internal;

  return policy::VirtualPathResolutionCallbacks {
    .key_resolution = policy::KeyResolutionCallbacks {
      .source_has_asset = [&states](const data::SourceInstanceId source_id,
                            const data::AssetKey& key) -> bool {
        if (const auto* state = FindState(states, source_id);
          state != nullptr) {
          return state->assets.contains(key);
        }
        return false;
      },
      .source_tombstones_asset
      = [&states](const data::SourceInstanceId source_id,
          const data::AssetKey& key) -> bool {
        if (const auto* state = FindState(states, source_id);
          state != nullptr) {
          return state->tombstones.contains(key);
        }
        return false;
      },
    },
    .resolve_virtual_path
    = [&states](const data::SourceInstanceId source_id,
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

NOLINT_TEST(PatchResolutionPolicyTest,
  MultiMountVirtualPathCollisionsEmitMaskedSourceDiagnostics)
{
  namespace policy = oxygen::content::internal;

  constexpr auto kVirtualPath = "/.cooked/collision.bin";
  constexpr auto kSourceA = data::SourceInstanceId { 10U };
  constexpr auto kSourceB = data::SourceInstanceId { 20U };
  constexpr auto kSourceC = data::SourceInstanceId { 30U };
  constexpr auto kSourceD = data::SourceInstanceId { 40U };

  const auto key_a = MakeAssetKey(kAssetSeedA);
  const auto key_b = MakeAssetKey(kAssetSeedB);
  const auto key_c = MakeAssetKey(kAssetSeedC);

  const auto source_ids = std::array<data::SourceInstanceId, 4> {
    kSourceA,
    kSourceB,
    kSourceC,
    kSourceD,
  };

  auto states
    = std::unordered_map<data::SourceInstanceId, SourceResolutionState> {};
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
  EXPECT_EQ(result.asset_key, std::optional { key_a });
  EXPECT_EQ(result.key_result.status, policy::KeyResolutionStatus::kFound);
  ASSERT_TRUE(result.key_result.source_id.has_value());
  EXPECT_EQ(result.key_result.source_id, std::optional { kSourceD });

  ASSERT_EQ(result.collisions.size(), 2U);
  EXPECT_EQ(result.collisions.at(0).winner_source_id, kSourceD);
  EXPECT_EQ(result.collisions.at(0).masked_source_id, kSourceC);
  EXPECT_EQ(result.collisions.at(0).winner_key, key_a);
  EXPECT_EQ(result.collisions.at(0).masked_key, key_b);

  EXPECT_EQ(result.collisions.at(1).winner_source_id, kSourceD);
  EXPECT_EQ(result.collisions.at(1).masked_source_id, kSourceB);
  EXPECT_EQ(result.collisions.at(1).winner_key, key_a);
  EXPECT_EQ(result.collisions.at(1).masked_key, key_c);
}

NOLINT_TEST(PatchResolutionPolicyTest,
  TombstoneInHigherPrecedenceMountMasksWinnerAndPreservesCollisionDiagnostics)
{
  namespace policy = oxygen::content::internal;

  constexpr auto kVirtualPath = "/.cooked/masked.bin";
  constexpr auto kSourceBase = data::SourceInstanceId { 11U };
  constexpr auto kSourceMid = data::SourceInstanceId { 21U };
  constexpr auto kSourceWinner = data::SourceInstanceId { 31U };
  constexpr auto kSourceTombstone = data::SourceInstanceId { 41U };

  const auto winner_key = MakeAssetKey(kAssetSeedA);
  const auto masked_key = MakeAssetKey(kAssetSeedB);

  const auto source_ids = std::array<data::SourceInstanceId, 4> {
    kSourceBase,
    kSourceMid,
    kSourceWinner,
    kSourceTombstone,
  };

  auto states
    = std::unordered_map<data::SourceInstanceId, SourceResolutionState> {};
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
  EXPECT_EQ(result.key_result.source_id, std::optional { kSourceTombstone });

  ASSERT_EQ(result.collisions.size(), 1U);
  EXPECT_EQ(result.collisions.at(0).winner_source_id, kSourceWinner);
  EXPECT_EQ(result.collisions.at(0).masked_source_id, kSourceMid);
  EXPECT_EQ(result.collisions.at(0).winner_key, winner_key);
  EXPECT_EQ(result.collisions.at(0).masked_key, masked_key);
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
  header.version = oxygen::data::loose_cooked::kIndexVersion;
  header.content_version = 0;
  header.flags = oxygen::data::loose_cooked::kHasVirtualPaths
    | oxygen::data::loose_cooked::kHasFileRecords;

  for (size_t i = 0; i < sizeof(header.source_identity); ++i) {
    oxygen::base::CheckedAt(std::span { header.source_identity }, i)
      = static_cast<uint8_t>(guid_seed + i);
  }
  oxygen::base::CheckedAt(std::span { header.source_identity }, 6)
    = static_cast<uint8_t>(
      (oxygen::base::CheckedAt(std::span { header.source_identity }, 6) & 0x0FU)
      | 0x70U);
  oxygen::base::CheckedAt(std::span { header.source_identity }, 8)
    = static_cast<uint8_t>(
      (oxygen::base::CheckedAt(std::span { header.source_identity }, 8) & 0x3FU)
      | 0x80U);

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
  EXPECT_EQ(resolved, std::optional { key1 });
}

} // namespace
