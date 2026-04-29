//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Vortex/Diagnostics/DiagnosticsService.h>

#include <exception>
#include <string>
#include <utility>

#include <Oxygen/Vortex/Internal/GpuTimelineProfiler.h>

namespace oxygen::vortex {

auto DiagnosticsConfig::Default() noexcept -> DiagnosticsConfig
{
#ifdef NDEBUG
  return DiagnosticsConfig {
    .default_features = DiagnosticsFeature::kNone,
  };
#else
  return DiagnosticsConfig {
    .default_features = DiagnosticsFeature::kFrameLedger
      | DiagnosticsFeature::kShaderDebugModes,
  };
#endif
}

DiagnosticsService::DiagnosticsService(const CapabilitySet renderer_capabilities,
  const DiagnosticsConfig config)
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

auto DiagnosticsService::SetShaderDebugMode(
  const ShaderDebugMode mode) noexcept -> void
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

auto DiagnosticsService::SetGpuTimelineProfiler(
  const observer_ptr<internal::GpuTimelineProfiler> profiler) -> void
{
  std::scoped_lock lock(mutex_);
  gpu_timeline_profiler_ = profiler;
  ApplyGpuTimelineEnabled();
  RefreshLedgerState();
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
  return gpu_timeline_enabled_requested_ && gpu_timeline_profiler_ != nullptr
    && IsGpuTimelineFeatureEnabled();
}

auto DiagnosticsService::SetGpuTimelineMaxScopesPerFrame(
  const std::uint32_t max_scopes) -> void
{
  std::scoped_lock lock(mutex_);
  if (gpu_timeline_profiler_ != nullptr) {
    gpu_timeline_profiler_->SetMaxScopesPerFrame(max_scopes);
  }
}

auto DiagnosticsService::SetGpuTimelineRetainLatestFrame(
  const bool retain_latest_frame) -> void
{
  std::scoped_lock lock(mutex_);
  if (gpu_timeline_profiler_ != nullptr) {
    gpu_timeline_profiler_->SetRetainLatestFrame(retain_latest_frame);
  }
}

auto DiagnosticsService::RequestGpuTimelineExport(
  const std::filesystem::path& path) -> void
{
  std::scoped_lock lock(mutex_);
  if (gpu_timeline_profiler_ != nullptr) {
    gpu_timeline_profiler_->RequestOneShotExport(path);
  }
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

auto DiagnosticsService::ExportCaptureManifest(const std::filesystem::path& path,
  const DiagnosticsCaptureManifestOptions& options) -> bool
{
  try {
    WriteDiagnosticsCaptureManifest(path, GetLatestSnapshot(), options);
    return true;
  } catch (const std::exception& ex) {
    auto issue = MakeDiagnosticsIssue(
      DiagnosticsIssueCode::kManifestWriteFailed, DiagnosticsSeverity::kError,
      ex.what());
    issue.product_name = "Vortex.DiagnosticsCaptureManifest";
    ReportIssue(std::move(issue));
    return false;
  }
}

auto DiagnosticsService::BeginFrame(const frame::SequenceNumber frame) -> void
{
  std::scoped_lock lock(mutex_);
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
  if (!HasAllCapabilities(
        renderer_capabilities_, RendererCapabilityFamily::kDiagnosticsAndProfiling)) {
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

auto DiagnosticsService::HasAvailableGpuTimelineFrame() const -> bool
{
  return gpu_timeline_profiler_ != nullptr
    && gpu_timeline_profiler_->GetLastPublishedFrame().has_value();
}

auto DiagnosticsService::ApplyGpuTimelineEnabled() -> void
{
  if (gpu_timeline_profiler_ != nullptr) {
    gpu_timeline_profiler_->SetEnabled(
      gpu_timeline_enabled_requested_ && IsGpuTimelineFeatureEnabled());
  }
}

auto DiagnosticsService::RefreshLedgerState() -> void
{
  frame_ledger_.UpdateState(shader_debug_mode_, requested_features_,
    enabled_features_,
    gpu_timeline_enabled_requested_ && gpu_timeline_profiler_ != nullptr
      && IsGpuTimelineFeatureEnabled(),
    HasAvailableGpuTimelineFrame());
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

} // namespace oxygen::vortex
