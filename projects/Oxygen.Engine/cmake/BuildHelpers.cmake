# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

# ------------------------------------------------------------------------------
# Build Helpers to simplify target creation.
# ------------------------------------------------------------------------------

if(__build_helpers)
  return()
endif()
set(__build_helpers YES)

# We must run the following at "include" time, not at function call time, to
# find the path to this module rather than the path to a calling list file
get_filename_component(_build_helpers_dir ${CMAKE_CURRENT_LIST_FILE} PATH)

include(GenerateExportHeader)

# ------------------------------------------------------------------------------
# Meta information about this module.
#
# Of particular importance is the MODULE_NAME, which can be composed of multiple
# segments separated by '.' or '_'. In such case, these segments will be used
# as path segments for the `api_export` generated file, and as identifier segments
# in the corresponding `_API` compiler defines.
#
# For example, `Super.Hero.Module` will produce a file that can be included as
# "super/hero/module/api_export.h" and will provide the export macro as
# `SUPER_HERO_MODULE_API`.
#
# It is a common practice and a recommended one to use a target name for that
# module with the same name (i.e. Super.Hero.Module).
# ------------------------------------------------------------------------------

function(asap_module_declare)
  set(options WITHOUT_VERSION_H)
  set(
    oneValueArgs
    MODULE_NAME
    MODULE_TARGET_NAME
    DESCRIPTION
  )
  set(multiValueArgs)

  cmake_parse_arguments(
    x
    "${options}"
    "${oneValueArgs}"
    "${multiValueArgs}"
    ${ARGN}
  )

  if(NOT DEFINED x_MODULE_NAME)
    message(FATAL_ERROR "Module name is required.")
    return()
  endif()

  # Split the module_full_name into a list
  string(REPLACE "." ";" module_parts ${x_MODULE_NAME})

  # Extract the namespace
  list(GET module_parts 0 namespace)
  string(TOLOWER ${namespace} namespace)

  # Extract and process the module_name (remaining parts)
  list(REMOVE_AT module_parts 0)
  string(JOIN "." unqualified_module_name ${module_parts})
  string(TOLOWER ${unqualified_module_name} unqualified_module_name)
  string(REPLACE "." "-" unqualified_module_name ${unqualified_module_name})

  # Define the module's meta variables
  set(META_MODULE_NAME "${x_MODULE_NAME}" PARENT_SCOPE)
  set(META_MODULE_DESCRIPTION "${x_DESCRIPTION}" PARENT_SCOPE)
  set(META_MODULE_NAMESPACE "${namespace}" PARENT_SCOPE)
  set(META_MODULE_TARGET "${namespace}-${unqualified_module_name}" PARENT_SCOPE)
  set(
    META_MODULE_TARGET_ALIAS
    "${namespace}::${unqualified_module_name}"
    PARENT_SCOPE
  )

  # Generate the version.h file for the module
  if(NOT x_WITHOUT_VERSION_H)
    set(version_h_in "${_build_helpers_dir}/module_version.h.in")
    asap_module_name(
      SEGMENTS ${x_MODULE_NAME}
      OUTPUT_VARIABLE path_segments
      TO_LOWER
    )
    cmake_path(
      APPEND
      ${CMAKE_CURRENT_BINARY_DIR}
      "include"
      ${path_segments}
      "version.h"
      OUTPUT_VARIABLE version_h_file
    )
    configure_file("${version_h_in}" "${version_h_file}")
  endif()

  # Check if the module has been pushed on top of the hierarchy stack
  if(NOT ASAP_LOG_PROJECT_HIERARCHY MATCHES "(${META_MODULE_NAME})")
    message(
      AUTHOR_WARNING
      "Can't find module `${META_MODULE_NAME}` on the hierarchy stack. "
      "Please make sure it has been pushed with asap_push_module()."
    )
  endif()
endfunction()

function(asap_module_name)
  set(
    options
    TO_LOWER
    TO_UPPER
  )
  set(
    oneValueArgs
    SEGMENTS
    API_EXPORT_MACRO
    OUTPUT_VARIABLE
  )
  set(multiValueArgs)

  cmake_parse_arguments(
    x
    "${options}"
    "${oneValueArgs}"
    "${multiValueArgs}"
    ${ARGN}
  )

  if(NOT DEFINED x_OUTPUT_VARIABLE)
    message(
      FATAL_ERROR
      "Must specify an `OUTPUT_VARIABLE` to receive the result."
    )
    return()
  endif()

  if(x_SEGMENTS)
    set(module_name ${x_SEGMENTS})
  endif()
  if(x_API_EXPORT_MACRO)
    set(module_name ${x_API_EXPORT_MACRO})
  endif()
  if(NOT DEFINED module_name)
    message(
      FATAL_ERROR
      "Must specify `SEGMENTS` or `EXPORT_MACRO` and provide a module name."
    )
    return()
  endif()

  if(x_TO_LOWER AND x_TO_UPPER)
    message(
      FATAL_ERROR
      "Can only specify either `TO_LOWER` or `TO_UPPER` but not both."
    )
    return()
  endif()

  # Extract words from the identifier expecting it to be using '_' or '.' to
  # compose a hierarchy of segments
  string(REGEX MATCHALL "[A-Za-z][^_.]*" words ${module_name})

  # Process each word
  set(result)
  foreach(word IN LISTS words)
    if(x_TO_LOWER)
      string(TOLOWER "${word}" word)
    endif()
    if(x_TO_UPPER)
      string(TOUPPER "${word}" word)
    endif()
    list(APPEND result "${word}")
  endforeach()

  if(x_API_EXPORT_MACRO)
    string(JOIN "_" result ${result})
  endif()

  set(${x_OUTPUT_VARIABLE} "${result}" PARENT_SCOPE)
endfunction()

function(asap_generate_export_headers target module)
  asap_module_name(SEGMENTS ${module} OUTPUT_VARIABLE path_segments TO_LOWER)
  cmake_path(
    APPEND
    ${CMAKE_CURRENT_BINARY_DIR}
    "include"
    ${path_segments}
    "api_export.h"
    OUTPUT_VARIABLE export_file
  )

  asap_module_name(
    API_EXPORT_MACRO ${module}
    OUTPUT_VARIABLE export_base_name
    TO_UPPER
  )
  set(export_macro_name "${export_base_name}_API")

  generate_export_header(
    ${target}
    BASE_NAME ${export_base_name}
    EXPORT_FILE_NAME ${export_file}
    EXPORT_MACRO_NAME ${export_macro_name}
  )
endfunction()
