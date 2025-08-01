# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

cmake_minimum_required(VERSION 3.29)

list(PREPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
# If this project is not the master project, prioritize the master project
# cmake include dir if it exists.
if(
  NOT
    CMAKE_CURRENT_SOURCE_DIR
      STREQUAL
      CMAKE_SOURCE_DIR
  AND
    EXISTS
      "${CMAKE_SOURCE_DIR}/cmake"
)
  list(PREPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
endif()
# Include the log helpers module early so we can use early for logging the
# project hierarchy
include(LogHelpers)

set(CMAKE_DEBUG_POSTFIX "-d")

# ------------------------------------------------------------------------------
# Project description and (meta) information
# ------------------------------------------------------------------------------

# Get git revision
include(GetGitRevisionDescription)
get_git_head_revision(GIT_REFSPEC GIT_SHA1)
string(SUBSTRING "${GIT_SHA1}" 0 12 GIT_REV)
if(NOT GIT_SHA1)
  set(GIT_REV "0")
endif()

# Meta information about the project
include(VersionHelpers)
# gersemi: off
set(META_PROJECT_NAME        "Oxygen")
set(META_PROJECT_DESCRIPTION "Oxygen game engine")
set(META_AUTHOR_ORGANIZATION "The Authors")
set(META_GITHUB_REPO         "https://github.com/abdes/OxygenEngine")
set(META_AUTHOR_DOMAIN       "https://github.com/abdes/OxygenEngine")
set(META_AUTHOR_MAINTAINER   "Abdessattar Sassi")
asap_version_read() # from `VERSION` file
set(META_VERSION_REVISION    "${GIT_REV}")
set(META_VERSION             "${META_VERSION_MAJOR}.${META_VERSION_MINOR}.${META_VERSION_PATCH}")
set(META_NAME_VERSION        "${META_PROJECT_NAME} v${META_VERSION} (${META_VERSION_REVISION})")
# gersemi: on

string(MAKE_C_IDENTIFIER ${META_PROJECT_NAME} META_PROJECT_ID)
string(TOUPPER ${META_PROJECT_ID} META_PROJECT_ID)
string(TOLOWER ${META_PROJECT_ID} META_PROJECT_ID_LOWER)

# Determine if this is built as a subproject  or if it is the master project.
if(NOT DEFINED ${META_PROJECT_ID}_IS_MASTER_PROJECT)
  set(${META_PROJECT_ID}_IS_MASTER_PROJECT OFF)
  if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    set(${META_PROJECT_ID}_IS_MASTER_PROJECT ON)
  endif()
endif()

# Push the project on the hierarchy stack after we have determined if it is a
# master or a sub-project.
asap_push_project(${META_PROJECT_NAME})

# ------------------------------------------------------------------------------
# Project Declaration
# ------------------------------------------------------------------------------

# Generate folders for IDE targets (e.g., VisualStudio solutions)
set_property(
  GLOBAL
  PROPERTY
    USE_FOLDERS
      ON
)
set(IDE_FOLDER "${META_PROJECT_NAME}")

# Declare project
project(
  ${META_PROJECT_NAME}
  VERSION ${META_VERSION}
  DESCRIPTION ${META_PROJECT_DESCRIPTION}
  HOMEPAGE_URL ${META_GITHUB_REPO}
  LANGUAGES
    CXX
)

# The default build type provided by CMake is to include no compiler flags for
# optimization. For some projects you may want to set a default build type so
# that you do not have to remember to set it.

# TODO: check for multi-config
# get_property(IS_MULTICONFIG GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
# if (NOT IS_MULTICONFIG AND NOT DEFINED CMAKE_BUILD_TYPE)
#     set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose the type of build.")
# endif()

if(NOT CMAKE_CONFIGURATION_TYPES)
  message(STATUS "Setting build type to 'Debug' as none was specified.")
  set(
    DCMAKE_DEFAULT_CONFIGS
    Debug
    CACHE STRING
    "Choose the type of build."
    FORCE
  )
  set_property(
    CACHE
      DCMAKE_CONFIGURATION_TYPES
    PROPERTY
      STRINGS
        "Debug"
        "Release"
        "RelWithDebInfo"
  )
endif()

# Set output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Set the source tree root
set(OXYGEN_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src")

# ------------------------------------------------------------------------------
# Project configuration options
# ------------------------------------------------------------------------------

# Project options
# gersemi: off
set(OPTIONS_LIST
  BUILD_SHARED_LIBS        "Build shared instead of static libraries." OFF
  OXYGEN_BUILD_TESTS       "Setup target to build and run tests." ON
  OXYGEN_BUILD_EXAMPLES    "Setup target to build the examples." ON
  OXYGEN_BUILD_DOCS        "Setup target to build the documentation." ON
  OXYGEN_WITH_ASAN         "Instrument code with address sanitizer" OFF
  OXYGEN_WITH_COVERAGE     "Instrument code to measure coverage" OFF
  # OXYGEN_WITH_UBSAN        "Instrument code with undefined behavior sanitizer" OFF
  # OXYGEN_WITH_TSAN         "Instrument code with thread sanitizer" OFF
  # OXYGEN_WITH_VALGRIND     "Instrument code with valgrind profilers" OFF
  OXYGEN_WITH_DOXYGEN      "Create targets for doxygen api docs" ON
  # OXYGEN_WITH_SPHINX       "Create targets for sphinx docs" OFF
  # OXYGEN_WITH_CLANG_FORMAT "Create targets for clang-format" ON
  # OXYGEN_WITH_CLANG_TIDY   "Create targets for clang-tidy" ON
  OXYGEN_USE_CCACHE        "Enable caching of compiled artifacts using ccache" OFF
)
# gersemi: on
# Process each option

message(STATUS "PROJECT OPTIONS:")
list(LENGTH OPTIONS_LIST OPTIONS_LIST_length)
math(EXPR OPTIONS_LIST_length "${OPTIONS_LIST_length}-1")
foreach(index RANGE 0 ${OPTIONS_LIST_length} 3)
  list(GET OPTIONS_LIST ${index} OPTION_NAME)
  math(EXPR index "${index}+1")
  list(GET OPTIONS_LIST ${index} OPTION_DESCRIPTION)
  math(EXPR index "${index}+1")
  list(GET OPTIONS_LIST ${index} OPTION_DEFAULT)

  if(DEFINED ${OPTION_NAME})
    if(NOT "${${OPTION_NAME}}" STREQUAL "$CACHE{${OPTION_NAME}}")
      message(
        STATUS
        ". ${OPTION_NAME}: ${${OPTION_NAME}} - provided (cache: $CACHE{${OPTION_NAME}})"
      )
    else()
      message(STATUS ". ${OPTION_NAME}: ${${OPTION_NAME}} - cache")
    endif()
  else()
    set(${OPTION_NAME} ${OPTION_DEFAULT} CACHE BOOL ${OPTION_DESCRIPTION} FORCE)
    message(STATUS ". ${OPTION_NAME}: ${${OPTION_NAME}} - force set")
  endif()
endforeach()

# ------------------------------------------------------------------------------
# Top level code generation
# ------------------------------------------------------------------------------

# Generate a clangd configuration file that points to the compilation database
# in the cmake build directory. We need to do this as the build directory is
# different for every preset and can be different as well when the user decides
# to build somewhere else. Currently we cannot configure this properly in vscode
# settings. See https://github.com/clangd/vscode-clangd/issues/48
if(NOT EXISTS ${CMAKE_SOURCE_DIR}/.clangd)
  configure_file(.clangd.in ${CMAKE_SOURCE_DIR}/.clangd @ONLY)
endif()

# ------------------------------------------------------------------------------
# Testing
# ------------------------------------------------------------------------------

if(OXYGEN_BUILD_TESTS)
  include(CTest)
endif()

# ------------------------------------------------------------------------------
# Documentation - doxygen, sphinx/breathe/exhale
# ------------------------------------------------------------------------------

if(OXYGEN_BUILD_DOCS)
  if(OXYGEN_WITH_DOXYGEN)
    # Doxygen
    set(DOXYGEN_BUILD_DIR "${CMAKE_BINARY_DIR}/dox")
    include(DoxGeneration)
  endif()
endif()

# ------------------------------------------------------------------------------
# Additional CMake modules
# ------------------------------------------------------------------------------

# Register general cmake commands
include(BuildHelpers)
#include(AsapTargets)

# Override the ${META_PROJECT_ID}_INSTALL option to ON/OFF to respectively force
# install/no-install behavior for this project. This is particularly useful when
# this project is used as a sub-project and the user publicly depends on it and
# wants to have a self-contained install.
include(Install)

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

set(OXYGEN_COMMON_CXX_FLAGS)
if(OXYGEN_WITH_ASAN)
  list(APPEND OXYGEN_COMMON_CXX_FLAGS "-fsanitize=address")
  if(
    CMAKE_CXX_COMPILER_ID
      STREQUAL
      "Clang"
    OR
      CMAKE_CXX_COMPILER_ID
        STREQUAL
        "AppleClang"
    OR
      CMAKE_CXX_COMPILER_ID
        STREQUAL
        "GNU"
  )
    add_link_options("-fsanitize=address")
  endif()
endif()
if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
  if(OXYGEN_WITH_ASAN OR OXYGEN_WITH_UBSAN OR OXYGEN_WITH_TSAN)
    add_link_options("/INCREMENTAL:NO")
  endif()
  # Enable C++ latest features
  list(
    APPEND
    OXYGEN_COMMON_CXX_FLAGS
    "/Zc:__cplusplus"
    "/Zc:preprocessor"
  )
  list(APPEND OXYGEN_COMMON_CXX_FLAGS "/utf-8") # Set character set to UTF-8
  list(APPEND OXYGEN_COMMON_CXX_FLAGS "/GR-") # Disable RTTI
  list(APPEND OXYGEN_COMMON_CXX_FLAGS "/W4") # Warning level 4
  list(APPEND OXYGEN_COMMON_CXX_FLAGS "/DNOMINMAX") # Always define NOMINMAX for MSVC
  #  list(APPEND OXYGEN_COMMON_CXX_FLAGS "/WX") # Treat warnings as errors
else()
  list(APPEND OXYGEN_COMMON_CXX_FLAGS "-fno-rtti") # Disable RTTI
  list(APPEND OXYGEN_COMMON_CXX_FLAGS "-fPIC")
  # Enable all warnings and treat them as errors
  list(
    APPEND
    OXYGEN_COMMON_CXX_FLAGS
    "-Wall"
    "-Wextra"
    "-Werror"
  )
endif()

# ---- Speedup build using ccache (needs CPM) ----
include(cmake/FasterBuild.cmake)

# ------------------------------------------------------------------------------
# Print a summary of the configuration
# ------------------------------------------------------------------------------

message(STATUS "PROJECT `${META_NAME_VERSION}`")
message(STATUS "")
message(STATUS "  -- Directories --")
message(STATUS "     Source directory: ${CMAKE_SOURCE_DIR}")
message(STATUS "     Build directory: ${CMAKE_BINARY_DIR}")
message(STATUS "     Install prefix: ${CMAKE_INSTALL_PREFIX}")
message(STATUS "")
message(STATUS "  -- General configuration --")
message(STATUS "     CMake version: ${CMAKE_VERSION}")
message(
  STATUS
  "     C++ compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}"
)
message(STATUS "     Compiler flags: ${OXYGEN_COMMON_CXX_FLAGS}")
message(STATUS "     Exe Linker flags: ${CMAKE_EXE_LINKER_FLAGS}")
message(STATUS "     Build system: ${CMAKE_GENERATOR}")
message(STATUS "")
message(STATUS "  -- Build configuration --")
message(STATUS "     Export compile commands: ${CMAKE_EXPORT_COMPILE_COMMANDS}")
message(STATUS "     Build shared libraries: ${BUILD_SHARED_LIBS}")
message(STATUS "     Build tests: ${OXYGEN_BUILD_TESTS}")
message(STATUS "     Build examples: ${OXYGEN_BUILD_EXAMPLES}")
message(STATUS "     Build documentation: ${OXYGEN_BUILD_DOCS}")
message(STATUS "     Address sanitizer: ${OXYGEN_WITH_ASAN}")
message(STATUS "     Code coverage: ${OXYGEN_WITH_COVERAGE}")
message(STATUS "     Doxygen: ${OXYGEN_WITH_DOXYGEN}")
# message(STATUS "     Undefined behavior sanitizer: ${OXYGEN_WITH_UBSAN}")
# message(STATUS "     Thread sanitizer: ${OXYGEN_WITH_TSAN}")
# message(STATUS "     Valgrind: ${OXYGEN_WITH_VALGRIND}")
# message(STATUS "     Sphinx: ${OXYGEN_WITH_SPHINX}")
# message(STATUS "     Clang format: ${OXYGEN_WITH_CLANG_FORMAT}")
# message(STATUS "     Clang tidy: ${OXYGEN_WITH_CLANG_TIDY}")
message(STATUS "     CCache: ${OXYGEN_USE_CCACHE}")
message(STATUS "")

# ------------------------------------------------------------------------------
# Project modules
# ------------------------------------------------------------------------------

add_subdirectory("src")

if(OXYGEN_BUILD_EXAMPLES)
  add_subdirectory("Examples")
endif()

# ------------------------------------------------------------------------------
# Code analyzers: clang-tidy, cppcheck, valgrind, sanitizers, etc...
#
# Call after sub-modules have been added so that source code files can be
# properly collected for analysis.
# ------------------------------------------------------------------------------

# if(${META_PROJECT_ID}_IS_MASTER_PROJECT)
#   if(OXYGEN_WITH_CLANG_TIDY)
#     include(ClangTidy)
#     asap_create_clang_tidy_targets()
#   endif()

#   if(OXYGEN_WITH_CLANG_FORMAT)
#     include(ClangFormat)
#     asap_create_clang_format_targets()
#   endif()
# endif()

asap_pop_project(${META_PROJECT_NAME})
