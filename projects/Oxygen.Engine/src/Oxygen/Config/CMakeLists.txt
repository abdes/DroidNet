# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause).
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

# ------------------------------------------------------------------------------
# Meta information about the this module
# ------------------------------------------------------------------------------

asap_module_declare(
  MODULE_NAME "Oxygen.Config"
  DESCRIPTION "Oxygen engine configuration module"
  WITHOUT_VERSION_H
)
asap_push_module("${META_MODULE_NAME}")

# ------------------------------------------------------------------------------
# Dependencies
# ------------------------------------------------------------------------------

find_package(fmt CONFIG REQUIRED)

# ==============================================================================
# Build instructions
# ==============================================================================

# ------------------------------------------------------------------------------
# Main module target
# ------------------------------------------------------------------------------

add_library(${META_MODULE_TARGET} INTERFACE)
add_library(${META_MODULE_TARGET_ALIAS} ALIAS ${META_MODULE_TARGET})

set(
  OXYGEN_CONFIG_HEADERS
  "PlatformConfig.h"
  "EngineConfig.h"
  "GraphicsConfig.h"
)

target_sources(
  ${META_MODULE_TARGET}
  PRIVATE
    ${OXYGEN_CONFIG_HEADERS}
  PUBLIC
    FILE_SET HEADERS
    TYPE HEADERS
    BASE_DIRS ${OXYGEN_SOURCE_DIR}
    FILES ${OXYGEN_CONFIG_HEADERS}
)

arrange_target_files_for_ide(${META_MODULE_TARGET})

# Set include directories
target_include_directories(
  ${META_MODULE_TARGET}
  INTERFACE
    # Includes will have the following format: #include <Oxygen/OxCo/Logging.h>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../..>
    $<INSTALL_INTERFACE:include>
)

target_compile_features(${META_MODULE_TARGET} INTERFACE cxx_std_20)
target_compile_options(${META_MODULE_TARGET} INTERFACE ${OXYGEN_COMMON_CXX_FLAGS})

# Header only libraries don't get a Visual Studio project file generated for
# them. This is a workaround to make sure the header files are visible in the
# IDE.
add_custom_target(_${META_MODULE_NAME} SOURCES ${header_files})
set_target_properties(
  _${META_MODULE_NAME}
  PROPERTIES
    PROJECT_LABEL
      ${META_MODULE_NAME}
)

# ------------------------------------------------------------------------------
# Tests
# ------------------------------------------------------------------------------

# if(OXYGEN_BUILD_TESTS)
#   add_subdirectory("Test")
# endif()

# ==============================================================================
# Deployment instructions
# ==============================================================================

oxygen_module_install(
  EXPORT ${META_MODULE_NAMESPACE}
  TARGETS
    ${META_MODULE_TARGET}
)

asap_pop_module("${META_MODULE_NAME}")
