# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

# ------------------------------------------------------------------------------
# Meta information about the this module
# ------------------------------------------------------------------------------

asap_module_declare(
  MODULE_NAME "Oxygen.Graphics.Direct3D12"
  DESCRIPTION "Oxygen graphics backend for Direct3D 12"
  WITHOUT_VERSION_H
)
asap_push_module("${META_MODULE_NAME}")

# ------------------------------------------------------------------------------
# Dependencies
# ------------------------------------------------------------------------------

find_package(glm CONFIG REQUIRED)
find_package(nlohmann_json REQUIRED)

# Define the path to the DXC compiler
set(dxc_arch "")
if(CMAKE_SYSTEM_PROCESSOR STREQUAL "AMD64")
  set(dxc_arch "x64")
elseif(CMAKE_SYSTEM_PROCESSOR STREQUAL "x86")
  set(dxc_arch "x86")
elseif(CMAKE_SYSTEM_PROCESSOR STREQUAL "ARM64")
  set(dxc_arch "arm64")
else()
  message(
    FATAL_ERROR
    "Unsupported processor architecture: ${CMAKE_SYSTEM_PROCESSOR}"
  )
endif()
set(dxc_lib_path "${CMAKE_SOURCE_DIR}/packages/DXC/lib/${dxc_arch}")
set(dxc_inc_path "${CMAKE_SOURCE_DIR}/packages/DXC/inc")
set(dxc_bin_path "${CMAKE_SOURCE_DIR}/packages/DXC/bin/${dxc_arch}")

# ==============================================================================
# Build instructions
# ==============================================================================

# ------------------------------------------------------------------------------
# Main module target
# ------------------------------------------------------------------------------

add_library(${META_MODULE_TARGET})
add_library(${META_MODULE_TARGET_ALIAS} ALIAS ${META_MODULE_TARGET})
set_target_properties(
  ${META_MODULE_TARGET}
  PROPERTIES
    DEFINE_SYMBOL
      "OXGN_D3D12_EXPORTS"
    OUTPUT_NAME
      $<IF:$<BOOL:${BUILD_SHARED_LIBS}>,${META_MODULE_NAME},${META_MODULE_NAME}-static>
    VERSION
      ${META_VERSION}
    SOVERSION
      ${META_VERSION_MAJOR}
    PROJECT_LABEL
      ${META_MODULE_NAME}
)

set(
  OXYGEN_GFX_D3D12_HEADERS
  "api_export.h"
  "Allocator/D3D12MemAlloc.h"
  "Buffer.h"
  "CommandList.h"
  "CommandQueue.h"
  "CommandRecorder.h"
  "Constants.h"
  "Devices/DeviceManager.h"
  "GraphicResource.h"
  "Graphics.h"
  "Texture.h"
  "ImGui/ImGuiBackend.h"
)
set(
  OXYGEN_GFX_D3D12_PRIVATE_SOURCES
  "Allocator/D3D12MemAlloc.cpp"
  "Bindless/D3D12HeapAllocationStrategy.cpp"
  "Bindless/D3D12HeapAllocationStrategy.h"
  "Bindless/DescriptorAllocator.cpp"
  "Bindless/DescriptorAllocator.h"
  "Bindless/DescriptorSegment.cpp"
  "Bindless/DescriptorSegment.h"
  "Buffer.cpp"
  "CommandList.cpp"
  "CommandQueue.cpp"
  "CommandRecorder.cpp"
  "Detail/CompositionSurface.h"
  "Detail/CompositionSwapChain.cpp"
  "Detail/CompositionSwapChain.h"
  "Detail/Converters.cpp"
  "Detail/Converters.h"
  "Detail/dx12_utils.h"
  "Detail/FormatUtils.cpp"
  "Detail/FormatUtils.h"
  "Detail/PipelineStateCache.cpp"
  "Detail/PipelineStateCache.h"
  "Detail/SwapChain.h"
  "Detail/SwapChain.cpp"
  "Detail/Types.h"
  "Detail/WindowSurface.h"
  "Devices/DebugLayer.cpp"
  "Devices/DebugLayer.h"
  "Devices/DeviceManager.cpp"
  "Graphics.cpp"
  "ImGui/imgui_impl_dx12.cpp"
  "ImGui/imgui_impl_dx12.h"
  "Shaders/EngineShaders.cpp"
  "Shaders/EngineShaders.h"
  "Shaders/ShaderCompiler.cpp"
  "Shaders/ShaderCompiler.h"
  "Texture.cpp"
  "ImGui/ImGuiBackend.cpp"
)

