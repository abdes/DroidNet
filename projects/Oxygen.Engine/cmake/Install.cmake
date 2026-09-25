# Distributed under the 3-Clause BSD License. See accompanying file LICENSE.
# SPDX-License-Identifier: BSD-3-Clause

include_guard(GLOBAL)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
option(OXYGEN_INSTALL "Install Oxygen artifacts." ${PROJECT_IS_TOP_LEVEL})

set(OXYGEN_INSTALL_LIB "${CMAKE_INSTALL_LIBDIR}")
set(OXYGEN_INSTALL_BIN "${CMAKE_INSTALL_BINDIR}")
set(OXYGEN_INSTALL_INCLUDE "${CMAKE_INSTALL_INCLUDEDIR}")
set(OXYGEN_INSTALL_CMAKE "${CMAKE_INSTALL_LIBDIR}/cmake/Oxygen")
set(OXYGEN_INSTALL_DATA "${CMAKE_INSTALL_DATADIR}/oxygen")
set(OXYGEN_INSTALL_SCHEMAS "${OXYGEN_INSTALL_DATA}/schemas")
set(OXYGEN_INSTALL_MISC "${OXYGEN_INSTALL_DATA}")
set(OXYGEN_INSTALL_DOC "${CMAKE_INSTALL_DOCDIR}")
set(OXYGEN_INSTALL_SHARED "${CMAKE_INSTALL_LIBDIR}")
if(WIN32)
  set(OXYGEN_INSTALL_SHARED "${CMAKE_INSTALL_BINDIR}")
endif()

if(OXYGEN_INSTALL)
  if(PROJECT_IS_TOP_LEVEL AND NOT OXYGEN_CONAN_PACKAGE_BUILD)
    if(NOT DEFINED OXYGEN_CONAN_DEPLOY_DIR)
      set(OXYGEN_CONAN_DEPLOY_DIR "${OXYGEN_PROJECT_SOURCE_DIR}/out/install")
    endif()
    if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
      set_property(
        CACHE
          CMAKE_INSTALL_PREFIX
        PROPERTY
          VALUE
            "${OXYGEN_CONAN_DEPLOY_DIR}"
      )
    endif()
    # Only the default developer destination follows the selected configuration.
    # In a fresh install-script process, --prefix is a cache definition, whereas
    # the generated default is a normal variable. Never rewrite that override.
    # ALL_COMPONENTS keeps full and component installs on the same destination.
    set(OXYGEN_DEFAULT_INSTALL_ROOT "${OXYGEN_CONAN_DEPLOY_DIR}")
    configure_file(
      "${CMAKE_CURRENT_LIST_DIR}/InstallPrefix.cmake.in"
      "${PROJECT_BINARY_DIR}/oxygen-install-prefix.cmake"
      @ONLY
    )
    install(
      SCRIPT "${PROJECT_BINARY_DIR}/oxygen-install-prefix.cmake"
      ALL_COMPONENTS
    )
  endif()
  if(PROJECT_IS_TOP_LEVEL)
    if(OXYGEN_BUILD_FULL_ENGINE)
      install(
        FILES
          "${CMAKE_CURRENT_LIST_DIR}/SDK_README.md"
        DESTINATION .
        RENAME README.md
        COMPONENT Oxygen_dev
      )
    endif()
    install(
      FILES
        "${OXYGEN_PROJECT_SOURCE_DIR}/AUTHORS"
        "${OXYGEN_PROJECT_SOURCE_DIR}/LICENSE"
      DESTINATION "${OXYGEN_INSTALL_MISC}"
      COMPONENT Oxygen_dev
    )
    install(
      FILES
        "${CMAKE_CURRENT_LIST_DIR}/oxygen-source.schema.json"
      DESTINATION "${OXYGEN_INSTALL_SCHEMAS}"
      COMPONENT Oxygen_data
    )
  endif()
endif()

