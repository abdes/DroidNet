# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

# Offline candidate generation reuses the qualified test reference. Shipping
# lighting consumes qualified generated data, never this reference code.
add_executable(
  Oxygen.Vortex.GgxMomentTable
  "Tools/GgxMomentTable.cpp"
  "Reference/GgxMoments.cpp"
  "Reference/ReferenceQuadrature.cpp"
)
target_link_libraries(
  Oxygen.Vortex.GgxMomentTable
  PRIVATE
    oxygen::base
    nlohmann_json::nlohmann_json
)
set_target_properties(
  Oxygen.Vortex.GgxMomentTable
  PROPERTIES
    FOLDER
      "Testing"
)

gtest_program(
  "Oxygen.Vortex.LightingImageReference.Tests"
  GPU
  SOURCES
    "LightingImageReference_test.cpp"
    "ShadowAdmissionGpu_test.cpp"
    "UnculledLightingFixture.cpp"
    "UnculledLightingFixture.h"
    "Reference/Photometry.cpp"
    "Reference/Photometry.h"
    "MaterialRasterReference_test.cpp"
    "LightingWorkloadGpu_test.cpp"
    "Reference/MaterialEvaluation.cpp"
    "Reference/MaterialEvaluation.h"
    "Reference/MaterialDecode.cpp"
    "Reference/MaterialDecode.h"
  DEPS
    oxygen-vortex-exposure-test-support
    oxygen::vortex-test-support
)

gtest_program(
  "Oxygen.Vortex.LightingInstrumentation.Tests"
  SOURCES
    "CpuTimingCapture_test.cpp"
    "MemoryCapture_test.cpp"
    "ResourceCreationCounter_test.cpp"
    "LightingWorkload_test.cpp"
  DEPS
    oxygen::testing
    oxygen::vortex-test-support
)

target_compile_definitions(
  Oxygen.Vortex.LightingInstrumentation.Tests
  PRIVATE
    OXYGEN_LIGHTING_WORKLOAD_MANIFEST="${CMAKE_CURRENT_SOURCE_DIR}/LightingWorkloads.json"
)

gtest_program(
  "Oxygen.Vortex.LightingInstrumentationGpu.Tests"
  GPU
  SOURCES
    "LightingInstrumentationGpu_test.cpp"
    "MemoryAccountingGpu_test.cpp"
    "ResourceChurnGpu_test.cpp"
  DEPS
    oxygen-vortex-exposure-test-support
    oxygen::vortex-test-support
)

gtest_program(
  "Oxygen.Vortex.LightingGpuAbi.Tests"
  GPU
  SOURCES
    "LightingGpuAbiFixture.cpp"
    "LightingGpuAbiFixture.h"
    "LightGridAbi_test.cpp"
    "LightEvaluationAbi_test.cpp"
    "LightGridLookup_test.cpp"
    "ShadowRecordAbi_test.cpp"
    "ShadowAllocationRequirements_test.cpp"
    "MaterialDecodeGpu_test.cpp"
    "MaterialUvGpu_test.cpp"
    "PhotometryGpu_test.cpp"
    "BrdfGpu_test.cpp"
    "IblProductsGpu_test.cpp"
    "CubemapGeometryGpu_test.cpp"
    "HeightFogGpu_test.cpp"
    "IblConvolutionGpu_test.cpp"
    "Reference/GgxEnergyLookup.h"
    "FiniteEmitterGpu_test.cpp"
    "AllocationBudgetGpu_test.cpp"
    "Reference/FiniteEmitter.cpp"
    "Reference/FiniteEmitter.h"
    "Reference/Photometry.cpp"
    "Reference/Photometry.h"
    "Reference/MaterialDecode.cpp"
    "Reference/MaterialDecode.h"
    "Reference/MaterialEvaluation.cpp"
    "Reference/MaterialEvaluation.h"
    "Reference/GgxBrdf.cpp"
    "Reference/GgxBrdf.h"
    "Reference/GgxMoments.cpp"
    "Reference/GgxMoments.h"
    "Reference/ReferenceQuadrature.cpp"
    "Reference/ReferenceQuadrature.h"
  DEPS
    oxygen::vortex
    oxygen::graphics-direct3d12
    oxygen::testing
    nlohmann_json::nlohmann_json
)

gtest_program(
  "Oxygen.Vortex.LightingReference.Tests"
  SOURCES
    "Reference/MaterialEvaluation.cpp"
    "Reference/MaterialEvaluation.h"
    "MaterialEvaluationReference_test.cpp"
    "Reference/GgxMoments.cpp"
    "Reference/GgxMoments.h"
    "Reference/GgxBrdf.cpp"
    "Reference/GgxBrdf.h"
    "Reference/Photometry.cpp"
    "Reference/Photometry.h"
    "Reference/ReferenceQuadrature.cpp"
    "Reference/ReferenceQuadrature.h"
    "Reference/FiniteEmitter.cpp"
    "Reference/FiniteEmitter.h"
    "Reference/MaterialDecode.cpp"
    "Reference/MaterialDecode.h"
    "MaterialDecodeReference_test.cpp"
    "FiniteEmitterReference_test.cpp"
    "FiniteEmitterOffAxis_test.cpp"
    "PhotometryReference_test.cpp"
    "GgxBrdfReference_test.cpp"
    "GgxFurnaceReference_test.cpp"
    "GgxReference_test.cpp"
    "GgxEndpointReference_test.cpp"
    "GgxMomentCertificate_test.cpp"
    "GgxMeanMomentCertificate_test.cpp"
  DEPS
    oxygen::testing
    nlohmann_json::nlohmann_json
)
target_compile_definitions(
  Oxygen.Vortex.LightingReference.Tests
  PRIVATE
    OXYGEN_GGX_ENDPOINT_REFERENCE_FILE="${CMAKE_CURRENT_SOURCE_DIR}/Reference/GgxEndpointReference.json"
    OXYGEN_GGX_MOMENT_CERTIFICATE_FILE="${CMAKE_CURRENT_SOURCE_DIR}/Reference/GgxMomentCertificates.json"
    OXYGEN_GGX_MEAN_CERTIFICATE_FILE="${CMAKE_CURRENT_SOURCE_DIR}/Reference/GgxMeanMomentCertificates.json"
)

# The probes reach camera helpers, so they compile as camera view entries.
oxygen_add_dxc_compute_probe(
  oxygen-vortex-lighting-abi-probe
  SOURCE "../Shaders/LightingGpuAbiProbe.hlsl"
  PROFILE cs_6_6
  CONSUMER Oxygen.Vortex.LightingGpuAbi.Tests
  DEFINITION OXYGEN_LIGHTING_ABI_PROBE
  CAMERA_VIEW_ENTRY
)
target_compile_definitions(
  Oxygen.Vortex.LightingGpuAbi.Tests
  PRIVATE
    OXYGEN_LIGHTING_ABI_WORKSPACE="${PROJECT_SOURCE_DIR}"
    OXYGEN_GGX_MEAN_CERTIFICATE_FILE="${CMAKE_CURRENT_SOURCE_DIR}/Reference/GgxMeanMomentCertificates.json"
)
oxygen_add_dxc_compute_probe(
  oxygen-vortex-unculled-lighting-probe
  SOURCE "../Shaders/UnculledLightingReference.hlsl"
  PROFILE cs_6_6
  CONSUMER Oxygen.Vortex.LightingImageReference.Tests
  DEFINITION OXYGEN_UNCULLED_LIGHTING_PROBE
  CAMERA_VIEW_ENTRY
)
