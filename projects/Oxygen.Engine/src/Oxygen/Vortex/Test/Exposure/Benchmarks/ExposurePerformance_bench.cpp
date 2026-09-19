//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Vortex/Test/Exposure/Benchmarks/ExposureBenchmarkFixture.h>

namespace oxygen::vortex::testing::exposure {

using namespace oxygen::graphics;

NOLINT_TEST_F(ExposureProfilingOverheadTest, DISABLED_ReleaseControlledBaseline)
{
  MeasureReleaseBaseline(BaselineRecipe::kControlled);
}

NOLINT_TEST_F(ExposureProfilingOverheadTest, DISABLED_ReleaseMixedBaseline)
{
  MeasureReleaseBaseline(BaselineRecipe::kMixed);
}

NOLINT_TEST_F(
  ExposureIndoorOutdoorBenchmarkTest, DISABLED_ReleaseIndoorOutdoorBaseline)
{
  MeasureReleaseBaseline(BaselineRecipe::kIndoorOutdoor);
}

} // namespace oxygen::vortex::testing::exposure
