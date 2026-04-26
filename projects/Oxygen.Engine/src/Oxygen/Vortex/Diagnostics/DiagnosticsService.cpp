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
  RefreshLedgerState();
}

DiagnosticsService::~DiagnosticsService() = default;

auto DiagnosticsService::SetRendererCapabilities(
  const CapabilitySet capabilities) -> void
{
  std::scoped_lock lock(mutex_);
  renderer_capabilities_ = capabilities;
  enabled_features_ = ComputeEffectiveFeatures();
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

auto DiagnosticsService::BeginFrame(const frame::SequenceNumber frame) -> void
{
  std::scoped_lock lock(mutex_);
  frame_ledger_.BeginFrame(frame);
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

auto DiagnosticsService::RefreshLedgerState() -> void
{
  frame_ledger_.UpdateState(
    shader_debug_mode_, requested_features_, enabled_features_);
}

} // namespace oxygen::vortex
