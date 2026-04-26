//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <string>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Vortex/Diagnostics/DiagnosticsTypes.h>
#include <Oxygen/Vortex/ShaderDebugMode.h>
#include <Oxygen/Vortex/api_export.h>

namespace oxygen::vortex {

class DiagnosticsFrameLedger {
public:
  static constexpr auto kMaxIssuesPerFrame = 64U;
  static constexpr auto kMaxIssueContextLength = 256U;

  DiagnosticsFrameLedger() = default;
  ~DiagnosticsFrameLedger() = default;

  OXYGEN_DEFAULT_COPYABLE(DiagnosticsFrameLedger)
  OXYGEN_DEFAULT_MOVABLE(DiagnosticsFrameLedger)

  OXGN_VRTX_API auto UpdateState(ShaderDebugMode debug_mode,
    DiagnosticsFeatureSet requested_features,
    DiagnosticsFeatureSet enabled_features) -> void;
  OXGN_VRTX_API auto BeginFrame(frame::SequenceNumber frame) -> void;
  OXGN_VRTX_API auto RecordPass(DiagnosticsPassRecord record) -> void;
  OXGN_VRTX_API auto RecordProduct(DiagnosticsProductRecord record) -> void;
  OXGN_VRTX_API auto ReportIssue(DiagnosticsIssue issue) -> void;
  OXGN_VRTX_API auto EndFrame() -> void;

  [[nodiscard]] OXGN_VRTX_API auto IsFrameOpen() const noexcept -> bool;
  [[nodiscard]] OXGN_VRTX_API auto GetLatestSnapshot() const
    -> DiagnosticsFrameSnapshot;
  [[nodiscard]] OXGN_VRTX_API auto GetCurrentSnapshot() const
    -> DiagnosticsFrameSnapshot;

private:
  [[nodiscard]] static auto SameIssue(
    const DiagnosticsIssue& lhs, const DiagnosticsIssue& rhs) noexcept -> bool;
  static auto ClampIssueContext(DiagnosticsIssue& issue) -> void;
  static auto ClampString(std::string& value) -> void;
  auto ApplyState(DiagnosticsFrameSnapshot& snapshot) const -> void;

  ShaderDebugMode debug_mode_ { ShaderDebugMode::kDisabled };
  DiagnosticsFeatureSet requested_features_ { DiagnosticsFeature::kNone };
  DiagnosticsFeatureSet enabled_features_ { DiagnosticsFeature::kNone };
  DiagnosticsFrameSnapshot frame_snapshot_ {};
  DiagnosticsFrameSnapshot latest_snapshot_ {};
  bool frame_open_ { false };
};

} // namespace oxygen::vortex
