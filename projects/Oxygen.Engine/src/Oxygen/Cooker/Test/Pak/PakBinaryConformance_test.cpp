//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <Oxygen/Cooker/Pak/PakBuilder.h>
#include <Oxygen/Data/PakFormat_core.h>

#include "PakTestSupport.h"

namespace {
namespace core = oxygen::data::pak::core;
namespace data = oxygen::data;
namespace pak = oxygen::content::pak;
namespace paktest = oxygen::content::pak::test;

constexpr auto kContentVersion = uint16_t { 17U };
constexpr auto kSourceKeySeed = uint8_t { 0x91U };
constexpr auto kIndexGuidSeed = uint8_t { 0x33U };
constexpr auto kAssetSeed = uint8_t { 0x44U };
constexpr auto kCrcInitialState = uint32_t { 0xFFFFFFFFU };
constexpr auto kCrcFinalXor = uint32_t { 0xFFFFFFFFU };
constexpr auto kCrcReflectedPolynomial = uint32_t { 0xEDB88320U };
constexpr auto kByteBitCount = uint32_t { 8U };
constexpr auto kCrcSkipFieldSize = size_t { sizeof(uint32_t) };

auto UpdateCrc32Ieee(
  const uint32_t state, const std::span<const std::byte> bytes) -> uint32_t
{
  auto crc = state;
  for (const auto byte : bytes) {
    crc ^= static_cast<uint32_t>(std::to_integer<uint8_t>(byte));
    for (auto bit = uint32_t { 0U }; bit < kByteBitCount; ++bit) {
      const auto lsb = (crc & 1U) != 0U;
      crc >>= 1U;
      if (lsb) {
        crc ^= kCrcReflectedPolynomial;
      }
    }
  }
  return crc;
}

auto ComputeFileCrc32SkippingRange(const std::span<const std::byte> bytes,
  const size_t skip_offset, const size_t skip_size) -> uint32_t
{
  auto state = kCrcInitialState;
  if (skip_offset > bytes.size()) {
    return 0U;
  }

  state = UpdateCrc32Ieee(state, bytes.first(skip_offset));

  auto resume_offset = skip_offset;
  if (skip_size <= (bytes.size() - skip_offset)) {
    resume_offset += skip_size;
  } else {
    resume_offset = bytes.size();
  }
  state = UpdateCrc32Ieee(
    state, bytes.subspan(resume_offset, bytes.size() - resume_offset));
  return state ^ kCrcFinalXor;
}

auto ReadAllBytes(const std::filesystem::path& path) -> std::vector<std::byte>
{
  auto stream = std::ifstream(path, std::ios::binary);
  if (!stream.good()) {
    return {};
  }

  const auto bytes = std::vector<char>(
    std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
  auto out = std::vector<std::byte> {};
  out.reserve(bytes.size());
  for (const auto value : bytes) {
    out.push_back(static_cast<std::byte>(value));
  }
  return out;
}

template <typename T>
auto TryReadStructAt(const std::span<const std::byte> bytes,
  const size_t offset) -> std::optional<T>
{
  if (offset > bytes.size()) {
    return std::nullopt;
  }
  if ((bytes.size() - offset) < sizeof(T)) {
    return std::nullopt;
  }

  auto value = T {};
  const auto view = bytes.subspan(offset, sizeof(T));
  std::memcpy(&value, view.data(), sizeof(T)); // NOLINT
  return value;
}

class PakBinaryConformanceTest : public paktest::TempDirFixture {
protected:
  auto MakeRequest(const std::filesystem::path& output_path, const bool crc)
    -> pak::PakBuildRequest
  {
    return pak::PakBuildRequest {
      .mode = pak::BuildMode::kFull,
      .sources = {},
      .output_pak_path = output_path,
      .output_manifest_path = {},
      .content_version = kContentVersion,
      .source_key = paktest::MakeSourceKey(kSourceKeySeed),
      .base_catalogs = {},
      .patch_compat = {},
      .options = pak::PakBuildOptions {
        .deterministic = true,
        .embed_browse_index = false,
        .emit_manifest_in_full = false,
        .compute_crc32 = crc,
        .fail_on_warnings = false,
      },
    };
  }
};

NOLINT_TEST_F(PakBinaryConformanceTest,
  EmptyBuildEmitsConsistentHeaderFooterAndDirectoryBounds)
{
  const auto request = MakeRequest(Path("empty_conformance.pak"), true);
  const auto result_or = pak::PakBuilder {}.Build(request);
  ASSERT_TRUE(result_or.has_value());
  const auto& result = result_or.value();
  ASSERT_FALSE(paktest::HasError(result.diagnostics));

  const auto bytes = ReadAllBytes(request.output_pak_path);
  ASSERT_EQ(bytes.size(), static_cast<size_t>(result.file_size));
  ASSERT_GE(bytes.size(), sizeof(core::PakHeader) + sizeof(core::PakFooter));

  const auto header = TryReadStructAt<core::PakHeader>(bytes, 0U);
  ASSERT_TRUE(header.has_value());
  const auto footer_offset = bytes.size() - sizeof(core::PakFooter);
  const auto footer = TryReadStructAt<core::PakFooter>(bytes, footer_offset);
  ASSERT_TRUE(footer.has_value());

  EXPECT_TRUE(std::equal(std::begin(header->magic), std::end(header->magic),
    core::kPakHeaderMagic.begin()));
  EXPECT_TRUE(std::equal(std::begin(footer->footer_magic),
    std::end(footer->footer_magic), core::kPakFooterMagic.begin()));
  EXPECT_EQ(header->content_version, request.content_version);

  const auto directory_end = footer->directory_offset + footer->directory_size;
  EXPECT_LE(directory_end, bytes.size());
  EXPECT_EQ(footer->asset_count, result.summary.assets_processed);
  EXPECT_EQ(footer->browse_index_offset, 0U);
  EXPECT_EQ(footer->browse_index_size, 0U);
}

NOLINT_TEST_F(PakBinaryConformanceTest, CrcSkipFieldMatchesFooterValue)
{
  const auto request = MakeRequest(Path("crc_skip.pak"), true);
  const auto result_or = pak::PakBuilder {}.Build(request);
  ASSERT_TRUE(result_or.has_value());
  const auto& result = result_or.value();
  ASSERT_FALSE(paktest::HasError(result.diagnostics));

  const auto bytes = ReadAllBytes(request.output_pak_path);
  ASSERT_GE(bytes.size(), sizeof(core::PakFooter));
  const auto footer_offset = bytes.size() - sizeof(core::PakFooter);
  const auto footer = TryReadStructAt<core::PakFooter>(bytes, footer_offset);
  ASSERT_TRUE(footer.has_value());

  const auto crc_offset = footer_offset + offsetof(core::PakFooter, pak_crc32);
  const auto computed_crc
    = ComputeFileCrc32SkippingRange(bytes, crc_offset, kCrcSkipFieldSize);
  EXPECT_EQ(computed_crc, footer->pak_crc32);
  EXPECT_EQ(result.pak_crc32, footer->pak_crc32);
}

NOLINT_TEST_F(
  PakBinaryConformanceTest, DirectoryEntryDescriptorOffsetsMatchSerializedBytes)
{
  constexpr auto kDescriptorByteCount = size_t { 19U };

  const auto source_root = Path("single_asset_source");
  const auto descriptor_path = source_root / "asset.desc";
  const auto descriptor = std::vector<std::byte>(
    kDescriptorByteCount, std::byte { static_cast<uint8_t>(0xABU) });
  ASSERT_TRUE(paktest::WriteFileBytes(descriptor_path, descriptor));

  const auto asset = paktest::AssetSpec {
    .key = paktest::MakeAssetKey(kAssetSeed),
    .asset_type = data::AssetType::kMaterial,
    .descriptor_relpath = "asset.desc",
    .virtual_path = "/Game/Materials/SingleAsset.mat",
    .descriptor_size = static_cast<uint64_t>(descriptor.size()),
    .descriptor_sha = {},
  };
  const auto assets = std::array { asset };
  ASSERT_TRUE(paktest::WriteLooseIndex(source_root,
    std::span<const paktest::AssetSpec>(assets.data(), assets.size()),
    std::span<const paktest::FileSpec> {}, kIndexGuidSeed));

  auto request = MakeRequest(Path("single_asset_conformance.pak"), true);
  request.sources = {
    data::CookedSource {
      .kind = data::CookedSourceKind::kLooseCooked,
      .path = source_root,
    },
  };

  const auto result_or = pak::PakBuilder {}.Build(request);
  ASSERT_TRUE(result_or.has_value());
  const auto& result = result_or.value();
  ASSERT_FALSE(paktest::HasError(result.diagnostics));
  ASSERT_EQ(result.summary.assets_processed, 1U);

  const auto bytes = ReadAllBytes(request.output_pak_path);
  ASSERT_GE(bytes.size(), sizeof(core::PakFooter));
  const auto footer_offset = bytes.size() - sizeof(core::PakFooter);
  const auto footer = TryReadStructAt<core::PakFooter>(bytes, footer_offset);
  ASSERT_TRUE(footer.has_value());
  ASSERT_EQ(footer->asset_count, 1U);
  ASSERT_EQ(footer->directory_size, sizeof(core::AssetDirectoryEntry));

  const auto directory_entry = TryReadStructAt<core::AssetDirectoryEntry>(
    bytes, static_cast<size_t>(footer->directory_offset));
  ASSERT_TRUE(directory_entry.has_value());
  EXPECT_EQ(directory_entry->asset_key, asset.key);
  EXPECT_EQ(directory_entry->desc_size, descriptor.size());
  EXPECT_LE(
    directory_entry->desc_offset + directory_entry->desc_size, bytes.size());

  const auto payload_offset = static_cast<size_t>(directory_entry->desc_offset);
  const auto payload_size = static_cast<size_t>(directory_entry->desc_size);
  const auto descriptor_bytes
    = std::span<const std::byte>(bytes).subspan(payload_offset, payload_size);
  EXPECT_TRUE(std::ranges::equal(descriptor_bytes, descriptor));
}

NOLINT_TEST_F(PakBinaryConformanceTest, CrcDisabledBuildLeavesFooterCrcZero)
{
  const auto request = MakeRequest(Path("crc_disabled.pak"), false);
  const auto result_or = pak::PakBuilder {}.Build(request);
  ASSERT_TRUE(result_or.has_value());
  const auto& result = result_or.value();
  ASSERT_FALSE(paktest::HasError(result.diagnostics));

  const auto bytes = ReadAllBytes(request.output_pak_path);
  ASSERT_GE(bytes.size(), sizeof(core::PakFooter));
  const auto footer_offset = bytes.size() - sizeof(core::PakFooter);
  const auto footer = TryReadStructAt<core::PakFooter>(bytes, footer_offset);
  ASSERT_TRUE(footer.has_value());

  EXPECT_EQ(footer->pak_crc32, 0U);
  EXPECT_EQ(result.pak_crc32, 0U);
  EXPECT_FALSE(result.summary.crc_computed);
}

} // namespace
