# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

gtest_program(
  "Oxygen.Vortex.LightingGpuAbi.Tests"
  SOURCES
    "LightingGpuAbiFixture.cpp"
    "LightingGpuAbiFixture.h"
    "LightGridAbi_test.cpp"
    "LightGridLookup_test.cpp"
    "ShadowRecordAbi_test.cpp"
  DEPS
    oxygen::vortex
    oxygen::graphics-direct3d12
    oxygen::testing
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
)
