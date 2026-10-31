//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Pak/PakBuilder.cpp

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

#include <Oxygen/Base/Crc32.h>
#include <Oxygen/Cooker/Pak/PakBuildRequest.h>
#include <Oxygen/Cooker/Pak/PakBuildResult.h>
#include <Oxygen/Cooker/Pak/PakBuilder.h>
#include <Oxygen/Cooker/test/Support/Diagnostics.h>
#include <Oxygen/Cooker/testing/LooseCookedRoot.h>
#include <Oxygen/Cooker/testing/PackedPak.h>
#include <Oxygen/Cooker/testing/PakWorkspace.h>
#include <Oxygen/Cooker/testing/Seeds.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Serio/Testing/TestBytes.h>
#include <Oxygen/Testing/GTest.h>

namespace {
namespace core = oxygen::data::pak::core;
namespace data = oxygen::data;
namespace pak = oxygen::content::pak;

using oxygen::cooker::testing::LooseCookedRoot;
using oxygen::cooker::testing::PackedPak;
using oxygen::cooker::testing::PakWorkspace;
using oxygen::cooker::testing::SeedBytes;
using oxygen::serio::testing::BytesEqual;

constexpr auto kContentVersion = uint16_t { 17U };

//! The bytes of the pak's one descriptor.
auto MaterialDescriptor() -> std::vector<std::byte>
{
  constexpr auto kByte = uint8_t { 0xABU };
  constexpr auto kSize = size_t { 19U };
  return SeedBytes(kByte, kSize);
}

//! The offset of the footer of a pak of `bytes`.
auto FooterOffset(const std::span<const std::byte> bytes) -> size_t
{
  return bytes.size() - sizeof(core::PakFooter);
}

//! The CRC32 of `bytes`, a whole pak, without the footer's CRC field.
auto CrcWithoutFooterField(const std::span<const std::byte> bytes) -> uint32_t
{
  const auto field = FooterOffset(bytes) + offsetof(core::PakFooter, pak_crc32);
  auto crc = oxygen::base::Crc32 {};
  crc.Update(bytes.first(field));
  crc.Update(bytes.subspan(field + sizeof(uint32_t)));
  return crc.Finalize();
}

//! Builds the smallest pak there is: one material. A pak that lists nothing
//! names no mount, so a build always has at least one asset.
class PakBinaryConformanceTest : public ::testing::Test {
protected:
  //! Builds the pak, its footer CRC computed or not; throws, with every
  //! diagnostic, when the build fails.
  auto Build(const bool crc) -> pak::PakBuildResult
  {
    auto game = LooseCookedRoot {};
    material_ = game.Add({
      .type = data::AssetType::kMaterial,
      .leaf = "Materials/Single.omat",
      .descriptor = MaterialDescriptor(),
    });
    request_ = workspace_.FullRequest({ workspace_.Add(game) });
    request_.content_version = kContentVersion;
    request_.options.compute_crc32 = crc;
    auto result = pak::PakBuilder {}.Build(request_).value();
    if (oxygen::cooker::test::HasErrorDiagnostic(result.diagnostics)) {
      throw std::runtime_error(
        oxygen::cooker::test::DiagnosticSummary(result.diagnostics));
    }
    return result;
  }

  [[nodiscard]] auto Packed() const -> PackedPak
  {
    return workspace_.Open(request_.output_pak_path);
  }

  [[nodiscard]] auto Material() const -> const data::AssetKey&
  {
    return material_;
  }

private:
  PakWorkspace workspace_;
  pak::PakBuildRequest request_;
  data::AssetKey material_ {};
};

//! The header and footer carry their magics, the request's content version and
//! bounds that lie inside the pak; the planner always writes a browse index.
NOLINT_TEST_F(
  PakBinaryConformanceTest, MinimalBuildEmitsConsistentHeaderFooterAndBounds)
{
  const auto result = Build(true);

  const auto pak = Packed();
  const auto bytes = pak.Bytes();
  ASSERT_EQ(bytes.size(), result.file_size);
  ASSERT_GE(bytes.size(), sizeof(core::PakHeader) + sizeof(core::PakFooter));
  const auto header = PackedPak::RecordAt<core::PakHeader>(bytes, 0U);
  const auto footer
    = PackedPak::RecordAt<core::PakFooter>(bytes, FooterOffset(bytes));
  EXPECT_TRUE(std::ranges::equal(header.magic, core::kPakHeaderMagic));
  EXPECT_TRUE(std::ranges::equal(footer.footer_magic, core::kPakFooterMagic));
  EXPECT_EQ(header.content_version, kContentVersion);
  EXPECT_EQ(footer.asset_count, result.summary.assets_processed);
  const auto directory_end = footer.directory_offset + footer.directory_size;
  EXPECT_LE(directory_end, footer.browse_index_offset);
  EXPECT_GT(footer.browse_index_size, 0U);
  EXPECT_LE(
    footer.browse_index_offset + footer.browse_index_size, FooterOffset(bytes));
}

NOLINT_TEST_F(PakBinaryConformanceTest, CrcSkipFieldMatchesFooterValue)
{
  const auto result = Build(true);

  const auto pak = Packed();
  const auto footer = PackedPak::RecordAt<core::PakFooter>(
    pak.Bytes(), FooterOffset(pak.Bytes()));
  EXPECT_EQ(footer.pak_crc32, CrcWithoutFooterField(pak.Bytes()));
  EXPECT_EQ(result.pak_crc32, footer.pak_crc32);
}

NOLINT_TEST_F(
  PakBinaryConformanceTest, DirectoryEntryDescriptorOffsetsMatchSerializedBytes)
{
  const auto result = Build(true);

  ASSERT_EQ(result.summary.assets_processed, 1U);
  const auto pak = Packed();
  const auto bytes = pak.Bytes();
  const auto footer
    = PackedPak::RecordAt<core::PakFooter>(bytes, FooterOffset(bytes));
  ASSERT_EQ(footer.asset_count, 1U);
  ASSERT_EQ(footer.directory_size, sizeof(core::AssetDirectoryEntry));
  const auto entry = PackedPak::RecordAt<core::AssetDirectoryEntry>(
    bytes, static_cast<size_t>(footer.directory_offset));
  EXPECT_EQ(entry.asset_key, Material());
  ASSERT_EQ(entry.desc_size, MaterialDescriptor().size());
  ASSERT_LE(entry.desc_offset + entry.desc_size, bytes.size());
  EXPECT_THAT(bytes.subspan(static_cast<size_t>(entry.desc_offset),
                static_cast<size_t>(entry.desc_size)),
    BytesEqual(MaterialDescriptor()));
}

NOLINT_TEST_F(PakBinaryConformanceTest, CrcDisabledBuildLeavesFooterCrcZero)
{
  const auto result = Build(false);

  const auto pak = Packed();
  const auto footer = PackedPak::RecordAt<core::PakFooter>(
    pak.Bytes(), FooterOffset(pak.Bytes()));
  EXPECT_EQ(footer.pak_crc32, 0U);
  EXPECT_EQ(result.pak_crc32, 0U);
  EXPECT_FALSE(result.summary.crc_computed);
}

} // namespace
