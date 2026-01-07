# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

# ------------------------------------------------------------------------------
# Meta information about the this module
# ------------------------------------------------------------------------------

set(
  description
  "A tool to dump the contents of a PAK file and analyze it in different ways."
)
asap_module_declare(
  MODULE_NAME "Oxygen.Graphics.Direct3D12.ShaderBake"
  DESCRIPTION description
  WITHOUT_VERSION_H
)
asap_push_module("${META_MODULE_NAME}")

# ------------------------------------------------------------------------------
# Dependencies
# ------------------------------------------------------------------------------

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

# -- Input System Example ------------------------------------------------------

add_executable(
  ${META_MODULE_TARGET}
  "Bake.cpp"
  "Bake.h"
  "DxcShaderCompiler.cpp"
  "DxcShaderCompiler.h"
  "Inspect.cpp"
  "Inspect.h"
  "Reflect.cpp"
  "Reflect.h"
  "main.cpp"
  "../../Shaders/EngineShaderCatalog.h"
  "../../Shaders/ShaderCatalogBuilder.h"
)

# Convenience build target: allow building by module name (matches OUTPUT_NAME)
# in addition to the generated target name (e.g. oxygen-content-pakdump).
add_custom_target(${META_MODULE_NAME} DEPENDS ${META_MODULE_TARGET})
arrange_target_files_for_ide(${META_MODULE_TARGET})
set_target_properties(
  ${META_MODULE_TARGET}
  PROPERTIES
    FOLDER
      "Tools"
    OUTPUT_NAME
      ${META_MODULE_NAME}
)
target_compile_options(${META_MODULE_TARGET} PRIVATE ${OXYGEN_COMMON_CXX_FLAGS})
target_include_directories(
  ${META_MODULE_TARGET}
  PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${dxc_inc_path}"
)
target_link_directories(${META_MODULE_TARGET} PRIVATE ${dxc_lib_path})
target_link_libraries(
  ${META_MODULE_TARGET}
  PRIVATE
    oxygen::graphics-common
    oxygen::clap
    dxcompiler
    dxil
)

# Custom command to copy the dxcompiler dll to the build output directory so
# the tool can run as part of the build.
add_custom_command(
  TARGET ${META_MODULE_TARGET}
  POST_BUILD
  COMMAND
    ${CMAKE_COMMAND} -E copy_if_different "${dxc_bin_path}/dxcompiler.dll"
    "$<TARGET_FILE_DIR:${META_MODULE_TARGET}>"
  COMMAND
    ${CMAKE_COMMAND} -E copy_if_different "${dxc_bin_path}/dxil.dll"
    "$<TARGET_FILE_DIR:${META_MODULE_TARGET}>"
)

# dxcompiler is required for this tool. Fail configure early if the runtime
# DLL is not present in the expected package location so the user can fix the
# package layout before attempting to build.
if(WIN32)
  if(NOT EXISTS "${dxc_bin_path}/dxcompiler.dll")
    message(
      FATAL_ERROR
      "dxcompiler.dll not found at: ${dxc_bin_path}/dxcompiler.dll\n"
      "Please install the DXC package into ${CMAKE_SOURCE_DIR}/packages/DXC/"
    )
  endif()
  if(NOT EXISTS "${dxc_bin_path}/dxil.dll")
    message(
      FATAL_ERROR
      "dxil.dll not found at: ${dxc_bin_path}/dxil.dll\n"
      "Please install the DXC package into ${CMAKE_SOURCE_DIR}/packages/DXC/"
    )
  endif()
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
