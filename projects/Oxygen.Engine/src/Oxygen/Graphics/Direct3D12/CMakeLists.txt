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
      "OXYGEN_D3D12_EXPORTS"
    OUTPUT_NAME
      $<IF:$<BOOL:${BUILD_SHARED_LIBS}>,${META_MODULE_NAME},${META_MODULE_NAME}-static>
    VERSION
      ${META_VERSION}
    SOVERSION
      ${META_VERSION_MAJOR}
    PROJECT_LABEL
      ${META_MODULE_NAME}
)

target_sources(
  ${META_MODULE_TARGET}
  PRIVATE
    "Buffer.cpp"
    "CommandList.cpp"
    "CommandQueue.cpp"
    "CommandRecorder.cpp"
    "Framebuffer.cpp"
    "Graphics.cpp"
    "RenderController.cpp"
    "Texture.cpp"
    "Allocator/D3D12MemAlloc.cpp"
    "Detail/Converters.cpp"
    "Detail/Converters.h"
    "Detail/FormatUtils.cpp"
    "Detail/FormatUtils.h"
    "Detail/PipelineStateCache.h"
    "Detail/PipelineStateCache.cpp"
    "Detail/SwapChain.cpp"
    "Detail/SwapChain.h"
    "Detail/Types.h"
    "Detail/WindowSurface.h"
    "Detail/dx12_utils.h"
    "Devices/DebugLayer.cpp"
    "Devices/DebugLayer.h"
    "Devices/DeviceManager.cpp"
    # "ImGui/ImGuiModule.cpp"
    "ImGui/imgui_impl_dx12.h"
    "Shaders/EngineShaders.cpp"
    "Shaders/EngineShaders.h"
    "Shaders/ShaderCompiler.cpp"
    "Shaders/ShaderCompiler.h"
    "Bindless/D3D12HeapAllocationStrategy.h"
    "Bindless/D3D12HeapAllocationStrategy.cpp"
    "Bindless/DescriptorHeapSegment.h"
    "Bindless/DescriptorHeapSegment.cpp"
    "Bindless/DescriptorAllocator.h"
    "Bindless/DescriptorAllocator.cpp"
  PUBLIC
    FILE_SET HEADERS
    BASE_DIRS "${OXYGEN_SOURCE_DIR}"
    FILES
      "api_export.h"
      "Buffer.h"
      "CommandList.h"
      "CommandQueue.h"
      "CommandRecorder.h"
      "Constants.h"
      "Framebuffer.h"
      "GraphicResource.h"
      "Graphics.h"
      "RenderController.h"
      "Texture.h"
      "Allocator/D3D12MemAlloc.h"
      "Devices/DeviceManager.h"
  # "ImGui/ImGuiModule.h"
)

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
    $<$<NOT:$<BOOL:${BUILD_SHARED_LIBS}>>:OXYGEN_D3D12_STATIC>
)

target_include_directories(${META_MODULE_TARGET} PRIVATE "${dxc_inc_path}")
target_link_directories(${META_MODULE_TARGET} PRIVATE ${dxc_lib_path})
target_link_libraries(
  ${META_MODULE_TARGET}
  PUBLIC
    oxygen::base
    oxygen::platform
    oxygen::graphics-common
    oxygen::imgui
    glm::glm
    nlohmann_json::nlohmann_json
    dxcompiler
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

asap_pop_module("${META_MODULE_NAME}")
