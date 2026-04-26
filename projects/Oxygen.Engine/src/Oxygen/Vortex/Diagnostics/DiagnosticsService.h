//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <filesystem>
#include <mutex>
#include <optional>
#include <span>
#include <memory>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Vortex/Diagnostics/DiagnosticsFrameLedger.h>
#include <Oxygen/Vortex/Diagnostics/ShaderDebugModeRegistry.h>
#include <Oxygen/Vortex/Diagnostics/DiagnosticsTypes.h>
#include <Oxygen/Vortex/Internal/GpuTimelineProfiler.h>
#include <Oxygen/Vortex/RendererCapability.h>
#include <Oxygen/Vortex/api_export.h>

namespace oxygen::vortex {

struct DiagnosticsConfig {
  DiagnosticsFeatureSet default_features { DiagnosticsFeature::kNone };

  [[nodiscard]] OXGN_VRTX_API static auto Default() noexcept
    -> DiagnosticsConfig;
};

class DiagnosticsService {
public:
  OXGN_VRTX_API explicit DiagnosticsService(
    CapabilitySet renderer_capabilities,
    DiagnosticsConfig config = DiagnosticsConfig::Default());
  OXGN_VRTX_API ~DiagnosticsService();

  OXYGEN_MAKE_NON_COPYABLE(DiagnosticsService)
  OXYGEN_MAKE_NON_MOVABLE(DiagnosticsService)

  OXGN_VRTX_API auto SetRendererCapabilities(CapabilitySet capabilities)
    -> void;
  [[nodiscard]] OXGN_VRTX_API auto GetRendererCapabilities() const
    -> CapabilitySet;

  OXGN_VRTX_API auto SetEnabledFeatures(DiagnosticsFeatureSet features) -> void;
  [[nodiscard]] OXGN_VRTX_API auto GetRequestedFeatures() const
    -> DiagnosticsFeatureSet;
  [[nodiscard]] OXGN_VRTX_API auto GetEnabledFeatures() const
    -> DiagnosticsFeatureSet;

  OXGN_VRTX_API auto SetShaderDebugMode(ShaderDebugMode mode) noexcept -> void;
  [[nodiscard]] OXGN_VRTX_API auto GetShaderDebugMode() const noexcept
    -> ShaderDebugMode;
  [[nodiscard]] OXGN_VRTX_API auto EnumerateShaderDebugModes() const noexcept
    -> std::span<const ShaderDebugModeInfo>;
  [[nodiscard]] OXGN_VRTX_API auto FindShaderDebugMode(
    std::string_view canonical_name) const noexcept
    -> std::optional<ShaderDebugMode>;

  OXGN_VRTX_API auto SetGpuTimelineProfiler(
    observer_ptr<internal::GpuTimelineProfiler> profiler) -> void;
  OXGN_VRTX_API auto SetGpuTimelineEnabled(bool enabled) -> void;
  [[nodiscard]] OXGN_VRTX_API auto IsGpuTimelineEnabled() const -> bool;
  OXGN_VRTX_API auto SetGpuTimelineMaxScopesPerFrame(std::uint32_t max_scopes)
    -> void;
  OXGN_VRTX_API auto SetGpuTimelineRetainLatestFrame(bool retain_latest_frame)
    -> void;
  OXGN_VRTX_API auto AddGpuTimelineSink(
    std::shared_ptr<internal::GpuTimelineSink> sink) -> void;
  OXGN_VRTX_API auto RequestGpuTimelineExport(
    const std::filesystem::path& path) -> void;
  [[nodiscard]] OXGN_VRTX_API auto GetLatestGpuTimelineFrame() const
    -> std::optional<internal::GpuTimelineFrame>;
  OXGN_VRTX_API auto SyncGpuTimelineDiagnostics() -> void;

  OXGN_VRTX_API auto BeginFrame(frame::SequenceNumber frame) -> void;
  OXGN_VRTX_API auto RecordPass(DiagnosticsPassRecord record) -> void;
  OXGN_VRTX_API auto RecordProduct(DiagnosticsProductRecord record) -> void;
  OXGN_VRTX_API auto ReportIssue(DiagnosticsIssue issue) -> void;
  OXGN_VRTX_API auto EndFrame() -> void;

  [[nodiscard]] OXGN_VRTX_API auto GetLatestSnapshot() const
    -> DiagnosticsFrameSnapshot;

private:
  [[nodiscard]] auto ComputeEffectiveFeatures() const noexcept
    -> DiagnosticsFeatureSet;
  [[nodiscard]] auto IsFrameLedgerEnabled() const noexcept -> bool;
  [[nodiscard]] auto IsGpuTimelineFeatureEnabled() const noexcept -> bool;
  [[nodiscard]] auto HasAvailableGpuTimelineFrame() const -> bool;
  auto ApplyGpuTimelineEnabled() -> void;
  auto RefreshLedgerState() -> void;
  auto ReportGpuTimelineIssue(const internal::GpuTimelineDiagnostic& diagnostic)
    -> void;

  mutable std::mutex mutex_;
  CapabilitySet renderer_capabilities_ { RendererCapabilityFamily::kNone };
  DiagnosticsFeatureSet requested_features_ { DiagnosticsFeature::kNone };
  DiagnosticsFeatureSet enabled_features_ { DiagnosticsFeature::kNone };
  ShaderDebugMode shader_debug_mode_ { ShaderDebugMode::kDisabled };
  observer_ptr<internal::GpuTimelineProfiler> gpu_timeline_profiler_ { nullptr };
  bool gpu_timeline_enabled_requested_ { false };
  DiagnosticsFrameLedger frame_ledger_ {};
};

} // namespace oxygen::vortex
