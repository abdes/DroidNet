//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Vortex/ShaderDebugMode.h>
#include <Oxygen/Vortex/api_export.h>

namespace oxygen::vortex {

// NOLINTNEXTLINE(*-enum-size)
enum class DiagnosticsFeature : std::uint32_t {
  kNone = 0,
  kFrameLedger = OXYGEN_FLAG(0),
  kGpuTimeline = OXYGEN_FLAG(1),
  kShaderDebugModes = OXYGEN_FLAG(2),
  kImGuiPanels = OXYGEN_FLAG(3),
  kCaptureManifest = OXYGEN_FLAG(4),
  kGpuDebugPrimitives = OXYGEN_FLAG(5),
};

OXYGEN_DEFINE_FLAGS_OPERATORS(DiagnosticsFeature)

using DiagnosticsFeatureSet = DiagnosticsFeature;

enum class DiagnosticsSeverity : std::uint8_t {
  kInfo,
  kWarning,
  kError,
};

enum class DiagnosticsIssueCode : std::uint8_t {
  kFeatureUnavailable,
  kManifestWriteFailed,
  kTimelineOverflow,
  kTimelineIncompleteScope,
  kUnsupportedDebugMode,
  kMissingDebugModeProduct,
  kStaleProduct,
};

enum class DiagnosticsPassKind : std::uint8_t {
  kCpuOnly,
  kGraphics,
  kCompute,
  kCopy,
  kComposite,
};

enum class DiagnosticsDebugPath : std::uint8_t {
  kNone,
  kForwardMeshVariant,
  kDeferredFullscreen,
  kServicePass,
  kExternalToolOnly,
};

struct DiagnosticsIssue {
  DiagnosticsSeverity severity { DiagnosticsSeverity::kInfo };
  std::string code;
  std::string message;
  std::string view_name;
  std::string pass_name;
  std::string product_name;
  std::uint32_t occurrences { 1U };
};

struct DiagnosticsPassRecord {
  std::string name;
  DiagnosticsPassKind kind { DiagnosticsPassKind::kCpuOnly };
  bool executed { false };
  std::vector<std::string> inputs;
  std::vector<std::string> outputs;
  std::vector<std::string> missing_inputs;
  std::optional<float> gpu_duration_ms;
};

struct DiagnosticsProductRecord {
  std::string name;
  std::string producer_pass;
  std::string resource_name;
  std::string descriptor;
  bool published { false };
  bool valid { false };
  bool stale { false };
};

struct DiagnosticsFrameSnapshot {
  frame::SequenceNumber frame_index { 0U };
  ShaderDebugMode active_shader_debug_mode { ShaderDebugMode::kDisabled };
  DiagnosticsFeatureSet requested_features { DiagnosticsFeature::kNone };
  DiagnosticsFeatureSet enabled_features { DiagnosticsFeature::kNone };
  bool imgui_overlay_active { false };
  bool gpu_timeline_enabled { false };
  bool gpu_timeline_frame_available { false };
  bool capture_manifest_available { false };
  std::vector<DiagnosticsPassRecord> passes;
  std::vector<DiagnosticsProductRecord> products;
  std::vector<DiagnosticsIssue> issues;
};

[[nodiscard]] constexpr auto HasAnyFeature(
  const DiagnosticsFeatureSet features,
  const DiagnosticsFeatureSet requested) noexcept -> bool
{
  using Underlying = std::underlying_type_t<DiagnosticsFeatureSet>;
  return (static_cast<Underlying>(features)
           & static_cast<Underlying>(requested))
    != 0U;
}

[[nodiscard]] constexpr auto HasAllFeatures(
  const DiagnosticsFeatureSet features,
  const DiagnosticsFeatureSet requested) noexcept -> bool
{
  using Underlying = std::underlying_type_t<DiagnosticsFeatureSet>;
  return (static_cast<Underlying>(features)
           & static_cast<Underlying>(requested))
    == static_cast<Underlying>(requested);
}

[[nodiscard]] OXGN_VRTX_API auto to_string(DiagnosticsFeatureSet features)
  -> std::string;

[[nodiscard]] constexpr auto to_string(
  const DiagnosticsSeverity severity) noexcept -> std::string_view
{
  switch (severity) {
  case DiagnosticsSeverity::kInfo:
    return "Info";
  case DiagnosticsSeverity::kWarning:
    return "Warning";
  case DiagnosticsSeverity::kError:
    return "Error";
  }
  return "__Unknown__";
}

[[nodiscard]] constexpr auto to_string(
  const DiagnosticsIssueCode code) noexcept -> std::string_view
{
  switch (code) {
  case DiagnosticsIssueCode::kFeatureUnavailable:
    return "diag.feature-unavailable";
  case DiagnosticsIssueCode::kManifestWriteFailed:
    return "capture-manifest.write-failed";
  case DiagnosticsIssueCode::kTimelineOverflow:
    return "gpu-timeline.overflow";
  case DiagnosticsIssueCode::kTimelineIncompleteScope:
    return "gpu-timeline.incomplete-scope";
  case DiagnosticsIssueCode::kUnsupportedDebugMode:
    return "debug-mode.unsupported";
  case DiagnosticsIssueCode::kMissingDebugModeProduct:
    return "debug-mode.missing-product";
  case DiagnosticsIssueCode::kStaleProduct:
    return "product.stale";
  }
  return "__Unknown__";
}

[[nodiscard]] constexpr auto to_string(
  const DiagnosticsPassKind kind) noexcept -> std::string_view
{
  switch (kind) {
  case DiagnosticsPassKind::kCpuOnly:
    return "CpuOnly";
  case DiagnosticsPassKind::kGraphics:
    return "Graphics";
  case DiagnosticsPassKind::kCompute:
    return "Compute";
  case DiagnosticsPassKind::kCopy:
    return "Copy";
  case DiagnosticsPassKind::kComposite:
    return "Composite";
  }
  return "__Unknown__";
}

[[nodiscard]] constexpr auto to_string(
  const DiagnosticsDebugPath path) noexcept -> std::string_view
{
  switch (path) {
  case DiagnosticsDebugPath::kNone:
    return "None";
  case DiagnosticsDebugPath::kForwardMeshVariant:
    return "ForwardMeshVariant";
  case DiagnosticsDebugPath::kDeferredFullscreen:
    return "DeferredFullscreen";
  case DiagnosticsDebugPath::kServicePass:
    return "ServicePass";
  case DiagnosticsDebugPath::kExternalToolOnly:
    return "ExternalToolOnly";
  }
  return "__Unknown__";
}

[[nodiscard]] OXGN_VRTX_API auto MakeDiagnosticsIssue(
  DiagnosticsIssueCode code, DiagnosticsSeverity severity, std::string message)
  -> DiagnosticsIssue;

} // namespace oxygen::vortex
