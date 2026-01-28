# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

# ------------------------------------------------------------------------------
# Meta information about this module
# ------------------------------------------------------------------------------

set(description "Simple example using AsyncEngine.")

asap_module_declare(
  MODULE_NAME "Oxygen.Examples.Async"
  DESCRIPTION description
  WITHOUT_VERSION_H
)
asap_push_module("${META_MODULE_NAME}")

# ------------------------------------------------------------------------------
# Dependencies
# ------------------------------------------------------------------------------

find_package(glm CONFIG REQUIRED)
find_package(asio CONFIG REQUIRED)
find_package(sdl3 CONFIG REQUIRED)

# ------------------------------------------------------------------------------
# Sources
# ------------------------------------------------------------------------------

add_executable(
  ${META_MODULE_TARGET}
  "../main.cpp"
  "main_impl.cpp"
  "MainModule.cpp"
  "MainModule.h"
)

set_target_properties(
  ${META_MODULE_TARGET}
  PROPERTIES
    FOLDER
      "Examples"
    OUTPUT_NAME
      ${META_MODULE_NAME}
)

target_compile_features(${META_MODULE_TARGET} PRIVATE cxx_std_23)
target_compile_options(${META_MODULE_TARGET} PRIVATE ${OXYGEN_COMMON_CXX_FLAGS})
target_compile_features(${META_MODULE_TARGET} PRIVATE cxx_std_23)

target_include_directories(
  ${META_MODULE_TARGET}
  PRIVATE
    ${OXYGEN_EXAMPLES_SOURCE_DIR}
)

target_link_libraries(
  ${META_MODULE_TARGET}
  PUBLIC
    $<$<NOT:$<BOOL:${BUILD_SHARED_LIBS}>>:oxygen::cs-init>
    glm::glm
    oxygen::base
    oxygen::composition
    oxygen::oxco
    oxygen::clap
    oxygen::examples-demoshell
    oxygen::platform
    oxygen::input
    oxygen::imgui
    oxygen::loader
    oxygen::graphics-common
    oxygen::graphics-direct3d12
    oxygen::renderer
    oxygen::engine
    oxygen::data
    oxygen::scene
)
add_custom_command(
  TARGET ${META_MODULE_TARGET}
  POST_BUILD
  COMMAND_EXPAND_LISTS
  COMMAND
    ${CMAKE_COMMAND} -E copy_if_different
    $<TARGET_RUNTIME_DLLS:${META_MODULE_TARGET}>
    $<TARGET_FILE_DIR:${META_MODULE_TARGET}>
)
# ------------------------------------------------------------------------------
# Install
# ------------------------------------------------------------------------------

oxygen_module_install(
  EXPORT ${META_MODULE_NAMESPACE}
  TARGETS
    ${META_MODULE_TARGET}
)

# ------------------------------------------------------------------------------
# Tests
# ------------------------------------------------------------------------------

asap_pop_module("${META_MODULE_NAME}")
