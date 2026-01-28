# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

# ------------------------------------------------------------------------------
# Meta information about the this module
# ------------------------------------------------------------------------------

asap_module_declare(
  MODULE_NAME "Oxygen.Examples.TexturedCube"
  DESCRIPTION
    [=[
A minimal example that renders a textured cube using the engine's bindless
material/texture binding path. The sample intentionally references an invalid
texture resource index for the cube material so the TextureBinder resolves it
to the error checkerboard texture.

Controls:
- Mouse wheel: zoom
- RMB + mouse drag: orbit

An ImGui overlay shows the current camera/orbit parameters.
]=]
  WITHOUT_VERSION_H
)

asap_push_module("${META_MODULE_NAME}")

# ------------------------------------------------------------------------------
# Build instructions
# ------------------------------------------------------------------------------

add_executable(
  ${META_MODULE_TARGET}
  "../main.cpp"
  "main_impl.cpp"
  "MainModule.h"
  "MainModule.cpp"
  "CameraController.h"
  "CameraController.cpp"
  "TextureLoadingService.h"
  "TextureLoadingService.cpp"
  "SceneSetup.h"
  "SceneSetup.cpp"
  "DebugUI.h"
  "DebugUI.cpp"
)

set_target_properties(
  ${META_MODULE_TARGET}
  PROPERTIES
    FOLDER
      "Examples"
    OUTPUT_NAME
      ${META_MODULE_NAME}
)

target_compile_options(${META_MODULE_TARGET} PRIVATE ${OXYGEN_COMMON_CXX_FLAGS})

target_include_directories(
  ${META_MODULE_TARGET}
  PRIVATE
    ${OXYGEN_EXAMPLES_SOURCE_DIR}
)

target_link_libraries(
  ${META_MODULE_TARGET}
  PRIVATE
    oxygen::examples-demoshell
    oxygen::platform
    oxygen::input
    oxygen::data
    oxygen::scene
    oxygen::core
    oxygen::clap
    oxygen::engine
    oxygen::imgui
    oxygen::graphics-direct3d12
    oxygen::renderer
    oxygen::loader
    oxygen::content
)

if(WIN32)
  target_link_libraries(
    ${META_MODULE_TARGET}
    PRIVATE
      ole32
      shell32
      windowscodecs
  )
endif()

# ------------------------------------------------------------------------------
# Deployment instructions
# ------------------------------------------------------------------------------

oxygen_module_install(
  EXPORT ${META_MODULE_NAMESPACE}
  TARGETS
    ${META_MODULE_TARGET}
)

asap_pop_module("${META_MODULE_NAME}")
