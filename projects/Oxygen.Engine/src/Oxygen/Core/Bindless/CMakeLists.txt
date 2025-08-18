# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

# ------------------------------------------------------------------------------
# Bindless header and JSON generation (from Spec.yaml)
# ------------------------------------------------------------------------------

find_package(Python3 COMPONENTS Interpreter REQUIRED)

# Generator invocation produces headers and JSON sidecars from the SSoT
set(_sot "${CMAKE_CURRENT_SOURCE_DIR}/Spec.yaml")
set(_out_base "${CMAKE_CURRENT_SOURCE_DIR}/Generated.")
set(_out_cpp "${_out_base}Constants.h")
set(_out_hlsl "${_out_base}BindlessLayout.hlsl")
set(_out_rs "${_out_base}RootSignature.h")
set(_out_json "${_out_base}All.json")
set(_out_heaps "${_out_base}Heaps.D3D12.json")
set(_out_heaps_hpp "${_out_base}Heaps.D3D12.h")
set(_out_meta "${_out_base}Meta.h")

# Make generation sensitive to tool script changes
file(
  GLOB _bindless_codegen_py
  "${CMAKE_SOURCE_DIR}/src/Oxygen/Core/Tools/BindlessCodeGen/src/bindless_codegen/*.py"
)
set(
  _tool_py
  ${_bindless_codegen_py}
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
# - Rebuilds are still correct because the stamp depends on the SSoT (Spec.yaml)
#   and the generator scripts; when inputs change, the stamp is re-generated,
#   and the generator runs again to refresh the versioned files.
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
    ${Python3_EXECUTABLE} -m bindless_codegen.cli --input ${_sot} --out-base
    ${_out_base}
  COMMAND
    ${CMAKE_COMMAND} -E touch ${_gen_stamp}
  DEPENDS
    bindless_codegen_editable_install
    ${_sot}
    ${_tool_py}
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
  VERBATIM
)

add_custom_target(
  generate_bindless_headers
  DEPENDS
  ${_gen_stamp}
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
  COMMENT "[Bindless] Running BindlessCodeGen compile-check"
)
