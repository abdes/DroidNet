//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Pak/PakWriter.cpp

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "PakWriterTestSupport.h"

#include <Oxygen/Cooker/test/Support/Diagnostics.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Serio/Testing/TestBytes.h>
#include <Oxygen/Testing/GTest.h>

namespace {
namespace core = oxygen::data::pak::core;
namespace data = oxygen::data;
namespace pak = oxygen::content::pak;
namespace paktest = oxygen::content::pak::test;

using oxygen::cooker::test::HasDiagnostic;
using oxygen::cooker::test::HasNoErrors;
using oxygen::serio::testing::PatternBytes;
using ::testing::Not;

//! The writer must reject plans that contradict themselves, with a diagnostic
//! that names the broken invariant.
class PakWriterPlanContractTest : public paktest::PakWriterFixture { };

//! A region planned inside the header puts the cursor past its offset.
NOLINT_TEST_F(
  PakWriterPlanContractTest, OffsetMismatchEmitsActionableDiagnostic)
{
  constexpr auto kInsideTheHeader = uint64_t { 1U };
  auto plan = CanonicalPlan();
  plan.regions.front().offset = kInsideTheHeader;

  const auto written = Write(Request(1U), std::move(plan));

  EXPECT_THAT(written.diagnostics, Not(HasNoErrors()));
  EXPECT_THAT(written.diagnostics, HasDiagnostic("pak.write.offset_mismatch"));
}

NOLINT_TEST_F(PakWriterPlanContractTest, InvalidCrcPatchOffsetEmitsDiagnostic)
{
  constexpr auto kContentVersion = uint16_t { 5U };
  auto plan = CanonicalPlan({ .content_version = kContentVersion });
  plan.footer.crc32_field_absolute_offset += 1U;

  const auto written = Write(Request(kContentVersion), std::move(plan));

  EXPECT_THAT(written.diagnostics, Not(HasNoErrors()));
  EXPECT_THAT(
    written.diagnostics, HasDiagnostic("pak.write.crc_field_offset_invalid"));
}

//! One texture entry is announced, but the table's byte size stays zero.
NOLINT_TEST_F(
  PakWriterPlanContractTest, TablePayloadSizeMismatchEmitsDiagnostic)
{
  constexpr auto kContentVersion = uint16_t { 7U };
  auto plan = CanonicalPlan({ .content_version = kContentVersion });
  plan.tables.front().count = 1U;

  const auto written = Write(Request(kContentVersion), std::move(plan));

  EXPECT_THAT(written.diagnostics, Not(HasNoErrors()));
  EXPECT_THAT(
    written.diagnostics, HasDiagnostic("pak.write.table_size_mismatch"));
}

//=== Resource vs. asset payload plans ===-----------------------------------//

enum class PayloadKind : uint8_t { kResource, kAsset };

auto PayloadKindName(const ::testing::TestParamInfo<PayloadKind>& info)
  -> std::string
{
  return info.param == PayloadKind::kResource ? "Resource" : "Asset";
}

//! A plan of one texture, or of one geometry, whose payload the test sources.
class PakWriterPlanContractPayloadKindTest
  : public paktest::PakWriterFixture,
    public ::testing::WithParamInterface<PayloadKind> {
protected:
  static constexpr auto kContentVersion = uint16_t { 10U };
  static constexpr auto kPayloadSize = size_t { 8U };

  //! The plan of the payload of the parameter's kind, with no payload source.
  [[nodiscard]] static auto UnsourcedPlan() -> pak::PakPlan::Data
  {
    if (GetParam() == PayloadKind::kResource) {
      return CanonicalPlan({
        .content_version = kContentVersion,
        .texture_payload_size = kPayloadSize,
      });
    }
    auto plan = CanonicalPlan({ .content_version = kContentVersion });
    paktest::AddGeometryAsset(plan,
      data::AssetKey::FromVirtualPath("/Game/Mesh.ogeo"),
      PatternBytes(kPayloadSize));
    return plan;
  }
};

//! A placement without a matching payload source is a plan contract violation.
NOLINT_TEST_P(
  PakWriterPlanContractPayloadKindTest, RejectsPayloadSourceCountMismatch)
{
  const auto plan = UnsourcedPlan();

  const auto written = Write(Request(kContentVersion), plan);

  EXPECT_THAT(written.diagnostics, Not(HasNoErrors()));
  EXPECT_THAT(written.diagnostics,
    HasDiagnostic(GetParam() == PayloadKind::kResource
        ? "pak.write.resource_source_count_mismatch"
        : "pak.write.asset_source_count_mismatch"));
}

//! A payload source that cannot be read surfaces as a store diagnostic.
NOLINT_TEST_P(PakWriterPlanContractPayloadKindTest,
  EmitsStoreDiagnosticWhenSourceFileMissing)
{
  auto plan = UnsourcedPlan();
  if (GetParam() == PayloadKind::kResource) {
    plan.resource_payload_sources
      = { MissingSource("texture.bin", kPayloadSize) };
    // Only the payload is missing; the descriptor source must exist.
    plan.resource_descriptor_sources = { AddSource("texture_descriptor.bin",
      std::vector<std::byte>(
        sizeof(core::TextureResourceDesc), std::byte { 0 })) };
  } else {
    plan.asset_payload_sources = { MissingSource("mesh.ogeo", kPayloadSize) };
  }

  const auto written = Write(Request(kContentVersion), std::move(plan));

  EXPECT_THAT(written.diagnostics, Not(HasNoErrors()));
  EXPECT_THAT(written.diagnostics,
    HasDiagnostic(GetParam() == PayloadKind::kResource
        ? "pak.write.resource_store_failed"
        : "pak.write.asset_descriptor_store_failed"));
}

INSTANTIATE_TEST_SUITE_P(PayloadKinds, PakWriterPlanContractPayloadKindTest,
  ::testing::Values(PayloadKind::kResource, PayloadKind::kAsset),
  PayloadKindName);

} // namespace
