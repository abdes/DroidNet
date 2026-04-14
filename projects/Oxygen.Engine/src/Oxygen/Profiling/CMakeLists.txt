# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

# ------------------------------------------------------------------------------
# Meta information about the this module
# ------------------------------------------------------------------------------

asap_module_declare(
  MODULE_NAME "Oxygen.Profiling"
  DESCRIPTION "Unified CPU/GPU profiling infrastructure"
  WITHOUT_VERSION_H
)
asap_push_module("${META_MODULE_NAME}")

# ------------------------------------------------------------------------------
# Dependencies
# ------------------------------------------------------------------------------

set(
  OXYGEN_WINPIXEVENTRUNTIME_DIR
  ""
  CACHE PATH
  "Optional root directory containing WinPixEventRuntime (Include/Lib)."
)

set(OXYGEN_PROFILING_PIX_ENABLED OFF)

if(WIN32)
  set(_oxygen_winpixeventruntime_hint_paths)

  if(OXYGEN_WINPIXEVENTRUNTIME_DIR)
    list(
      APPEND _oxygen_winpixeventruntime_hint_paths
      "${OXYGEN_WINPIXEVENTRUNTIME_DIR}"
    )
  endif()

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
    OXYGEN_PROFILING_WINPIX_INCLUDE_DIR
    NAMES
      pix3.h
    PATHS
      ${_oxygen_winpixeventruntime_hint_paths}
    PATH_SUFFIXES
      Include
      include
  )

  find_library(
    OXYGEN_PROFILING_WINPIX_LIBRARY
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
    OXYGEN_PROFILING_WINPIX_DLL
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
    OXYGEN_PROFILING_WINPIX_INCLUDE_DIR
    AND
      OXYGEN_PROFILING_WINPIX_LIBRARY
    AND
      OXYGEN_PROFILING_WINPIX_DLL
  )
    set(OXYGEN_PROFILING_PIX_ENABLED ON)
  endif()
endif()

# ==============================================================================
# Build instructions
# ==============================================================================

# ------------------------------------------------------------------------------
# Main module targets
# ------------------------------------------------------------------------------

add_library(${META_MODULE_TARGET}-cpu)
add_library(oxygen::profiling-cpu ALIAS ${META_MODULE_TARGET}-cpu)

add_library(${META_MODULE_TARGET}-gpu INTERFACE)
add_library(oxygen::profiling-gpu ALIAS ${META_MODULE_TARGET}-gpu)

add_library(${META_MODULE_TARGET} INTERFACE)
add_library(${META_MODULE_TARGET_ALIAS} ALIAS ${META_MODULE_TARGET})

set_target_properties(
  ${META_MODULE_TARGET}-cpu
  PROPERTIES
    DEFINE_SYMBOL
      "OXGN_PROF_EXPORTS"
    OUTPUT_NAME
      $<IF:$<BOOL:${BUILD_SHARED_LIBS}>,${META_MODULE_NAME}.Cpu,${META_MODULE_NAME}.Cpu-static>
    VERSION
      ${META_VERSION}
    SOVERSION
      ${META_VERSION_MAJOR}
    PROJECT_LABEL
      "${META_MODULE_NAME}.Cpu"
)

set_target_properties(
  ${META_MODULE_TARGET}-gpu
  PROPERTIES
    PROJECT_LABEL
      "${META_MODULE_NAME}.Gpu"
)

set_target_properties(
  ${META_MODULE_TARGET}
  PROPERTIES
    PROJECT_LABEL
      ${META_MODULE_NAME}
)

set(
  OXYGEN_PROFILING_CPU_HEADERS
  "api_export.h"
  "CpuProfileScope.h"
  "Internal/CpuProfileCoordinator.h"
  "Internal/ScopeNameFormatter.h"
  "ProfileScope.h"
)
set(
  OXYGEN_PROFILING_CPU_PRIVATE_SOURCES
  "CpuProfileScope.cpp"
  "Internal/CpuProfileCoordinator.cpp"
  "Internal/ScopeNameFormatter.cpp"
  "ProfileScope.cpp"
)
set(OXYGEN_PROFILING_GPU_HEADERS "GpuEventScope.h")

