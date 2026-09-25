# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

# -----------------------------------------------------------------------------
# Spec Regeneration (Always Enabled): build .pak + manifest from YAML specs
# -----------------------------------------------------------------------------

# Macro: discover YAML spec files in a set of candidate directories.
macro(pakgen_discover_yaml out_var)
  set(_candidates "${CMAKE_CURRENT_SOURCE_DIR}/specs")
  set(_all_specs)
  foreach(_d IN LISTS _candidates)
    if(EXISTS "${_d}")
      file(
        GLOB _found
        CONFIGURE_DEPENDS
        "${_d}/*.yaml"
        "${_d}/*.yml"
      )
      list(APPEND _all_specs ${_found})
    endif()
  endforeach()
  if(NOT _all_specs)
    message(STATUS "[PakGen] No YAML specs discovered in candidate dirs.")
  endif()
  list(REMOVE_DUPLICATES _all_specs)
  # NOTE: This is a macro, which already executes in the caller's scope. Using
  # PARENT_SCOPE would incorrectly push the variable to the directory above,
  # leaving it unset locally. We intentionally set it directly here.
  set(${out_var} ${_all_specs})
  list(LENGTH _all_specs _spec_count)
  if(_spec_count GREATER 0)
    string(JOIN ", " _spec_list ${_all_specs})
    message(STATUS "[PakGen] Discovered ${_spec_count} YAML spec(s)")
  endif()
endmacro()

# Function: define build rules for each YAML spec -> .pak + .manifest.json
function(pakgen_register_specs spec_list)
  if(NOT Python3_Interpreter_FOUND)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
  endif()
  set(_outputs)
  foreach(_spec IN LISTS spec_list)
    get_filename_component(_dir "${_spec}" DIRECTORY)
    get_filename_component(_name "${_spec}" NAME_WE)
    set(_out_pak "${_dir}/${_name}.pak")
    set(_out_manifest "${_dir}/${_name}.manifest.json")
    add_custom_command(
      OUTPUT
        "${_out_pak}"
        "${_out_manifest}"
      COMMAND
        ${CMAKE_COMMAND} -E echo "[PakGen] Building ${_name}.pak"
      COMMAND
        ${Python3_EXECUTABLE} -m pakgen.cli build "${_spec}" "${_out_pak}"
        --deterministic --emit-manifest "${_out_manifest}"
      DEPENDS
        "${_spec}"
        ${_pakgen_sources}
        "${CMAKE_CURRENT_BINARY_DIR}/pakgen-sources.txt"
        "${CMAKE_CURRENT_SOURCE_DIR}/pyproject.toml"
        "${OXYGEN_PYTHON_LOCKFILE}"
      WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
      VERBATIM
    )
    list(
      APPEND
      _outputs
      "${_out_pak}"
      "${_out_manifest}"
    )
  endforeach()
  if(_outputs)
    add_custom_target(pakgen_specs ALL DEPENDS ${_outputs})
    set_target_properties(
      pakgen_specs
      PROPERTIES
        FOLDER
          "Tools"
        OUTPUT_NAME
          "Regenerate PakGen Specs"
    )
  endif()
endfunction()

file(
  GLOB_RECURSE _pakgen_sources
  CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/src/*.py"
)
list(JOIN _pakgen_sources "\n" _pakgen_inventory)
file(
  GENERATE
  OUTPUT
  "${CMAKE_CURRENT_BINARY_DIR}/pakgen-sources.txt"
  CONTENT "${_pakgen_inventory}\n"
)
pakgen_discover_yaml(_pakgen_specs)
pakgen_register_specs("${_pakgen_specs}")

# Python tools are provisioned during setup and verified by PythonTools.cmake.
