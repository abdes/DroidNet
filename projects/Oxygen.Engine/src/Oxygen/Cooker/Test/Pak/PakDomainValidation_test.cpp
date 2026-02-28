//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <array>
#include <cstdint>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include <Oxygen/Cooker/Pak/PakPlanBuilder.h>
#include <Oxygen/Cooker/Pak/PakPlanPolicy.h>
#include <Oxygen/Cooker/Pak/PakValidation.h>
#include <Oxygen/Data/PakFormat_render.h>

#include "PakTestSupport.h"

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
constexpr auto kScriptParamRecordCount = uint32_t { 4U };

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
  out.script_param_ranges.assign(
    plan.ScriptParamRanges().begin(), plan.ScriptParamRanges().end());
  out.script_param_record_count = plan.ScriptParamRecordCount();
  out.planned_file_size = plan.PlannedFileSize();
  return out;
}

class PakDomainValidationTest : public paktest::TempDirFixture {
protected:
  auto MakeBaselinePlan() -> BaselinePlanInput
  {
    auto request = pak::PakBuildRequest {
      .mode = pak::BuildMode::kFull,
      .sources = {},
      .output_pak_path = Path("domain_validation.pak"),
      .output_manifest_path = {},
      .content_version = kContentVersion,
      .source_key = paktest::MakeSourceKey(kSourceKeySeed),
      .base_catalogs = {},
      .patch_compat = {},
      .options = pak::PakBuildOptions {
        .deterministic = true,
        .embed_browse_index = false,
        .emit_manifest_in_full = false,
        .compute_crc32 = true,
        .fail_on_warnings = false,
      },
    };

    const auto build_result = pak::PakPlanBuilder {}.Build(request);
    EXPECT_FALSE(paktest::HasError(build_result.diagnostics));
    EXPECT_TRUE(build_result.plan.has_value());

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
  table.expected_entry_size
    = static_cast<uint32_t>(sizeof(render::TextureResourceDesc));
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

NOLINT_TEST_F(PakDomainValidationTest, RejectsOverlappingScriptParamRanges)
{
  auto baseline = MakeBaselinePlan();
  auto data = ClonePlanData(baseline.plan);
  data.script_param_record_count = kScriptParamRecordCount;
  data.script_param_ranges = {
    pak::PakScriptParamRangePlan {
      .slot_index = 0U, .params_array_offset = 0U, .params_count = 3U },
    pak::PakScriptParamRangePlan {
      .slot_index = 1U, .params_array_offset = 2U, .params_count = 2U },
  };

  const auto policy = pak::DerivePakPlanPolicy(baseline.request);
  const auto result = pak::PakValidation::Validate(
    pak::PakPlan(std::move(data)), policy, baseline.request);

  EXPECT_FALSE(result.success);
  EXPECT_TRUE(paktest::HasDiagnosticCode(
    result.diagnostics, "pak.plan.script_param_overlap"));
}

} // namespace
