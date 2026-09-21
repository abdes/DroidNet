# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

# These opt-in GoogleTest workloads share the native exposure fixture owners.
# Test is configured first by the Vortex module, defining the support library.
if(WIN32)
  include(GTestHelpers)
  gtest_program(
    "Oxygen.Vortex.Exposure.Benchmarks"
    SOURCES
      "ExposureAllocationLifecycle.cpp"
      "ExposureAllocationMeasurement.cpp"
      "ExposureAllocationScenario.h"
      "ExposureAllocationSetup.cpp"
      "ExposureAllocation_bench.cpp"
      "ExposureBaselineRendering.cpp"
      "ExposureBaselineEvents.cpp"
      "ExposureBaselineResults.cpp"
      "ExposureBaselineScenario.h"
      "ExposureBaselineSetup.cpp"
      "ExposureCpuTiming.cpp"
      "ExposureCpuTiming.h"
      "ExposureBenchmarkFixture.cpp"
      "ExposureBenchmarkFixture.h"
      "ExposureKernel_bench.cpp"
      "ExposureOverhead_bench.cpp"
      "ExposurePerformance_bench.cpp"
    DEPS
      oxygen-vortex-exposure-test-support
  )
  set_target_properties(
    Oxygen.Vortex.Exposure.Benchmarks
    PROPERTIES
      FOLDER
        "Benchmarks"
  )
endif()
