//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Vortex/Diagnostics/DiagnosticsTypes.h>

#include <string>
#include <utility>

namespace oxygen::vortex {

auto to_string(const DiagnosticsFeatureSet features) -> std::string
{
  if (features == DiagnosticsFeature::kNone) {
    return "None";
  }

  std::string result;
  auto checked = DiagnosticsFeature::kNone;
  bool first = true;

  const auto append_feature
    = [&](const DiagnosticsFeature feature, const char* name) {
        if (HasAllFeatures(features, feature)) {
          if (!first) {
            result += " | ";
          }
          result += name;
          checked |= feature;
          first = false;
        }
      };

  append_feature(DiagnosticsFeature::kFrameLedger, "FrameLedger");
  append_feature(DiagnosticsFeature::kGpuTimeline, "GpuTimeline");
  append_feature(DiagnosticsFeature::kShaderDebugModes, "ShaderDebugModes");
  append_feature(DiagnosticsFeature::kImGuiPanels, "ImGuiPanels");
  append_feature(DiagnosticsFeature::kCaptureManifest, "CaptureManifest");
  append_feature(
    DiagnosticsFeature::kGpuDebugPrimitives, "GpuDebugPrimitives");

  if (checked != features) {
    if (!result.empty()) {
      result += " | ";
    }
    result += "__Unknown__";
  }

  if (result.empty()) {
    return "None";
  }
  return result;
}

auto MakeDiagnosticsIssue(const DiagnosticsIssueCode code,
  const DiagnosticsSeverity severity, std::string message) -> DiagnosticsIssue
{
  return DiagnosticsIssue {
    .severity = severity,
    .code = std::string { to_string(code) },
    .message = std::move(message),
  };
}

} // namespace oxygen::vortex
