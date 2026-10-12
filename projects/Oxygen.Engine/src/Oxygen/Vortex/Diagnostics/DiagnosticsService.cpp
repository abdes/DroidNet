//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Graphics/Common/CommandQueue.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/Types/QueueRole.h>
#include <Oxygen/Vortex/Diagnostics/DiagnosticsCaptureManifest.h>
#include <Oxygen/Vortex/Diagnostics/DiagnosticsService.h>
#include <Oxygen/Vortex/Diagnostics/DiagnosticsTypes.h>
#include <Oxygen/Vortex/Diagnostics/GpuWorkSummary.h>
#include <Oxygen/Vortex/Diagnostics/Internal/GpuWorkSummarizer.h>
#include <Oxygen/Vortex/Diagnostics/ShaderDebugModeRegistry.h>
#include <Oxygen/Vortex/Internal/GpuTimelineAccess.h>
#include <Oxygen/Vortex/Internal/GpuTimelineProfiler.h>
#include <Oxygen/Vortex/RendererCapability.h>
#include <Oxygen/Vortex/ShaderDebugMode.h>

namespace oxygen::vortex {

auto DiagnosticsConfig::Default() noexcept -> DiagnosticsConfig
{
#ifdef NDEBUG
  return DiagnosticsConfig {
    .default_features = DiagnosticsFeature::kNone,
  };
#else
  return DiagnosticsConfig {
    .default_features
    = DiagnosticsFeature::kFrameLedger | DiagnosticsFeature::kShaderDebugModes,
  };
#endif
}

DiagnosticsService::DiagnosticsService(
  const CapabilitySet renderer_capabilities, const DiagnosticsConfig config)
  : renderer_capabilities_(renderer_capabilities)
  , requested_features_(config.default_features)
  , enabled_features_(ComputeEffectiveFeatures())
{
  RefreshLedgerState();
}

DiagnosticsService::~DiagnosticsService() = default;

auto DiagnosticsService::SetRendererCapabilities(
  const CapabilitySet capabilities) -> void
{
  std::scoped_lock lock(mutex_);
  renderer_capabilities_ = capabilities;
  enabled_features_ = ComputeEffectiveFeatures();
  ApplyGpuTimelineEnabled();
  RefreshLedgerState();
}

auto DiagnosticsService::GetRendererCapabilities() const -> CapabilitySet
{
  std::scoped_lock lock(mutex_);
  return renderer_capabilities_;
}

auto DiagnosticsService::SetEnabledFeatures(
  const DiagnosticsFeatureSet features) -> void
{
  std::scoped_lock lock(mutex_);
  requested_features_ = features;
  enabled_features_ = ComputeEffectiveFeatures();
  ApplyGpuTimelineEnabled();
  RefreshLedgerState();
}

auto DiagnosticsService::GetRequestedFeatures() const -> DiagnosticsFeatureSet
{
  std::scoped_lock lock(mutex_);
  return requested_features_;
}

auto DiagnosticsService::GetEnabledFeatures() const -> DiagnosticsFeatureSet
{
  std::scoped_lock lock(mutex_);
  return enabled_features_;
}

auto DiagnosticsService::SetShaderDebugMode(const ShaderDebugMode mode) noexcept
  -> void
{
  std::scoped_lock lock(mutex_);
  shader_debug_mode_ = mode;
  RefreshLedgerState();
}

auto DiagnosticsService::GetShaderDebugMode() const noexcept -> ShaderDebugMode
{
  std::scoped_lock lock(mutex_);
  return shader_debug_mode_;
}

auto DiagnosticsService::EnumerateShaderDebugModes() const noexcept
  -> std::span<const ShaderDebugModeInfo>
{
  return vortex::EnumerateShaderDebugModes();
}

auto DiagnosticsService::FindShaderDebugMode(
  const std::string_view canonical_name) const noexcept
  -> std::optional<ShaderDebugMode>
{
  return ResolveShaderDebugMode(canonical_name);
}

auto DiagnosticsService::SetHdrFp32ReferenceEnabled(const bool enabled) -> void
{
  SetHdrPrecisionControl(enabled ? HdrPrecisionControl::kFp32Reference
                                 : HdrPrecisionControl::kProduction);
}

auto DiagnosticsService::IsHdrFp32ReferenceEnabled() const -> bool
{
  std::scoped_lock lock(mutex_);
  return hdr_precision_control_ == HdrPrecisionControl::kFp32Reference;
}

auto DiagnosticsService::SetHdrPrecisionControl(
  const HdrPrecisionControl control) -> void
{
  std::scoped_lock lock(mutex_);
  if (hdr_precision_control_ != control) {
    CHECK_NE_F(hdr_precision_control_revision_,
      (std::numeric_limits<std::uint64_t>::max)());
    ++hdr_precision_control_revision_;
  }
  hdr_precision_control_ = control;
}

auto DiagnosticsService::GetHdrPrecisionControl() const -> HdrPrecisionControl
{
  std::scoped_lock lock(mutex_);
  return hdr_precision_control_;
}

auto DiagnosticsService::GetHdrPrecisionControlRevision() const -> std::uint64_t
{
  std::scoped_lock lock(mutex_);
  return hdr_precision_control_revision_;
}

auto DiagnosticsService::AttachGpuTimelineCollector(
  graphics::CommandRecorder& recorder) const -> void
{
  std::scoped_lock lock(mutex_);
  const auto queue = recorder.GetTargetQueue();
  if (!queue || queue->GetQueueRole() != graphics::QueueRole::kGraphics) {
    return;
  }
  recorder.SetTelemetryCollector(observer_ptr<graphics::IGpuProfileCollector> {
    IsViewerTimingEnabled() ? gpu_timeline_profiler_.get() : nullptr });
}

auto DiagnosticsService::SetGpuTimelineProfiler(
  const observer_ptr<internal::GpuTimelineProfiler> profiler) -> void
{
  std::scoped_lock lock(mutex_);
  gpu_timeline_profiler_ = profiler;
  if (gpu_timeline_profiler_ != nullptr) {
    if (!gpu_work_summarizer_) {
      gpu_work_summarizer_ = std::make_shared<internal::GpuWorkSummarizer>();
    }
    gpu_timeline_profiler_->AddSink(gpu_work_summarizer_);
  }
  ApplyGpuTimelineEnabled();
  RefreshLedgerState();
}

auto DiagnosticsService::IsGpuDebugPrimitivesEnabled() const -> bool
{
  std::scoped_lock lock(mutex_);
  return HasAllFeatures(
    enabled_features_, DiagnosticsFeature::kGpuDebugPrimitives);
}

auto DiagnosticsService::SetGpuDebugPrimitiveSources(
  const GpuDebugPrimitiveSource sources) -> void
{
  std::scoped_lock lock(mutex_);
  gpu_debug_sources_ = sources;
}

auto DiagnosticsService::GetGpuDebugPrimitiveSources() const
  -> GpuDebugPrimitiveSource
{
  std::scoped_lock lock(mutex_);
  return gpu_debug_sources_;
}

auto DiagnosticsService::SetGpuDebugLineCapacity(const std::uint32_t lines)
  -> void
{
  std::scoped_lock lock(mutex_);
  gpu_debug_line_capacity_ = (std::max)(lines, 1U);
}

auto DiagnosticsService::GetGpuDebugLineCapacity() const -> std::uint32_t
{
  std::scoped_lock lock(mutex_);
  return gpu_debug_line_capacity_;
}

auto DiagnosticsService::SetGpuTimelineEnabled(const bool enabled) -> void
{
  std::scoped_lock lock(mutex_);
  gpu_timeline_enabled_requested_ = enabled;
  ApplyGpuTimelineEnabled();
  RefreshLedgerState();
}

auto DiagnosticsService::IsGpuTimelineEnabled() const -> bool
{
  std::scoped_lock lock(mutex_);
  return IsUserTimingEnabled();
}

auto DiagnosticsService::SetGpuTimelineMaxScopesPerFrame(
  const std::uint32_t max_scopes) -> void
{
  std::scoped_lock lock(mutex_);
  if (gpu_timeline_profiler_ != nullptr) {
    gpu_timeline_profiler_->SetMaxScopesPerFrame(max_scopes);
  }
}

auto DiagnosticsService::AcquireGpuTimeline() -> GpuTimelineLease
{
  std::scoped_lock lock(mutex_);
  if (!HasAllCapabilities(renderer_capabilities_,
        RendererCapabilityFamily::kDiagnosticsAndProfiling)) {
    return {};
  }
  return AcquireLease(true);
}

auto DiagnosticsService::GetLatestGpuWorkSummary() const
  -> std::shared_ptr<const GpuWorkSummary>
{
  std::scoped_lock lock(mutex_);
  return viewer_timing_active_ && gpu_work_summarizer_
    ? gpu_work_summarizer_->Latest()
    : nullptr;
}

auto DiagnosticsService::AcquireLease(const bool viewer) -> GpuTimelineLease
{
  ++(viewer ? viewer_leases_ : production_leases_);
  ApplyGpuTimelineEnabled();
  RefreshLedgerState();
  return GpuTimelineLease { observer_ptr { this }, viewer };
}

auto DiagnosticsService::ReleaseLease(const bool viewer) noexcept -> void
{
  std::scoped_lock lock(mutex_);
  auto& count = viewer ? viewer_leases_ : production_leases_;
  CHECK_GT_F(count, 0U);
  --count;
  ApplyGpuTimelineEnabled();
  RefreshLedgerState();
}

auto DiagnosticsService::RequestGpuTimelineExport(
  const std::filesystem::path& path) -> void
{
  std::scoped_lock lock(mutex_);
  if (gpu_timeline_profiler_ != nullptr) {
    gpu_timeline_profiler_->RequestOneShotExport(path);
  }
}

auto DiagnosticsService::RequestGpuTimelineRecording(
  const std::filesystem::path& path, const std::uint32_t frame_count) -> bool
{
  std::scoped_lock lock(mutex_);
  return gpu_timeline_profiler_ != nullptr
    && gpu_timeline_profiler_->RequestRecording(path, frame_count);
}

auto DiagnosticsService::SyncGpuTimelineDiagnostics() -> void
{
  std::scoped_lock lock(mutex_);
  if (!frame_ledger_.IsFrameOpen() || !IsFrameLedgerEnabled()
    || !IsGpuTimelineFeatureEnabled() || gpu_timeline_profiler_ == nullptr) {
    RefreshLedgerState();
    return;
  }

  const auto frame = gpu_timeline_profiler_->GetLastPublishedFrame();
  if (!frame.has_value()) {
    RefreshLedgerState();
    return;
  }
  for (const auto& diagnostic : frame->diagnostics) {
    ReportGpuTimelineIssue(diagnostic);
  }
  RefreshLedgerState();
}

auto DiagnosticsService::ExportCaptureManifest(
  const std::filesystem::path& path,
  const DiagnosticsCaptureManifestOptions& options) -> bool
{
  try {
    WriteDiagnosticsCaptureManifest(path, GetLatestSnapshot(), options);
    return true;
  } catch (const std::exception& ex) {
    auto issue
      = MakeDiagnosticsIssue(DiagnosticsIssueCode::kManifestWriteFailed,
        DiagnosticsSeverity::kError, ex.what());
    issue.product_name = "Vortex.DiagnosticsCaptureManifest";
    ReportIssue(std::move(issue));
    return false;
  }
}

auto DiagnosticsService::BeginFrame(const frame::SequenceNumber frame) -> void
{
  std::scoped_lock lock(mutex_);
  current_frame_ = frame.get();
  frame_ledger_.BeginFrame(frame);
  RefreshLedgerState();
}

auto DiagnosticsService::RecordPass(DiagnosticsPassRecord record) -> void
{
  std::scoped_lock lock(mutex_);
  if (!frame_ledger_.IsFrameOpen() || !IsFrameLedgerEnabled()) {
    return;
  }
  frame_ledger_.RecordPass(std::move(record));
}

auto DiagnosticsService::RecordProduct(DiagnosticsProductRecord record) -> void
{
  std::scoped_lock lock(mutex_);
  if (!frame_ledger_.IsFrameOpen() || !IsFrameLedgerEnabled()) {
    return;
  }
  frame_ledger_.RecordProduct(std::move(record));
}

auto DiagnosticsService::ReportIssue(DiagnosticsIssue issue) -> void
{
  std::scoped_lock lock(mutex_);
  if (!frame_ledger_.IsFrameOpen() || !IsFrameLedgerEnabled()) {
    return;
  }
  frame_ledger_.ReportIssue(std::move(issue));
}

auto DiagnosticsService::EndFrame() -> void
{
  std::scoped_lock lock(mutex_);
  if (!frame_ledger_.IsFrameOpen()) {
    return;
  }
  frame_ledger_.EndFrame();
}

auto DiagnosticsService::GetLatestSnapshot() const -> DiagnosticsFrameSnapshot
{
  std::scoped_lock lock(mutex_);
  return frame_ledger_.GetLatestSnapshot();
}

auto DiagnosticsService::ComputeEffectiveFeatures() const noexcept
  -> DiagnosticsFeatureSet
{
  if (!HasAllCapabilities(renderer_capabilities_,
        RendererCapabilityFamily::kDiagnosticsAndProfiling)) {
    return DiagnosticsFeature::kNone;
  }
  return requested_features_;
}

auto DiagnosticsService::IsFrameLedgerEnabled() const noexcept -> bool
{
  return HasAllFeatures(enabled_features_, DiagnosticsFeature::kFrameLedger);
}

auto DiagnosticsService::IsGpuTimelineFeatureEnabled() const noexcept -> bool
{
  return HasAllFeatures(enabled_features_, DiagnosticsFeature::kGpuTimeline);
}

auto DiagnosticsService::IsUserTimingEnabled() const noexcept -> bool
{
  return gpu_timeline_profiler_ != nullptr && gpu_timeline_enabled_requested_
    && IsGpuTimelineFeatureEnabled();
}

auto DiagnosticsService::IsViewerTimingEnabled() const noexcept -> bool
{
  return gpu_timeline_profiler_ != nullptr
    && (IsUserTimingEnabled() || viewer_leases_ != 0U);
}

auto DiagnosticsService::HasAvailableGpuTimelineFrame() const -> bool
{
  return gpu_timeline_profiler_ != nullptr
    && gpu_timeline_profiler_->GetLastPublishedFrame().has_value();
}

auto DiagnosticsService::ApplyGpuTimelineEnabled() -> void
{
  const auto viewer_timing = IsViewerTimingEnabled();
  if (gpu_work_summarizer_ && viewer_timing != viewer_timing_active_) {
    // Frames already in flight were recorded without view collection.
    if (viewer_timing) {
      gpu_work_summarizer_->Activate(current_frame_ + 1U);
    } else {
      gpu_work_summarizer_->Deactivate();
    }
  }
  viewer_timing_active_ = viewer_timing;
  if (gpu_timeline_profiler_ != nullptr) {
    gpu_timeline_profiler_->SetEnabled(
      production_leases_ != 0U || viewer_timing);
  }
}

auto DiagnosticsService::RefreshLedgerState() -> void
{
  frame_ledger_.UpdateState(shader_debug_mode_, requested_features_,
    enabled_features_, IsUserTimingEnabled(), HasAvailableGpuTimelineFrame());
}

auto DiagnosticsService::ReportGpuTimelineIssue(
  const internal::GpuTimelineDiagnostic& diagnostic) -> void
{
  auto code = DiagnosticsIssueCode::kTimelineOverflow;
  if (diagnostic.code == "gpu.timestamp.incomplete_scope") {
    code = DiagnosticsIssueCode::kTimelineIncompleteScope;
  }

  auto issue = MakeDiagnosticsIssue(code, DiagnosticsSeverity::kWarning,
    diagnostic.message.empty() ? diagnostic.code : diagnostic.message);
  issue.pass_name = "Vortex.GpuTimeline";
  issue.product_name = "Vortex.GpuTimelineFrame";
  issue.view_name = "frame:" + std::to_string(diagnostic.frame_sequence);
  frame_ledger_.ReportIssue(std::move(issue));
}

GpuTimelineLease::~GpuTimelineLease() { Release(); }

GpuTimelineLease::GpuTimelineLease(GpuTimelineLease&& other) noexcept
  : service_(std::exchange(other.service_, nullptr))
  , viewer_(other.viewer_)
{
}

auto GpuTimelineLease::operator=(GpuTimelineLease&& other) noexcept
  -> GpuTimelineLease&
{
  if (this != &other) {
    Release();
    service_ = std::exchange(other.service_, nullptr);
    viewer_ = other.viewer_;
  }
  return *this;
}

auto GpuTimelineLease::Release() noexcept -> void
{
  if (const auto service = std::exchange(service_, nullptr)) {
    service->ReleaseLease(viewer_);
  }
}

namespace internal {

