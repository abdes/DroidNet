# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

gtest_program(
  "Oxygen.Vortex.LightingInstrumentation.Tests"
  SOURCES
    "CpuTimingCapture_test.cpp"
  DEPS
    oxygen::testing
    oxygen-vortex-timing-test-support
)

gtest_program(
  "Oxygen.Vortex.LightingGpuAbi.Tests"
  SOURCES
    "LightingGpuAbiFixture.cpp"
    "LightingGpuAbiFixture.h"
    "LightGridAbi_test.cpp"
    "LightEvaluationAbi_test.cpp"
    "LightGridLookup_test.cpp"
    "ShadowRecordAbi_test.cpp"
    "ShadowAllocationRequirements_test.cpp"
    "MaterialDecodeGpu_test.cpp"
    "PhotometryGpu_test.cpp"
    "BrdfGpu_test.cpp"
    "../../Lighting/Internal/LightPhotometry.cpp"
    "Reference/Photometry.cpp"
    "Reference/Photometry.h"
    "Reference/MaterialDecode.cpp"
    "Reference/MaterialDecode.h"
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

if(CMAKE_SYSTEM_PROCESSOR STREQUAL "AMD64")
  set(lighting_probe_arch "x64")
elseif(CMAKE_SYSTEM_PROCESSOR STREQUAL "x86")
  set(lighting_probe_arch "x86")
elseif(CMAKE_SYSTEM_PROCESSOR STREQUAL "ARM64")
  set(lighting_probe_arch "arm64")
else()
  message(FATAL_ERROR "Unsupported architecture for lighting ABI probe")
endif()
set(
  lighting_shader_root
  "${PROJECT_SOURCE_DIR}/src/Oxygen/Graphics/Direct3D12/Shaders"
)
set(
  lighting_probe_output
  "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/LightingGpuAbiProbe.dxil"
)
set(
  lighting_probe_source
  "${CMAKE_CURRENT_SOURCE_DIR}/../Shaders/LightingGpuAbiProbe.hlsl"
)
add_custom_command(
  OUTPUT
    "${lighting_probe_output}"
  COMMAND
    ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>"
  COMMAND
    "${PROJECT_SOURCE_DIR}/packages/DXC/bin/${lighting_probe_arch}/dxc.exe" -T
    cs_6_6 -E CS -O3 -HV 2021 -I "${lighting_shader_root}" -I
    "${PROJECT_SOURCE_DIR}/src/Oxygen" -Fo "${lighting_probe_output}"
    "${lighting_probe_source}"
  COMMAND
    "${PROJECT_SOURCE_DIR}/packages/DXC/bin/${lighting_probe_arch}/dxc.exe" -T
    cs_6_6 -E CS -O3 -HV 2021 -I "${lighting_shader_root}" -I
    "${PROJECT_SOURCE_DIR}/src/Oxygen" -MD -MF "${lighting_probe_output}.d" -Fo
    "${lighting_probe_output}" "${lighting_probe_source}"
  DEPENDS
    "${lighting_probe_source}"
    "${lighting_shader_root}/Vortex/Contracts/Lighting/LightGridData.hlsli"
    "${lighting_shader_root}/Vortex/Contracts/Shadows/ShadowRecords.hlsli"
  DEPFILE "${lighting_probe_output}.d"
  VERBATIM
)
add_custom_target(
  oxygen-vortex-lighting-abi-probe
  DEPENDS
    "${lighting_probe_output}"
)
add_dependencies(
  Oxygen.Vortex.LightingGpuAbi.Tests
  oxygen-vortex-lighting-abi-probe
)
target_compile_definitions(
  Oxygen.Vortex.LightingGpuAbi.Tests
  PRIVATE
    OXYGEN_LIGHTING_ABI_PROBE="${lighting_probe_output}"
    OXYGEN_LIGHTING_ABI_WORKSPACE="${PROJECT_SOURCE_DIR}"
    OXYGEN_GGX_MEAN_CERTIFICATE_FILE="${CMAKE_CURRENT_SOURCE_DIR}/Reference/GgxMeanMomentCertificates.json"
)
