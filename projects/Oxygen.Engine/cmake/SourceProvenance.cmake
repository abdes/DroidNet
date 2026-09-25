# Distributed under the 3-Clause BSD License. See accompanying file LICENSE.
# SPDX-License-Identifier: BSD-3-Clause

include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/VersionHelpers.cmake")

function(_oxygen_git source_dir output status)
  execute_process(
    COMMAND
      "${GIT_EXECUTABLE}" --no-optional-locks -C "${source_dir}" ${ARGN}
    RESULT_VARIABLE _status
    OUTPUT_VARIABLE _output
    ERROR_QUIET
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )
  set(${output} "${_output}" PARENT_SCOPE)
  set(${status} "${_status}" PARENT_SCOPE)
endfunction()

# Schema: oxygen-source.schema.json. Unknown provenance is null/null, never a
# fabricated commit or a claim that an unverifiable source tree is clean.
function(oxygen_read_source_provenance source_dir prefix)
  asap_version_read(VERSION_FILE "${source_dir}/VERSION")
  set(_commit "")
  set(_dirty null)
  set(_capsule "${source_dir}/oxygen-source.json")
  if(EXISTS "${_capsule}")
    file(READ "${_capsule}" _json)
    string(JSON _members ERROR_VARIABLE _error LENGTH "${_json}")
    if(_error OR NOT _members EQUAL 4)
      message(FATAL_ERROR "Invalid source provenance: ${_capsule}")
    endif()
    string(JSON _schema_type TYPE "${_json}" schema)
    string(JSON _schema GET "${_json}" schema)
    string(JSON _version_type TYPE "${_json}" version)
    string(JSON _version GET "${_json}" version)
    string(JSON _commit_type TYPE "${_json}" commit)
    string(JSON _dirty_type TYPE "${_json}" dirty)
    if(
      NOT
        _schema_type
          STREQUAL
          "NUMBER"
      OR
        NOT
          _schema
            EQUAL
            1
      OR
        NOT
          _version_type
            STREQUAL
            "STRING"
      OR
        NOT
          _version
            STREQUAL
            META_VERSION
    )
      message(
        FATAL_ERROR
        "Source provenance schema/version does not match VERSION: ${_capsule}"
      )
    endif()
    if(_commit_type STREQUAL "NULL" AND _dirty_type STREQUAL "NULL")
      # Explicitly unknown source provenance.
    elseif(_commit_type STREQUAL "STRING" AND _dirty_type STREQUAL "BOOLEAN")
      string(JSON _commit GET "${_json}" commit)
      string(LENGTH "${_commit}" _length)
      if(
        NOT
          _commit
            MATCHES
            "^[0-9a-f]+$"
        OR
          NOT
            (
              _length
                EQUAL
                40
              OR
                _length
                  EQUAL
                  64
            )
      )
        message(FATAL_ERROR "Invalid full commit in ${_capsule}")
      endif()
      string(JSON _modified GET "${_json}" dirty)
      if(_modified)
        set(_dirty true)
      else()
        set(_dirty false)
      endif()
    else()
      message(FATAL_ERROR "Invalid commit/dirty state in ${_capsule}")
    endif()
  else()
    # This also runs before project(). find_package() can initialize the
    # toolchain in this function scope, losing its normal variables on return.
    # Git is a host executable; locating it must not initialize target platforms.
    if(NOT CMAKE_DISABLE_FIND_PACKAGE_Git)
      find_program(GIT_EXECUTABLE NAMES git NO_CMAKE_FIND_ROOT_PATH)
    endif()
    if(GIT_EXECUTABLE AND NOT CMAKE_DISABLE_FIND_PACKAGE_Git)
      # Reject an archive merely sitting inside an unrelated Git worktree.
      _oxygen_git(
        "${source_dir}"
        _tracked
        _status
        ls-files
        --error-unmatch
        --
        VERSION
        CMakeLists.txt
      )
      if(_status STREQUAL "0")
        _oxygen_git(
          "${source_dir}"
          _shallow
          _status
          rev-parse
          --is-shallow-repository
        )
        if(_status STREQUAL "0" AND _shallow STREQUAL "false")
          # The engine directory is the component boundary. No shared repository build
          # files outside this directory are currently consumed. Keep this scope aligned with Conan.
          set(
            _scope
            .
            ":(exclude)plans/CMAKE_CONAN_MODERNIZATION_PLAN.md"
          )
          _oxygen_git(
            "${source_dir}"
            _candidate
            _status
            log
            -1
            --format=%H
            --
            ${_scope}
          )
          string(LENGTH "${_candidate}" _length)
          if(
            _status
              STREQUAL
              "0"
            AND
              _candidate
                MATCHES
                "^[0-9a-f]+$"
            AND
              (
                _length
                  EQUAL
                  40
                OR
                  _length
                    EQUAL
                    64
              )
          )
            _oxygen_git(
              "${source_dir}"
              _changes
              _status
              status
              --porcelain=v1
              --untracked-files=all
              --
              ${_scope}
            )
            if(_status STREQUAL "0")
              set(_commit "${_candidate}")
              if(_changes STREQUAL "")
                set(_dirty false)
              else()
                set(_dirty true)
              endif()
            endif()
          endif()
        endif()
      endif()
    endif()
  endif()

  set(_revision unknown)
  set(_short unknown)
  set(_commit_json null)
  if(NOT _commit STREQUAL "")
    set(_revision "${_commit}")
    string(SUBSTRING "${_commit}" 0 12 _short)
    set(_commit_json "\"${_commit}\"")
    if(_dirty STREQUAL "true")
      string(APPEND _revision "-dirty")
      string(APPEND _short "-dirty")
    endif()
  endif()
  set(
    _json
    "{\n  \"schema\": 1,\n  \"version\": \"${META_VERSION}\",\n  \"commit\": ${_commit_json},\n  \"dirty\": ${_dirty}\n}\n"
  )
  foreach(_part VERSION VERSION_MAJOR VERSION_MINOR VERSION_PATCH)
    set(${prefix}_${_part} "${META_${_part}}" PARENT_SCOPE)
  endforeach()
  set(${prefix}_COMMIT "${_commit}" PARENT_SCOPE)
  set(${prefix}_DIRTY "${_dirty}" PARENT_SCOPE)
  set(${prefix}_REVISION "${_revision}" PARENT_SCOPE)
  set(${prefix}_SHORT_REVISION "${_short}" PARENT_SCOPE)
  set(${prefix}_JSON "${_json}" PARENT_SCOPE)
