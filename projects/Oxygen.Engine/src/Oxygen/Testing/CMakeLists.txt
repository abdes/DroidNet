# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause).
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

# ------------------------------------------------------------------------------
# Meta information about the this module
# ------------------------------------------------------------------------------

asap_module_declare(
  MODULE_NAME "Oxygen.Testing"
  DESCRIPTION "Testing helpers for unit test code writing"
  WITHOUT_VERSION_H
)
asap_push_module("${META_MODULE_NAME}")

# ------------------------------------------------------------------------------
# Dependencies
# ------------------------------------------------------------------------------

find_package(asio CONFIG REQUIRED)

# ==============================================================================
# Build instructions
# ==============================================================================

# ------------------------------------------------------------------------------
# Main module target
# ------------------------------------------------------------------------------

add_library(${META_MODULE_TARGET} INTERFACE)
add_library(${META_MODULE_TARGET_ALIAS} ALIAS ${META_MODULE_TARGET})

set(
  header_files
  "GTest.h"
)

source_group(
  TREE ${CMAKE_CURRENT_SOURCE_DIR}
  FILES
    ${header_files}
)

target_sources(
  ${META_MODULE_TARGET}
  INTERFACE
    FILE_SET HEADERS
    TYPE HEADERS
    BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}
    FILES ${header_files}
)

# Set include directories
target_include_directories(
  ${META_MODULE_TARGET}
  INTERFACE
    # Includes will have the following format: #include "Oxygen/OxCo/Logging.h"
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../..>
    $<INSTALL_INTERFACE:include>
)

set_target_properties(
  ${META_MODULE_TARGET}
  PROPERTIES
    OUTPUT_NAME
      "${META_MODULE_NAME}"
)

target_compile_features(${META_MODULE_TARGET} INTERFACE cxx_std_20)
target_compile_options(
  ${META_MODULE_TARGET}
  INTERFACE
    ${OXYGEN_COMMON_CXX_FLAGS}
    $<$<CXX_COMPILER_ID:MSVC>:/wd4702> # Unreachable code
)
target_link_libraries(
  ${META_MODULE_TARGET}
  INTERFACE
    GTest::gtest
)

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

# ==============================================================================
# Deployment instructions
# ==============================================================================

oxygen_module_install(
  EXPORT ${META_MODULE_NAMESPACE}
  TARGETS
    ${META_MODULE_TARGET}
)

asap_pop_module("${META_MODULE_NAME}")
