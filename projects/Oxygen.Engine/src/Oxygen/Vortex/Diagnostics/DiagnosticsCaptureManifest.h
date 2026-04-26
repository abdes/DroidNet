//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include <Oxygen/Vortex/Diagnostics/DiagnosticsTypes.h>
#include <Oxygen/Vortex/api_export.h>

namespace oxygen::vortex {

inline constexpr auto kDiagnosticsCaptureManifestSchema
  = "vortex.diagnostics.capture-manifest.v1";

struct DiagnosticsCaptureManifestOptions {
  std::optional<std::filesystem::path> gpu_timeline_export_path {};
};

[[nodiscard]] OXGN_VRTX_API auto BuildDiagnosticsCaptureManifestJson(
  const DiagnosticsFrameSnapshot& snapshot,
  const DiagnosticsCaptureManifestOptions& options = {}) -> std::string;

OXGN_VRTX_API auto WriteDiagnosticsCaptureManifest(
  const std::filesystem::path& path, const DiagnosticsFrameSnapshot& snapshot,
  const DiagnosticsCaptureManifestOptions& options = {}) -> void;

} // namespace oxygen::vortex