function(oxygen_module_install)
  if(NOT OXYGEN_INSTALL)
    return()
  endif()
  cmake_parse_arguments(PARSE_ARGV 0 arg "" "EXPORT;INCLUDE_PREFIX" "TARGETS")
  if(
    DEFINED
      arg_UNPARSED_ARGUMENTS
    OR
      DEFINED
        arg_KEYWORDS_MISSING_VALUES
    OR
      NOT
        arg_TARGETS
  )
    message(FATAL_ERROR "oxygen_module_install: invalid/missing arguments.")
  endif()
  if(NOT arg_EXPORT STREQUAL "oxygen")
    message(FATAL_ERROR "oxygen_module_install: expected EXPORT oxygen.")
  endif()
  foreach(_target IN LISTS arg_TARGETS)
    if(NOT TARGET "${_target}")
      message(FATAL_ERROR "Cannot install missing target ${_target}.")
    endif()
    get_target_property(_type "${_target}" TYPE)
    if(_type STREQUAL "EXECUTABLE")
      install(
        TARGETS
          "${_target}"
        RUNTIME
          DESTINATION "${OXYGEN_INSTALL_BIN}"
          COMPONENT Oxygen_runtime
      )
    else()
      string(REGEX REPLACE "^oxygen-" "" _export_name "${_target}")
      set_property(
        TARGET
          "${_target}"
        PROPERTY
          EXPORT_NAME
            "${_export_name}"
      )
      install(
        TARGETS
          "${_target}"
        EXPORT OxygenTargets
        RUNTIME
          DESTINATION "${OXYGEN_INSTALL_BIN}"
          COMPONENT Oxygen_runtime
        LIBRARY
          DESTINATION "${OXYGEN_INSTALL_SHARED}"
          COMPONENT Oxygen_runtime
        ARCHIVE
          DESTINATION "${OXYGEN_INSTALL_LIB}"
          COMPONENT Oxygen_dev
        FILE_SET
        HEADERS
          DESTINATION "${OXYGEN_INSTALL_INCLUDE}/${arg_INCLUDE_PREFIX}"
          COMPONENT Oxygen_dev
      )
      set_property(
        GLOBAL
        APPEND
        PROPERTY
          OXYGEN_INSTALLED_MODULES
            "${META_MODULE_NAME}"
      )
      set_property(
        GLOBAL
        APPEND
        PROPERTY
          OXYGEN_INSTALLED_TARGETS
            "${_target}"
      )
    endif()
  endforeach()
endfunction()

function(oxygen_finalize_install)
  if(NOT OXYGEN_INSTALL)
    return()
  endif()
  get_property(
    OXYGEN_INSTALLED_MODULES
    GLOBAL
    PROPERTY OXYGEN_INSTALLED_MODULES
  )
  if(NOT OXYGEN_INSTALLED_MODULES)
    return()
  endif()
  list(REMOVE_DUPLICATES OXYGEN_INSTALLED_MODULES)
  include("${CMAKE_CURRENT_FUNCTION_LIST_DIR}/ConanMetadata.cmake")
  oxygen_generate_conan_metadata()
  # Conan's graph produces these rules per configuration. Their destinations are
  # relative; installed consumers never load the producer's Conan toolchain.
  if(NOT OXYGEN_CONAN_PACKAGE_BUILD AND OXYGEN_SDK_DEPENDENCY_DIR)
    file(
      GLOB _oxygen_dependency_rules
      "${OXYGEN_SDK_DEPENDENCY_DIR}/install-*.cmake"
    )
    foreach(_rules IN LISTS _oxygen_dependency_rules)
      include("${_rules}")
    endforeach()
    configure_package_config_file(
      "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/OxygenSDKPaths.cmake.in"
      "${PROJECT_BINARY_DIR}/OxygenSDKPaths.cmake"
      INSTALL_DESTINATION "${OXYGEN_INSTALL_CMAKE}"
      NO_SET_AND_CHECK_MACRO
      NO_CHECK_REQUIRED_COMPONENTS_MACRO
    )
    install(
      FILES
        "${PROJECT_BINARY_DIR}/OxygenSDKPaths.cmake"
      DESTINATION "${OXYGEN_INSTALL_CMAKE}"
      COMPONENT Oxygen_dev
    )
  endif()
  configure_package_config_file(
    "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/OxygenConfig.cmake.in"
    "${PROJECT_BINARY_DIR}/OxygenConfig.cmake"
    INSTALL_DESTINATION "${OXYGEN_INSTALL_CMAKE}"
  )
  write_basic_package_version_file(
    "${PROJECT_BINARY_DIR}/OxygenConfigVersion.cmake"
    VERSION "${META_VERSION}"
    COMPATIBILITY ExactVersion
  )
  install(
    EXPORT OxygenTargets
    NAMESPACE oxygen::
    DESTINATION "${OXYGEN_INSTALL_CMAKE}"
    COMPONENT Oxygen_dev
  )
  install(
    FILES
      "${PROJECT_BINARY_DIR}/OxygenConfig.cmake"
      "${PROJECT_BINARY_DIR}/OxygenConfigVersion.cmake"
    DESTINATION "${OXYGEN_INSTALL_CMAKE}"
    COMPONENT Oxygen_dev
  )
endfunction()
