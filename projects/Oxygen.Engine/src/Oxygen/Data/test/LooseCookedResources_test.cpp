//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <vector>

#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Data/CookedFormatError.h>
#include <Oxygen/Data/LooseCookedIndexFormat.h>
#include <Oxygen/Data/LooseCookedResources.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Data/PakFormat_physics.h>
#include <Oxygen/Data/PakFormat_scripting.h>
#include <Oxygen/Serio/MemoryFile.h>
#include <Oxygen/Testing/GTest.h>

namespace {

namespace lc = oxygen::data::loose_cooked;
namespace core = oxygen::data::pak::core;
using lc::FileKind;
using lc::ResourcePayloadFailure;
using oxygen::base::ComputeSha256;
using oxygen::data::CookedDefect;
using oxygen::data::CookedFormatError;
using oxygen::serio::MemoryFile;

//! A data file holding `payloads` back to back, and the buffer rows that
//! describe them, after a reserved empty row.
struct BufferRoot final {
  std::vector<core::BufferResourceDesc> rows { core::BufferResourceDesc {} };
  std::vector<std::byte> data;

  auto Add(const std::span<const std::byte> payload) -> void
  {
    rows.push_back({
      .data_offset = data.size(),
      .size_bytes = static_cast<core::DataBlobSizeT>(payload.size()),
      .usage_flags = 0U,
      .element_stride = 1U,
      .element_format = 0U,
      .content_hash = ComputeSha256(payload),
    });
    data.insert(data.end(), payload.begin(), payload.end());
  }

  [[nodiscard]] auto Rows() const -> std::span<const std::byte>
  {
    return std::as_bytes(std::span(rows));
  }

