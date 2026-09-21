//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <chrono>
#include <fstream>
#include <ios>
#include <ratio>
#include <string>
#include <tuple>

#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Vortex/Benchmarks/ExposureBaselineScenario.h>
#include <Oxygen/Vortex/Benchmarks/ExposureBenchmarkFixture.h>
#include <Oxygen/Vortex/RendererCapability.h>

namespace oxygen::vortex::testing::exposure {

auto ExposureProfilingOverheadTest::BackendConfigJson() const -> std::string
{
  return R"({"enable_debug_layer":false})";
}

auto ExposureProfilingOverheadTest::AdditionalCapabilities() const
  -> CapabilitySet
{
  return RendererCapabilityFamily::kDiagnosticsAndProfiling;
}

auto ExposureIndoorOutdoorBenchmarkTest::AdditionalCapabilities() const
  -> CapabilitySet
{
  return ExposureProfilingOverheadTest::AdditionalCapabilities()
    | RendererCapabilityFamily::kShadowing;
}

auto ExposureProfilingOverheadTest::MeasureReleaseBaseline(
  const BaselineRecipe kind) -> void
{
#ifndef NDEBUG
  std::ignore = kind;
  FAIL() << "This performance measurement requires Release.";
#else
  auto scenario = ExposureBaselineScenario(*this, kind);
  scenario.Run();
#endif
}
auto ExposureBaselineScenario::Run() -> void
{
  ASSERT_NO_FATAL_FAILURE(Setup());
  view_settings.fill(fixture_.settings);
  ASSERT_NO_FATAL_FAILURE(WarmUp());
  if (warmup_only) {
    // Untimed calibration selects one common sample count for a decision pair.
    // No timeline recording or measured population is created by this mode.
    auto output = std::ofstream(manifest_path, std::ios::binary);
    ASSERT_TRUE(output.is_open());
    output << nlohmann::json({
                               {
                                 "warmup_only",
                                 true,
                               },
                               {
                                 "workload",
                                 workload,
                               },
                               {
                                 "precision",
                                 precision,
                               },
                               {
                                 "width",
                                 width,
                               },
                               {
                                 "warmup_frames",
                                 warm_frames,
                               },
                               {
                                 "warmup_seconds",
                                 warm_seconds,
                               },
                               {
                                 "simulation_dt_ns",
                                 simulation_dt_ns,
                               },
                             })
                .dump(2)
           << '\n';
    output.close();
    ASSERT_TRUE(output.good());
    return;
  }
  ASSERT_NO_FATAL_FAILURE(MeasureFrames());
  if (acceptance && workload == "I02" && width == 1920U) {
    ASSERT_NO_FATAL_FAILURE(MeasureEventWindows());
  }
  ASSERT_NO_FATAL_FAILURE(CaptureEndpoints());
  if (acceptance) {
    ASSERT_NO_FATAL_FAILURE(SaveAcceptanceWindows());
  }
  ASSERT_NO_FATAL_FAILURE(WriteAndValidateResults());
}
auto ExposureBaselineScenario::Milliseconds(const Clock::duration duration)
  -> double
{
  return std::chrono::duration<double, std::milli>(duration).count();
}
} // namespace oxygen::vortex::testing::exposure
