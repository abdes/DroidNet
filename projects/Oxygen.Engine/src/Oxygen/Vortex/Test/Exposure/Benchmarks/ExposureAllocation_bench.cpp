//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Vortex/Test/Exposure/Benchmarks/ExposureAllocationScenario.h>

namespace oxygen::vortex::testing::exposure {

auto ExposureLightingGpuTest::MeasureHdrAllocationAccounting(bool temporal)
  -> void
{
  ExposureAllocationScenario(*this, temporal).Run();
}

NOLINT_TEST_F(
  ExposureLightingGpuTest, DISABLED_ProductionHdrAllocationAccounting)
{
  MeasureHdrAllocationAccounting(false);
}

NOLINT_TEST_F(ExposureLightingGpuTest,
  DISABLED_ProductionHdrAllocationAccountingWithTemporalFog)
{
  MeasureHdrAllocationAccounting(true);
}

} // namespace oxygen::vortex::testing::exposure
