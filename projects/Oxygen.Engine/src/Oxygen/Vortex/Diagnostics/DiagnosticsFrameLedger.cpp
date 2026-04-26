//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Vortex/Diagnostics/DiagnosticsFrameLedger.h>

#include <algorithm>
#include <utility>

#include <Oxygen/Base/Logging.h>

namespace oxygen::vortex {

auto DiagnosticsFrameLedger::UpdateState(const ShaderDebugMode debug_mode,
  const DiagnosticsFeatureSet requested_features,
  const DiagnosticsFeatureSet enabled_features,
  const bool gpu_timeline_enabled,
  const bool gpu_timeline_frame_available) -> void
{
  debug_mode_ = debug_mode;
  requested_features_ = requested_features;
  enabled_features_ = enabled_features;
  gpu_timeline_enabled_ = gpu_timeline_enabled;
  gpu_timeline_frame_available_ = gpu_timeline_frame_available;
  ApplyState(latest_snapshot_);
  if (frame_open_) {
    ApplyState(frame_snapshot_);
  }
}

auto DiagnosticsFrameLedger::BeginFrame(const frame::SequenceNumber frame)
  -> void
{
  frame_open_ = true;
  frame_snapshot_ = {};
  frame_snapshot_.frame_index = frame;
  ApplyState(frame_snapshot_);
}

auto DiagnosticsFrameLedger::RecordPass(DiagnosticsPassRecord record) -> void
{
  if (!frame_open_) {
    return;
  }
  CHECK_F(!record.name.empty(), "diagnostics pass records require a name");
  frame_snapshot_.passes.push_back(std::move(record));
}

auto DiagnosticsFrameLedger::RecordProduct(DiagnosticsProductRecord record)
  -> void
{
  if (!frame_open_) {
    return;
  }
  CHECK_F(!record.name.empty(), "diagnostics product records require a name");
  frame_snapshot_.products.push_back(std::move(record));
}

auto DiagnosticsFrameLedger::ReportIssue(DiagnosticsIssue issue) -> void
{
  if (!frame_open_) {
    return;
  }
  CHECK_F(!issue.code.empty(), "diagnostics issues require a stable code");
  ClampIssueContext(issue);
  issue.occurrences = std::max(issue.occurrences, 1U);

  const auto existing = std::ranges::find_if(frame_snapshot_.issues,
    [&issue](const DiagnosticsIssue& candidate) {
      return SameIssue(candidate, issue);
    });
  if (existing != frame_snapshot_.issues.end()) {
    existing->occurrences += issue.occurrences;
    if (static_cast<int>(issue.severity)
      > static_cast<int>(existing->severity)) {
      existing->severity = issue.severity;
    }
    if (!issue.message.empty()) {
      existing->message = std::move(issue.message);
    }
    return;
  }

  if (frame_snapshot_.issues.size() >= kMaxIssuesPerFrame) {
    return;
  }
  frame_snapshot_.issues.push_back(std::move(issue));
}

auto DiagnosticsFrameLedger::EndFrame() -> void
{
  if (!frame_open_) {
    return;
  }
  ApplyState(frame_snapshot_);
  latest_snapshot_ = frame_snapshot_;
  frame_open_ = false;
}

auto DiagnosticsFrameLedger::IsFrameOpen() const noexcept -> bool
{
  return frame_open_;
}

auto DiagnosticsFrameLedger::GetLatestSnapshot() const
  -> DiagnosticsFrameSnapshot
{
  return latest_snapshot_;
}

auto DiagnosticsFrameLedger::GetCurrentSnapshot() const
  -> DiagnosticsFrameSnapshot
{
  return frame_snapshot_;
}

auto DiagnosticsFrameLedger::SameIssue(const DiagnosticsIssue& lhs,
  const DiagnosticsIssue& rhs) noexcept -> bool
{
  return lhs.code == rhs.code && lhs.view_name == rhs.view_name
    && lhs.pass_name == rhs.pass_name && lhs.product_name == rhs.product_name;
}

auto DiagnosticsFrameLedger::ClampIssueContext(DiagnosticsIssue& issue) -> void
{
  ClampString(issue.message);
  ClampString(issue.view_name);
  ClampString(issue.pass_name);
  ClampString(issue.product_name);
}

auto DiagnosticsFrameLedger::ClampString(std::string& value) -> void
{
  if (value.size() > kMaxIssueContextLength) {
    value.resize(kMaxIssueContextLength);
  }
}

auto DiagnosticsFrameLedger::ApplyState(DiagnosticsFrameSnapshot& snapshot) const
  -> void
{
  snapshot.active_shader_debug_mode = debug_mode_;
  snapshot.requested_features = requested_features_;
  snapshot.enabled_features = enabled_features_;
  snapshot.gpu_timeline_enabled = gpu_timeline_enabled_;
  snapshot.gpu_timeline_frame_available = gpu_timeline_frame_available_;
}

} // namespace oxygen::vortex