target_sources(
  ${META_MODULE_TARGET}
  PRIVATE
    ${OXYGEN_GFX_D3D12_PRIVATE_SOURCES}
    ${OXYGEN_GFX_D3D12_HEADERS}
  PUBLIC
    FILE_SET HEADERS
    BASE_DIRS "${OXYGEN_SOURCE_DIR}"
    FILES ${OXYGEN_GFX_D3D12_HEADERS}
)

arrange_target_files_for_ide(${META_MODULE_TARGET})

# Silence specific MSVC warnings for D3D12MemAlloc.cpp
if(MSVC)
  set_source_files_properties(
    Allocator/D3D12MemAlloc.cpp
    PROPERTIES
      COMPILE_OPTIONS
        "/wd4100;/wd4324;/wd4505;/wd4189"
  )
endif()

target_compile_options(${META_MODULE_TARGET} PUBLIC ${OXYGEN_COMMON_CXX_FLAGS})
target_compile_definitions(
  ${META_MODULE_TARGET}
  PRIVATE
    $<$<PLATFORM_ID:Windows>:WIN32_LEAN_AND_MEAN>
    $<$<PLATFORM_ID:Windows>:NOMINMAX>
    $<$<NOT:$<BOOL:${BUILD_SHARED_LIBS}>>:OXGN_D3D12_STATIC>
)

target_include_directories(${META_MODULE_TARGET} PRIVATE "${dxc_inc_path}")
target_link_directories(${META_MODULE_TARGET} PRIVATE ${dxc_lib_path})
target_link_libraries(
  ${META_MODULE_TARGET}
  PUBLIC
    oxygen::base
    oxygen::platform
    oxygen::core
    oxygen::graphics-common
    oxygen::imgui
    glm::glm
    nlohmann_json::nlohmann_json
    dxcompiler
    d3dcompiler
    d3d12
    dxgi
    dxguid
)

# Custom command to copy the dxcompiler dll to the build output directory
add_custom_command(
  TARGET ${META_MODULE_TARGET}
  POST_BUILD
  COMMAND
    ${CMAKE_COMMAND} -E copy_if_different "${dxc_bin_path}/dxcompiler.dll"
    "$<TARGET_FILE_DIR:${META_MODULE_TARGET}>"
)

# ------------------------------------------------------------------------------
# Shader compilation
# ------------------------------------------------------------------------------

add_subdirectory("Shaders")

# ------------------------------------------------------------------------------
# Tests
# ------------------------------------------------------------------------------

if(OXYGEN_BUILD_TESTS)
  add_subdirectory("Test")
endif()

# ==============================================================================
# Deployment instructions
# ==============================================================================

oxygen_module_install(
  EXPORT ${META_MODULE_NAMESPACE}
  TARGETS
    ${META_MODULE_TARGET}
)

# dxcompiler is required for this module. Fail configure early if the runtime
# DLL is not present in the expected package location so the user can fix the
# package layout before attempting to build or install.
if(WIN32)
  if(NOT EXISTS "${dxc_bin_path}/dxcompiler.dll")
    message(FATAL_ERROR
      "dxcompiler.dll not found at: ${dxc_bin_path}/dxcompiler.dll\n"
      "Please install the DXC package into ${CMAKE_SOURCE_DIR}/packages/DXC/"
    )
  endif()

  # Install the dxcompiler runtime alongside the module binaries so redistributable
  # packages include the DXC runtime next to the built DLLs.
  install(FILES "${dxc_bin_path}/dxcompiler.dll"
    DESTINATION ${OXYGEN_INSTALL_SHARED}
  )
endif()

asap_pop_module("${META_MODULE_NAME}")