  auto GpuTimelineAccess::AcquireLease() const -> GpuTimelineLease
  {
    std::scoped_lock lock(service_->mutex_);
    return service_->AcquireLease(false);
  }

  auto GpuTimelineAccess::AddSink(std::shared_ptr<GpuTimelineSink> sink) const
    -> void
  {
    std::scoped_lock lock(service_->mutex_);
    CHECK_NOTNULL_F(service_->gpu_timeline_profiler_);
    service_->gpu_timeline_profiler_->AddSink(std::move(sink));
  }

  auto GpuTimelineAccess::AttachCollector(
    graphics::CommandRecorder& recorder) const -> void
  {
    std::scoped_lock lock(service_->mutex_);
    const auto queue = recorder.GetTargetQueue();
    if (queue && queue->GetQueueRole() == graphics::QueueRole::kGraphics) {
      recorder.SetTelemetryCollector(
        observer_ptr<graphics::IGpuProfileCollector> {
          service_->gpu_timeline_profiler_.get() });
    }
  }

  auto GpuTimelineAccess::InvalidateCurrentFrame() const noexcept -> void
  {
    std::scoped_lock lock(service_->mutex_);
    if (service_->gpu_timeline_profiler_) {
      service_->gpu_timeline_profiler_->InvalidateCurrentFrame();
    }
  }

} // namespace internal

} // namespace oxygen::vortex
