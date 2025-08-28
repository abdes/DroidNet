# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

# ------------------------------------------------------------------------------
# Bindless header and JSON generation (from Bindless.yaml)
# ------------------------------------------------------------------------------

find_package(Python3 COMPONENTS Interpreter REQUIRED)

# Generate headers and JSON sidecars from the Bindless YAML (SSoT).
# We emit into a temporary build-tree folder, then move results into the
# source tree so generated files can be versioned under `Meta`/`Generated.`
set(_gen_temp_dir "${CMAKE_CURRENT_BINARY_DIR}/.generated/")
# Filename prefix the generator uses for outputs (e.g. Generated.All.json)
set(_gen_prefix "Generated.")

set(_candidate_meta_dir "${CMAKE_CURRENT_SOURCE_DIR}/../Meta")
get_filename_component(_bindless_meta_dir "${_candidate_meta_dir}" ABSOLUTE)
set(_bindless_spec_file "${_bindless_meta_dir}/Bindless.yaml")

# Optional JSON Schema path (may not exist)
set(_bindless_schema_file "${_bindless_meta_dir}/Bindless.schema.json")

# Temp-generated meta file paths (in the build tree)
set(_gen_all_json "${_gen_temp_dir}/${_gen_prefix}All.json")
set(_gen_heaps_json "${_gen_temp_dir}/${_gen_prefix}Heaps.D3D12.json")

# Make generation sensitive to tool script changes
set(_gentool_dir "${CMAKE_SOURCE_DIR}/src/Oxygen/Core/Tools/BindlessCodeGen")
file(GLOB _gentool_py_scripts "${_gentool_dir}/src/bindless_codegen/*.py")
set(
  _gentool_sensitive_files
  ${_gentool_py_scripts}
  "${_gentool_dir}/pyproject.toml"
  "${_gentool_dir}/requirements.txt"
)

# Local helper invocation for copying generated files (keeps repeated usage DRY)
set(
  _copy_helper
  ${Python3_EXECUTABLE}
  -m
  bindless_codegen._copy_helper
  --verbose
)

# ------------------------------------------------------------------------------
# - We intentionally write successfully generated files into the SOURCE tree
#   (under this folder) so they are versioned in git.
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
set(_gen_stamp "${CMAKE_CURRENT_BINARY_DIR}/bindless_gen.stamp")

add_custom_command(
  OUTPUT
    ${_gen_stamp}
  COMMAND
    ${CMAKE_COMMAND} -E echo
    "[BindlessCodeGen] Generating bindless headers and JSON (source tree)"
  COMMAND
    ${Python3_EXECUTABLE} -m bindless_codegen.cli --input ${_bindless_spec_file}
    --out-base "${_gen_temp_dir}/${_gen_prefix}" --schema
    ${_bindless_schema_file}
  # After generation, move generated files to the appropriate source locations.
  COMMAND
    ${CMAKE_COMMAND} -E echo "Copying generated files to the source tree..."
  COMMAND
    ${CMAKE_COMMAND} -E make_directory "${_bindless_meta_dir}"
  # First, move JSON sidecars (meta files) from the temporary output into
  # the Meta folder so they become the authoritative, versioned copies.
  # Use a helper to copy JSON sidecars (meta files). The helper will avoid
  # overwriting files when the only difference is a single-line timestamp.
  COMMAND
    ${_copy_helper} "${_gen_all_json}"
    "${_bindless_meta_dir}/Generated.All.json"
  COMMAND
    ${_copy_helper} "${_gen_heaps_json}"
    "${_bindless_meta_dir}/Generated.Heaps.D3D12.json"
  # Remove the meta JSON files from the temporary output so they are not
  # included when we copy the remaining generated files into the source tree.
  COMMAND
    ${CMAKE_COMMAND} -E remove "${_gen_temp_dir}Generated.All.json"
    "${_gen_temp_dir}Generated.Heaps.D3D12.json"
  # Now copy the rest of the generated files into the source tree root using
  # the same helper so we avoid spurious commits caused by timestamp-only
  # differences. We iterate files in the temp dir and copy them individually.
  COMMAND
  COMMAND
    ${_copy_helper} "${_gen_temp_dir}" "${CMAKE_CURRENT_SOURCE_DIR}"
  # Remove the temporary generation directory
  COMMAND
    ${CMAKE_COMMAND} -E remove_directory "${_gen_temp_dir}"
  COMMAND
    ${CMAKE_COMMAND} -E touch ${_gen_stamp}
  DEPENDS
    bindless_codegen_editable_install
    ${_bindless_spec_file}
    ${_gentool_sensitive_files}
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
  VERBATIM
)

# Run the compile-check after generation. This will fail the generation step if
# the compile-check fails, enforcing that generated headers remain compilable.
add_custom_target(oxygen-core_bindless_gen DEPENDS ${_gen_stamp})
add_custom_command(
  TARGET oxygen-core_bindless_gen
  POST_BUILD
  COMMAND
    ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target
    BindlessCodeGen_CompileCheck --config $<CONFIG>
  COMMENT "Running BindlessCodeGen compile-check"
)
