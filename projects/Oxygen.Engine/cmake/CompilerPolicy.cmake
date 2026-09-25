# Distributed under the 3-Clause BSD License. See accompanying file LICENSE.
# SPDX-License-Identifier: BSD-3-Clause

include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/TestRuntime.cmake")

# Apply implementation policy without exporting warnings or disabling RTTI in
# unrelated consumer code. Code deriving from Oxygen's polymorphic classes must
# itself be compiled without native RTTI (see README.md).
function(oxygen_apply_build_policy target)
  get_target_property(_type ${target} TYPE)
  if(
    NOT
      _type
        MATCHES
        "^(STATIC_LIBRARY|SHARED_LIBRARY|MODULE_LIBRARY|OBJECT_LIBRARY|EXECUTABLE)$"
  )
    return()
  endif()
  get_target_property(_applied ${target} OXYGEN_BUILD_POLICY_APPLIED)
  if(_applied)
    return()
  endif()
  set_property(
    TARGET
      ${target}
    PROPERTY
      OXYGEN_BUILD_POLICY_APPLIED
        TRUE
  )
  if(_type STREQUAL "EXECUTABLE")
    oxygen_configure_test_runtime(${target})
  endif()

  target_compile_options(
    ${target}
    PRIVATE
      "$<$<COMPILE_LANG_AND_ID:C,MSVC>:/W4;/utf-8;/Zc:preprocessor>"
      "$<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/W4;/utf-8;/Zc:preprocessor;/Zc:__cplusplus;/GR->"
      "$<$<COMPILE_LANG_AND_ID:C,GNU,Clang,AppleClang>:-Wall;-Wextra>"
      "$<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang,AppleClang>:-Wall;-Wextra;-fno-rtti>"
  )
  target_compile_definitions(
    ${target}
    PRIVATE
      "$<$<PLATFORM_ID:Windows>:NOMINMAX>"
  )
  if(CMAKE_GENERATOR MATCHES "^Visual Studio")
    target_compile_options(
      ${target}
      PRIVATE
        "$<$<COMPILE_LANG_AND_ID:C,MSVC>:/MP>"
        "$<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/MP>"
    )
  endif()

  # Preserve the existing PIC default on portable static/object libraries, but
  # respect the property initialized by Conan or the embedding parent.
  get_property(_pic_set TARGET ${target} PROPERTY POSITION_INDEPENDENT_CODE SET)
  if(
    NOT
      WIN32
    AND
      NOT
        _pic_set
    AND
      _type
        MATCHES
        "^(STATIC_LIBRARY|OBJECT_LIBRARY)$"
  )
    set_property(
      TARGET
        ${target}
      PROPERTY
        POSITION_INDEPENDENT_CODE
          ON
    )
  endif()

  # /Zi and /ZI are not cacheable with MSVC ccache. Default only Oxygen-owned
  # targets using a recognizable native ccache launcher to embedded symbols.
  # Explicit target/toolchain choices (including an empty format) remain intact.
  if(MSVC AND CMAKE_GENERATOR MATCHES "Ninja|Makefiles|WMake")
    get_property(
      _debug_format_set
      TARGET ${target}
      PROPERTY MSVC_DEBUG_INFORMATION_FORMAT
      SET
    )
    if(NOT _debug_format_set)
      get_target_property(_sources ${target} SOURCES)
      get_target_property(_target_source_dir ${target} SOURCE_DIR)
      set(_compiled_languages)
      foreach(_source IN LISTS _sources)
        # Policy is applied from Oxygen's root after subdirectories are processed.
        # Resolve source names in the owning target's directory before lookup.
        cmake_path(
          ABSOLUTE_PATH
          _source
          BASE_DIRECTORY "${_target_source_dir}"
          NORMALIZE
        )
        # get_property materializes lazy target_sources() entries so CMake can
        # determine their language; get_source_file_property can return NOTFOUND.
        get_property(
          _header_only
          SOURCE "${_source}"
          TARGET_DIRECTORY ${target}
          PROPERTY HEADER_FILE_ONLY
        )
        if(NOT _header_only)
          get_property(
            _language
            SOURCE "${_source}"
            TARGET_DIRECTORY ${target}
            PROPERTY LANGUAGE
          )
          if(_language MATCHES "^(C|CXX)$")
            list(APPEND _compiled_languages "${_language}")
          endif()
        endif()
      endforeach()
      list(REMOVE_DUPLICATES _compiled_languages)
      foreach(_language IN LISTS _compiled_languages)
        get_target_property(_launcher ${target} ${_language}_COMPILER_LAUNCHER)
        if(_launcher)
          list(GET _launcher 0 _program)
          get_filename_component(_name "${_program}" NAME)
          string(TOLOWER "${_name}" _name)
          if(
            _name
              MATCHES
              "^ccache(\\.exe)?$"
            OR
              (
                CCACHE_TOOL_PATH
                AND
                  _program
                    STREQUAL
                    CCACHE_TOOL_PATH
              )
          )
            set_property(
              TARGET
                ${target}
              PROPERTY
                MSVC_DEBUG_INFORMATION_FORMAT
                  "$<$<CONFIG:Debug,RelWithDebInfo>:Embedded>"
            )
            break()
          endif()
        endif()
      endforeach()
    endif()
  endif()

  if(NOT OXYGEN_WITH_ASAN)
    return()
  endif()
  if(MSVC)
    set_property(
      TARGET
        ${target}
      PROPERTY
        MSVC_RUNTIME_CHECKS
          ""
    )
    # Embedded debug information also works with ccache. Never use /ZI (Edit
    # and Continue) in an ASan target.
    set_property(
      TARGET
        ${target}
      PROPERTY
        MSVC_DEBUG_INFORMATION_FORMAT
          Embedded
    )
    target_compile_options(
      ${target}
      PRIVATE
        "$<$<COMPILE_LANG_AND_ID:C,MSVC>:/fsanitize=address>"
        "$<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/fsanitize=address>"
    )
    set(
      _link_options
      /INCREMENTAL:NO
      /DEBUG
    )
  elseif(CMAKE_CXX_COMPILER_ID MATCHES "^(GNU|Clang|AppleClang)$")
    target_compile_options(
      ${target}
      PRIVATE
        "$<$<COMPILE_LANGUAGE:C,CXX>:-fsanitize=address;-fno-omit-frame-pointer>"
    )
    set(_link_options -fsanitize=address)
  else()
    message(
      FATAL_ERROR
      "Oxygen ASan is unsupported with ${CMAKE_CXX_COMPILER_ID}."
    )
  endif()

  # Archives have no link step. Their final consumer must link the sanitizer
  # runtime; do not pass linker switches to the archiver.
  if(_type MATCHES "^(STATIC_LIBRARY|OBJECT_LIBRARY)$")
    target_link_options(${target} INTERFACE ${_link_options})
  elseif(_type STREQUAL "EXECUTABLE")
    target_link_options(${target} PRIVATE ${_link_options})
  else()
    # On ELF platforms the executable must also link ASan so its runtime is
    # loaded before an instrumented shared library. Carry that requirement.
    target_link_options(${target} PUBLIC ${_link_options})
  endif()
endfunction()

# Native directory target enumeration covers auxiliary libraries, tools, tests
# and examples as well as module targets. Call only on Oxygen-owned source trees;
# dependencies and an embedding parent's targets keep their own policy.
function(oxygen_apply_directory_build_policy directory)
  get_property(_targets DIRECTORY "${directory}" PROPERTY BUILDSYSTEM_TARGETS)
  foreach(_target IN LISTS _targets)
    oxygen_apply_build_policy(${_target})
  endforeach()
  get_property(_children DIRECTORY "${directory}" PROPERTY SUBDIRECTORIES)
  foreach(_child IN LISTS _children)
    cmake_path(IS_PREFIX directory "${_child}" NORMALIZE _owned)
    if(_owned)
      oxygen_apply_directory_build_policy("${_child}")
    endif()
  endforeach()
endfunction()
