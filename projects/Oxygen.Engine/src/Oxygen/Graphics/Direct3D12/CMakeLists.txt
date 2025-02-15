# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause).
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
    "D3D12MemAlloc.cpp"
    "DebugLayer.cpp"
    "Fence.cpp"
    "Graphics.cpp"
    "MemoryBlock.cpp"
    "Renderer.cpp"
    "ShaderCompiler.cpp"
    "Texture.cpp"
    "WindowSurface.cpp"
    "Detail/DescriptorHeap.cpp"
    "Detail/DescriptorHeap.h"
    "Detail/dx12_utils.cpp"
    "Detail/dx12_utils.h"
    "Detail/WindowSurfaceImpl.cpp"
    "Detail/WindowSurfaceImpl.h"
    "ImGui/ImGuiModule.cpp"
    "ImGui/imgui_impl_dx12.h"
    "ImGui/imgui_impl_dx12.cpp"
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
      "D3D12MemAlloc.h"
      "D3DResource.h"
      "DebugLayer.h"
      "DeferredObjectRelease.h"
      "Fence.h"
      "Forward.h"
      "Graphics.h"
      "MemoryBlock.h"
      "Renderer.h"
      "RenderTarget.h"
      "ShaderCompiler.h"
      "Texture.h"
      "WindowSurface.h"
      "ImGui/ImGuiModule.h"
)

target_compile_options(${META_MODULE_TARGET} PUBLIC ${OXYGEN_COMMON_CXX_FLAGS})
target_compile_definitions(
  ${META_MODULE_TARGET}
  PRIVATE
    $<$<PLATFORM_ID:Windows>:WIN32_LEAN_AND_MEAN>
    $<$<PLATFORM_ID:Windows>:NOMINMAX>
    $<$<NOT:$<BOOL:${BUILD_SHARED_LIBS}>>:OXYGEN_D3D12_STATIC>
)

target_link_directories(${META_MODULE_TARGET} PRIVATE ${dxc_lib_path})
target_link_libraries(
  ${META_MODULE_TARGET}
  PUBLIC
    oxygen::base
    oxygen::platform
    oxygen::graphics-common
    oxygen::imgui
    glm::glm
    d3d12
    dxgi
    dxguid
    dxcompiler
)

# ------------------------------------------------------------------------------
# Shader compilation
# ------------------------------------------------------------------------------

set(dxc_bin_path "${CMAKE_SOURCE_DIR}/packages/DXC/bin/${dxc_arch}")
set(dxc_exe "${dxc_bin_path}/dxc.exe")
message(STATUS "Using DXC Compiler: ${dxc_exe}")

# Function to add custom shader compilation commands
function(add_shader_compilation target shader_file)
  get_filename_component(shader_dir ${shader_file} DIRECTORY)
  get_filename_component(shader_name ${shader_file} NAME_WE)

  # Vertex shader compilation command
  add_custom_command(
    OUTPUT
      "${shader_dir}/${shader_name}_vs.cso"
    COMMAND
      ${CMAKE_COMMAND} -E echo "Compiling vertex shader: ${shader_file}"
    COMMAND
      ${dxc_exe} -T vs_6_8 -E VS -Fo "${shader_dir}/${shader_name}_vs.cso"
      "${shader_file}"
    DEPENDS
      ${shader_file}
    COMMENT "Using DXC Compiler ${dxc_exe} for vertex shader"
  )

  # Pixel shader compilation command
  add_custom_command(
    OUTPUT
      "${shader_dir}/${shader_name}_ps.cso"
    COMMAND
      ${CMAKE_COMMAND} -E echo "Compiling pixel shader: ${shader_file}"
    COMMAND
      ${dxc_exe} -T ps_6_8 -E PS -Fo "${shader_dir}/${shader_name}_ps.cso"
      "${shader_file}"
    DEPENDS
      ${shader_file}
    COMMENT "Using DXC Compiler ${dxc_exe} for pixel shader"
  )

  # Add the compiled shaders as dependencies to the target
  add_custom_target(
    ${target}_shaders
    ALL
    DEPENDS
      "${shader_dir}/${shader_name}_vs.cso"
      "${shader_dir}/${shader_name}_ps.cso"
  )
endfunction()

# Find all HLSL files and add shader compilation commands
file(
  GLOB_RECURSE HLSL_FILES
  "${CMAKE_SOURCE_DIR}/src/Oxygen/Graphics/Direct3D12/*.hlsl"
)
foreach(HLSL_FILE ${HLSL_FILES})
  add_shader_compilation(${META_MODULE_TARGET} ${HLSL_FILE})
endforeach()

# ------------------------------------------------------------------------------
# Tests
# ------------------------------------------------------------------------------

#if(OXYGEN_BUILD_TESTS)
#  add_subdirectory("Test")
#endif()

# ==============================================================================
# Deployment instructions
# ==============================================================================

oxygen_module_install(
  EXPORT ${META_MODULE_NAMESPACE}
  TARGETS
    ${META_MODULE_TARGET}
)

asap_pop_module("${META_MODULE_NAME}")
