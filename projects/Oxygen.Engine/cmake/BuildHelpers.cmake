# Distributed under the 3-Clause BSD License. See accompanying file LICENSE.
# SPDX-License-Identifier: BSD-3-Clause

# Definitions only: including this file must not initialize caller-owned state.
include_guard(GLOBAL)

# Declare metadata; callers still create targets with native CMake commands.
# Oxygen.Graphics.Direct3D12 -> oxygen-graphics-direct3d12 / oxygen::graphics-direct3d12.
function(asap_module_declare)
  cmake_parse_arguments(PARSE_ARGV 0 arg "" "MODULE_NAME;DESCRIPTION" "")
  if(DEFINED arg_UNPARSED_ARGUMENTS OR DEFINED arg_KEYWORDS_MISSING_VALUES)
    message(
      FATAL_ERROR
      "asap_module_declare: invalid arguments: ${arg_UNPARSED_ARGUMENTS}; missing values: ${arg_KEYWORDS_MISSING_VALUES}"
    )
  endif()
  if(
    NOT
      DEFINED
        arg_MODULE_NAME
    OR
      NOT
        arg_MODULE_NAME
          MATCHES
          "^[A-Za-z][A-Za-z0-9_-]*(\\.[A-Za-z][A-Za-z0-9_-]*)+$"
  )
    message(
      FATAL_ERROR
      "asap_module_declare: MODULE_NAME must contain nonempty dotted identifier segments (for example Oxygen.Base)."
    )
  endif()
  string(REPLACE "." ";" parts "${arg_MODULE_NAME}")
  list(POP_FRONT parts namespace)
  string(TOLOWER "${namespace}" namespace)
  list(JOIN parts "-" unqualified)
  string(TOLOWER "${unqualified}" unqualified)
  set(target "${namespace}-${unqualified}")
  set(alias "${namespace}::${unqualified}")
  if(TARGET "${target}" OR TARGET "${alias}")
    message(
      FATAL_ERROR
      "asap_module_declare: ${arg_MODULE_NAME} conflicts with existing target ${target} or ${alias}."
    )
  endif()
  set(META_MODULE_NAME "${arg_MODULE_NAME}" PARENT_SCOPE)
  set(META_MODULE_DESCRIPTION "${arg_DESCRIPTION}" PARENT_SCOPE)
  set(META_MODULE_NAMESPACE "${namespace}" PARENT_SCOPE)
  set(META_MODULE_TARGET "${target}" PARENT_SCOPE)
  set(META_MODULE_TARGET_ALIAS "${alias}" PARENT_SCOPE)
endfunction()

# This is an advisory inventory, not a generator-expression evaluator. Recognize
# only a single literal path inside simple guards. Count inactive guarded paths
# as declared so another platform/configuration is not a forgotten source.
function(_oxygen_inventory_path entry output)
  if(entry MATCHES "^\\$<(0|1|BUILD_INTERFACE):([^<>;]+)>$")
    set(entry "${CMAKE_MATCH_2}")
  elseif(
    entry
      MATCHES
      "^\\$<\\$<(BOOL|PLATFORM_ID|CONFIG):[^<>;]*>:([^<>;]+)>$"
  )
    set(entry "${CMAKE_MATCH_2}")
  elseif(entry MATCHES "\\$<")
    message(
      AUTHOR_WARNING
      "Oxygen source inventory cannot inspect expression '${entry}'. Inventory is incomplete for this expression; use a literal source path with a supported guard or an explicit EXCLUDE_PATTERNS entry for intentionally uninspected files."
    )
    set(entry "")
  endif()
  set(${output} "${entry}" PARENT_SCOPE)
endfunction()

