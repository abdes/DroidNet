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
set(
  OXYGEN_RENDERDOC_DIR
  ""
  CACHE PATH
  "Optional root directory containing RenderDoc package files (Include/Bin)."
)
set(
  OXYGEN_AFTERMATH_DIR
  ""
  CACHE PATH
  "Optional root directory containing Nsight Aftermath SDK (Include/Lib/Bin)."
)
set(
  OXYGEN_PIX_DIR
  ""
  CACHE PATH
  "Optional PIX install root used to locate WinPixGpuCapturer.dll and WinPixTimingCapturer.dll."
)
set(
  OXYGEN_PIX_GPU_CAPTURER_DLL
  ""
  CACHE FILEPATH
  "Optional explicit path to WinPixGpuCapturer.dll."
)
set(
  OXYGEN_PIX_TIMING_CAPTURER_DLL
  ""
  CACHE FILEPATH
  "Optional explicit path to WinPixTimingCapturer.dll."
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
  "PixFrameCaptureController.h"
  "RenderDocFrameCaptureController.h"
  "ReadbackManager.h"
  "Texture.h"
  "TimestampQueryBackend.h"
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
  "Detail/TextureReadback.cpp"
  "Detail/TextureReadback.h"
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
  "Devices/AftermathTracker.cpp"
  "Devices/AftermathTracker.h"
  "Devices/DeviceManager.cpp"
  "Graphics.cpp"
  "PixFrameCaptureController.cpp"
  "RenderDocFrameCaptureController.cpp"
  "ReadbackManager.cpp"
  "ImGui/imgui_impl_dx12.cpp"
  "ImGui/imgui_impl_dx12.h"
  "Shaders/EngineShaders.cpp"
  "Shaders/EngineShaders.h"
  "Shaders/EngineShaderCatalog.h"
  "Shaders/ShaderCatalogBuilder.h"
  "TimestampQueryBackend.cpp"
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
set(
  OXYGEN_D3D12_SHADER_SOURCE_FILES
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
    HEADER_FILE_ONLY
      TRUE
    VS_SETTINGS
      "ExcludedFromBuild=true"
)
source_group(
  TREE "${CMAKE_CURRENT_SOURCE_DIR}"
  FILES
    ${OXYGEN_D3D12_SHADER_SOURCE_FILES}
)

target_sources(
  ${META_MODULE_TARGET}
  PRIVATE
    ${OXYGEN_GFX_D3D12_PRIVATE_SOURCES}
    ${OXYGEN_GFX_D3D12_HEADERS}
  PUBLIC
    FILE_SET HEADERS
      BASE_DIRS
        "${OXYGEN_SOURCE_DIR}"
      FILES
        ${OXYGEN_GFX_D3D12_HEADERS}
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

set(OXYGEN_D3D12_PIX_MARKERS_ENABLED OFF)
set(OXYGEN_D3D12_PIX_GPU_CAPTURE_AVAILABLE OFF)
set(OXYGEN_D3D12_PIX_TIMING_CAPTURE_AVAILABLE OFF)
set(OXYGEN_D3D12_PIX_UI_AVAILABLE OFF)
set(OXYGEN_D3D12_RENDERDOC_ENABLED OFF)
set(OXYGEN_D3D12_RENDERDOC_DLL_DEPLOYABLE OFF)
set(OXYGEN_D3D12_AFTERMATH_ENABLED OFF)
set(OXYGEN_D3D12_AFTERMATH_DLL_DEPLOYABLE OFF)
set(OXYGEN_RENDERDOC_INCLUDE_DIR "")
set(OXYGEN_AFTERMATH_INCLUDE_DIR "")
set(OXYGEN_PIX_INSTALL_ROOT_RESOLVED "")

if(WIN32)
  # PIX marker/runtime discovery prefers an explicit WinPixEventRuntime package
  # root and then falls back to the repo-local package layout populated by:
  #  - GetPIX.bat
  #  - GetPIX.ps1
  set(_oxygen_winpixeventruntime_hint_paths)

  if(OXYGEN_WINPIXEVENTRUNTIME_DIR)
    list(
      APPEND _oxygen_winpixeventruntime_hint_paths
      "${OXYGEN_WINPIXEVENTRUNTIME_DIR}"
    )
  endif()

  # Repo-local package layout (provisioned by GetPIX.bat/.ps1).
  if(DEFINED OXYGEN_SOURCE_DIR)
    list(
      APPEND _oxygen_winpixeventruntime_hint_paths
      "${OXYGEN_SOURCE_DIR}/packages/WinPixEventRuntime"
    )
  endif()
  list(
    APPEND _oxygen_winpixeventruntime_hint_paths
    "${CMAKE_CURRENT_LIST_DIR}/../../../../packages/WinPixEventRuntime"
  )

  find_path(
    OXYGEN_WINPIXEVENTRUNTIME_INCLUDE_DIR
    NAMES
      pix3.h
    PATHS
      ${_oxygen_winpixeventruntime_hint_paths}
    PATH_SUFFIXES
      Include
      include
  )

  find_library(
    OXYGEN_WINPIXEVENTRUNTIME_LIBRARY
    NAMES
      WinPixEventRuntime
    PATHS
      ${_oxygen_winpixeventruntime_hint_paths}
    PATH_SUFFIXES
      Lib/x64
      lib/x64
      Lib
      lib
  )

  find_file(
    OXYGEN_WINPIXEVENTRUNTIME_DLL
    NAMES
      WinPixEventRuntime.dll
    PATHS
      ${_oxygen_winpixeventruntime_hint_paths}
    PATH_SUFFIXES
      Bin/x64
      bin/x64
      Bin
      bin
  )

  if(
    OXYGEN_WINPIXEVENTRUNTIME_INCLUDE_DIR
    AND
      OXYGEN_WINPIXEVENTRUNTIME_LIBRARY
    AND
      OXYGEN_WINPIXEVENTRUNTIME_DLL
  )
    set(OXYGEN_D3D12_PIX_MARKERS_ENABLED ON)
  endif()

  # PIX GPU capture discovery prefers explicit DLL overrides, then an explicit
  # PIX install root, then the common Program Files install roots.
  set(_oxygen_pix_hint_paths)
  if(OXYGEN_PIX_DIR)
    list(APPEND _oxygen_pix_hint_paths "${OXYGEN_PIX_DIR}")
  endif()
  list(
    APPEND _oxygen_pix_hint_paths
    "C:/Program Files/Microsoft PIX"
    "C:/Program Files (x86)/Microsoft PIX"
  )

  set(_oxygen_pix_install_hint_paths)
  foreach(_oxygen_pix_hint IN LISTS _oxygen_pix_hint_paths)
    if(EXISTS "${_oxygen_pix_hint}")
      list(APPEND _oxygen_pix_install_hint_paths "${_oxygen_pix_hint}")
      file(
        GLOB _oxygen_pix_version_dirs
        LIST_DIRECTORIES true
        "${_oxygen_pix_hint}/*"
      )
      foreach(_oxygen_pix_version_dir IN LISTS _oxygen_pix_version_dirs)
        if(IS_DIRECTORY "${_oxygen_pix_version_dir}")
          list(
            APPEND _oxygen_pix_install_hint_paths
            "${_oxygen_pix_version_dir}"
          )
        endif()
      endforeach()
    endif()
  endforeach()

  unset(OXYGEN_PIX_GPU_CAPTURER_DLL_RESOLVED)
  unset(OXYGEN_PIX_TIMING_CAPTURER_DLL_RESOLVED)
  unset(OXYGEN_PIX_UI_EXECUTABLE)
  unset(OXYGEN_PIX_GPU_CAPTURER_DLL_RESOLVED CACHE)
  unset(OXYGEN_PIX_TIMING_CAPTURER_DLL_RESOLVED CACHE)
  unset(OXYGEN_PIX_UI_EXECUTABLE CACHE)

  if(OXYGEN_PIX_GPU_CAPTURER_DLL AND EXISTS "${OXYGEN_PIX_GPU_CAPTURER_DLL}")
    set(
      OXYGEN_PIX_GPU_CAPTURER_DLL_RESOLVED
      "${OXYGEN_PIX_GPU_CAPTURER_DLL}"
      CACHE FILEPATH
      "Resolved path to WinPixGpuCapturer.dll"
      FORCE
    )
  else()
    find_file(
      OXYGEN_PIX_GPU_CAPTURER_DLL_RESOLVED
      NAMES
        WinPixGpuCapturer.dll
      PATHS
        ${_oxygen_pix_install_hint_paths}
    )
  endif()

  if(
    OXYGEN_PIX_TIMING_CAPTURER_DLL
    AND
      EXISTS
        "${OXYGEN_PIX_TIMING_CAPTURER_DLL}"
  )
    set(
      OXYGEN_PIX_TIMING_CAPTURER_DLL_RESOLVED
      "${OXYGEN_PIX_TIMING_CAPTURER_DLL}"
      CACHE FILEPATH
      "Resolved path to WinPixTimingCapturer.dll"
      FORCE
    )
  else()
    find_file(
      OXYGEN_PIX_TIMING_CAPTURER_DLL_RESOLVED
      NAMES
        WinPixTimingCapturer.dll
      PATHS
        ${_oxygen_pix_install_hint_paths}
    )
  endif()

  find_file(
    OXYGEN_PIX_UI_EXECUTABLE
    NAMES
      pixtool.exe
      PIX.exe
    PATHS
      ${_oxygen_pix_install_hint_paths}
  )

  if(OXYGEN_PIX_DIR AND EXISTS "${OXYGEN_PIX_DIR}")
    set(OXYGEN_PIX_INSTALL_ROOT_RESOLVED "${OXYGEN_PIX_DIR}")
  elseif(OXYGEN_PIX_GPU_CAPTURER_DLL_RESOLVED)
    get_filename_component(
      OXYGEN_PIX_INSTALL_ROOT_RESOLVED
      "${OXYGEN_PIX_GPU_CAPTURER_DLL_RESOLVED}"
      DIRECTORY
    )
  elseif(OXYGEN_PIX_TIMING_CAPTURER_DLL_RESOLVED)
    get_filename_component(
      OXYGEN_PIX_INSTALL_ROOT_RESOLVED
      "${OXYGEN_PIX_TIMING_CAPTURER_DLL_RESOLVED}"
      DIRECTORY
    )
  endif()

  if(OXYGEN_D3D12_PIX_MARKERS_ENABLED AND OXYGEN_PIX_GPU_CAPTURER_DLL_RESOLVED)
    set(OXYGEN_D3D12_PIX_GPU_CAPTURE_AVAILABLE ON)
  endif()

  if(
    OXYGEN_D3D12_PIX_MARKERS_ENABLED
    AND
      OXYGEN_PIX_TIMING_CAPTURER_DLL_RESOLVED
  )
    set(OXYGEN_D3D12_PIX_TIMING_CAPTURE_AVAILABLE ON)
  endif()

  if(OXYGEN_PIX_UI_EXECUTABLE)
    set(OXYGEN_D3D12_PIX_UI_AVAILABLE ON)
  endif()

  # RenderDoc discovery checks default Windows install locations first, then
  # project-local packages.
  # Preferred install path is via either:
  #  - GetRenderDoc.bat
  #  - GetRenderDoc.ps1
  # Both scripts install to packages/RenderDoc by default.
  set(_oxygen_renderdoc_hint_paths)

  # Common RenderDoc default install locations on Windows.
  list(
    APPEND _oxygen_renderdoc_hint_paths
    "C:/Program Files/RenderDoc"
    "C:/Program Files (x86)/RenderDoc"
  )

  if(OXYGEN_RENDERDOC_DIR)
    list(APPEND _oxygen_renderdoc_hint_paths "${OXYGEN_RENDERDOC_DIR}")
  endif()

  # Repo-local package layout (downloaded by GetRenderDoc.bat/.ps1).
  if(DEFINED OXYGEN_SOURCE_DIR)
    list(
      APPEND _oxygen_renderdoc_hint_paths
      "${OXYGEN_SOURCE_DIR}/packages/RenderDoc"
    )
  endif()
  list(
    APPEND _oxygen_renderdoc_hint_paths
    "${CMAKE_CURRENT_LIST_DIR}/../../../../packages/RenderDoc"
  )

  # Reset cached discovery values so updates to probing logic are honored on
  # subsequent re-configures in existing build directories.
  unset(OXYGEN_RENDERDOC_HEADER CACHE)
  unset(OXYGEN_RENDERDOC_DLL CACHE)
  unset(OXYGEN_RENDERDOC_INCLUDE_DIR CACHE)

  # Prefer the install root first (for example C:/Program Files/RenderDoc),
  # then common include layouts.
  find_file(
    OXYGEN_RENDERDOC_HEADER
    NAMES
      renderdoc_app.h
    PATHS
      ${_oxygen_renderdoc_hint_paths}
  )

  if(NOT OXYGEN_RENDERDOC_HEADER)
    find_file(
      OXYGEN_RENDERDOC_HEADER
      NAMES
        renderdoc_app.h
      PATHS
        ${_oxygen_renderdoc_hint_paths}
      PATH_SUFFIXES
        Include
        include
        api/app
        renderdoc/api/app
    )
  endif()

  # Prefer 64-bit/default runtime DLL locations, then fall back to x86.
  find_file(
    OXYGEN_RENDERDOC_DLL
    NAMES
      renderdoc.dll
    PATHS
      ${_oxygen_renderdoc_hint_paths}
  )

  if(NOT OXYGEN_RENDERDOC_DLL)
    find_file(
      OXYGEN_RENDERDOC_DLL
      NAMES
        renderdoc.dll
      PATHS
        ${_oxygen_renderdoc_hint_paths}
      PATH_SUFFIXES
        x64
        Bin/x64
        bin/x64
        Bin
        bin
        x86
    )
  endif()

  if(OXYGEN_RENDERDOC_HEADER)
    get_filename_component(
      OXYGEN_RENDERDOC_INCLUDE_DIR
      "${OXYGEN_RENDERDOC_HEADER}"
      DIRECTORY
    )
    set(
      OXYGEN_RENDERDOC_INCLUDE_DIR
      "${OXYGEN_RENDERDOC_INCLUDE_DIR}"
      CACHE PATH
      "Resolved directory containing renderdoc_app.h"
      FORCE
    )
    set(OXYGEN_D3D12_RENDERDOC_ENABLED ON)
  endif()

  if(OXYGEN_RENDERDOC_INCLUDE_DIR AND OXYGEN_RENDERDOC_DLL)
    set(OXYGEN_D3D12_RENDERDOC_DLL_DEPLOYABLE ON)
  endif()

  # Nsight Aftermath discovery checks default Windows install locations first,
  # then project-local packages.
  # Preferred install path is via either:
  #  - GetAftermath.bat
  #  - GetAftermath.ps1
  # Both scripts install to packages/NsightAftermath by default.
  set(_oxygen_aftermath_hint_paths)

  # Common Nsight Aftermath SDK install locations on Windows.
  list(
    APPEND _oxygen_aftermath_hint_paths
    "C:/Program Files/NVIDIA Corporation/Nsight Aftermath SDK"
    "C:/Program Files/NVIDIA Corporation/NVIDIA Nsight Aftermath SDK"
    "C:/Program Files (x86)/NVIDIA Corporation/Nsight Aftermath SDK"
  )

  if(OXYGEN_AFTERMATH_DIR)
    list(APPEND _oxygen_aftermath_hint_paths "${OXYGEN_AFTERMATH_DIR}")
  endif()

  # Repo-local package layout (downloaded by GetAftermath.bat/.ps1).
  if(DEFINED OXYGEN_SOURCE_DIR)
    list(
      APPEND _oxygen_aftermath_hint_paths
      "${OXYGEN_SOURCE_DIR}/packages/NsightAftermath"
    )
  endif()
  list(
    APPEND _oxygen_aftermath_hint_paths
    "${CMAKE_CURRENT_LIST_DIR}/../../../../packages/NsightAftermath"
  )

  unset(OXYGEN_AFTERMATH_HEADER CACHE)
  unset(OXYGEN_AFTERMATH_LIBRARY CACHE)
  unset(OXYGEN_AFTERMATH_DLL CACHE)
  unset(OXYGEN_AFTERMATH_INCLUDE_DIR CACHE)

  find_file(
    OXYGEN_AFTERMATH_HEADER
    NAMES
      GFSDK_Aftermath_DX12.h
      GFSDK_Aftermath.h
    PATHS
      ${_oxygen_aftermath_hint_paths}
  )

  if(NOT OXYGEN_AFTERMATH_HEADER)
    find_file(
      OXYGEN_AFTERMATH_HEADER
      NAMES
        GFSDK_Aftermath_DX12.h
        GFSDK_Aftermath.h
      PATHS
        ${_oxygen_aftermath_hint_paths}
      PATH_SUFFIXES
        Include
        include
        sdk
        include/Aftermath
    )
  endif()

  find_library(
    OXYGEN_AFTERMATH_LIBRARY
    NAMES
      GFSDK_Aftermath_Lib.x64
      GFSDK_Aftermath_Lib
    PATHS
      ${_oxygen_aftermath_hint_paths}
    PATH_SUFFIXES
      Lib/x64
      lib/x64
      Lib
      lib
      .
  )

  find_file(
    OXYGEN_AFTERMATH_DLL
    NAMES
      GFSDK_Aftermath_Lib.x64.dll
      GFSDK_Aftermath_Lib.dll
    PATHS
      ${_oxygen_aftermath_hint_paths}
    PATH_SUFFIXES
      Bin/x64
      bin/x64
      Bin
      bin
      .
  )

  if(OXYGEN_AFTERMATH_HEADER)
    get_filename_component(
      OXYGEN_AFTERMATH_INCLUDE_DIR
      "${OXYGEN_AFTERMATH_HEADER}"
      DIRECTORY
    )
    set(
      OXYGEN_AFTERMATH_INCLUDE_DIR
      "${OXYGEN_AFTERMATH_INCLUDE_DIR}"
      CACHE PATH
      "Resolved directory containing GFSDK_Aftermath headers"
      FORCE
    )
  endif()

  if(
    OXYGEN_AFTERMATH_INCLUDE_DIR
    AND
      OXYGEN_AFTERMATH_LIBRARY
    AND
      OXYGEN_AFTERMATH_DLL
  )
    set(OXYGEN_D3D12_AFTERMATH_ENABLED ON)
    set(OXYGEN_D3D12_AFTERMATH_DLL_DEPLOYABLE ON)
  endif()
endif()

if(OXYGEN_D3D12_RENDERDOC_DLL_DEPLOYABLE)
  set(_oxygen_renderdoc_dll_path_literal "${OXYGEN_RENDERDOC_DLL}")
  file(
    TO_CMAKE_PATH
    "${_oxygen_renderdoc_dll_path_literal}"
    _oxygen_renderdoc_dll_path_literal
  )
endif()

if(OXYGEN_D3D12_AFTERMATH_DLL_DEPLOYABLE)
  set(_oxygen_aftermath_dll_path_literal "${OXYGEN_AFTERMATH_DLL}")
  file(
    TO_CMAKE_PATH
    "${_oxygen_aftermath_dll_path_literal}"
    _oxygen_aftermath_dll_path_literal
  )
endif()

if(OXYGEN_PIX_INSTALL_ROOT_RESOLVED)
  set(_oxygen_pix_install_root_literal "${OXYGEN_PIX_INSTALL_ROOT_RESOLVED}")
  file(
    TO_CMAKE_PATH
    "${_oxygen_pix_install_root_literal}"
    _oxygen_pix_install_root_literal
  )
endif()

if(OXYGEN_PIX_GPU_CAPTURER_DLL_RESOLVED)
  set(
    _oxygen_pix_gpu_capturer_dll_literal
    "${OXYGEN_PIX_GPU_CAPTURER_DLL_RESOLVED}"
  )
  file(
    TO_CMAKE_PATH
    "${_oxygen_pix_gpu_capturer_dll_literal}"
    _oxygen_pix_gpu_capturer_dll_literal
  )
endif()

if(OXYGEN_PIX_TIMING_CAPTURER_DLL_RESOLVED)
  set(
    _oxygen_pix_timing_capturer_dll_literal
    "${OXYGEN_PIX_TIMING_CAPTURER_DLL_RESOLVED}"
  )
  file(
    TO_CMAKE_PATH
    "${_oxygen_pix_timing_capturer_dll_literal}"
    _oxygen_pix_timing_capturer_dll_literal
  )
endif()

target_compile_definitions(
  ${META_MODULE_TARGET}
  PRIVATE
    $<$<PLATFORM_ID:Windows>:WIN32_LEAN_AND_MEAN>
    $<$<PLATFORM_ID:Windows>:NOMINMAX>
    $<$<NOT:$<BOOL:${BUILD_SHARED_LIBS}>>:OXGN_D3D12_STATIC>
    $<$<AND:$<PLATFORM_ID:Windows>,$<BOOL:${OXYGEN_D3D12_PIX_MARKERS_ENABLED}>>:USE_PIX>
    $<$<AND:$<PLATFORM_ID:Windows>,$<BOOL:${OXYGEN_D3D12_PIX_GPU_CAPTURE_AVAILABLE}>>:OXYGEN_PIX_GPU_CAPTURE_AVAILABLE=1>
    $<$<AND:$<PLATFORM_ID:Windows>,$<BOOL:${OXYGEN_D3D12_PIX_TIMING_CAPTURE_AVAILABLE}>>:OXYGEN_PIX_TIMING_CAPTURE_AVAILABLE=1>
    $<$<AND:$<PLATFORM_ID:Windows>,$<BOOL:${OXYGEN_D3D12_PIX_UI_AVAILABLE}>>:OXYGEN_PIX_UI_AVAILABLE=1>
    $<$<AND:$<PLATFORM_ID:Windows>,$<BOOL:${OXYGEN_PIX_INSTALL_ROOT_RESOLVED}>>:OXYGEN_PIX_INSTALL_ROOT_PATH="${_oxygen_pix_install_root_literal}">
    $<$<AND:$<PLATFORM_ID:Windows>,$<BOOL:${OXYGEN_PIX_GPU_CAPTURER_DLL_RESOLVED}>>:OXYGEN_PIX_GPU_CAPTURER_DLL_PATH="${_oxygen_pix_gpu_capturer_dll_literal}">
    $<$<AND:$<PLATFORM_ID:Windows>,$<BOOL:${OXYGEN_PIX_TIMING_CAPTURER_DLL_RESOLVED}>>:OXYGEN_PIX_TIMING_CAPTURER_DLL_PATH="${_oxygen_pix_timing_capturer_dll_literal}">
    $<$<AND:$<PLATFORM_ID:Windows>,$<BOOL:${OXYGEN_D3D12_RENDERDOC_ENABLED}>>:USE_RENDERDOC>
    $<$<AND:$<PLATFORM_ID:Windows>,$<BOOL:${OXYGEN_D3D12_RENDERDOC_DLL_DEPLOYABLE}>>:OXYGEN_RENDERDOC_DLL_PATH="${_oxygen_renderdoc_dll_path_literal}">
    $<$<AND:$<PLATFORM_ID:Windows>,$<BOOL:${OXYGEN_D3D12_AFTERMATH_ENABLED}>>:USE_AFTERMATH>
    $<$<AND:$<PLATFORM_ID:Windows>,$<BOOL:${OXYGEN_D3D12_AFTERMATH_DLL_DEPLOYABLE}>>:OXYGEN_AFTERMATH_DLL_PATH="${_oxygen_aftermath_dll_path_literal}">
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

if(WIN32)
  if(OXYGEN_D3D12_PIX_MARKERS_ENABLED)
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
        ${CMAKE_COMMAND} -E copy_if_different "${OXYGEN_WINPIXEVENTRUNTIME_DLL}"
        "$<TARGET_FILE_DIR:${META_MODULE_TARGET}>"
      VERBATIM
    )

    # Install the runtime DLL alongside executables.
    if(DEFINED OXYGEN_INSTALL_BIN)
      install(
        FILES
          "${OXYGEN_WINPIXEVENTRUNTIME_DLL}"
        DESTINATION ${OXYGEN_INSTALL_BIN}
        COMPONENT "${META_MODULE_NAME}_runtime"
      )
    endif()

    message(
      STATUS
      "WinPixEventRuntime enabled for ${META_MODULE_NAME}: "
      "${OXYGEN_WINPIXEVENTRUNTIME_INCLUDE_DIR}"
    )
    if(OXYGEN_D3D12_PIX_GPU_CAPTURE_AVAILABLE)
      message(
        STATUS
        "PIX GPU capture discovery enabled for ${META_MODULE_NAME}: "
        "${OXYGEN_PIX_GPU_CAPTURER_DLL_RESOLVED}"
      )
    else()
      message(
        STATUS
        "PIX markers enabled for ${META_MODULE_NAME}, but no PIX GPU capturer "
        "was discovered at configure time. Attached-only runtime capture may "
        "still work when PIX injects the capturer into the process."
      )
    endif()
    if(OXYGEN_D3D12_PIX_TIMING_CAPTURE_AVAILABLE)
      message(
        STATUS
        "PIX timing capturer discovery enabled for ${META_MODULE_NAME}: "
        "${OXYGEN_PIX_TIMING_CAPTURER_DLL_RESOLVED}"
      )
    endif()
  elseif(DEFINED USE_PIX AND USE_PIX)
    message(
      WARNING
      "USE_PIX is ON but WinPixEventRuntime was not found. "
      "\n  - Recommended: run GetPIX.bat or GetPIX.ps1 from the repo root "
      "(it provisions WinPixEventRuntime into packages/WinPixEventRuntime and validates the installed PIX tool)."
      "\n  - Alternative: set OXYGEN_WINPIXEVENTRUNTIME_DIR to a folder "
      "containing Include/pix3.h, Lib/x64/WinPixEventRuntime.lib, and "
      "Bin/x64/WinPixEventRuntime.dll."
    )
  endif()

  if(OXYGEN_D3D12_RENDERDOC_ENABLED)
    target_include_directories(
      ${META_MODULE_TARGET}
      PRIVATE
        "${OXYGEN_RENDERDOC_INCLUDE_DIR}"
    )

    if(OXYGEN_D3D12_RENDERDOC_DLL_DEPLOYABLE)
      # Ensure the RenderDoc runtime DLL is available next to built binaries.
      add_custom_command(
        TARGET ${META_MODULE_TARGET}
        POST_BUILD
        COMMAND
          ${CMAKE_COMMAND} -E copy_if_different "${OXYGEN_RENDERDOC_DLL}"
          "$<TARGET_FILE_DIR:${META_MODULE_TARGET}>"
        VERBATIM
      )

      # Install the RenderDoc runtime DLL alongside executables.
      if(DEFINED OXYGEN_INSTALL_BIN)
        install(
          FILES
            "${OXYGEN_RENDERDOC_DLL}"
          DESTINATION ${OXYGEN_INSTALL_BIN}
          COMPONENT "${META_MODULE_NAME}_runtime"
        )
      endif()

      message(
        STATUS
        "RenderDoc API enabled for ${META_MODULE_NAME}: "
        "${OXYGEN_RENDERDOC_INCLUDE_DIR}"
      )
      message(
        STATUS
        "RenderDoc runtime deployment enabled for ${META_MODULE_NAME}: "
        "${OXYGEN_RENDERDOC_DLL}"
      )
    else()
      message(
        STATUS
        "RenderDoc API enabled for ${META_MODULE_NAME}, but renderdoc.dll "
        "was not found in package paths. Runtime loading will still be "
        "optional and non-fatal."
      )
    endif()
  elseif(DEFINED USE_RENDERDOC AND USE_RENDERDOC)
    message(
      WARNING
      "USE_RENDERDOC is ON but renderdoc_app.h was not found. "
      "\n  - Recommended: run GetRenderDoc.bat or GetRenderDoc.ps1 from the repo root "
      "(it installs to packages/RenderDoc by default)."
      "\n  - Alternative: set OXYGEN_RENDERDOC_DIR to a folder containing "
      "Include/renderdoc_app.h and optionally Bin/x64/renderdoc.dll."
    )
  endif()

  if(OXYGEN_D3D12_AFTERMATH_ENABLED)
    target_include_directories(
      ${META_MODULE_TARGET}
      PRIVATE
        "${OXYGEN_AFTERMATH_INCLUDE_DIR}"
    )

    target_link_libraries(
      ${META_MODULE_TARGET}
      PRIVATE
        "${OXYGEN_AFTERMATH_LIBRARY}"
    )

    if(OXYGEN_D3D12_AFTERMATH_DLL_DEPLOYABLE)
      if(CMAKE_CONFIGURATION_TYPES)
        set(
          _oxygen_aftermath_runtime_bin_dir
          "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/$<CONFIG>"
        )
      else()
        set(
          _oxygen_aftermath_runtime_bin_dir
          "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
        )
      endif()

      # Ensure the Nsight Aftermath runtime DLL is available next to built
      # binaries and in the build tree runtime bin directory used by executables.
      add_custom_command(
        TARGET ${META_MODULE_TARGET}
        POST_BUILD
        COMMAND
          ${CMAKE_COMMAND} -E make_directory
          "${_oxygen_aftermath_runtime_bin_dir}"
        COMMAND
          ${CMAKE_COMMAND} -E copy_if_different "${OXYGEN_AFTERMATH_DLL}"
          "$<TARGET_FILE_DIR:${META_MODULE_TARGET}>"
        COMMAND
          ${CMAKE_COMMAND} -E copy_if_different "${OXYGEN_AFTERMATH_DLL}"
          "${_oxygen_aftermath_runtime_bin_dir}"
        VERBATIM
      )

      # Install the runtime DLL alongside executables.
      if(DEFINED OXYGEN_INSTALL_BIN)
        install(
          FILES
            "${OXYGEN_AFTERMATH_DLL}"
          DESTINATION ${OXYGEN_INSTALL_BIN}
          COMPONENT "${META_MODULE_NAME}_runtime"
        )
      endif()

      message(
        STATUS
        "Nsight Aftermath enabled for ${META_MODULE_NAME}: "
        "${OXYGEN_AFTERMATH_INCLUDE_DIR}"
      )
      message(
        STATUS
        "Nsight Aftermath runtime deployment enabled for ${META_MODULE_NAME}: "
        "${OXYGEN_AFTERMATH_DLL}"
      )
    endif()
  elseif(DEFINED USE_AFTERMATH AND USE_AFTERMATH)
    message(
      WARNING
      "USE_AFTERMATH is ON but Nsight Aftermath SDK artifacts were not found. "
      "\n  - Recommended: run GetAftermath.bat or GetAftermath.ps1 from the repo root "
      "(it installs to packages/NsightAftermath by default)."
      "\n  - Alternative: set OXYGEN_AFTERMATH_DIR to a folder containing "
      "GFSDK_Aftermath.h (or GFSDK_Aftermath_DX12.h), GFSDK_Aftermath_Lib.x64.lib, and "
      "GFSDK_Aftermath_Lib.x64.dll."
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
