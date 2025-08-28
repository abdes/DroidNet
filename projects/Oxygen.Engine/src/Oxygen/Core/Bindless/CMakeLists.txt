# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

# ------------------------------------------------------------------------------
# Bindless header and JSON generation (from Spec.yaml)
# ------------------------------------------------------------------------------

find_package(Python3 COMPONENTS Interpreter REQUIRED)

# Generate headers and JSON sidecars from the Spec YAML (SSoT).
# We emit into a temporary build-tree folder, then move results into the
# source tree so generated files can be versioned under `Meta`/`Generated.`
set(BINDLESS_GEN_TEMP_DIR "${CMAKE_CURRENT_BINARY_DIR}/.generated/")
# Filename prefix the generator uses for outputs (e.g. Generated.All.json)
set(BINDLESS_GEN_PREFIX "Generated.")

set(_candidate_meta_dir "${CMAKE_CURRENT_SOURCE_DIR}/../Meta")
  get_filename_component(BINDLESS_META_DIR "${_candidate_meta_dir}" ABSOLUTE)
set(BINDLESS_SPEC_FILE "${BINDLESS_META_DIR}/Bindless.yaml")

# Optional JSON Schema path (may not exist)
set(BINDLESS_SCHEMA_FILE "${BINDLESS_META_DIR}/Bindless.schema.json")

# Temp-generated meta file paths (in the build tree)
set(BINDLESS_GENERATED_ALL_JSON "${BINDLESS_GEN_TEMP_DIR}/${BINDLESS_GEN_PREFIX}All.json")
set(BINDLESS_GENERATED_HEAPS_JSON "${BINDLESS_GEN_TEMP_DIR}/${BINDLESS_GEN_PREFIX}Heaps.D3D12.json")

# Make generation sensitive to tool script changes
file(
  GLOB BINDLESS_CODEGEN_PY_SCRIPTS
  "${CMAKE_SOURCE_DIR}/src/Oxygen/Core/Tools/BindlessCodeGen/src/bindless_codegen/*.py"
)
set(
  BINDLESS_TOOL_FILES
  ${BINDLESS_CODEGEN_PY_SCRIPTS}
  "${CMAKE_SOURCE_DIR}/src/Oxygen/Core/Tools/BindlessCodeGen/pyproject.toml"
  "${CMAKE_SOURCE_DIR}/src/Oxygen/Core/Tools/BindlessCodeGen/requirements.txt"
)

# ------------------------------------------------------------------------------
# Generation model (Option 2):
# - We intentionally write generated files into the SOURCE tree (under this
#   folder) so they are versioned in git.
# - To prevent CMake/VS "Clean" from deleting these versioned files, we DO NOT
#   list the generated headers/JSON as OUTPUT or BYPRODUCTS of the custom
#   command. Instead, we use a stamp file in the BINARY tree as the only
#   declared OUTPUT. Clean will remove the stamp only, leaving source files
#   untouched.
# - Rebuilds are still correct because the stamp depends on the SSoT
#   (Bindless.yaml) and the generator scripts; when inputs change, the stamp is
#   re-generated, and the generator runs again to refresh the versioned files.
# ------------------------------------------------------------------------------

# Stamp file that represents completion of generation; lives in the build tree
set(BINDLESS_GEN_STAMP "${CMAKE_CURRENT_BINARY_DIR}/bindless_gen.stamp")

add_custom_command(
  OUTPUT
  ${BINDLESS_GEN_STAMP}
  COMMAND
    ${CMAKE_COMMAND} -E echo
    "[BindlessCodeGen] Generating bindless headers and JSON (source tree)"
  COMMAND
    ${Python3_EXECUTABLE} -m bindless_codegen.cli --input ${BINDLESS_SPEC_FILE} --out-base
    "${BINDLESS_GEN_TEMP_DIR}/${BINDLESS_GEN_PREFIX}" --schema ${BINDLESS_SCHEMA_FILE}
  # After generation, move generated files to the appropriate source locations.
  COMMAND
    ${CMAKE_COMMAND} -E make_directory "${BINDLESS_META_DIR}"
  # First, move JSON sidecars (meta files) from the temporary output into
  # the Meta folder so they become the authoritative, versioned copies.
  COMMAND
    ${CMAKE_COMMAND} -E copy_if_different "${BINDLESS_GENERATED_ALL_JSON}" "${BINDLESS_META_DIR}"
  COMMAND
    ${CMAKE_COMMAND} -E copy_if_different "${BINDLESS_GENERATED_HEAPS_JSON}" "${BINDLESS_META_DIR}"
  # Remove the meta JSON files from the temporary output so they are not
  # included when we copy the remaining generated files into the source tree.
  COMMAND
    ${CMAKE_COMMAND} -E remove "${BINDLESS_GEN_TEMP_DIR}Generated.All.json" "${BINDLESS_GEN_TEMP_DIR}Generated.Heaps.D3D12.json"
  # Now copy the rest of the generated files into the source tree root.
  COMMAND
    ${CMAKE_COMMAND} -E copy_directory "${BINDLESS_GEN_TEMP_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}"
  # Remove the temporary generation directory
  COMMAND
    ${CMAKE_COMMAND} -E remove_directory "${BINDLESS_GEN_TEMP_DIR}"
  COMMAND
    ${CMAKE_COMMAND} -E touch ${BINDLESS_GEN_STAMP}
  DEPENDS
    bindless_codegen_editable_install
    ${BINDLESS_SPEC_FILE}
    ${BINDLESS_TOOL_FILES}
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
  VERBATIM
)

add_custom_target(
  generate_bindless_headers
  DEPENDS
  ${BINDLESS_GEN_STAMP}
)
set_target_properties(
  generate_bindless_headers
  PROPERTIES
    FOLDER
      "Tools"
    OUTPUT_NAME
      "Generate Bindless BindingSlots"
)

# Run the compile-check after generation. This will fail the generation step if
# the compile-check fails, enforcing that generated headers remain compilable.
add_custom_command(
  TARGET generate_bindless_headers
  POST_BUILD
  COMMAND
    ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target
    BindlessCodeGen_CompileCheck --config $<CONFIG>
  COMMENT "Running BindlessCodeGen compile-check"
)
