//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Testing/GTest.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <Oxygen/Cooker/Pak/PakBuildReport.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/LooseCookedIndexFormat.h>
#include <Oxygen/Data/SourceKey.h>

namespace oxygen::content::pak::test {

namespace data = oxygen::data;
namespace lc = oxygen::data::loose_cooked;

struct AssetSpec final {
  data::AssetKey key {};
  data::AssetType asset_type = data::AssetType::kUnknown;
  std::string descriptor_relpath;
  std::string virtual_path;
  uint64_t descriptor_size = 0U;
  std::array<uint8_t, lc::kSha256Size> descriptor_sha {};
};

struct FileSpec final {
  lc::FileKind kind = lc::FileKind::kUnknown;
  std::string relpath;
  std::vector<std::byte> payload;
};

class TempDirFixture : public testing::Test {
protected:
  void SetUp() override
  {
    static auto counter = std::atomic_uint64_t { 0U };
    const auto id = ++counter;
    root_ = std::filesystem::temp_directory_path() / "oxygen_pak_tests"
      / std::to_string(id);
    std::filesystem::create_directories(root_);
  }

  void TearDown() override { std::filesystem::remove_all(root_); }

  [[nodiscard]] auto Root() const -> const std::filesystem::path&
  {
    return root_;
  }

  [[nodiscard]] auto Path(std::string_view leaf) const -> std::filesystem::path
  {
    return root_ / std::filesystem::path(leaf);
  }

private:
  std::filesystem::path root_ {};
};

[[nodiscard]] inline auto MakeAssetKey(const uint8_t seed) -> data::AssetKey
{
  auto bytes = std::array<uint8_t, data::AssetKey::kSizeBytes> {};
  bytes.fill(seed);
  return data::AssetKey::FromBytes(bytes);
}

[[nodiscard]] inline auto MakeSourceKey(const uint8_t seed) -> data::SourceKey
{
  auto bytes = std::array<uint8_t, data::SourceKey::kSizeBytes> {};
  bytes.fill(0U);
  bytes[0] = seed;
  return data::SourceKey::FromBytes(bytes);
}

[[nodiscard]] inline auto MakeDigest(const uint8_t seed)
  -> std::array<uint8_t, lc::kSha256Size>
{
  auto digest = std::array<uint8_t, lc::kSha256Size> {};
  digest.fill(seed);
  return digest;
}

[[nodiscard]] inline auto HasError(std::span<const PakDiagnostic> diagnostics)
  -> bool
{
  return std::ranges::any_of(diagnostics, [](const PakDiagnostic& diagnostic) {
    return diagnostic.severity == PakDiagnosticSeverity::kError;
  });
}

[[nodiscard]] inline auto HasDiagnosticCode(
  std::span<const PakDiagnostic> diagnostics, const std::string_view code)
  -> bool
{
  return std::ranges::any_of(
    diagnostics, [code](const PakDiagnostic& d) { return d.code == code; });
}

[[nodiscard]] inline auto WriteFileBytes(
  const std::filesystem::path& path, std::span<const std::byte> bytes) -> bool
{
  std::filesystem::create_directories(path.parent_path());
  auto stream = std::ofstream(path, std::ios::binary | std::ios::trunc);
  if (!stream.good()) {
    return false;
  }
  if (!bytes.empty()) {
    // NOLINTNEXTLINE(*-reinterpret-cast)
    stream.write(reinterpret_cast<const char*>(bytes.data()),
      static_cast<std::streamsize>(bytes.size()));
  }
  stream.flush();
  return stream.good();
}

[[nodiscard]] inline auto WriteLooseIndex(const std::filesystem::path& root,
  std::span<const AssetSpec> assets, std::span<const FileSpec> files,
  const uint8_t guid_seed) -> bool
{
  std::filesystem::create_directories(root);
  for (const auto& file : files) {
    if (!WriteFileBytes(root / file.relpath,
          std::span<const std::byte>(
            file.payload.data(), file.payload.size()))) {
      return false;
    }
  }

  auto strings = std::string {};
  strings.push_back('\0');

  auto asset_entries = std::vector<lc::AssetEntry> {};
  asset_entries.reserve(assets.size());
  for (const auto& asset : assets) {
    const auto descriptor_offset = static_cast<uint32_t>(strings.size());
    strings += asset.descriptor_relpath;
    strings.push_back('\0');

    const auto virtual_path_offset = static_cast<uint32_t>(strings.size());
    strings += asset.virtual_path;
    strings.push_back('\0');

    auto entry = lc::AssetEntry {};
    entry.asset_key = asset.key;
    entry.descriptor_relpath_offset = descriptor_offset;
    entry.virtual_path_offset = virtual_path_offset;
    entry.asset_type = static_cast<uint8_t>(asset.asset_type);
    entry.descriptor_size = asset.descriptor_size;
    std::ranges::copy(
      asset.descriptor_sha, std::begin(entry.descriptor_sha256));
    asset_entries.push_back(entry);
  }

  auto file_entries = std::vector<lc::FileRecord> {};
  file_entries.reserve(files.size());
  for (const auto& file : files) {
    const auto relpath_offset = static_cast<uint32_t>(strings.size());
    strings += file.relpath;
    strings.push_back('\0');

    auto entry = lc::FileRecord {};
    entry.kind = file.kind;
    entry.relpath_offset = relpath_offset;
    entry.size = static_cast<uint64_t>(file.payload.size());
    file_entries.push_back(entry);
  }

  auto header = lc::IndexHeader {};
  header.version = 1U;
  header.flags = static_cast<uint32_t>(lc::kHasVirtualPaths);
  if (!file_entries.empty()) {
    header.flags |= static_cast<uint32_t>(lc::kHasFileRecords);
  }
  for (size_t i = 0; i < std::size(header.guid); ++i) {
    header.guid[i]
      = static_cast<uint8_t>(guid_seed + static_cast<uint8_t>(i + 1U));
  }
  header.string_table_offset = sizeof(lc::IndexHeader);
  header.string_table_size = static_cast<uint64_t>(strings.size());
  header.asset_entries_offset
    = header.string_table_offset + header.string_table_size;
  header.asset_count = static_cast<uint32_t>(asset_entries.size());
  header.asset_entry_size = sizeof(lc::AssetEntry);
  header.file_records_offset = header.asset_entries_offset
    + (static_cast<uint64_t>(asset_entries.size()) * sizeof(lc::AssetEntry));
  header.file_record_count = static_cast<uint32_t>(file_entries.size());
  header.file_record_size = file_entries.empty() ? 0U : sizeof(lc::FileRecord);

  auto index = std::ofstream(
    root / "container.index.bin", std::ios::binary | std::ios::trunc);
  if (!index.good()) {
    return false;
  }
  // NOLINTNEXTLINE(*-reinterpret-cast)
  index.write(reinterpret_cast<const char*>(&header), sizeof(header));
  index.write(strings.data(), static_cast<std::streamsize>(strings.size()));
  for (const auto& asset : asset_entries) {
    // NOLINTNEXTLINE(*-reinterpret-cast)
    index.write(reinterpret_cast<const char*>(&asset), sizeof(asset));
  }
  for (const auto& file : file_entries) {
    // NOLINTNEXTLINE(*-reinterpret-cast)
    index.write(reinterpret_cast<const char*>(&file), sizeof(file));
  }
  return index.good();
}

} // namespace oxygen::content::pak::test
