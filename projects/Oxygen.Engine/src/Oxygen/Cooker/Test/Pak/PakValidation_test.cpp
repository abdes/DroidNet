//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Pak/PakValidation.cpp, Pak/PakPlanPolicy.cpp

#include <array>
#include <cstdint>
#include <filesystem>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "PakTestSupport.h"

#include <Oxygen/Cooker/Pak/PakPlanBuilder.h>
#include <Oxygen/Cooker/Pak/PakPlanPolicy.h>
#include <Oxygen/Cooker/Pak/PakValidation.h>
#include <Oxygen/Cooker/Test/Support/Diagnostics.h>
#include <Oxygen/Data/PakFormat_render.h>
#include <Oxygen/Testing/GTest.h>

namespace {
namespace data = oxygen::data;
namespace pak = oxygen::content::pak;
namespace paktest = oxygen::content::pak::test;
namespace render = oxygen::data::pak::render;

constexpr auto kContentVersion = uint16_t { 23U };
constexpr auto kSourceKeySeed = uint8_t { 0x7CU };
constexpr auto kRegionBaseOffset = uint64_t { 256U };
constexpr auto kRegionSize = uint64_t { 64U };
constexpr auto kOverlapOffset = uint64_t { 288U };

struct BaselinePlanInput final {
  pak::PakBuildRequest request;
  pak::PakPlan plan;
};

auto ClonePlanData(const pak::PakPlan& plan) -> pak::PakPlan::Data
{
  auto out = pak::PakPlan::Data {};
  out.header = plan.Header();
  out.regions.assign(plan.Regions().begin(), plan.Regions().end());
  out.tables.assign(plan.Tables().begin(), plan.Tables().end());
  out.assets.assign(plan.Assets().begin(), plan.Assets().end());
  out.resources.assign(plan.Resources().begin(), plan.Resources().end());
  out.asset_payload_sources.assign(
    plan.AssetPayloadSources().begin(), plan.AssetPayloadSources().end());
  out.resource_payload_sources.assign(
    plan.ResourcePayloadSources().begin(), plan.ResourcePayloadSources().end());
  out.directory = plan.Directory();
  out.browse_index = plan.BrowseIndex();
  out.footer = plan.Footer();
  out.patch_actions.assign(
    plan.PatchActions().begin(), plan.PatchActions().end());
  out.patch_closure.assign(
    plan.PatchClosure().begin(), plan.PatchClosure().end());
  out.planned_file_size = plan.PlannedFileSize();
  return out;
}

class PakDomainValidationTest : public ::testing::Test {
protected:
  auto MakeBaselinePlan() -> BaselinePlanInput
  {
    auto request
      = paktest::MakeFullRequest(std::filesystem::path("domain_validation.pak"),
        { .content_version = kContentVersion,
          .source_key = paktest::MakeSourceKey(kSourceKeySeed) });

    const auto build_result = pak::PakPlanBuilder {}.Build(request);
    EXPECT_FALSE(paktest::HasError(build_result.diagnostics));
    if (!build_result.plan.has_value()) {
      throw std::runtime_error("baseline pak plan build failed: "
        + oxygen::cooker::test::DiagnosticSummary(build_result.diagnostics));
    }

    return BaselinePlanInput {
      .request = std::move(request),
      .plan = std::move(*build_result.plan),
    };
  }
};

NOLINT_TEST_F(PakDomainValidationTest, BaselinePlannerOutputPassesValidation)
{
  auto baseline = MakeBaselinePlan();
  const auto policy = pak::DerivePakPlanPolicy(baseline.request);
  const auto result
    = pak::PakValidation::Validate(baseline.plan, policy, baseline.request);

  EXPECT_TRUE(result.success);
  EXPECT_FALSE(paktest::HasError(result.diagnostics));
}

NOLINT_TEST_F(PakDomainValidationTest, RejectsOverlappingSections)
{
  auto baseline = MakeBaselinePlan();
  auto data = ClonePlanData(baseline.plan);
  ASSERT_GE(data.regions.size(), 2U);

  data.regions[0].offset = kRegionBaseOffset;
  data.regions[0].size_bytes = kRegionSize;
  data.regions[0].alignment = 1U;
  data.regions[1].offset = kOverlapOffset;
  data.regions[1].size_bytes = kRegionSize;
  data.regions[1].alignment = 1U;

  const auto policy = pak::DerivePakPlanPolicy(baseline.request);
  const auto result = pak::PakValidation::Validate(
    pak::PakPlan(std::move(data)), policy, baseline.request);

  EXPECT_FALSE(result.success);
  const auto has_region_overlap
    = paktest::HasDiagnosticCode(result.diagnostics, "pak.plan.region_overlap");
  const auto has_section_overlap = paktest::HasDiagnosticCode(
    result.diagnostics, "pak.plan.section_overlap");
  EXPECT_TRUE(has_region_overlap || has_section_overlap);
}

NOLINT_TEST_F(PakDomainValidationTest, RejectsTableEntrySizeMismatch)
{
  auto baseline = MakeBaselinePlan();
  auto data = ClonePlanData(baseline.plan);
  ASSERT_FALSE(data.tables.empty());

  auto& table = data.tables.front();
  table.table_name = "texture_table";
  table.count = 1U;
  table.expected_entry_size = static_cast<uint32_t>(
    sizeof(oxygen::data::pak::core::TextureResourceDesc));
  table.entry_size = table.expected_entry_size + 1U;
  table.size_bytes = table.entry_size;
  table.alignment = 1U;

  const auto policy = pak::DerivePakPlanPolicy(baseline.request);
  const auto result = pak::PakValidation::Validate(
    pak::PakPlan(std::move(data)), policy, baseline.request);

  EXPECT_FALSE(result.success);
  EXPECT_TRUE(paktest::HasDiagnosticCode(
    result.diagnostics, "pak.plan.table_entry_size_mismatch"));
}

} // namespace