  [[nodiscard]] auto DataFile() const -> MemoryFile
  {
    return MemoryFile::FromBytes(data).value();
  }
};

constexpr auto kFirst = std::array {
  std::byte { 1 },
  std::byte { 2 },
  std::byte { 3 },
};
constexpr auto kSecond = std::array {
  std::byte { 4 },
  std::byte { 5 },
};

//! The rule reading the rows of `table` reports broken; they must be
//! refused.
auto ReadDefect(const FileKind table, const std::span<const std::byte> rows)
  -> CookedDefect
{
  try {
    static_cast<void>(lc::ReadResourcePayloads(table, rows));
  } catch (const CookedFormatError& error) {
    return error.Defect();
  }
  ADD_FAILURE() << "the rows were read";
  return {};
}

//! Only the four resource data files are data files.
NOLINT_TEST(LooseCookedResourcesTest, DataFilesAreTheResourceDataKinds)
{
  EXPECT_TRUE(lc::IsResourceDataFile(FileKind::kBuffersData));
  EXPECT_TRUE(lc::IsResourceDataFile(FileKind::kTexturesData));
  EXPECT_TRUE(lc::IsResourceDataFile(FileKind::kScriptsData));
  EXPECT_TRUE(lc::IsResourceDataFile(FileKind::kPhysicsData));
  EXPECT_FALSE(lc::IsResourceDataFile(FileKind::kBuffersTable));
  EXPECT_FALSE(lc::IsResourceDataFile(FileKind::kTextureDescriptor));
  EXPECT_FALSE(lc::IsResourceDataFile(FileKind::kAuxiliary));
}

//! Each table kind pairs with its data file and has its descriptor's size.
NOLINT_TEST(LooseCookedResourcesTest, TablesPairWithTheirDataFiles)
{
  EXPECT_EQ(
    lc::DataFileKindOf(FileKind::kBuffersTable), FileKind::kBuffersData);
  EXPECT_EQ(
    lc::DataFileKindOf(FileKind::kTexturesTable), FileKind::kTexturesData);
  EXPECT_EQ(
    lc::DataFileKindOf(FileKind::kScriptsTable), FileKind::kScriptsData);
  EXPECT_EQ(
    lc::DataFileKindOf(FileKind::kPhysicsTable), FileKind::kPhysicsData);
  EXPECT_EQ(lc::DataFileKindOf(FileKind::kAuxiliary), std::nullopt);
  EXPECT_EQ(lc::ResourceRowSize(FileKind::kBuffersTable),
    sizeof(core::BufferResourceDesc));
  EXPECT_EQ(lc::ResourceRowSize(FileKind::kTexturesTable),
    sizeof(core::TextureResourceDesc));
  EXPECT_EQ(lc::ResourceRowSize(FileKind::kScriptsTable),
    sizeof(oxygen::data::pak::scripting::ScriptResourceDesc));
  EXPECT_EQ(lc::ResourceRowSize(FileKind::kPhysicsTable),
    sizeof(oxygen::data::pak::physics::PhysicsResourceDesc));
  EXPECT_THROW(static_cast<void>(lc::ResourceRowSize(FileKind::kAuxiliary)),
    std::invalid_argument);
}

//! The payloads of a table are its rows that hold bytes, in row order, with
//! their offsets, sizes and digests; the reserved empty row is left out.
NOLINT_TEST(LooseCookedResourcesTest, PayloadsAreTheRowsThatHoldBytes)
{
  auto root = BufferRoot {};
  root.Add(kFirst);
  root.Add(kSecond);

  const auto payloads
    = lc::ReadResourcePayloads(FileKind::kBuffersTable, root.Rows());

  ASSERT_EQ(payloads.size(), 2U);
  EXPECT_EQ(payloads.at(0).row, 1U);
  EXPECT_EQ(payloads.at(0).offset, 0U);
  EXPECT_EQ(payloads.at(0).size, kFirst.size());
  EXPECT_EQ(payloads.at(0).sha256, ComputeSha256(kFirst));
  EXPECT_EQ(payloads.at(1).row, 2U);
  EXPECT_EQ(payloads.at(1).offset, kFirst.size());
  EXPECT_EQ(payloads.at(1).sha256, ComputeSha256(kSecond));
}

//! A table that ends in a partial row has the wrong row size; a kind that
//! is no resource table is a caller error.
NOLINT_TEST(LooseCookedResourcesTest, MalformedTablesAreRefused)
{
  auto root = BufferRoot {};
  root.Add(kFirst);
  const auto partial = root.Rows().first(root.Rows().size() - 1U);

  EXPECT_EQ(
    ReadDefect(FileKind::kBuffersTable, partial), CookedDefect::kWrongRowSize);
  EXPECT_THROW(static_cast<void>(
                 lc::ReadResourcePayloads(FileKind::kAuxiliary, root.Rows())),
    std::invalid_argument);
}

//! Payloads whose bytes hash to their rows' digests verify.
NOLINT_TEST(LooseCookedResourcesTest, IntactPayloadsVerify)
{
  auto root = BufferRoot {};
  root.Add(kFirst);
  root.Add(kSecond);
  const auto payloads
    = lc::ReadResourcePayloads(FileKind::kBuffersTable, root.Rows());

  EXPECT_TRUE(lc::VerifyResourcePayloads(payloads, root.DataFile()).empty());
}

//! A payload whose bytes changed is reported by its row; the others verify.
NOLINT_TEST(LooseCookedResourcesTest, ChangedPayloadIsReportedByItsRow)
{
  auto root = BufferRoot {};
  root.Add(kFirst);
  root.Add(kSecond);
  const auto payloads
    = lc::ReadResourcePayloads(FileKind::kBuffersTable, root.Rows());
  root.data.back() = std::byte { 0xFF };

  const auto issues = lc::VerifyResourcePayloads(payloads, root.DataFile());

  ASSERT_EQ(issues.size(), 1U);
  EXPECT_EQ(issues.front().row, 2U);
  EXPECT_EQ(issues.front().reason, ResourcePayloadFailure::kDigestMismatch);
}

//! A payload that lies past the end of its data file is out of bounds.
NOLINT_TEST(LooseCookedResourcesTest, PayloadPastTheDataFileIsOutOfBounds)
{
  auto root = BufferRoot {};
  root.Add(kFirst);
  root.Add(kSecond);
  const auto payloads
    = lc::ReadResourcePayloads(FileKind::kBuffersTable, root.Rows());
  root.data.resize(kFirst.size());

  const auto issues = lc::VerifyResourcePayloads(payloads, root.DataFile());

  ASSERT_EQ(issues.size(), 1U);
  EXPECT_EQ(issues.front().row, 2U);
  EXPECT_EQ(issues.front().reason, ResourcePayloadFailure::kOutOfBounds);
}

//! A row that holds bytes but no digest makes its table malformed: its
//! payload could never verify.
NOLINT_TEST(LooseCookedResourcesTest, RowWithoutDigestIsRefused)
{
  auto root = BufferRoot {};
  root.Add(kFirst);
  root.rows.back().content_hash = {};

  EXPECT_EQ(ReadDefect(FileKind::kBuffersTable, root.Rows()),
    CookedDefect::kMissingDigest);
}

//! A texture row records the compression its format implies; a BC7 format
//! recorded as uncompressed, or an unknown compression, is refused.
NOLINT_TEST(LooseCookedResourcesTest, TextureRowOfAnotherCompressionIsRefused)
{
  const auto row = [](const oxygen::Format format,
                     const uint8_t compression) -> core::TextureResourceDesc {
    return {
      .data_offset = 0U,
      .size_bytes = static_cast<core::DataBlobSizeT>(kFirst.size()),
      .texture_type = 0U,
      .compression_type = compression,
      .width = 4U,
      .height = 4U,
      .depth = 1U,
      .array_layers = 1U,
      .mip_levels = 1U,
      .format = static_cast<uint8_t>(format),
      .alignment = 256U,
      .content_hash = ComputeSha256(kFirst),
    };
  };
  const auto valid = std::array {
    row(oxygen::Format::kBC7UNorm, core::kTextureCompressionBc7),
    row(oxygen::Format::kRGBA8UNorm, core::kTextureCompressionNone),
  };
  const auto bc7_as_none = std::array {
    row(oxygen::Format::kBC7UNorm, core::kTextureCompressionNone),
  };
  const auto unknown = std::array {
    row(oxygen::Format::kRGBA8UNorm, uint8_t { 0xEEU }),
  };

  const auto payloads = lc::ReadResourcePayloads(
    FileKind::kTexturesTable, std::as_bytes(std::span(valid)));

  EXPECT_EQ(payloads.size(), valid.size());
  EXPECT_EQ(
    ReadDefect(FileKind::kTexturesTable, std::as_bytes(std::span(bc7_as_none))),
    CookedDefect::kInvalidValue);
  EXPECT_EQ(
    ReadDefect(FileKind::kTexturesTable, std::as_bytes(std::span(unknown))),
    CookedDefect::kInvalidValue);
}

//! Each format records its compression in one place.
NOLINT_TEST(LooseCookedResourcesTest, CompressionFollowsTheFormat)
{
  EXPECT_EQ(core::TextureCompressionOf(oxygen::Format::kBC7UNorm),
    core::kTextureCompressionBc7);
  EXPECT_EQ(core::TextureCompressionOf(oxygen::Format::kBC7UNormSRGB),
    core::kTextureCompressionBc7);
  EXPECT_EQ(core::TextureCompressionOf(oxygen::Format::kRGBA8UNorm),
    core::kTextureCompressionNone);
}

//! A table size is whole rows, no more than a resource index addresses.
NOLINT_TEST(LooseCookedResourcesTest, TableSizeIsWholeAddressableRows)
{
  const auto row = uint64_t { sizeof(core::BufferResourceDesc) };
  const auto max_rows = uint64_t { std::numeric_limits<uint32_t>::max() };

  EXPECT_TRUE(lc::IsResourceTableSize(FileKind::kBuffersTable, 0U));
  EXPECT_TRUE(lc::IsResourceTableSize(FileKind::kBuffersTable, 2U * row));
  EXPECT_TRUE(lc::IsResourceTableSize(FileKind::kBuffersTable, max_rows * row));
  EXPECT_FALSE(lc::IsResourceTableSize(FileKind::kBuffersTable, row + 1U));
  EXPECT_FALSE(
    lc::IsResourceTableSize(FileKind::kBuffersTable, (max_rows + 1U) * row));
  EXPECT_THROW(
    static_cast<void>(lc::IsResourceTableSize(FileKind::kAuxiliary, 0U)),
    std::invalid_argument);
}

} // namespace
