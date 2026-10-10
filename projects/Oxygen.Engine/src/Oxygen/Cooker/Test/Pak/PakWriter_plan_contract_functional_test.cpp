//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Pak/PakWriter.cpp

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "PakTestSupport.h"
#include "PakWriterTestSupport.h"

#include <Oxygen/Cooker/Pak/PakPlan.h>
#include <Oxygen/Cooker/Pak/PakWriter.h>
#include <Oxygen/Cooker/Test/Support/FileIo.h>
#include <Oxygen/Cooker/Test/Support/TestValues.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Testing/GTest.h>

namespace {
namespace core = oxygen::data::pak::core;
namespace pak = oxygen::content::pak;
namespace paktest = oxygen::content::pak::test;
namespace cooktest = oxygen::cooker::test;

using paktest::HasDiagnosticCode;
using paktest::HasError;
using paktest::MakeAssetKey;

//! The writer must reject plans that contradict themselves, with a diagnostic
//! that names the broken invariant.
class PakWriterPlanContractTest : public paktest::PakWriterFixture { };

NOLINT_TEST_F(
  PakWriterPlanContractTest, OffsetMismatchEmitsActionableDiagnostic)
{
  constexpr auto kMismatchedOffset = uint64_t { 1U };

  auto plan_data = CanonicalPlan({ .base_offset = kMismatchedOffset });

  const auto request = paktest::MakeFullRequest(
    Root() / "offset_mismatch.pak", { .content_version = 1U });

  const auto write_result
    = pak::PakWriter {}.Write(request, pak::PakPlan(std::move(plan_data)));
  EXPECT_TRUE(HasError(write_result.diagnostics));
  EXPECT_TRUE(
    HasDiagnosticCode(write_result.diagnostics, "pak.write.offset_mismatch"));
}

NOLINT_TEST_F(PakWriterPlanContractTest, InvalidCrcPatchOffsetEmitsDiagnostic)
{
  auto plan_data = CanonicalPlan({ .content_version = 5U });
  plan_data.footer.crc32_field_absolute_offset += 1U;

  const auto request = paktest::MakeFullRequest(
    Root() / "invalid_crc_offset.pak", { .content_version = 5U });

  const auto write_result
    = pak::PakWriter {}.Write(request, pak::PakPlan(std::move(plan_data)));
  EXPECT_TRUE(HasError(write_result.diagnostics));
  EXPECT_TRUE(HasDiagnosticCode(
    write_result.diagnostics, "pak.write.crc_field_offset_invalid"));
}

NOLINT_TEST_F(
  PakWriterPlanContractTest, TablePayloadSizeMismatchEmitsDiagnostic)
{
  // One texture entry is announced, but the table's byte size stays zero.
  auto plan_data
    = CanonicalPlan({ .content_version = 7U, .texture_count = 1U });

  const auto request = paktest::MakeFullRequest(
    Root() / "table_size_mismatch.pak", { .content_version = 7U });

  const auto write_result
    = pak::PakWriter {}.Write(request, pak::PakPlan(std::move(plan_data)));
  EXPECT_TRUE(HasError(write_result.diagnostics));
  EXPECT_TRUE(HasDiagnosticCode(
    write_result.diagnostics, "pak.write.table_size_mismatch"));
}

//=== Resource vs. asset payload plans ===-----------------------------------//

enum class PayloadKind : uint8_t { kResource, kAsset };

auto PayloadKindName(const ::testing::TestParamInfo<PayloadKind>& info)
  -> std::string
{
  return info.param == PayloadKind::kResource ? "Resource" : "Asset";
}

class PakWriterPlanContractPayloadKindTest
  : public paktest::PakWriterFixture,
    public ::testing::WithParamInterface<PayloadKind> { };

//! A placement without a matching payload source is a plan contract violation.
NOLINT_TEST_P(
  PakWriterPlanContractPayloadKindTest, RejectsPayloadSourceCountMismatch)
{
  constexpr auto kContentVersion = uint16_t { 10U };
  constexpr auto kPayloadSize = uint64_t { 8U };

  auto plan_data = pak::PakPlan::Data {};
  auto diagnostic_code = std::string_view {};
  if (GetParam() == PayloadKind::kResource) {
    plan_data = CanonicalPlan({
      .content_version = kContentVersion,
      .texture_region_size = 4U,
      .tables_offset = 512U,
      .footer_offset = 512U,
    });
    plan_data.resources = { paktest::MakeTexturePlacement(256U, 4U) };
    diagnostic_code = "pak.write.resource_source_count_mismatch";
  } else {
    plan_data = CanonicalPlan({
      .content_version = kContentVersion,
      .directory_offset = 512U,
      .footer_offset = 640U,
    });
    paktest::AddGeometryAsset(
      plan_data, MakeAssetKey(0x2AU), 384U, kPayloadSize);
    diagnostic_code = "pak.write.asset_source_count_mismatch";
  }

  const auto request
    = paktest::MakeFullRequest(Root() / "source_count_mismatch.pak",
      { .content_version = kContentVersion });

  const auto write_result
    = pak::PakWriter {}.Write(request, pak::PakPlan(std::move(plan_data)));
  EXPECT_TRUE(HasError(write_result.diagnostics));
  EXPECT_TRUE(HasDiagnosticCode(write_result.diagnostics, diagnostic_code));
}

//! A payload source that cannot be read surfaces as a store diagnostic.
NOLINT_TEST_P(PakWriterPlanContractPayloadKindTest,
  EmitsStoreDiagnosticWhenSourceFileMissing)
{
  constexpr auto kContentVersion = uint16_t { 12U };
  constexpr auto kPayloadSize = uint64_t { 8U };

  auto plan_data = pak::PakPlan::Data {};
  auto diagnostic_code = std::string_view {};
  if (GetParam() == PayloadKind::kResource) {
    plan_data = CanonicalPlan({
      .content_version = kContentVersion,
      .texture_region_size = kPayloadSize,
      .tables_offset = 512U,
      .footer_offset = 896U,
      .texture_count = 1U,
      .texture_table_size = sizeof(core::TextureResourceDesc),
    });
    plan_data.resources = { paktest::MakeTexturePlacement(256U, kPayloadSize) };
    plan_data.resource_payload_sources = {
      pak::PakPayloadSourceSlicePlan {
        .source_path = Root() / "missing_texture_payload.bin",
        .source_offset = 0U,
        .size_bytes = kPayloadSize,
      },
    };
    // Only the payload is missing; the descriptor source must exist.
    const auto descriptor_source_path
      = Root() / "missing_texture_descriptor.bin";
    cooktest::WriteBytes(descriptor_source_path,
      std::vector<std::byte>(
        sizeof(core::TextureResourceDesc), std::byte { 0 }));
    plan_data.resource_descriptor_sources = {
      pak::PakPayloadSourceSlicePlan {
        .source_path = descriptor_source_path,
        .source_offset = 0U,
        .size_bytes = sizeof(core::TextureResourceDesc),
      },
    };
    diagnostic_code = "pak.write.resource_store_failed";
  } else {
    plan_data = CanonicalPlan({
      .content_version = kContentVersion,
      .directory_offset = 768U,
      .footer_offset = 1024U,
    });
    paktest::AddGeometryAsset(
      plan_data, MakeAssetKey(0x4CU), 512U, kPayloadSize);
    plan_data.asset_payload_sources = {
      pak::PakPayloadSourceSlicePlan {
        .source_path = Root() / "missing_descriptor_payload.bin",
        .source_offset = 0U,
        .size_bytes = kPayloadSize,
      },
    };
    diagnostic_code = "pak.write.asset_descriptor_store_failed";
  }

  const auto request
    = paktest::MakeFullRequest(Root() / "missing_payload_source.pak",
      { .content_version = kContentVersion });

  const auto write_result
    = pak::PakWriter {}.Write(request, pak::PakPlan(std::move(plan_data)));
  EXPECT_TRUE(HasError(write_result.diagnostics));
  EXPECT_TRUE(HasDiagnosticCode(write_result.diagnostics, diagnostic_code));
}

INSTANTIATE_TEST_SUITE_P(PayloadKinds, PakWriterPlanContractPayloadKindTest,
  ::testing::Values(PayloadKind::kResource, PayloadKind::kAsset),
  PayloadKindName);

} // namespace