endfunction()

# Core owns the compiled version API. Run the small metadata check whenever Core
# is built, but touch the header/capsule only if content actually changed.
function(oxygen_add_version_metadata target)
  get_target_property(_type ${target} TYPE)
  if(MSVC AND _type STREQUAL "STATIC_LIBRARY")
    # MSVC incremental linking can retain old data from a rebuilt archive.
    # Carry the correctness requirement to static Core's final consumers;
    # shared Core and unrelated targets retain their existing link policy.
    target_link_options(${target} INTERFACE /INCREMENTAL:NO)
  endif()
  set(OXYGEN_VERSION_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/version/include")
  set(
    OXYGEN_VERSION_HEADER
    "${OXYGEN_VERSION_INCLUDE_DIR}/Oxygen/Core/version-info.h"
  )
  set(
    OXYGEN_VERSION_CAPSULE
    "${CMAKE_CURRENT_BINARY_DIR}/version/oxygen-source.json"
  )
  set(OXYGEN_VERSION_TEMPLATE "${CMAKE_CURRENT_SOURCE_DIR}/version.h.in")
  set(OXYGEN_VERSION_CONFIG "${CMAKE_CURRENT_BINARY_DIR}/version/config.cmake")
  configure_file(
    "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/VersionConfig.cmake.in"
    "${OXYGEN_VERSION_CONFIG}"
    @ONLY
  )
  include("${CMAKE_CURRENT_FUNCTION_LIST_DIR}/GenerateVersion.cmake")
  add_custom_target(
    ${target}_version_info
    COMMAND
      "${CMAKE_COMMAND}" "-DOXYGEN_VERSION_CONFIG=${OXYGEN_VERSION_CONFIG}" -P
      "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/GenerateVersion.cmake"
    BYPRODUCTS
      "${OXYGEN_VERSION_HEADER}"
      "${OXYGEN_VERSION_CAPSULE}"
    COMMENT "Checking Oxygen source provenance"
    VERBATIM
  )
  add_dependencies(${target} ${target}_version_info)
  target_include_directories(
    ${target}
    BEFORE
    PUBLIC
      "$<BUILD_INTERFACE:${OXYGEN_VERSION_INCLUDE_DIR}>"
  )
  target_sources(
    ${target}
    PUBLIC
      FILE_SET HEADERS
      BASE_DIRS "${OXYGEN_VERSION_INCLUDE_DIR}"
      FILES "${OXYGEN_VERSION_HEADER}"
  )
  # One source capsule accompanies the native SDK, independent of configuration.
  if(OXYGEN_INSTALL)
    install(
      FILES
        "${OXYGEN_VERSION_CAPSULE}"
      DESTINATION share/oxygen
      COMPONENT Oxygen_dev
    )
  endif()
endfunction()
