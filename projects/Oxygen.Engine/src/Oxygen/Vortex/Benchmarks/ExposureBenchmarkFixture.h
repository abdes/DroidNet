//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>

#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureLightingFixture.h>

namespace oxygen::vortex::testing::exposure {

class ExposureBaselineScenario;

class ExposureProfilingOverheadTest : public ExposureLightingGpuTest {
  friend class ExposureBaselineScenario;

protected:
  enum class BaselineRecipe : std::uint8_t {
    kControlled,
    kMixed,
    kIndoorOutdoor,
  };
  auto MeasureReleaseBaseline(BaselineRecipe kind) -> void;
  auto BackendConfigJson() const -> std::string override;
  auto AdditionalCapabilities() const -> CapabilitySet override;
};

class ExposureIndoorOutdoorBenchmarkTest
  : public ExposureProfilingOverheadTest {
protected:
  auto AdditionalCapabilities() const -> CapabilitySet override;
};

} // namespace oxygen::vortex::testing::exposure
