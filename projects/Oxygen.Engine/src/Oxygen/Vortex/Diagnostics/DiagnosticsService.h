//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <utility>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Vortex/Diagnostics/DiagnosticsCaptureManifest.h>
#include <Oxygen/Vortex/Diagnostics/DiagnosticsFrameLedger.h>
#include <Oxygen/Vortex/Diagnostics/DiagnosticsTypes.h>
#include <Oxygen/Vortex/Diagnostics/GpuWorkSummary.h>
#include <Oxygen/Vortex/Diagnostics/ShaderDebugModeRegistry.h>
#include <Oxygen/Vortex/RendererCapability.h>
#include <Oxygen/Vortex/api_export.h>

namespace oxygen::graphics {
class CommandRecorder;
}

namespace oxygen::vortex {

namespace internal {
  class GpuTimelineAccess;
  class GpuTimelineProfiler;
  class GpuWorkSummarizer;
  struct GpuTimelineDiagnostic;
} // namespace internal

class DiagnosticsService;

//! Keeps GPU timeline collection on while held.
/*!
 Move-only. Releasing the last lease turns collection off unless the user
 setting (`SetGpuTimelineEnabled`) is on. A default-constructed lease, or a
 viewer lease from a renderer without `kDiagnosticsAndProfiling`, is inert.
 A lease that outlives its service becomes inert: modules shut down after
 the renderer can still release theirs.
*/
class GpuTimelineLease {
public:
  GpuTimelineLease() = default;
  OXGN_VRTX_API ~GpuTimelineLease();

  OXYGEN_MAKE_NON_COPYABLE(GpuTimelineLease)
  OXGN_VRTX_API GpuTimelineLease(GpuTimelineLease&& other) noexcept;
  OXGN_VRTX_API auto operator=(GpuTimelineLease&& other) noexcept
    -> GpuTimelineLease&;

  OXGN_VRTX_API auto Release() noexcept -> void;
  //! Whether this lease keeps collection on.
  [[nodiscard]] auto IsActive() const noexcept -> bool
  {
    return !service_.expired();
  }

private:
  friend class DiagnosticsService;
  //! Points to the service while it lives.
  using Anchor = observer_ptr<DiagnosticsService>;

  GpuTimelineLease(std::weak_ptr<const Anchor> service, const bool viewer)
    : service_(std::move(service))
    , viewer_(viewer)
  {
  }

  std::weak_ptr<const Anchor> service_;
  bool viewer_ { false };
};

struct DiagnosticsConfig {
  DiagnosticsFeatureSet default_features { DiagnosticsFeature::kNone };

  [[nodiscard]] OXGN_VRTX_API static auto Default() noexcept
    -> DiagnosticsConfig;
};

enum class HdrPrecisionControl : std::uint8_t {
  kProduction,
  kFp32Reference,
  kFp32Only,
  kQualified,
};

class DiagnosticsService {
public:
  static constexpr std::uint32_t kDefaultGpuDebugLineCapacity { 65536U };

