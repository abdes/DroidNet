# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

# ------------------------------------------------------------------------------
# Meta information about the this module
# ------------------------------------------------------------------------------

asap_module_declare(
  MODULE_NAME "Oxygen.Examples.MultiView"
  DESCRIPTION [=[
An example demonstrating the Oxygen engine multi-view rendering capabilities.
]=]
  WITHOUT_VERSION_H
)
asap_push_module("${META_MODULE_NAME}")

# ------------------------------------------------------------------------------
# Dependencies
# ------------------------------------------------------------------------------

# ==============================================================================
# Build instructions
# ==============================================================================

# -- Input System Example ------------------------------------------------------

add_executable(
  ${META_MODULE_TARGET}
  "../main.cpp"
  "../Common/AsyncEngineApp.h"
  "../Common/WorkspaceRoot.h"
  "../Common/AppWindow.h"
  "../Common/AppWindow.cpp"
  "../Common/ExampleModuleBase.h"
  "../Common/ExampleModuleBase.cpp"
  "main_impl.cpp"
  "MainModule.h"
  "MainModule.cpp"
  "SceneBootstrapper.h"
  "SceneBootstrapper.cpp"
  "ViewRenderer.h"
  "ViewRenderer.cpp"
  "OffscreenCompositor.h"
  "OffscreenCompositor.cpp"
  "DemoView.h"
  "DemoView.cpp"
  "MainView.h"
  "MainView.cpp"
  "PipView.h"
  "PipView.cpp"
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
target_link_libraries(
  ${META_MODULE_TARGET}
  PRIVATE
    oxygen::platform
    oxygen::input
    oxygen::core
    oxygen::clap
    oxygen::engine
    oxygen::graphics-direct3d12
    oxygen::renderer
    oxygen::loader
)

# ==============================================================================
# Deployment instructions
# ==============================================================================

oxygen_module_install(
  EXPORT ${META_MODULE_NAMESPACE}
  TARGETS
    ${META_MODULE_TARGET}
)

asap_pop_module("${META_MODULE_NAME}")
