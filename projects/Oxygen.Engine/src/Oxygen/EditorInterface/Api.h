//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/EditorInterface/api_export.h>

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

struct EngineConfig { };

OXGN_EI_API auto ConfigureLogging(const LoggingConfig& config) -> bool;
OXGN_EI_API auto CreateEngine(const EngineConfig& config) -> bool;

OXGN_EI_API auto CreateScene(const char* name) -> bool;
OXGN_EI_API auto RemoveScene(const char* name) -> bool;

} // namespace oxygen::engine::interop
