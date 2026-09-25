# Distributed under the 3-Clause BSD License. See accompanying file LICENSE.
# SPDX-License-Identifier: BSD-3-Clause

# Included only when OXYGEN_BUILD_DOCS and OXYGEN_WITH_DOXYGEN are enabled.
# Modules opt in with asap_with_doxygen(). Build documentation explicitly with
# cmake --build --preset oxygen-ninja-debug --target dox (or <module>_dox).
include_guard(GLOBAL)

# The project template uses Doxygen 1.14 configuration tags and Graphviz graphs.
# Missing explicitly requested tools must not silently remove documentation.
find_package(
  Doxygen
  1.14
  MODULE
  REQUIRED
  COMPONENTS
    doxygen
    dot
)
foreach(_tool IN ITEMS Doxygen::doxygen Doxygen::dot)
  get_target_property(_executable ${_tool} IMPORTED_LOCATION)
  if(NOT EXISTS "${_executable}" OR IS_DIRECTORY "${_executable}")
    message(
      FATAL_ERROR
      "Required documentation tool ${_tool} is missing: ${_executable}"
    )
  endif()
endforeach()

if(NOT EXISTS "${OXYGEN_PROJECT_SOURCE_DIR}/doxygen/Doxyfile.in")
  message(FATAL_ERROR "Oxygen's doxygen/Doxyfile.in template is missing.")
endif()

include(FetchContent)
# Deliberately follow upstream main, as selected by the project's theme policy.
FetchContent_Declare(
  doxygen-awesome-css
  URL
    https://github.com/jothepro/doxygen-awesome-css/archive/refs/heads/main.zip
)
FetchContent_MakeAvailable(doxygen-awesome-css)
FetchContent_GetProperties(doxygen-awesome-css SOURCE_DIR AWESOME_CSS_DIR)

if(NOT TARGET dox)
  # No ALL: documentation remains an explicit developer action.
  add_custom_target(dox)
  set(_collector "${CMAKE_CURRENT_BINARY_DIR}/collect_doxygen_warnings.cmake")
  file(
    GENERATE
    OUTPUT
    "${_collector}"
    CONTENT
      "set(OXYGEN_DOXYGEN_WARNING_FILES [==[$<TARGET_PROPERTY:dox,OXYGEN_DOXYGEN_WARNING_FILES>]==])
set(OXYGEN_DOXYGEN_WARNINGS [==[${DOXYGEN_BUILD_DIR}/doxygen_warnings.txt]==])
include([==[${CMAKE_CURRENT_LIST_DIR}/CollectDoxygenWarnings.cmake]==])
"
  )
  add_custom_command(
    TARGET dox
    POST_BUILD
    COMMAND
      "${CMAKE_COMMAND}" -P "${_collector}"
    COMMENT "Collecting Oxygen documentation warnings"
    VERBATIM
  )
endif()

# Doxygen list values are space-separated quoted paths, not comma-separated lists.
function(_oxygen_doxygen_paths output)
  set(_quoted)
  foreach(_path IN LISTS ARGN)
    cmake_path(
      ABSOLUTE_PATH
      _path
      BASE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
      NORMALIZE
    )
    string(REPLACE "\"" "\\\"" _path "${_path}")
    list(APPEND _quoted "\"${_path}\"")
  endforeach()
  list(JOIN _quoted " " _value)
  set(${output} "${_value}" PARENT_SCOPE)
endfunction()

function(asap_with_doxygen)
  # Preserve the existing standalone-module documentation contract.
  if(NOT ${META_PROJECT_ID}_IS_MASTER_PROJECT)
    return()
  endif()

  cmake_parse_arguments(
    PARSE_ARGV
    0
    x
    ""
    "MODULE_NAME;VERSION;TITLE;BRIEF"
    "INPUT_PATH"
  )
  foreach(_required IN ITEMS MODULE_NAME VERSION INPUT_PATH)
    if(NOT DEFINED x_${_required} OR x_${_required} STREQUAL "")
      message(FATAL_ERROR "asap_with_doxygen: ${_required} is required.")
    endif()
  endforeach()
  if(x_UNPARSED_ARGUMENTS OR x_KEYWORDS_MISSING_VALUES)
    message(
      FATAL_ERROR
      "Invalid asap_with_doxygen arguments: ${x_UNPARSED_ARGUMENTS};${x_KEYWORDS_MISSING_VALUES}"
    )
  endif()
  if(NOT DEFINED x_TITLE)
    set(x_TITLE "\"Module ${x_MODULE_NAME}\"")
  endif()
  if(NOT DEFINED x_BRIEF)
    set(x_BRIEF "\"Documentation for ${x_MODULE_NAME}\"")
  endif()

  set(DOXY_OUTPUT_DIR "${DOXYGEN_BUILD_DIR}/${x_MODULE_NAME}")
  set(DOXY_MODULE_VERSION "${x_VERSION}")
  set(DOXY_MODULE_NAME "${x_MODULE_NAME}")
  set(DOXY_MODULE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
  set(DOXY_TITLE "${x_TITLE}")
  set(DOXY_BRIEF "${x_BRIEF}")
  _oxygen_doxygen_paths(DOXY_INPUT_PATH ${x_INPUT_PATH})
  set(_examples)
  foreach(_directory IN ITEMS Test Examples)
    if(IS_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/${_directory}")
      list(APPEND _examples "${CMAKE_CURRENT_SOURCE_DIR}/${_directory}")
    endif()
  endforeach()
  _oxygen_doxygen_paths(DOXY_EXAMPLE_PATH ${_examples})
  get_filename_component(DOXY_DOT_DIR "${DOXYGEN_DOT_EXECUTABLE}" DIRECTORY)

  set(_doxyfile "${CMAKE_CURRENT_BINARY_DIR}/${x_MODULE_NAME}.Doxyfile")
  configure_file(
    "${OXYGEN_PROJECT_SOURCE_DIR}/doxygen/Doxyfile.in"
    "${_doxyfile}"
    @ONLY
  )
  set(_warning_file "${DOXY_OUTPUT_DIR}/module_warnings.txt")
  add_custom_target(
    ${x_MODULE_NAME}_dox
    COMMAND
      "${CMAKE_COMMAND}"
      "-DOXYGEN_DOXYGEN_EXECUTABLE=$<TARGET_FILE:Doxygen::doxygen>"
      "-DOXYGEN_DOXYGEN_CONFIG=${_doxyfile}"
      "-DOXYGEN_DOXYGEN_WARNINGS=${_warning_file}" -P
      "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/RunDoxygen.cmake"
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    COMMENT "Generating documentation for ${x_MODULE_NAME}"
    VERBATIM
  )
  add_dependencies(dox ${x_MODULE_NAME}_dox)
  set_property(
    TARGET
      dox
    APPEND
    PROPERTY
      OXYGEN_DOXYGEN_WARNING_FILES
        "${_warning_file}"
  )
endfunction()
