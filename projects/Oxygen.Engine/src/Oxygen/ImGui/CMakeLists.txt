# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause).
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

# ------------------------------------------------------------------------------
# Meta information about the this module
# ------------------------------------------------------------------------------

asap_module_declare(
  MODULE_NAME "Oxygen.ImGui"
  DESCRIPTION "Integrates ImGui as a module in Oxygen Engine."
  WITHOUT_VERSION_H
)
asap_push_module("${META_MODULE_NAME}")

# ------------------------------------------------------------------------------
# Dependencies
# ------------------------------------------------------------------------------

find_package(imgui CONFIG REQUIRED)

# ==============================================================================
# Build instructions
# ==============================================================================

# ------------------------------------------------------------------------------
# Main module target
# ------------------------------------------------------------------------------

add_library(${META_MODULE_TARGET})
add_library(${META_MODULE_TARGET_ALIAS} ALIAS ${META_MODULE_TARGET})
set_target_properties(
  ${META_MODULE_TARGET}
  PROPERTIES
    DEFINE_SYMBOL
      "OXYGEN_IMGUI_EXPORTS"
    OUTPUT_NAME
      $<IF:$<BOOL:${BUILD_SHARED_LIBS}>,${META_MODULE_NAME},${META_MODULE_NAME}-static>
    VERSION
      ${META_VERSION}
    SOVERSION
      ${META_VERSION_MAJOR}
    PROJECT_LABEL
      ${META_MODULE_NAME}
)

target_sources(
  ${META_MODULE_TARGET}
  PRIVATE
    "ImGuiModule.cpp"
    "ImGuiRenderInterface.cpp"
    "SDL/imgui_impl_sdl3.h"
    "SDL/imgui_impl_sdl3.cpp"
    "SDL/ImGuiSdl3Backend.h"
    "SDL/ImGuiSdl3Backend.cpp"
  PUBLIC
    FILE_SET HEADERS
    BASE_DIRS "${OXYGEN_SOURCE_DIR}"
    FILES
      "api_export.h"
      "ImGuiModule.h"
      "ImGuiPlatformBackend.h"
      "ImGuiRenderInterface.h"
)

target_compile_options(${META_MODULE_TARGET} PUBLIC ${OXYGEN_COMMON_CXX_FLAGS})
target_compile_definitions(
  ${META_MODULE_TARGET}
  PRIVATE
    $<$<NOT:$<BOOL:${BUILD_SHARED_LIBS}>>:OXYGEN_IMGUI_STATIC>
)

target_link_libraries(
  ${META_MODULE_TARGET}
  PUBLIC
    oxygen::core
    oxygen::platform
    imgui::imgui
)

# ------------------------------------------------------------------------------
# Tests
# ------------------------------------------------------------------------------

#if(OXYGEN_BUILD_TESTS)
#  add_subdirectory("Test")
#endif()

# ==============================================================================
# Deployment instructions
# ==============================================================================

oxygen_module_install(
  EXPORT ${META_MODULE_NAMESPACE}
  TARGETS
    ${META_MODULE_TARGET}
)

asap_pop_module("${META_MODULE_NAME}")
