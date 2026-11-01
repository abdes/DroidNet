//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Pak/PakValidation.cpp, Pak/PakPlanPolicy.cpp

#include <cstdint>
#include <utility>

#include "PakWriterTestSupport.h"

#include <Oxygen/Cooker/Pak/PakBuildRequest.h>
#include <Oxygen/Cooker/Pak/PakPlan.h>
#include <Oxygen/Cooker/Pak/PakPlanPolicy.h>
#include <Oxygen/Cooker/Pak/PakValidation.h>
#include <Oxygen/Cooker/test/Support/Diagnostics.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Testing/GTest.h>

namespace {
namespace pak = oxygen::content::pak;
namespace paktest = oxygen::content::pak::test;

using oxygen::cooker::test::HasDiagnostic;
using oxygen::cooker::test::HasNoErrors;

using paktest::kFirstPayloadOffset;
using paktest::kPayloadAlignment;

constexpr auto kContentVersion = uint16_t { 23U };
//! A size smaller than a payload boundary.
constexpr auto kSubBoundarySize = uint64_t { 64U };

//! Validates canonical plans, empty paks of library `Game`, that each test
//! mutates in the one field it is about.
class PakDomainValidationTest : public paktest::PakWriterFixture {
protected:
  [[nodiscard]] static auto BaselinePlan() -> pak::PakPlan::Data
  {
    return CanonicalPlan({ .content_version = kContentVersion });
  }

  //! Validates `plan` against a full build request of the same version.
  [[nodiscard]] auto Validate(pak::PakPlan::Data plan)
    -> pak::PakValidation::Result
  {
    const auto request = Request(kContentVersion, "domain_validation.pak");
    return pak::PakValidation::Validate(pak::PakPlan(std::move(plan)),
      pak::DerivePakPlanPolicy(request), request);
  }
};

NOLINT_TEST_F(PakDomainValidationTest, BaselinePlannerOutputPassesValidation)
{
  const auto result = Validate(BaselinePlan());

  EXPECT_TRUE(result.success);
  EXPECT_THAT(result.diagnostics, HasNoErrors());
}

NOLINT_TEST_F(PakDomainValidationTest, RejectsOverlappingSections)
{
  auto plan = BaselinePlan();
  ASSERT_GE(plan.regions.size(), 2U);
  auto& first = plan.regions.at(0);
  auto& second = plan.regions.at(1);
  first.offset = kFirstPayloadOffset;
  first.size_bytes = 2U * kPayloadAlignment;
  second.offset = kFirstPayloadOffset + kPayloadAlignment;
  second.size_bytes = kPayloadAlignment;

  const auto result = Validate(std::move(plan));

  EXPECT_FALSE(result.success);
  EXPECT_THAT(result.diagnostics,
    ::testing::AnyOf(HasDiagnostic("pak.plan.region_overlap"),
      HasDiagnostic("pak.plan.section_overlap")));
}

NOLINT_TEST_F(PakDomainValidationTest, RejectsTableEntrySizeMismatch)
{
  auto plan = BaselinePlan();
  ASSERT_FALSE(plan.tables.empty());
  auto& table = plan.tables.front();
  table.table_name = "texture_table";
  table.count = 1U;
  table.expected_entry_size = static_cast<uint32_t>(
    sizeof(oxygen::data::pak::core::TextureResourceDesc));
  table.entry_size = table.expected_entry_size + 1U;
  table.size_bytes = table.entry_size;
  table.alignment = 1U;

  const auto result = Validate(std::move(plan));

  EXPECT_FALSE(result.success);
  EXPECT_THAT(
    result.diagnostics, HasDiagnostic("pak.plan.table_entry_size_mismatch"));
}

//! A payload must start on a payload boundary, whatever smaller alignment its
//! descriptor records.
NOLINT_TEST_F(PakDomainValidationTest, RejectsAPayloadOffItsBoundary)
{
  auto plan = BaselinePlan();
  plan.regions.front().size_bytes = kPayloadAlignment;
  plan.resources = {
    paktest::MakeTexturePlacement(
      kFirstPayloadOffset + kSubBoundarySize, kSubBoundarySize),
  };

  const auto result = Validate(std::move(plan));

  EXPECT_THAT(result.diagnostics,
    HasDiagnostic("pak.plan.resource_payload_alignment_mismatch"));
}

//! A region holding payloads is padded to a payload boundary.
NOLINT_TEST_F(PakDomainValidationTest, RejectsAnUnpaddedPayloadRegion)
{
  auto plan = BaselinePlan();
  plan.regions.front().size_bytes = kSubBoundarySize;

  const auto result = Validate(std::move(plan));

  EXPECT_THAT(result.diagnostics,
    HasDiagnostic("pak.plan.region_payload_alignment_mismatch"));
}

} // namespace
