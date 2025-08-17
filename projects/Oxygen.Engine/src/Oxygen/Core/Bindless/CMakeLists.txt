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

add_custom_command(
  OUTPUT
    ${_out_cpp}
    ${_out_hlsl}
    ${_out_json}
    ${_out_heaps}
    ${_out_heaps_hpp}
    ${_out_rs}
    ${_out_meta}
  COMMAND
    ${CMAKE_COMMAND} -E echo
    "[BindlessCodeGen] Generating bindless headers and JSON"
  COMMAND
    ${Python3_EXECUTABLE} -m bindless_codegen.cli --input ${_sot} --out-base
    ${_out_base}
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
    ${_out_cpp}
    ${_out_hlsl}
    ${_out_json}
    ${_out_heaps}
    ${_out_heaps_hpp}
    ${_out_rs}
    ${_out_meta}
)
set_target_properties(
  generate_bindless_headers
  PROPERTIES
    FOLDER
      "Tools"
    OUTPUT_NAME
      "Generate Bindless BindingSlots"
)
