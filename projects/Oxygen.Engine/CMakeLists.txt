# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

cmake_minimum_required(VERSION 4.2)

if(POLICY CMP0177)
  cmake_policy(SET CMP0177 NEW)
endif()

list(PREPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/LogHelpers.cmake")

set(CMAKE_DEBUG_POSTFIX "-d")

# ------------------------------------------------------------------------------
# Project description and (meta) information
# ------------------------------------------------------------------------------

# Meta information about the project
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/SourceProvenance.cmake")
# gersemi: off
set(META_PROJECT_NAME        "Oxygen")
set(META_PROJECT_DESCRIPTION "Oxygen game engine")
set(META_AUTHOR_ORGANIZATION "The Authors")
set(META_GITHUB_REPO         "https://github.com/abdes/OxygenEngine")
set(META_AUTHOR_DOMAIN       "https://github.com/abdes/OxygenEngine")
set(META_AUTHOR_MAINTAINER   "Abdessattar Sassi")
asap_version_read() # from `VERSION` file
set(META_VERSION             "${META_VERSION_MAJOR}.${META_VERSION_MINOR}.${META_VERSION_PATCH}")
# gersemi: on

string(MAKE_C_IDENTIFIER ${META_PROJECT_NAME} META_PROJECT_ID)
string(TOUPPER ${META_PROJECT_ID} META_PROJECT_ID)
string(TOLOWER ${META_PROJECT_ID} META_PROJECT_ID_LOWER)

# Silence redundant toolchain warnings (often caused by IDEs passing the
# toolchain manually when it is already defined in the CMake preset).
if(CMAKE_TOOLCHAIN_FILE)
endif()

# Declare project
project(
  ${META_PROJECT_NAME}
  VERSION ${META_VERSION}
  DESCRIPTION ${META_PROJECT_DESCRIPTION}
  HOMEPAGE_URL ${META_GITHUB_REPO}
  LANGUAGES
    C
    CXX
)

oxygen_read_source_provenance("${CMAKE_CURRENT_SOURCE_DIR}" OXYGEN_SOURCE)
set(META_VERSION_REVISION "${OXYGEN_SOURCE_REVISION}")
set(
  META_NAME_VERSION
  "${META_PROJECT_NAME} v${META_VERSION} (${OXYGEN_SOURCE_SHORT_REVISION})"
)

set(OXYGEN_IS_MASTER_PROJECT "${PROJECT_IS_TOP_LEVEL}")
set(OXYGEN_PROJECT_SOURCE_DIR "${PROJECT_SOURCE_DIR}")
asap_push_project(${META_PROJECT_NAME})
set(IDE_FOLDER "${META_PROJECT_NAME}")
if(PROJECT_IS_TOP_LEVEL)
  set_property(
    GLOBAL
    PROPERTY
      USE_FOLDERS
        ON
  )
endif()

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/ModuleSelection.cmake")
if(OXYGEN_BUILD_FULL_ENGINE)
  include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/ToolchainRequirements.cmake")
endif()

# Publish a complete File API reply even when configured outside an IDE.
# CMake Tools loads an existing index before configuring; a codemodel-only
# index prevents it from starting because its required cache object is absent.
cmake_file_api(
  QUERY
  API_VERSION 1
  CODEMODEL
    2
  CACHE
    2
  CMAKEFILES
    1
  TOOLCHAINS
    1
)

# Oxygen uses textual headers, not C++ modules. Keep ordinary compiler header
# dependencies without introducing an unused module-scanning/dyndep graph.
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

# Work around an MSBuild parallel project-reference failure mode observed with
# /m>1 (returns exit code 1 with no compiler/linker errors). Keep top-level
# parallelism enabled, but force referenced projects to build sequentially.
if(CMAKE_GENERATOR MATCHES "^Visual Studio")
  if(NOT CMAKE_VS_GLOBALS MATCHES "(^|;)BuildInParallel=")
    if(CMAKE_VS_GLOBALS)
      set(CMAKE_VS_GLOBALS "${CMAKE_VS_GLOBALS};BuildInParallel=false")
    else()
      set(CMAKE_VS_GLOBALS "BuildInParallel=false")
    endif()
  endif()
endif()

if(PROJECT_IS_TOP_LEVEL)
  include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/BuildConfiguration.cmake")
endif()

# Set output directories
foreach(_kind RUNTIME LIBRARY ARCHIVE)
  if(NOT DEFINED CMAKE_${_kind}_OUTPUT_DIRECTORY)
    if(_kind STREQUAL "ARCHIVE")
      set(CMAKE_${_kind}_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/lib")
    else()
      set(CMAKE_${_kind}_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin")
    endif()
  endif()
endforeach()

# Set the source tree root
set(OXYGEN_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src")

# ------------------------------------------------------------------------------
# Project configuration options
# ------------------------------------------------------------------------------

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/ProjectOptions.cmake")

if(OXYGEN_WITH_TRACY)
  add_compile_definitions(OXYGEN_WITH_TRACY=1)
endif()

# ------------------------------------------------------------------------------
# Installation Setup
# ------------------------------------------------------------------------------

# Install.cmake owns defaults and distinguishes developer deployment from Conan
# cache packaging. Never force a caller/toolchain's prefix here.

# ------------------------------------------------------------------------------
# Testing
# ------------------------------------------------------------------------------

include(CTest)
# Regenerate this directory's inventory even when test creation is disabled.
# Otherwise CTest can still execute stale registrations after an ON-to-OFF
# reconfigure. The two testing options control targets and add_test calls.
enable_testing()

# ------------------------------------------------------------------------------
# Documentation - doxygen, sphinx/breathe/exhale
# ------------------------------------------------------------------------------

if(OXYGEN_BUILD_DOCS)
  if(OXYGEN_WITH_DOXYGEN)
    # Doxygen
    set(DOXYGEN_BUILD_DIR "${PROJECT_BINARY_DIR}/dox")
    include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/DoxGeneration.cmake")
  endif()
endif()

# ------------------------------------------------------------------------------
# Additional CMake modules
# ------------------------------------------------------------------------------

# Register general cmake commands
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/BuildHelpers.cmake")
#include(AsapTargets)

# Override the ${META_PROJECT_ID}_INSTALL option to ON/OFF to respectively force
# install/no-install behavior for this project. This is particularly useful when
# this project is used as a sub-project and the user publicly depends on it and
# wants to have a self-contained install.
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/Install.cmake")

# ------------------------------------------------------------------------------
# Compiler flags
# ------------------------------------------------------------------------------

if(
  NOT
    DEFINED
      CMAKE_CXX_VISIBILITY_PRESET
  AND
    NOT
      DEFINED
        CMAKE_VISIBILITY_INLINES_HIDDEN
)
  set(CMAKE_CXX_VISIBILITY_PRESET hidden)
  set(CMAKE_VISIBILITY_INLINES_HIDDEN YES)
endif()

# Build policy is applied privately to all Oxygen-owned compiled targets below.
# Modules can opt individual targets into COMPILE_WARNING_AS_ERROR explicitly.
set(CMAKE_COMPILE_WARNING_AS_ERROR OFF)
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/CompilerPolicy.cmake")

# ---- Optional native compiler-cache launchers ----
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/FasterBuild.cmake")

# ------------------------------------------------------------------------------
# Print a summary of the configuration
# ------------------------------------------------------------------------------

message(STATUS "PROJECT `${META_NAME_VERSION}`")
message(STATUS "")
message(STATUS "  -- Directories --")
message(STATUS "     Source directory: ${PROJECT_SOURCE_DIR}")
message(STATUS "     Build directory: ${PROJECT_BINARY_DIR}")
message(STATUS "     Install prefix: ${CMAKE_INSTALL_PREFIX}")
message(STATUS "")
message(STATUS "  -- General configuration --")
message(STATUS "     CMake version: ${CMAKE_VERSION}")

message(
  STATUS
  "     C++ compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}"
)

message(STATUS "     Common Exe Linker flags: ${CMAKE_EXE_LINKER_FLAGS}")
message(STATUS "     Build system: ${CMAKE_GENERATOR}")
message(STATUS "")
message(STATUS "  -- Build configuration --")
message(STATUS "     Export compile commands: ${CMAKE_EXPORT_COMPILE_COMMANDS}")
message(STATUS "     Build shared libraries: ${BUILD_SHARED_LIBS}")
message(STATUS "     Build tests: ${OXYGEN_BUILD_TESTS}")
message(STATUS "     Build examples: ${OXYGEN_BUILD_EXAMPLES}")
message(STATUS "     Build documentation: ${OXYGEN_BUILD_DOCS}")
message(STATUS "     Address sanitizer: ${OXYGEN_WITH_ASAN}")
message(STATUS "     Tracy profiler: ${OXYGEN_WITH_TRACY}")
message(STATUS "     Doxygen: ${OXYGEN_WITH_DOXYGEN}")
# message(STATUS "     Undefined behavior sanitizer: ${OXYGEN_WITH_UBSAN}")
# message(STATUS "     Thread sanitizer: ${OXYGEN_WITH_TSAN}")
# message(STATUS "     Valgrind: ${OXYGEN_WITH_VALGRIND}")
# message(STATUS "     Sphinx: ${OXYGEN_WITH_SPHINX}")
# message(STATUS "     Clang format: ${OXYGEN_WITH_CLANG_FORMAT}")
# message(STATUS "     Clang tidy: ${OXYGEN_WITH_CLANG_TIDY}")
message(STATUS "     CCache: ${OXYGEN_CCACHE_STATUS}")
message(STATUS "     Configured C launcher: ${CMAKE_C_COMPILER_LAUNCHER}")
message(STATUS "     Configured CXX launcher: ${CMAKE_CXX_COMPILER_LAUNCHER}")
message(STATUS "")

# ------------------------------------------------------------------------------
# Project modules and Solution Virtual Folders
# ------------------------------------------------------------------------------

# Show the repo .github folder inside the IDE under a virtual folder named
# ".github".
file(GLOB_RECURSE _github_files "${PROJECT_SOURCE_DIR}/.github/*")

if(PROJECT_IS_TOP_LEVEL AND _github_files)
  source_group(
    TREE "${PROJECT_SOURCE_DIR}/.github"
    PREFIX ".github"
    FILES
      ${_github_files}
  )
  # Convenience target listing them (keeps them in IDE but not part of build)
  add_custom_target(GitHub SOURCES ${_github_files})
endif()

if(OXYGEN_BUILD_FULL_ENGINE)
  include(Dxc)
endif()

add_subdirectory("src")

if(OXYGEN_BUILD_FULL_ENGINE)
  add_subdirectory("Examples")
endif()

# ------------------------------------------------------------------------------
# Code analyzers: clang-tidy, cppcheck, valgrind, sanitizers, etc...
#
# Call after sub-modules have been added so that source code files can be
# properly collected for analysis.
# ------------------------------------------------------------------------------

oxygen_apply_directory_build_policy("${CMAKE_CURRENT_SOURCE_DIR}/src")
if(OXYGEN_BUILD_FULL_ENGINE)
  oxygen_apply_directory_build_policy("${CMAKE_CURRENT_SOURCE_DIR}/Examples")
endif()

oxygen_finalize_install()

asap_pop_project(${META_PROJECT_NAME})
