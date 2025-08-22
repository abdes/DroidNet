# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

# ------------------------------------------------------------------------------
# Meta information about this module
# ------------------------------------------------------------------------------

set(description "Async engine simulator: structured frame phases, parallel task group, async pipelines.")

asap_module_declare(
  MODULE_NAME "Oxygen.AsyncEngine.Simulator"
  DESCRIPTION description
  WITHOUT_VERSION_H
)
asap_push_module("${META_MODULE_NAME}")

# ------------------------------------------------------------------------------
# Sources
# ------------------------------------------------------------------------------

add_executable(
  ${META_MODULE_TARGET}
  "AsyncEngineSimulator.cpp"
  "AsyncEngineSimulator.h"
  "GraphicsLayer.cpp"
  "GraphicsLayer.h"
  "main_impl.cpp"
  "../main.cpp"
)

set_target_properties(
  ${META_MODULE_TARGET}
  PROPERTIES
    FOLDER "Examples"
    OUTPUT_NAME ${META_MODULE_NAME}
)

target_compile_options(${META_MODULE_TARGET} PRIVATE ${OXYGEN_COMMON_CXX_FLAGS})

target_include_directories(
  ${META_MODULE_TARGET}
  PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

# ------------------------------------------------------------------------------
# Dependencies
# ------------------------------------------------------------------------------

target_link_libraries(
  ${META_MODULE_TARGET}
  PRIVATE
    oxygen::base
    oxygen::composition
    $<$<NOT:$<BOOL:${BUILD_SHARED_LIBS}>>:oxygen::cs-init>
    oxygen::oxco
    oxygen::clap
)

# ------------------------------------------------------------------------------
# Install
# ------------------------------------------------------------------------------

oxygen_module_install(
  EXPORT ${META_MODULE_NAMESPACE}
  TARGETS ${META_MODULE_TARGET}
)

asap_pop_module("${META_MODULE_NAME}")
