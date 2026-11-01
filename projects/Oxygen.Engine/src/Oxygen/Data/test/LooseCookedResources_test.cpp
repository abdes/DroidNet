//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <stdexcept>
#include <vector>

#include <Oxygen/Base/Sha256.h>
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

//! A table that ends in a partial row, or that is no resource table, is
//! refused.
NOLINT_TEST(LooseCookedResourcesTest, MalformedTablesAreRefused)
{
  auto root = BufferRoot {};
  root.Add(kFirst);
  const auto partial = root.Rows().first(root.Rows().size() - 1U);

  EXPECT_THROW(static_cast<void>(
                 lc::ReadResourcePayloads(FileKind::kBuffersTable, partial)),
    std::invalid_argument);
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

//! A row that holds bytes but no digest does not verify.
NOLINT_TEST(LooseCookedResourcesTest, RowWithoutDigestDoesNotVerify)
{
  auto root = BufferRoot {};
  root.Add(kFirst);
  root.rows.back().content_hash = {};
  const auto payloads
    = lc::ReadResourcePayloads(FileKind::kBuffersTable, root.Rows());

  const auto issues = lc::VerifyResourcePayloads(payloads, root.DataFile());

  ASSERT_EQ(issues.size(), 1U);
  EXPECT_EQ(issues.front().reason, ResourcePayloadFailure::kDigestMismatch);
}

} // namespace
