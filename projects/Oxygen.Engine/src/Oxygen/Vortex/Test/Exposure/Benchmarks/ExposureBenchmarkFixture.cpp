//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <chrono>

#include <Oxygen/Vortex/Test/Exposure/Benchmarks/ExposureBaselineScenario.h>

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
  static_cast<void>(kind);
  FAIL() << "This performance measurement requires Release.";
#else
  auto scenario = ExposureBaselineScenario(*this, kind);
  scenario.Run();
#endif
}
auto ExposureBaselineScenario::Run() -> void
{
  ASSERT_NO_FATAL_FAILURE(Setup());
  ASSERT_NO_FATAL_FAILURE(WarmUp());
  ASSERT_NO_FATAL_FAILURE(MeasureFrames());
  ASSERT_NO_FATAL_FAILURE(CaptureEndpoints());
  ASSERT_NO_FATAL_FAILURE(WriteAndValidateResults());
}
auto ExposureBaselineScenario::Milliseconds(const Clock::duration duration)
  -> double
{
  return std::chrono::duration<double, std::milli>(duration).count();
}
} // namespace oxygen::vortex::testing::exposure
