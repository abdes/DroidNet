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
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Graphics/Common/ProfileScope.h>
#include <Oxygen/Renderer/api_export.h>

namespace oxygen {
class Graphics;
}

namespace oxygen::graphics {
class CommandQueue;
class CommandRecorder;
class TimestampQueryProvider;
} // namespace oxygen::graphics

namespace oxygen::engine::internal {

struct GpuTimelineDiagnostic {
  std::string code;
  std::string message;
  uint64_t frame_sequence { 0 };
  uint32_t used_query_slots { 0 };
  uint32_t max_query_slots { 0 };
};

struct GpuTimelineScope {
  uint32_t scope_id { 0 };
  uint32_t parent_scope_id { 0 };
  std::vector<uint32_t> child_scope_ids;
  uint64_t name_hash { 0 };
  std::string display_name;
  uint32_t begin_query_slot { 0 };
  uint32_t end_query_slot { 0 };
  float start_ms { 0.0F };
  float end_ms { 0.0F };
  float duration_ms { 0.0F };
  uint16_t depth { 0 };
  uint16_t stream_id { 0 };
  uint8_t flags { 0 };
  bool valid { false };
};

struct GpuTimelineFrame {
  uint64_t frame_sequence { 0 };
  uint64_t timestamp_frequency_hz { 0 };
  bool profiling_enabled { false };
  bool overflowed { false };
  uint32_t used_query_slots { 0 };
  std::vector<GpuTimelineScope> scopes;
  std::vector<GpuTimelineDiagnostic> diagnostics;
};

class GpuTimelineSink {
public:
  OXGN_RNDR_API virtual ~GpuTimelineSink() = default;

  [[nodiscard]] virtual auto ConsumeFrame(const GpuTimelineFrame& frame)
    -> bool
    = 0;
};

class GpuTimelineProfiler final : public graphics::IGpuProfileScopeHandler {
public:
  OXGN_RNDR_API explicit GpuTimelineProfiler(observer_ptr<Graphics> graphics);
  OXGN_RNDR_API ~GpuTimelineProfiler() override;

  OXYGEN_MAKE_NON_COPYABLE(GpuTimelineProfiler)
  OXYGEN_MAKE_NON_MOVABLE(GpuTimelineProfiler)

  OXGN_RNDR_API auto SetEnabled(bool enabled) -> void;
  OXGN_RNDR_API auto SetMaxScopesPerFrame(uint32_t max_scopes) -> void;
  OXGN_RNDR_API auto SetRetainLatestFrame(bool retain_latest_frame) -> void;
  [[nodiscard]] OXGN_RNDR_API auto MakeScopeOptions() const
    -> graphics::GpuEventScopeOptions;

  OXGN_RNDR_API auto OnFrameStart(frame::SequenceNumber frame_sequence)
    -> void;
  OXGN_RNDR_API auto OnFrameRecordTailResolve() -> void;

  [[nodiscard]] OXGN_RNDR_API auto BeginScope(graphics::CommandRecorder& recorder,
    std::string_view name, const graphics::GpuEventScopeOptions& options)
    -> graphics::GpuEventScopeToken override;

  OXGN_RNDR_API auto EndScope(graphics::CommandRecorder& recorder,
    const graphics::GpuEventScopeToken& token) -> void override;

  OXGN_RNDR_API auto AddSink(std::shared_ptr<GpuTimelineSink> sink) -> void;
  OXGN_RNDR_API auto RequestOneShotExport(
    const std::filesystem::path& path) -> void;

  [[nodiscard]] OXGN_RNDR_API auto GetLastPublishedFrame() const
    -> std::optional<GpuTimelineFrame>;

private:
  struct GpuScopeRecord {
    uint64_t scope_name_hash { 0 };
    const char* display_name { nullptr };
    uint32_t parent_scope_id { 0 };
    uint32_t begin_query_slot { 0 };
    uint32_t end_query_slot { 0 };
    uint16_t depth { 0 };
    uint16_t stream_id { 0 };
    uint8_t flags { 0 };
  };

  struct GpuFrameCapture {
    uint64_t frame_sequence { 0 };
    uint64_t resolve_fence_value { 0 };
    uint32_t used_query_slots { 0 };
    bool profiling_enabled { false };
    bool overflowed { false };
    bool resolve_submitted { false };
    std::vector<GpuScopeRecord> scopes;
    std::vector<GpuTimelineDiagnostic> diagnostics;
  };

  static constexpr uint32_t kInvalidScopeId = 0xFFFFFFFFU;
  static constexpr uint8_t kGpuScopeFlagComplete = 1U << 0U;
  static constexpr uint8_t kGpuScopeFlagValid = 1U << 1U;

  auto ConsumePreviousFrame() -> void;
  auto ResetForFrame(frame::SequenceNumber frame_sequence) -> void;
  auto CloseIncompleteScopes() -> void;
  auto BuildTimelineFrame(const std::span<const uint64_t>& ticks) const
    -> GpuTimelineFrame;
  auto PublishFrame(const GpuTimelineFrame& frame) -> void;
  auto InternName(std::string_view name) -> const char*;
  auto ResolveGraphicsQueue() const -> observer_ptr<graphics::CommandQueue>;
  auto ResolveTimestampProvider() const
    -> observer_ptr<graphics::TimestampQueryProvider>;
  auto AddDiagnostic(std::string code, std::string message) -> void;

  observer_ptr<Graphics> graphics_ { nullptr };
  bool enabled_ { false };
  bool retain_latest_frame_ { false };
  uint32_t max_scopes_per_frame_ { 4096U };
  uint64_t timestamp_frequency_hz_ { 0U };
  std::vector<uint32_t> scope_stack_ {};
  GpuFrameCapture frame_capture_ {};
  std::unordered_map<std::string, uint64_t> interned_names_ {};
  std::vector<std::shared_ptr<GpuTimelineSink>> sinks_ {};
  mutable std::mutex published_frame_mutex_;
  std::optional<GpuTimelineFrame> last_published_frame_ {};
};

} // namespace oxygen::engine::internal
