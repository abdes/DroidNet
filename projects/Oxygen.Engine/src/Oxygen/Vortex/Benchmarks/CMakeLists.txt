# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

# These opt-in GoogleTest workloads share the native exposure fixture owners.
# Test is configured first by the Vortex module, defining the support library.
if(WIN32)
  include(GTestHelpers)

  # A benchmark measures optimized code, so `all` builds it only in Release and
  # any other configuration refuses to link it, even when asked for by name.
  function(oxygen_benchmark_release_only target)
    get_property(is_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
    if(NOT is_multi_config)
      if(NOT CMAKE_BUILD_TYPE STREQUAL "Release")
        set_target_properties(
          ${target}
          PROPERTIES
            EXCLUDE_FROM_ALL
              TRUE
        )
      endif()
    elseif(CMAKE_GENERATOR STREQUAL "Ninja Multi-Config")
      set_target_properties(
        ${target}
        PROPERTIES
          EXCLUDE_FROM_ALL
            "$<NOT:$<CONFIG:Release>>"
      )
    else()
      foreach(config IN LISTS CMAKE_CONFIGURATION_TYPES)
        if(NOT config STREQUAL "Release")
          string(TOUPPER "${config}" config_upper)
          set_target_properties(
            ${target}
            PROPERTIES
              EXCLUDE_FROM_DEFAULT_BUILD_${config_upper}
                TRUE
          )
        endif()
      endforeach()
    endif()
    add_custom_command(
      TARGET ${target}
      PRE_LINK
      COMMAND
        "$<$<NOT:$<CONFIG:Release>>:${CMAKE_COMMAND};-E;false>"
      COMMAND_EXPAND_LISTS
      VERBATIM
    )
  endfunction()

  gtest_program(
    "Oxygen.Vortex.Lighting.Benchmarks"
    NO_TEST
    SOURCES
      "LightingCollection_bench.cpp"
      "ManyLightBaseline.h"
      "ManyLightBaseline_bench.cpp"
      "ManyLightBaselineResults.cpp"
    DEPS
      oxygen-vortex-exposure-test-support
      oxygen::vortex-test-support
  )
  oxygen_benchmark_release_only(Oxygen.Vortex.Lighting.Benchmarks)
  set_target_properties(
    Oxygen.Vortex.Lighting.Benchmarks
    PROPERTIES
      FOLDER
        "Benchmarks"
  )
  gtest_program(
    "Oxygen.Vortex.Exposure.Benchmarks"
    NO_TEST
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
      "ExposureBenchmarkFixture.cpp"
      "ExposureBenchmarkFixture.h"
      "ExposureKernel_bench.cpp"
      "IblUpdate_bench.cpp"
      "IblScene_bench.cpp"
      "ExposureOverhead_bench.cpp"
      "ExposurePerformance_bench.cpp"
    DEPS
      oxygen-vortex-exposure-test-support
      oxygen::vortex-test-support
  )
  oxygen_benchmark_release_only(Oxygen.Vortex.Exposure.Benchmarks)
  set_target_properties(
    Oxygen.Vortex.Exposure.Benchmarks
    PROPERTIES
      FOLDER
        "Benchmarks"
  )
endif()
