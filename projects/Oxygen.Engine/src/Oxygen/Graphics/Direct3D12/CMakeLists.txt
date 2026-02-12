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

set(
  OXYGEN_WINPIXEVENTRUNTIME_DIR
  ""
  CACHE PATH
  "Optional root directory containing WinPixEventRuntime (Include/Lib)."
)

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
  "Shaders/EngineShaderCatalog.h"
  "Shaders/ShaderCatalogBuilder.h"
  "Texture.cpp"
  "ImGui/ImGuiBackend.cpp"
)

# Find all HLSL entrypoints under the shader layout.
# Note: some entrypoints (e.g. IBL filtering) live outside Passes/.
file(GLOB_RECURSE HLSL_FILES "${CMAKE_CURRENT_SOURCE_DIR}/*.hlsl")

# Shared include files (HLSL headers)
file(GLOB_RECURSE HLSLI_FILES "${CMAKE_CURRENT_SOURCE_DIR}/*.hlsli")

# Ensure shader sources appear in IDE project trees (Visual Studio filters)
# without becoming standalone compilation units.
set(OXYGEN_D3D12_SHADER_SOURCE_FILES
  ${HLSL_FILES}
  ${HLSLI_FILES}
)
target_sources(
  ${META_MODULE_TARGET}
  PRIVATE
    ${OXYGEN_D3D12_SHADER_SOURCE_FILES}
)
set_source_files_properties(
  ${OXYGEN_D3D12_SHADER_SOURCE_FILES}
  PROPERTIES
    HEADER_FILE_ONLY TRUE
    VS_SETTINGS "ExcludedFromBuild=true"
)
source_group(
  TREE "${CMAKE_CURRENT_SOURCE_DIR}"
  FILES ${OXYGEN_D3D12_SHADER_SOURCE_FILES}
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
    $<$<AND:$<PLATFORM_ID:Windows>,$<BOOL:${USE_PIX}>>:USE_PIX>
)

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
    d3dcompiler
    d3d12
    dxgi
    dxguid
)

if(WIN32 AND USE_PIX)
  # WinPixEventRuntime discovery is intentionally restricted to project-local
  # packages (downloaded by GetWinPixEvent.ps1), optionally overridden by an
  # explicit path.
  set(_oxygen_winpixeventruntime_hint_paths)

  if(OXYGEN_WINPIXEVENTRUNTIME_DIR)
    list(APPEND _oxygen_winpixeventruntime_hint_paths
      "${OXYGEN_WINPIXEVENTRUNTIME_DIR}"
    )
  endif()

  # Repo-local package layout (downloaded by GetWinPixEvent.ps1).
  if(DEFINED OXYGEN_SOURCE_DIR)
    list(APPEND _oxygen_winpixeventruntime_hint_paths
      "${OXYGEN_SOURCE_DIR}/packages/WinPixEventRuntime"
    )
  endif()
  list(APPEND _oxygen_winpixeventruntime_hint_paths
    "${CMAKE_CURRENT_LIST_DIR}/../../../../packages/WinPixEventRuntime"
  )

  find_path(
    OXYGEN_WINPIXEVENTRUNTIME_INCLUDE_DIR
    NAMES pix3.h
    PATHS ${_oxygen_winpixeventruntime_hint_paths}
    PATH_SUFFIXES Include include
  )

  find_library(
    OXYGEN_WINPIXEVENTRUNTIME_LIBRARY
    NAMES WinPixEventRuntime
    PATHS ${_oxygen_winpixeventruntime_hint_paths}
    PATH_SUFFIXES Lib/x64 lib/x64 Lib lib
  )

  find_file(
    OXYGEN_WINPIXEVENTRUNTIME_DLL
    NAMES WinPixEventRuntime.dll
    PATHS ${_oxygen_winpixeventruntime_hint_paths}
    PATH_SUFFIXES Bin/x64 bin/x64 Bin bin
  )

  if(
    OXYGEN_WINPIXEVENTRUNTIME_INCLUDE_DIR
    AND OXYGEN_WINPIXEVENTRUNTIME_LIBRARY
    AND OXYGEN_WINPIXEVENTRUNTIME_DLL
  )
    target_include_directories(
      ${META_MODULE_TARGET}
      PRIVATE
        "${OXYGEN_WINPIXEVENTRUNTIME_INCLUDE_DIR}"
    )

    target_link_libraries(
      ${META_MODULE_TARGET}
      PRIVATE
        "${OXYGEN_WINPIXEVENTRUNTIME_LIBRARY}"
    )

    # Ensure the runtime DLL is available next to built binaries.
    add_custom_command(
      TARGET ${META_MODULE_TARGET}
      POST_BUILD
      COMMAND
        ${CMAKE_COMMAND} -E copy_if_different
          "${OXYGEN_WINPIXEVENTRUNTIME_DLL}"
          "$<TARGET_FILE_DIR:${META_MODULE_TARGET}>"
      VERBATIM
    )

    # Install the runtime DLL alongside executables.
    if(DEFINED OXYGEN_INSTALL_BIN)
      install(
        FILES "${OXYGEN_WINPIXEVENTRUNTIME_DLL}"
        DESTINATION ${OXYGEN_INSTALL_BIN}
        COMPONENT "${META_MODULE_NAME}_runtime"
      )
    endif()

    message(STATUS
      "WinPixEventRuntime enabled for ${META_MODULE_NAME}: "
      "${OXYGEN_WINPIXEVENTRUNTIME_INCLUDE_DIR}"
    )
  else()
    message(WARNING
      "USE_PIX is ON but WinPixEventRuntime was not found. "
      "\n  - Recommended: run GetWinPixEvent.bat from the repo root "
      "(it installs to packages/WinPixEventRuntime by default)."
      "\n  - Alternative: set OXYGEN_WINPIXEVENTRUNTIME_DIR to a folder "
      "containing Include/pix3.h, Lib/x64/WinPixEventRuntime.lib, and "
      "Bin/x64/WinPixEventRuntime.dll."
    )
  endif()
endif()

# ------------------------------------------------------------------------------
# Shader compilation
# ------------------------------------------------------------------------------

add_subdirectory("Tools/ShaderBake")

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