  OXGN_VRTX_API explicit DiagnosticsService(CapabilitySet renderer_capabilities,
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

  //! Diagnostic format control for exposure comparisons. Configure before
  //! preparing views. Keeps their numerical exposure and qualified frame P;
  //! all precision checks remain enabled. This is not authored exposure state.
  OXGN_VRTX_API auto SetHdrFp32ReferenceEnabled(bool enabled) -> void;
  [[nodiscard]] OXGN_VRTX_API auto IsHdrFp32ReferenceEnabled() const -> bool;
  //! Runtime comparison mode; capture before preparing a view's HDR domain.
  OXGN_VRTX_API auto SetHdrPrecisionControl(HdrPrecisionControl control)
    -> void;
  [[nodiscard]] OXGN_VRTX_API auto GetHdrPrecisionControl() const
    -> HdrPrecisionControl;
  [[nodiscard]] OXGN_VRTX_API auto GetHdrPrecisionControlRevision() const
    -> std::uint64_t;

  //! Whether views publish GPU debug buffers this frame: the
  //! `kGpuDebugPrimitives` feature is enabled.
  [[nodiscard]] OXGN_VRTX_API auto IsGpuDebugPrimitivesEnabled() const -> bool;
  //! Producers that emit primitives while GPU debug primitives are enabled.
  OXGN_VRTX_API auto SetGpuDebugPrimitiveSources(
    GpuDebugPrimitiveSource sources) -> void;
  [[nodiscard]] OXGN_VRTX_API auto GetGpuDebugPrimitiveSources() const
    -> GpuDebugPrimitiveSource;
  //! Lines each view stores per frame; at least one.
  OXGN_VRTX_API auto SetGpuDebugLineCapacity(std::uint32_t lines) -> void;
  [[nodiscard]] OXGN_VRTX_API auto GetGpuDebugLineCapacity() const
    -> std::uint32_t;

  OXGN_VRTX_API auto SetGpuTimelineEnabled(bool enabled) -> void;
  [[nodiscard]] OXGN_VRTX_API auto IsGpuTimelineEnabled() const -> bool;
  OXGN_VRTX_API auto SetGpuTimelineMaxScopesPerFrame(std::uint32_t max_scopes)
    -> void;
  //! Keeps collection on for a viewer of the GPU work summary. Viewers hold
  //! one while they show it; several may watch at once. Inert without
  //! `kDiagnosticsAndProfiling`.
  [[nodiscard]] OXGN_VRTX_API auto AcquireGpuTimeline() -> GpuTimelineLease;
  //! The latest windowed summary; null while no viewer lease or user setting
  //! keeps collection on, and before the first window completes. The
  //! snapshot never changes while held.
  [[nodiscard]] OXGN_VRTX_API auto GetLatestGpuWorkSummary() const
    -> std::shared_ptr<const GpuWorkSummary>;
  OXGN_VRTX_API auto RequestGpuTimelineExport(const std::filesystem::path& path)
    -> void;
  //! Record an exact frame-sequence window starting at the current frame.
  //! Requires enabled collection; only one recording may be active. Invalid
  //! samples remain in the JSON report; shutdown marks partial output
  //! incomplete.
  [[nodiscard]] OXGN_VRTX_API auto RequestGpuTimelineRecording(
    const std::filesystem::path& path, std::uint32_t frame_count) -> bool;
  //! Attach this renderer's collector to a graphics-queue pass recorder.
  //! The recorder and its scopes must finish before renderer shutdown.
  OXGN_VRTX_API auto AttachGpuTimelineCollector(
    graphics::CommandRecorder& recorder) const -> void;
  [[nodiscard]] OXGN_VRTX_API auto ExportCaptureManifest(
    const std::filesystem::path& path,
    const DiagnosticsCaptureManifestOptions& options = {}) -> bool;

  OXGN_VRTX_API auto BeginFrame(frame::SequenceNumber frame) -> void;
  OXGN_VRTX_API auto RecordPass(DiagnosticsPassRecord record) -> void;
  OXGN_VRTX_API auto RecordProduct(DiagnosticsProductRecord record) -> void;
  OXGN_VRTX_API auto ReportIssue(DiagnosticsIssue issue) -> void;
  OXGN_VRTX_API auto EndFrame() -> void;

  [[nodiscard]] OXGN_VRTX_API auto GetLatestSnapshot() const
    -> DiagnosticsFrameSnapshot;

private:
  friend class Renderer;
  friend class GpuTimelineLease;
  friend class internal::GpuTimelineAccess;

  auto AcquireLease(bool viewer) -> GpuTimelineLease;
  auto ReleaseLease(bool viewer) noexcept -> void;

  OXGN_VRTX_API auto SetGpuTimelineProfiler(
    observer_ptr<internal::GpuTimelineProfiler> profiler) -> void;
  OXGN_VRTX_API auto SyncGpuTimelineDiagnostics() -> void;

  [[nodiscard]] auto ComputeEffectiveFeatures() const noexcept
    -> DiagnosticsFeatureSet;
  [[nodiscard]] auto IsFrameLedgerEnabled() const noexcept -> bool;
  [[nodiscard]] auto IsGpuTimelineFeatureEnabled() const noexcept -> bool;
  [[nodiscard]] auto IsUserTimingEnabled() const noexcept -> bool;
  [[nodiscard]] auto IsViewerTimingEnabled() const noexcept -> bool;
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
  observer_ptr<internal::GpuTimelineProfiler> gpu_timeline_profiler_ {
    nullptr
  };
  GpuDebugPrimitiveSource gpu_debug_sources_ { GpuDebugPrimitiveSource::kNone };
  std::uint32_t gpu_debug_line_capacity_ { kDefaultGpuDebugLineCapacity };
  bool gpu_timeline_enabled_requested_ { false };
  bool viewer_timing_active_ { false };
  std::uint64_t current_frame_ {};
  std::uint32_t viewer_leases_ {};
  std::uint32_t production_leases_ {};
  std::shared_ptr<internal::GpuWorkSummarizer> gpu_work_summarizer_;
  HdrPrecisionControl hdr_precision_control_ {
    HdrPrecisionControl::kProduction,
  };
  std::uint64_t hdr_precision_control_revision_ { 0U };
  DiagnosticsFrameLedger frame_ledger_;
  //! Leases see this expire when the service is destroyed.
  std::shared_ptr<const GpuTimelineLease::Anchor> lease_anchor_;
};

} // namespace oxygen::vortex
