# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause).
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

# ------------------------------------------------------------------------------
# Meta information about the this module
# ------------------------------------------------------------------------------

asap_module_declare(
  MODULE_NAME "Oxygen.Loader"
  DESCRIPTION "Oxygen dynamic module loader"
  WITHOUT_VERSION_H
)
asap_push_module("${META_MODULE_NAME}")

# ------------------------------------------------------------------------------
# Dependencies
# ------------------------------------------------------------------------------

# ==============================================================================
# Build instructions
# ==============================================================================

# ------------------------------------------------------------------------------
# Main module target
# ------------------------------------------------------------------------------

# The loader is always built as a static library, even if the rest of the Oxygen
# modules are built as shared libraries. It is linked only to the main
# executable and its API can only be called from the main executable module.
add_library(${META_MODULE_TARGET} STATIC)
add_library(${META_MODULE_TARGET_ALIAS} ALIAS ${META_MODULE_TARGET})
set_target_properties(
  ${META_MODULE_TARGET}
  PROPERTIES
    OUTPUT_NAME
      ${META_MODULE_NAME}-static
    PROJECT_LABEL
      ${META_MODULE_NAME}
)

target_sources(
  ${META_MODULE_TARGET}
  PRIVATE
    $<$<BOOL:${WIN32}>:Detail/PlatformServices_win.cpp>
    "GraphicsBackendLoader.cpp"
  PUBLIC
    FILE_SET HEADERS
    BASE_DIRS "${OXYGEN_SOURCE_DIR}"
    FILES "GraphicsBackendLoader.h" "Detail/PlatformServices.h"
)

target_compile_options(${META_MODULE_TARGET} PUBLIC ${OXYGEN_COMMON_CXX_FLAGS})

target_link_libraries(
  ${META_MODULE_TARGET}
  PUBLIC
    oxygen::base
    oxygen::config
    oxygen::graphics-common
)

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
