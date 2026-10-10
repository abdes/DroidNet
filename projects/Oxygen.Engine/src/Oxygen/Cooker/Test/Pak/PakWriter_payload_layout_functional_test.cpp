//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Pak/PakWriter.cpp

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

#include "PakTestSupport.h"
#include "PakWriterTestSupport.h"

#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Cooker/Pak/PakPlan.h>
#include <Oxygen/Cooker/Pak/PakWriter.h>
#include <Oxygen/Cooker/Test/Support/FileIo.h>
#include <Oxygen/Cooker/Test/Support/TestValues.h>
#include <Oxygen/Data/PakCatalog.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Testing/GTest.h>

namespace {
namespace core = oxygen::data::pak::core;
namespace data = oxygen::data;
namespace pak = oxygen::content::pak;
namespace paktest = oxygen::content::pak::test;
namespace cooktest = oxygen::cooker::test;

using paktest::HasError;
using paktest::kFooterSize;
using paktest::kHeaderSize;
using paktest::MakeAssetKey;
using paktest::MakePatternBytes;

constexpr auto kTextureDataSize = uint64_t { 32U };

auto AddCatalog(pak::PakPlan::Data& plan) -> void
{
  data::PakCatalog catalog {
    .source_key = plan.header.source_key,
    .content_version = plan.header.content_version,
    .catalog_digest = {},
    .entries = {},
    .deleted = {},
    .bases = {},
  };
  for (size_t index = 0; index < plan.assets.size(); ++index) {
    const auto& asset = plan.assets.at(index);
    catalog.entries.push_back({
      .asset_key = asset.asset_key,
      .asset_type = asset.asset_type,
      .descriptor_digest = oxygen::base::ComputeFileSha256(
        plan.asset_payload_sources.at(index).source_path),
      .transitive_resource_digest = {},
    });
  }
  catalog.catalog_digest = catalog.ComputeDigest().value();
  plan.catalog = {
    .offset = plan.directory.offset + plan.directory.size_bytes,
    .bytes = catalog.Encode().value(),
  };
  ASSERT_LE(
    plan.catalog.offset + plan.catalog.bytes.size(), plan.footer.offset);
}

auto IsAllZero(std::span<const std::byte> bytes) -> bool
{
  return std::ranges::all_of(bytes,
    [](const std::byte byte) -> bool { return byte == std::byte { 0 }; });
}

//! The writer copies payload bytes verbatim and zero-fills everything the plan
//! leaves between them.
class PakWriterPayloadLayoutTest : public paktest::PakWriterFixture { };

NOLINT_TEST_F(PakWriterPayloadLayoutTest,
  StoresResourceAndDescriptorPayloadBytesFromSources)
{
  constexpr auto kTextureRegionOffset = uint64_t { 256U };
  constexpr auto kTexturePayloadSize = uint64_t { 8U };
  constexpr auto kTextureTableOffset = uint64_t { 512U };
  constexpr auto kAssetDescriptorOffset = uint64_t { 768U };
  constexpr auto kAssetDescriptorSize = uint64_t { 12U };
  constexpr auto kDirectoryOffset = uint64_t { 896U };
  constexpr auto kFooterOffset = uint64_t { 1152U };
  constexpr auto kPlannedFileSize = kFooterOffset + kFooterSize;
  constexpr auto kTexturePayloadStart = uint8_t { 1U };
  constexpr auto kDescriptorPayloadStart = uint8_t { 161U };

  const auto texture_source_path = Root() / "texture_payload.bin";
  const auto texture_descriptor_path = Root() / "texture_descriptor.bin";
  const auto descriptor_source_path = Root() / "descriptor_payload.bin";
  const auto texture_payload = MakePatternBytes(
    kTexturePayloadStart, static_cast<size_t>(kTexturePayloadSize));
  const auto texture_descriptor = std::vector<std::byte>(
    sizeof(core::TextureResourceDesc), std::byte { 0 });
  const auto descriptor_payload = MakePatternBytes(
    kDescriptorPayloadStart, static_cast<size_t>(kAssetDescriptorSize));
  cooktest::WriteBytes(texture_source_path, texture_payload);
  cooktest::WriteBytes(texture_descriptor_path, texture_descriptor);
  cooktest::WriteBytes(descriptor_source_path, descriptor_payload);

  const auto asset_key = MakeAssetKey(static_cast<uint8_t>(0x3D));
  auto plan_data = CanonicalPlan({
    .content_version = 8U,
    .base_offset = kTextureRegionOffset,
    .texture_region_size = kTexturePayloadSize,
    .tables_offset = kTextureTableOffset,
    .directory_offset = kDirectoryOffset,
    .footer_offset = kFooterOffset,
    .texture_count = 1U,
    .texture_table_size = sizeof(core::TextureResourceDesc),
  });
  plan_data.resources = {
    paktest::MakeTexturePlacement(kTextureRegionOffset, kTexturePayloadSize),
  };
  plan_data.resource_payload_sources = {
    pak::PakPayloadSourceSlicePlan {
      .source_path = texture_source_path,
      .source_offset = 0U,
      .size_bytes = kTexturePayloadSize,
    },
  };
  plan_data.resource_descriptor_sources = {
    pak::PakPayloadSourceSlicePlan {
      .source_path = texture_descriptor_path,
      .source_offset = 0U,
      .size_bytes = sizeof(core::TextureResourceDesc),
    },
  };
  paktest::AddGeometryAsset(
    plan_data, asset_key, kAssetDescriptorOffset, kAssetDescriptorSize);
  plan_data.asset_payload_sources = {
    pak::PakPayloadSourceSlicePlan {
      .source_path = descriptor_source_path,
      .source_offset = 0U,
      .size_bytes = kAssetDescriptorSize,
    },
  };

  const auto request = paktest::MakeFullRequest(Root() / "payload_copy.pak",
    { .content_version = 8U, .compute_crc32 = false });

  ASSERT_NO_FATAL_FAILURE(AddCatalog(plan_data));

  const auto write_result
    = pak::PakWriter {}.Write(request, pak::PakPlan(std::move(plan_data)));
  ASSERT_FALSE(HasError(write_result.diagnostics));

  const auto bytes = cooktest::ReadBytes(request.output_pak_path);
  ASSERT_EQ(bytes.size(), static_cast<size_t>(kPlannedFileSize));

  const auto texture_view
    = std::span<const std::byte>(bytes.data() + kTextureRegionOffset,
      static_cast<size_t>(kTexturePayloadSize));
  const auto descriptor_view
    = std::span<const std::byte>(bytes.data() + kAssetDescriptorOffset,
      static_cast<size_t>(kAssetDescriptorSize));
  EXPECT_TRUE(std::ranges::equal(texture_view, texture_payload));
  EXPECT_TRUE(std::ranges::equal(descriptor_view, descriptor_payload));
}

NOLINT_TEST_F(
  PakWriterPayloadLayoutTest, ZeroFillsPlannedPaddingAndTrailingGaps)
{
  constexpr auto kTextureRegionOffset = uint64_t { 512U };
  constexpr auto kTextureRegionEnd = kTextureRegionOffset + kTextureDataSize;
  constexpr auto kTableOffset = uint64_t { 1024U };
  constexpr auto kDirectoryOffset = uint64_t { 1152U };
  constexpr auto kFooterOffset = uint64_t { 1280U };
  constexpr auto kPlannedFileSize = uint64_t { 1792U };

  auto plan_data = CanonicalPlan({
    .content_version = 2U,
    .base_offset = kTextureRegionOffset,
    .texture_region_size = kTextureDataSize,
    .tables_offset = kTableOffset,
    .directory_offset = kDirectoryOffset,
    .footer_offset = kFooterOffset,
    .texture_count = 1U,
    .texture_table_size = sizeof(core::TextureResourceDesc),
  });
  // Trailing gap after the footer that must be zero-filled.
  plan_data.planned_file_size = kPlannedFileSize;
  plan_data.resources
    = { paktest::MakeTexturePlacement(kTextureRegionOffset, kTextureDataSize) };
  const auto resource_source_path = Root() / "writer_zero_fill_texture.bin";
  const auto resource_bytes = std::vector<std::byte>(
    static_cast<size_t>(kTextureDataSize), std::byte { 0 });
  cooktest::WriteBytes(resource_source_path, resource_bytes);
  plan_data.resource_payload_sources = {
    pak::PakPayloadSourceSlicePlan {
      .source_path = resource_source_path,
      .source_offset = 0U,
      .size_bytes = kTextureDataSize,
    },
  };
  const auto descriptor_source_path
    = Root() / "writer_zero_fill_texture_desc.bin";
  const auto descriptor_bytes = std::vector<std::byte>(
    sizeof(core::TextureResourceDesc), std::byte { 0 });
  cooktest::WriteBytes(descriptor_source_path, descriptor_bytes);
  plan_data.resource_descriptor_sources = {
    pak::PakPayloadSourceSlicePlan {
      .source_path = descriptor_source_path,
      .source_offset = 0U,
      .size_bytes = sizeof(core::TextureResourceDesc),
    },
  };

  const auto request = paktest::MakeFullRequest(
    Root() / "zero_fill.pak", { .content_version = 2U });

  ASSERT_NO_FATAL_FAILURE(AddCatalog(plan_data));

  const auto write_result
    = pak::PakWriter {}.Write(request, pak::PakPlan(std::move(plan_data)));
  ASSERT_FALSE(HasError(write_result.diagnostics));
  ASSERT_EQ(write_result.file_size, kPlannedFileSize);

  const auto bytes = cooktest::ReadBytes(request.output_pak_path);
  ASSERT_EQ(bytes.size(), static_cast<size_t>(kPlannedFileSize));

  EXPECT_TRUE(IsAllZero(std::span<const std::byte>(bytes.data() + kHeaderSize,
    static_cast<size_t>(kTextureRegionOffset - kHeaderSize))));
  EXPECT_TRUE(
    IsAllZero(std::span<const std::byte>(bytes.data() + kTextureRegionEnd,
      static_cast<size_t>(kTableOffset - kTextureRegionEnd))));
  EXPECT_TRUE(IsAllZero(
    std::span<const std::byte>(bytes.data() + (kFooterOffset + kFooterSize),
      static_cast<size_t>(kPlannedFileSize - (kFooterOffset + kFooterSize)))));
}

} // namespace