target_sources(
  ${META_MODULE_TARGET}-cpu
  PRIVATE
    ${OXYGEN_PROFILING_CPU_PRIVATE_SOURCES}
    ${OXYGEN_PROFILING_CPU_HEADERS}
  PUBLIC
    FILE_SET HEADERS
      BASE_DIRS
        "${OXYGEN_SOURCE_DIR}"
      FILES
        ${OXYGEN_PROFILING_CPU_HEADERS}
)

target_sources(
  ${META_MODULE_TARGET}-gpu
  INTERFACE
    FILE_SET HEADERS
      BASE_DIRS
        "${OXYGEN_SOURCE_DIR}"
      FILES
        ${OXYGEN_PROFILING_GPU_HEADERS}
)

target_compile_features(${META_MODULE_TARGET}-cpu PUBLIC cxx_std_23)
target_compile_options(
  ${META_MODULE_TARGET}-cpu
  PUBLIC
    ${OXYGEN_COMMON_CXX_FLAGS}
)
target_compile_definitions(
  ${META_MODULE_TARGET}-cpu
  PRIVATE
    $<$<PLATFORM_ID:Windows>:WIN32_LEAN_AND_MEAN>
    $<$<PLATFORM_ID:Windows>:NOMINMAX>
  PUBLIC
    $<$<NOT:$<BOOL:${BUILD_SHARED_LIBS}>>:OXGN_PROF_STATIC>
)

target_link_libraries(${META_MODULE_TARGET}-cpu PUBLIC oxygen::base)

if(TARGET oxygen::tracy)
  target_link_libraries(${META_MODULE_TARGET}-cpu PRIVATE oxygen::tracy)
endif()

if(OXYGEN_PROFILING_PIX_ENABLED)
  target_include_directories(
    ${META_MODULE_TARGET}-cpu
    PRIVATE
      "${OXYGEN_PROFILING_WINPIX_INCLUDE_DIR}"
  )
  target_link_libraries(
    ${META_MODULE_TARGET}-cpu
    PRIVATE
      "${OXYGEN_PROFILING_WINPIX_LIBRARY}"
  )
  target_compile_definitions(${META_MODULE_TARGET}-cpu PRIVATE USE_PIX)

  add_custom_command(
    TARGET ${META_MODULE_TARGET}-cpu
    POST_BUILD
    COMMAND
      ${CMAKE_COMMAND} -E copy_if_different "${OXYGEN_PROFILING_WINPIX_DLL}"
      "$<TARGET_FILE_DIR:${META_MODULE_TARGET}-cpu>"
    VERBATIM
  )
endif()

target_link_libraries(${META_MODULE_TARGET}-gpu INTERFACE oxygen::profiling-cpu)

target_link_libraries(
  ${META_MODULE_TARGET}
  INTERFACE
    oxygen::profiling-cpu
    oxygen::profiling-gpu
)

# ------------------------------------------------------------------------------
# Tests
# ------------------------------------------------------------------------------

if(OXYGEN_BUILD_TESTS)
  add_subdirectory("Test")
endif()

# ------------------------------------------------------------------------------
# API Documentation
# ------------------------------------------------------------------------------

if(OXYGEN_BUILD_DOCS AND OXYGEN_WITH_DOXYGEN)
  asap_with_doxygen(
    MODULE_NAME ${META_MODULE_TARGET}-cpu
    VERSION ${META_VERSION}
    TITLE "\"Profiling\""
    BRIEF "\"Unified profiling infrastructure for Oxygen\""
    INPUT_PATH "${CMAKE_CURRENT_SOURCE_DIR}"
  )
endif()

# ==============================================================================
# Deployment instructions
# ==============================================================================

oxygen_module_install(
  EXPORT ${META_MODULE_NAMESPACE}
  TARGETS
    ${META_MODULE_TARGET}-cpu
    ${META_MODULE_TARGET}-gpu
    ${META_MODULE_TARGET}
)

asap_pop_module("${META_MODULE_NAME}")
