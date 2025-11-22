//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Config/EngineConfig.h>
#include <Oxygen/Config/GraphicsConfig.h>
#include <Oxygen/Config/PlatformConfig.h>
#include <Oxygen/Config/RendererConfig.h>
#include <Oxygen/EditorInterface/EngineContext.h>
#include <Oxygen/EditorInterface/api_export.h>

namespace oxygen::graphics {
class Surface;
}

namespace oxygen::engine::interop {

struct LoggingConfig {
  // Log verbosity must be between Verbosity_OFF (-9) and Verbosity_MAX (+9)
  int verbosity { -9 }; // Verbosity_OFF
  // Whether to use colored logs
  bool is_colored { false };
  // Comma-separated list of vmodule patterns to set per-file verbosity levels.
  // Can be `nullptr`, in which case no vmodule overrides are applied.
  const char* vmodules { nullptr };
};

OXGN_EI_API auto ConfigureLogging(const LoggingConfig& config) -> bool;
OXGN_EI_API auto LogInfoMessage(const char* message) -> void;

OXGN_EI_API auto CreateEngine(const EngineConfig& config)
  -> std::unique_ptr<EngineContext>;
OXGN_EI_API auto RunEngine(std::shared_ptr<EngineContext> ctx) -> void;
OXGN_EI_API auto StopEngine(std::shared_ptr<EngineContext> ctx) -> void;

OXGN_EI_API auto CreateScene(const char* name) -> bool;
OXGN_EI_API auto RemoveScene(const char* name) -> bool;

OXGN_EI_API auto CreateCompositionSurface(std::shared_ptr<EngineContext> ctx,
  void** swap_chain_out) -> std::shared_ptr<graphics::Surface>;

OXGN_EI_API auto RequestCompositionSurfaceResize(
  const std::shared_ptr<graphics::Surface>& surface, uint32_t width,
  uint32_t height) -> void;

} // namespace oxygen::engine::interop
