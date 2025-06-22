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
  "OxCo sample demonstrating synchronous execution of a number of asynchronous \
operations in batch, using a repeatable shared"
)
asap_module_declare(
  MODULE_NAME "Oxygen.OxCo.Examples.BatchExecution.RepeatableShared"
  DESCRIPTION description
  WITHOUT_VERSION_H
)
asap_push_module("${META_MODULE_NAME}")

# ------------------------------------------------------------------------------
# Dependencies
# ------------------------------------------------------------------------------

# ==============================================================================
# Build instructions
# ==============================================================================

# -- Using Custom Awaiter ------------------------------------------------------

add_executable(
  ${META_MODULE_TARGET}
  ${OXCO_BATCH_COMMON_SOURCES}
  "repeatable_shared_example.cpp"
  "${OXYGEN_EXAMPLES_SOURCE_DIR}/main.cpp"
)
set_target_properties(
  ${META_MODULE_TARGET}
  PROPERTIES
    FOLDER
      "Examples"
    OUTPUT_NAME
      ${META_MODULE_NAME}
)
target_compile_options(${META_MODULE_TARGET} PRIVATE ${OXYGEN_COMMON_CXX_FLAGS})
target_link_libraries(
  ${META_MODULE_TARGET}
  PRIVATE
    oxygen::base
    $<$<NOT:$<BOOL:${BUILD_SHARED_LIBS}>>:oxygen::cs-init>
    oxygen::oxco
    oxygen::platform
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
