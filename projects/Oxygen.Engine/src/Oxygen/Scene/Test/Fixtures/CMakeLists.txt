# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

# ==============================================================================
# Code Generators
# ==============================================================================

# Generate TestSceneFactory_schema.h from TestSceneFactory.schema.json
set(schema_json "${CMAKE_CURRENT_SOURCE_DIR}/TestSceneFactory.schema.json")
set(schema_header "${CMAKE_CURRENT_SOURCE_DIR}/TestSceneFactory_schema.h")
set(schema_template "${CMAKE_CURRENT_SOURCE_DIR}/TestSceneFactory_schema.h.in")
set(schema_script "${CMAKE_CURRENT_BINARY_DIR}/generate_schema.cmake")

file(WRITE "${schema_script}" "
file(READ \"${schema_json}\" SCHEMA_CONTENT)
configure_file(\"${schema_template}\" \"${schema_header}\" @ONLY)
")

add_custom_command(
  OUTPUT "${schema_header}"
  COMMAND ${CMAKE_COMMAND} -P "${schema_script}"
  DEPENDS "${schema_json}" "${schema_template}" "${schema_script}"
  COMMENT "Generating TestSceneFactory_schema.h from TestSceneFactory.schema.json"
  VERBATIM
)

add_custom_target(
  "${META_MODULE_NAME}.TestSceneFactorySchema"
  DEPENDS "${schema_header}"
)

set_source_files_properties("${schema_header}" PROPERTIES GENERATED TRUE)
set_property(SOURCE "${schema_header}" APPEND PROPERTY OBJECT_DEPENDS "${schema_json}")
