//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Renderer/Internal/GpuTimelineProfiler.h>
#include <Oxygen/Renderer/api_export.h>

namespace oxygen::engine::imgui {

struct GpuTimelinePresentationScope {
  uint32_t scope_id { 0 };
  uint32_t parent_scope_id { 0 };
  uint64_t stable_key { 0 };
  uint64_t name_hash { 0 };
  std::string display_name;
  uint16_t depth { 0 };
  uint16_t stream_id { 0 };
  bool valid { false };
  float raw_start_ms { 0.0F };
  float raw_duration_ms { 0.0F };
  float raw_end_ms { 0.0F };
  float display_start_ms { 0.0F };
  float display_duration_ms { 0.0F };
  float display_end_ms { 0.0F };
};

struct GpuTimelinePresentationRow {
  std::string display_name;
  uint16_t depth { 0 };
  uint32_t grouped_scope_count { 1 };
  bool valid { false };
  bool is_grouped_pass { false };
  float raw_start_ms { 0.0F };
  float raw_duration_ms { 0.0F };
  float raw_end_ms { 0.0F };
  float display_start_ms { 0.0F };
  float display_duration_ms { 0.0F };
  float display_end_ms { 0.0F };
};

struct GpuTimelinePresentationFrame {
  uint64_t frame_sequence { 0 };
  float raw_frame_span_ms { 0.0F };
  float display_frame_span_ms { 0.0F };
  std::vector<GpuTimelinePresentationScope> scopes;
  std::vector<GpuTimelinePresentationRow> rows;
};

class GpuTimelinePresentationSmoother final {
public:
  static constexpr float kDefaultBlendFactor = 0.2F;
  static constexpr float kDefaultJitterDeadbandMs = 0.5F;

  OXGN_RNDR_API auto Apply(const internal::GpuTimelineFrame& frame)
    -> GpuTimelinePresentationFrame;

private:
  struct SmoothedScopeState {
    float start_ms { 0.0F };
    float duration_ms { 0.0F };
  };

  std::unordered_map<uint64_t, SmoothedScopeState> scope_state_ {};
  float smoothed_frame_span_ms_ { 0.0F };
  bool has_smoothed_frame_span_ { false };
};

} // namespace oxygen::engine::imgui