function(arrange_target_files_for_ide target)
  cmake_parse_arguments(PARSE_ARGV 1 arg "" "" "EXCLUDE_PATTERNS")
  if(DEFINED arg_UNPARSED_ARGUMENTS OR DEFINED arg_KEYWORDS_MISSING_VALUES)
    message(
      FATAL_ERROR
      "arrange_target_files_for_ide: invalid arguments: ${arg_UNPARSED_ARGUMENTS}; missing values: ${arg_KEYWORDS_MISSING_VALUES}"
    )
  endif()
  if(NOT TARGET "${target}")
    message(
      FATAL_ERROR
      "arrange_target_files_for_ide: unknown target '${target}'."
    )
  endif()
  get_target_property(alias "${target}" ALIASED_TARGET)
  get_target_property(imported "${target}" IMPORTED)
  if(alias OR imported)
    message(
      FATAL_ERROR
      "arrange_target_files_for_ide requires a locally defined, non-alias target: ${target}."
    )
  endif()
  get_target_property(source_dir "${target}" SOURCE_DIR)
  get_target_property(binary_dir "${target}" BINARY_DIR)
  get_target_property(target_type "${target}" TYPE)
  if(NOT source_dir STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    message(
      FATAL_ERROR
      "arrange_target_files_for_ide must be called in the directory that defines ${target}."
    )
  endif()
  set(entries)
  foreach(property IN ITEMS SOURCES INTERFACE_SOURCES)
    get_target_property(value "${target}" "${property}")
    if(value)
      list(APPEND entries ${value})
    endif()
  endforeach()
  # A public/interface HEADERS file set need not also be listed in SOURCES.
  set(header_sets)
  foreach(property IN ITEMS HEADER_SETS INTERFACE_HEADER_SETS)
    get_target_property(value "${target}" "${property}")
    if(value)
      list(APPEND header_sets ${value})
    endif()
  endforeach()
  list(REMOVE_DUPLICATES header_sets)
  foreach(header_set IN LISTS header_sets)
    get_target_property(value "${target}" "HEADER_SET_${header_set}")
    if(value)
      list(APPEND entries ${value})
    endif()
  endforeach()
  list(REMOVE_DUPLICATES entries)
  # Generated headers may have one file per build configuration. Expand this
  # known token only in otherwise literal build-tree paths; retain diagnostics
  # for expressions whose source inventory we cannot determine.
  set(inventory_entries)
  foreach(entry IN LISTS entries)
    string(REPLACE "$<CONFIG>" "" literal_path "${entry}")
    if(
      IS_ABSOLUTE
        "${entry}"
      AND
        entry
          MATCHES
          "\\$<CONFIG>"
      AND
        NOT
          literal_path
            MATCHES
            "\\$<"
    )
      cmake_path(IS_PREFIX binary_dir "${literal_path}" NORMALIZE in_binary)
      if(in_binary AND NOT binary_dir STREQUAL source_dir)
        if(CMAKE_CONFIGURATION_TYPES)
          foreach(configuration IN LISTS CMAKE_CONFIGURATION_TYPES)
            string(
              REPLACE
              "$<CONFIG>"
              "${configuration}"
              configured_path
              "${entry}"
            )
            list(APPEND inventory_entries "${configured_path}")
          endforeach()
        else()
          string(
            REPLACE
            "$<CONFIG>"
            "${CMAKE_BUILD_TYPE}"
            configured_path
            "${entry}"
          )
          list(APPEND inventory_entries "${configured_path}")
        endif()
        continue()
      endif()
    endif()
    list(APPEND inventory_entries "${entry}")
  endforeach()
  set(declared)
  set(source_files)
  set(generated_files)
  set(external_files)
  foreach(entry IN LISTS inventory_entries)
    _oxygen_inventory_path("${entry}" path)
    if(path STREQUAL "")
      continue()
    endif()
    get_source_file_property(generated "${path}" GENERATED)
    if(generated AND NOT IS_ABSOLUTE "${path}")
      cmake_path(ABSOLUTE_PATH path BASE_DIRECTORY "${binary_dir}" NORMALIZE)
    else()
      cmake_path(ABSOLUTE_PATH path BASE_DIRECTORY "${source_dir}" NORMALIZE)
    endif()
    # VS treats interface-library sources as generic None items by default.
    # Represent declared headers explicitly as C++ header items in both the
    # native project and its filters, without introducing a utility project.
    if(
      CMAKE_GENERATOR
        MATCHES
        "^Visual Studio"
      AND
        target_type
          STREQUAL
          "INTERFACE_LIBRARY"
      AND
        path
          MATCHES
          "\\.(h|hpp)$"
    )
      get_source_file_property(vs_tool "${path}" VS_TOOL_OVERRIDE)
      if(NOT vs_tool)
        set_source_files_properties(
          "${path}"
          PROPERTIES
            VS_TOOL_OVERRIDE
              ClInclude
        )
      endif()
    endif()
    cmake_path(IS_PREFIX source_dir "${path}" NORMALIZE in_source)
    cmake_path(IS_PREFIX binary_dir "${path}" NORMALIZE in_binary)
    if(in_binary AND NOT binary_dir STREQUAL source_dir)
      list(APPEND generated_files "${path}")
    elseif(in_source)
      list(APPEND source_files "${path}")
    else()
      list(APPEND external_files "${path}")
    endif()
    if(CMAKE_HOST_WIN32)
      string(TOLOWER "${path}" path)
    endif()
    list(APPEND declared "${path}")
  endforeach()
  if(source_files)
    source_group(TREE "${source_dir}" PREFIX "src" FILES ${source_files})
  endif()
  if(generated_files)
    source_group(
      TREE "${binary_dir}"
      PREFIX "generated"
      FILES
        ${generated_files}
    )
  endif()
  if(external_files)
    source_group("external" FILES ${external_files})
  endif()

  # Deliberately no CONFIGURE_DEPENDS: inventory runs at configure time, and its
  # glob must never become the build's source list or a source of build rules.
  file(
    GLOB_RECURSE existing_files
    RELATIVE "${source_dir}"
    "${source_dir}/*.h"
    "${source_dir}/*.hpp"
    "${source_dir}/*.c"
    "${source_dir}/*.cpp"
  )
  set(missing)
  foreach(file IN LISTS existing_files)
    string(TOLOWER "${file}" lower_file)
    if(lower_file MATCHES "(^|/)(test|benchmarks|examples|tools)/")
      continue()
    endif()
    set(skip FALSE)
    foreach(pattern IN LISTS arg_EXCLUDE_PATTERNS)
      string(TOLOWER "${pattern}" lower_pattern)
      if(
        NOT
          lower_pattern
            STREQUAL
            ""
        AND
          lower_file
            MATCHES
            "${lower_pattern}"
      )
        set(skip TRUE)
        break()
      endif()
    endforeach()
    if(skip)
      continue()
    endif()
    set(path "${source_dir}/${file}")
    cmake_path(NORMAL_PATH path)
    if(CMAKE_HOST_WIN32)
      string(TOLOWER "${path}" path)
    endif()
    if(NOT path IN_LIST declared)
      list(APPEND missing "${file}")
    endif()
  endforeach()
  if(missing)
    list(JOIN missing "\n  " missing_text)
    message(
      AUTHOR_WARNING
      "The following files exist in the source directory but are NOT part of the target '${target}':\n  ${missing_text}"
    )
  endif()
endfunction()
