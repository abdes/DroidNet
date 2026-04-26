//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Vortex/Diagnostics/DiagnosticsService.h>

#include <utility>

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
  RefreshSnapshotState(latest_snapshot_);
}

DiagnosticsService::~DiagnosticsService() = default;

auto DiagnosticsService::SetRendererCapabilities(
  const CapabilitySet capabilities) -> void
{
  std::scoped_lock lock(mutex_);
  renderer_capabilities_ = capabilities;
  enabled_features_ = ComputeEffectiveFeatures();
  RefreshSnapshotState(latest_snapshot_);
  if (frame_open_) {
    RefreshSnapshotState(frame_snapshot_);
  }
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
  RefreshSnapshotState(latest_snapshot_);
  if (frame_open_) {
    RefreshSnapshotState(frame_snapshot_);
  }
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
  RefreshSnapshotState(latest_snapshot_);
  if (frame_open_) {
    RefreshSnapshotState(frame_snapshot_);
  }
}

auto DiagnosticsService::GetShaderDebugMode() const noexcept -> ShaderDebugMode
{
  std::scoped_lock lock(mutex_);
  return shader_debug_mode_;
}

auto DiagnosticsService::BeginFrame(const frame::SequenceNumber frame) -> void
{
  std::scoped_lock lock(mutex_);
  frame_open_ = true;
  frame_snapshot_ = {};
  frame_snapshot_.frame_index = frame;
  RefreshSnapshotState(frame_snapshot_);
}

auto DiagnosticsService::RecordPass(DiagnosticsPassRecord record) -> void
{
  std::scoped_lock lock(mutex_);
  if (!frame_open_ || !IsFrameLedgerEnabled()) {
    return;
  }
  frame_snapshot_.passes.push_back(std::move(record));
}

auto DiagnosticsService::RecordProduct(DiagnosticsProductRecord record) -> void
{
  std::scoped_lock lock(mutex_);
  if (!frame_open_ || !IsFrameLedgerEnabled()) {
    return;
  }
  frame_snapshot_.products.push_back(std::move(record));
}

auto DiagnosticsService::ReportIssue(DiagnosticsIssue issue) -> void
{
  std::scoped_lock lock(mutex_);
  if (!frame_open_ || !IsFrameLedgerEnabled()) {
    return;
  }
  frame_snapshot_.issues.push_back(std::move(issue));
}

auto DiagnosticsService::EndFrame() -> void
{
  std::scoped_lock lock(mutex_);
  if (!frame_open_) {
    return;
  }
  RefreshSnapshotState(frame_snapshot_);
  latest_snapshot_ = frame_snapshot_;
  frame_open_ = false;
}

auto DiagnosticsService::GetLatestSnapshot() const -> DiagnosticsFrameSnapshot
{
  std::scoped_lock lock(mutex_);
  return latest_snapshot_;
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

auto DiagnosticsService::RefreshSnapshotState(
  DiagnosticsFrameSnapshot& snapshot) const -> void
{
  snapshot.active_shader_debug_mode = shader_debug_mode_;
  snapshot.requested_features = requested_features_;
  snapshot.enabled_features = enabled_features_;
}

} // namespace oxygen::vortex
