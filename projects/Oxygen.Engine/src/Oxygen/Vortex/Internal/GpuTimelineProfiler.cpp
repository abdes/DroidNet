//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstring>
#include <fstream>
#include <limits>

#include <fmt/format.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Graphics/Common/CommandQueue.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/TimestampQueryProvider.h>
#include <Oxygen/Graphics/Common/Types/QueueRole.h>
#include <Oxygen/Vortex/Internal/GpuTimelineProfiler.h>

namespace {

constexpr uint8_t kCollectorStateFlagActive = 1U << 0U;
constexpr std::size_t kJsonEscapeSlack = 8U;
constexpr double kMillisecondsPerSecond = 1000.0;

struct TimelineCollectorScopeState {
  uint32_t scope_id { 0U };
};

static_assert(sizeof(TimelineCollectorScopeState)
  <= sizeof(oxygen::graphics::GpuProfileCollectorState::storage));

auto HashName(std::string_view value) -> uint64_t
{
  constexpr uint64_t kFnvOffsetBasis = 1469598103934665603ULL;
  constexpr uint64_t kFnvPrime = 1099511628211ULL;

  uint64_t hash = kFnvOffsetBasis;
  for (const char c : value) {
    hash ^= static_cast<uint8_t>(c);
    hash *= kFnvPrime;
  }
  return hash;
}

auto EscapeJson(std::string_view input) -> std::string
{
  std::string escaped;
  escaped.reserve(input.size() + kJsonEscapeSlack);

  for (const char c : input) {
    switch (c) {
    case '\\':
      escaped += "\\\\";
      break;
    case '"':
      escaped += "\\\"";
      break;
    case '\n':
      escaped += "\\n";
      break;
    case '\r':
      escaped += "\\r";
      break;
    case '\t':
      escaped += "\\t";
      break;
    default:
      escaped.push_back(c);
      break;
    }
  }

  return escaped;
}

class FileExportSink final : public oxygen::vortex::internal::GpuTimelineSink {
public:
  OXYGEN_MAKE_NON_COPYABLE(FileExportSink)
  OXYGEN_MAKE_NON_MOVABLE(FileExportSink)

  explicit FileExportSink(std::filesystem::path output_path)
    : output_path_(std::move(output_path))
  {
  }

  ~FileExportSink() override = default;

  auto ConsumeFrame(const oxygen::vortex::internal::GpuTimelineFrame& frame)
    -> bool override
  {
    if (completed_) {
      return false;
    }

    try {
      std::filesystem::create_directories(output_path_.parent_path());
      if (output_path_.extension() == ".csv") {
        WriteCsv(frame);
      } else {
        WriteJson(frame);
      }
      completed_ = true;
    } catch (const std::exception& ex) {
      LOG_F(ERROR, "GPU timeline export failed for '{}': {}",
        output_path_.string(), ex.what());
    }

    return false;
  }

private:
  auto WriteCsv(const oxygen::vortex::internal::GpuTimelineFrame& frame) const
    -> void
  {
    std::ofstream out(output_path_, std::ios::binary | std::ios::trunc);
    out << fmt::format(
      "# timestamp_freq_hz={}\n", frame.timestamp_frequency_hz);
    out << "frame_seq,scope_id,parent_scope_id,depth,stream_id,name_hash,name,"
           "start_ms,end_ms,duration_ms,valid,flags\n";

    for (const auto& scope : frame.scopes) {
      out << fmt::format(
        "{},{},{},{},{},{},\"{}\",{:.6f},{:.6f},{:.6f},{},{}\n",
        frame.frame_sequence, scope.scope_id, scope.parent_scope_id,
        scope.depth, scope.stream_id, scope.name_hash,
        EscapeJson(scope.display_name), scope.start_ms, scope.end_ms,
        scope.duration_ms, scope.valid ? 1 : 0, scope.flags);
    }
  }

  auto WriteJson(const oxygen::vortex::internal::GpuTimelineFrame& frame) const
    -> void
  {
    std::ofstream out(output_path_, std::ios::binary | std::ios::trunc);
    out << "{\n";
    out << fmt::format("  \"version\": 1,\n");
    out << fmt::format("  \"frame_seq\": {},\n", frame.frame_sequence);
    out << fmt::format(
      "  \"timestamp_freq_hz\": {},\n", frame.timestamp_frequency_hz);
    out << fmt::format("  \"profiling_enabled\": {},\n",
      frame.profiling_enabled ? "true" : "false");
    out << fmt::format(
      "  \"overflowed\": {},\n", frame.overflowed ? "true" : "false");
    out << fmt::format("  \"used_query_slots\": {},\n", frame.used_query_slots);
    out << "  \"scopes\": [\n";
    for (std::size_t i = 0; i < frame.scopes.size(); ++i) {
      const auto& scope = frame.scopes[i];
      out << "    {\n";
      out << fmt::format("      \"scope_id\": {},\n", scope.scope_id);
      out << fmt::format(
        "      \"parent_scope_id\": {},\n", scope.parent_scope_id);
      out << fmt::format("      \"name_hash\": {},\n", scope.name_hash);
      out << fmt::format(
        "      \"name\": \"{}\",\n", EscapeJson(scope.display_name));
      out << fmt::format("      \"depth\": {},\n", scope.depth);
      out << fmt::format("      \"stream_id\": {},\n", scope.stream_id);
      out << fmt::format(
        "      \"begin_query_slot\": {},\n", scope.begin_query_slot);
      out << fmt::format(
        "      \"end_query_slot\": {},\n", scope.end_query_slot);
      out << fmt::format("      \"start_ms\": {:.6f},\n", scope.start_ms);
      out << fmt::format("      \"end_ms\": {:.6f},\n", scope.end_ms);
      out << fmt::format("      \"duration_ms\": {:.6f},\n", scope.duration_ms);
      out << fmt::format(
        "      \"valid\": {},\n", scope.valid ? "true" : "false");
      out << fmt::format("      \"flags\": {}\n", scope.flags);
      out << (i + 1U == frame.scopes.size() ? "    }\n" : "    },\n");
    }
    out << "  ],\n";
    out << "  \"diagnostics\": [\n";
    for (std::size_t i = 0; i < frame.diagnostics.size(); ++i) {
      const auto& diagnostic = frame.diagnostics[i];
      out << "    {\n";
      out << fmt::format(
        "      \"code\": \"{}\",\n", EscapeJson(diagnostic.code));
      out << fmt::format(
        "      \"message\": \"{}\"\n", EscapeJson(diagnostic.message));
      out << (i + 1U == frame.diagnostics.size() ? "    }\n" : "    },\n");
    }
    out << "  ]\n";
    out << "}\n";
  }

  std::filesystem::path output_path_;
  bool completed_ { false };
};

} // namespace

namespace oxygen::vortex::internal {

GpuTimelineProfiler::GpuTimelineProfiler(const observer_ptr<Graphics> graphics)
  : graphics_(graphics)
{
  frame_capture_.scopes.reserve(max_scopes_per_frame_);
  scope_stack_.reserve(max_scopes_per_frame_);
}

GpuTimelineProfiler::~GpuTimelineProfiler() = default;

auto GpuTimelineProfiler::SetEnabled(const bool enabled) -> void
{
  enabled_ = enabled;
}

auto GpuTimelineProfiler::SetMaxScopesPerFrame(const uint32_t max_scopes)
  -> void
{
  max_scopes_per_frame_ = std::max<uint32_t>(1U, max_scopes);
  frame_capture_.scopes.reserve(max_scopes_per_frame_);
  scope_stack_.reserve(max_scopes_per_frame_);
}

auto GpuTimelineProfiler::SetRetainLatestFrame(const bool retain_latest_frame)
  -> void
{
  retain_latest_frame_ = retain_latest_frame;
  if (!retain_latest_frame_) {
    std::scoped_lock lock(published_frame_mutex_);
    last_published_frame_.reset();
  }
}

auto GpuTimelineProfiler::OnFrameStart(
  const frame::SequenceNumber frame_sequence) -> void
{
  ConsumePreviousFrame();
  ResetForFrame(frame_sequence);
}

auto GpuTimelineProfiler::OnFrameRecordTailResolve() -> void
{
  if (frame_capture_.resolve_submitted) {
    return;
  }

  CloseIncompleteScopes();

  if (!frame_capture_.profiling_enabled
    || frame_capture_.used_query_slots == 0U) {
    return;
  }

  auto provider = ResolveTimestampProvider();
  auto queue = ResolveGraphicsQueue();
  if (provider == nullptr || queue == nullptr) {
    AddDiagnostic("gpu.timestamp.resolve_failed",
      "timestamp provider or graphics queue is unavailable at frame tail");
    frame_capture_.profiling_enabled = false;
    return;
  }

  const auto queue_key = graphics_->QueueKeyFor(graphics::QueueRole::kGraphics);
  {
    auto recorder
      = graphics_->AcquireCommandRecorder(queue_key, "GpuTimestamp.Resolve");
    if (!recorder) {
      AddDiagnostic("gpu.timestamp.resolve_failed",
        "failed to acquire resolve command recorder");
      frame_capture_.profiling_enabled = false;
      return;
    }

    if (!provider->RecordResolve(*recorder, frame_capture_.used_query_slots)) {
      AddDiagnostic("gpu.timestamp.resolve_failed",
        "backend failed to record timestamp resolve");
      frame_capture_.profiling_enabled = false;
      return;
    }

    frame_capture_.resolve_fence_value = queue->Signal();
    recorder->RecordQueueSignal(frame_capture_.resolve_fence_value);
  }

  frame_capture_.resolve_submitted = true;
}

auto GpuTimelineProfiler::BeginScope(graphics::CommandRecorder& recorder,
  const graphics::GpuProfileScopeInfo& info,
  graphics::GpuProfileCollectorState& state) -> void
{
  if (!frame_capture_.profiling_enabled
    || info.desc.granularity != profiling::ProfileGranularity::kTelemetry) {
    state.flags = 0U;
    return;
  }

  auto provider = ResolveTimestampProvider();
  if (provider == nullptr) {
    state.flags = 0U;
    return;
  }

  const auto max_query_slots = max_scopes_per_frame_ * 2U;
  if (frame_capture_.overflowed
    || frame_capture_.used_query_slots + 2U > max_query_slots
    || frame_capture_.scopes.size() >= max_scopes_per_frame_) {
    if (!frame_capture_.overflowed) {
      frame_capture_.overflowed = true;
      AddDiagnostic("gpu.timestamp.overflow",
        fmt::format("scope budget exhausted at {} / {} query slots",
          frame_capture_.used_query_slots, max_query_slots));
    }
    state.flags = 0U;
    return;
  }

  const auto scope_id = static_cast<uint32_t>(frame_capture_.scopes.size());
  const auto begin_slot = frame_capture_.used_query_slots++;
  const auto end_slot = frame_capture_.used_query_slots++;
  const auto* display_name = InternName(info.base_label);
  const auto parent_scope_id
    = scope_stack_.empty() ? kInvalidScopeId : scope_stack_.back();

  frame_capture_.scopes.push_back(GpuScopeRecord {
    .scope_name_hash = HashName(info.base_label),
    .display_name = display_name,
    .parent_scope_id = parent_scope_id,
    .begin_query_slot = begin_slot,
    .end_query_slot = end_slot,
    .depth = static_cast<uint16_t>(scope_stack_.size()),
    .stream_id = 0U,
    .flags = 0U,
  });

  scope_stack_.push_back(scope_id);

  if (!provider->WriteTimestamp(recorder, begin_slot)) {
    AddDiagnostic("gpu.timestamp.write_failed",
      fmt::format("failed to write begin timestamp for '{}'",
        info.base_label.empty() ? "<unnamed>" : std::string(info.base_label)));
    frame_capture_.profiling_enabled = false;
    scope_stack_.pop_back();
    state.flags = 0U;
    return;
  }

  const TimelineCollectorScopeState collector_state {
    .scope_id = scope_id,
  };
  std::memcpy(state.storage.data(), &collector_state, sizeof(collector_state));
  state.flags = kCollectorStateFlagActive;
}

auto GpuTimelineProfiler::EndScope(graphics::CommandRecorder& recorder,
  graphics::GpuProfileCollectorState& state) -> void
{
  if ((state.flags & kCollectorStateFlagActive) == 0U) {
    return;
  }
  TimelineCollectorScopeState collector_state {};
  std::memcpy(&collector_state, state.storage.data(), sizeof(collector_state));
  if (collector_state.scope_id >= frame_capture_.scopes.size()) {
    return;
  }

  auto provider = ResolveTimestampProvider();
  if (provider == nullptr) {
    return;
  }

  auto& scope = frame_capture_.scopes[collector_state.scope_id];
  if (!provider->WriteTimestamp(recorder, scope.end_query_slot)) {
    AddDiagnostic("gpu.timestamp.write_failed",
      fmt::format("failed to write end timestamp for '{}'",
        scope.display_name != nullptr ? scope.display_name : "<unnamed>"));
    frame_capture_.profiling_enabled = false;
    return;
  }

  scope.flags |= kGpuScopeFlagComplete;

  if (!scope_stack_.empty()) {
    if (scope_stack_.back() == collector_state.scope_id) {
      scope_stack_.pop_back();
    } else {
      const auto it = std::ranges::find(scope_stack_, collector_state.scope_id);
      if (it != scope_stack_.end()) {
        scope_stack_.erase(it);
      }
    }
  }
  state.flags = 0U;
}

auto GpuTimelineProfiler::AddSink(std::shared_ptr<GpuTimelineSink> sink) -> void
{
  if (sink) {
    sinks_.push_back(std::move(sink));
  }
}

auto GpuTimelineProfiler::RequestOneShotExport(
  const std::filesystem::path& path) -> void
{
  if (path.empty()) {
    return;
  }
  AddSink(std::make_shared<FileExportSink>(path));
}

auto GpuTimelineProfiler::GetLastPublishedFrame() const
  -> std::optional<GpuTimelineFrame>
{
  std::scoped_lock lock(published_frame_mutex_);
  return last_published_frame_;
}

auto GpuTimelineProfiler::ConsumePreviousFrame() -> void
{
  if (!frame_capture_.profiling_enabled
    || frame_capture_.used_query_slots == 0U) {
    return;
  }

  auto queue = ResolveGraphicsQueue();
  auto provider = ResolveTimestampProvider();
  if (queue == nullptr || provider == nullptr) {
    return;
  }

  const auto completed = queue->GetCompletedValue();
  const auto fence_complete = completed >= frame_capture_.resolve_fence_value
    && frame_capture_.resolve_submitted;
  if (!fence_complete) {
    AddDiagnostic("gpu.timestamp.fence_miss",
      fmt::format("resolve fence {} not completed (completed={})",
        frame_capture_.resolve_fence_value, completed));
    return;
  }

  if (sinks_.empty() && !retain_latest_frame_) {
    return;
  }

  const auto ticks = provider->GetResolvedTicks();
  if (ticks.empty()) {
    return;
  }

  const auto frame = BuildTimelineFrame(ticks);
  if (!sinks_.empty()) {
    PublishFrame(frame);
  }

  {
    std::scoped_lock lock(published_frame_mutex_);
    last_published_frame_ = frame;
  }
}

auto GpuTimelineProfiler::ResetForFrame(
  const frame::SequenceNumber frame_sequence) -> void
{
  frame_capture_.frame_sequence = frame_sequence.get();
  frame_capture_.resolve_fence_value = 0U;
  frame_capture_.used_query_slots = 0U;
  frame_capture_.overflowed = false;
  frame_capture_.resolve_submitted = false;
  frame_capture_.diagnostics.clear();
  frame_capture_.scopes.clear();
  scope_stack_.clear();
  interned_names_.clear();
  timestamp_frequency_hz_ = 0U;

  auto queue = ResolveGraphicsQueue();
  auto provider = ResolveTimestampProvider();
  if (!enabled_ || queue == nullptr || provider == nullptr) {
    frame_capture_.profiling_enabled = false;
    return;
  }

  const auto max_query_slots = max_scopes_per_frame_ * 2U;
  if (!provider->EnsureCapacity(max_query_slots)) {
    AddDiagnostic("gpu.timestamp.backend_unavailable",
      "timestamp backend failed to provision query capacity");
    frame_capture_.profiling_enabled = false;
    return;
  }

  if (!queue->TryGetTimestampFrequency(timestamp_frequency_hz_)) {
    AddDiagnostic("gpu.timestamp.unsupported",
      "graphics queue does not expose a timestamp frequency");
    frame_capture_.profiling_enabled = false;
    return;
  }

  frame_capture_.profiling_enabled = true;
}

auto GpuTimelineProfiler::CloseIncompleteScopes() -> void
{
  if (scope_stack_.empty()) {
    return;
  }

  for (const auto scope_id : scope_stack_) {
    if (scope_id < frame_capture_.scopes.size()) {
      auto& scope = frame_capture_.scopes[scope_id];
      scope.flags &= static_cast<uint8_t>(~kGpuScopeFlagComplete);
      scope.flags &= static_cast<uint8_t>(~kGpuScopeFlagValid);
    }
  }

  AddDiagnostic("gpu.timestamp.incomplete_scope",
    fmt::format(
      "{} scope(s) remained open at frame tail", scope_stack_.size()));
  scope_stack_.clear();
}

auto GpuTimelineProfiler::BuildTimelineFrame(
  const std::span<const uint64_t>& ticks) const -> GpuTimelineFrame
{
  GpuTimelineFrame frame {};
  frame.frame_sequence = frame_capture_.frame_sequence;
  frame.timestamp_frequency_hz = timestamp_frequency_hz_;
  frame.profiling_enabled = frame_capture_.profiling_enabled;
  frame.overflowed = frame_capture_.overflowed;
  frame.used_query_slots = frame_capture_.used_query_slots;
  frame.diagnostics = frame_capture_.diagnostics;
  frame.scopes.reserve(frame_capture_.scopes.size());

  uint64_t frame_origin_tick = std::numeric_limits<uint64_t>::max();
  for (const auto& record : frame_capture_.scopes) {
    const bool complete = (record.flags & kGpuScopeFlagComplete) != 0U;
    if (!complete || record.begin_query_slot >= ticks.size()
      || record.end_query_slot >= ticks.size()) {
      continue;
    }
    frame_origin_tick
      = std::min(frame_origin_tick, ticks[record.begin_query_slot]);
  }
  if (frame_origin_tick == std::numeric_limits<uint64_t>::max()) {
    frame_origin_tick = 0U;
  }

  const auto ticks_to_ms = [this](const uint64_t tick_delta) -> float {
    if (timestamp_frequency_hz_ == 0U) {
      return 0.0F;
    }
    return static_cast<float>((static_cast<double>(tick_delta)
                                * kMillisecondsPerSecond)
      / static_cast<double>(timestamp_frequency_hz_));
  };

  for (uint32_t scope_id = 0U; scope_id < frame_capture_.scopes.size();
    ++scope_id) {
    const auto& record = frame_capture_.scopes[scope_id];
    const bool complete = (record.flags & kGpuScopeFlagComplete) != 0U;
    const uint64_t begin_tick = record.begin_query_slot < ticks.size()
      ? ticks[record.begin_query_slot]
      : 0U;
    const uint64_t end_tick = record.end_query_slot < ticks.size()
      ? ticks[record.end_query_slot]
      : 0U;
    const bool valid = complete && record.begin_query_slot < ticks.size()
      && record.end_query_slot < ticks.size() && end_tick >= begin_tick;

    GpuTimelineScope scope {};
    scope.scope_id = scope_id;
    scope.parent_scope_id = record.parent_scope_id;
    scope.name_hash = record.scope_name_hash;
    scope.display_name
      = record.display_name != nullptr ? record.display_name : "";
    scope.begin_query_slot = record.begin_query_slot;
    scope.end_query_slot = record.end_query_slot;
    scope.depth = record.depth;
    scope.stream_id = record.stream_id;
    scope.flags = record.flags;
    scope.valid = valid;

    if (valid) {
      const auto relative_begin_tick = begin_tick - frame_origin_tick;
      const auto relative_end_tick = end_tick - frame_origin_tick;
      scope.start_ms = ticks_to_ms(relative_begin_tick);
      scope.end_ms = ticks_to_ms(relative_end_tick);
      scope.duration_ms = ticks_to_ms(end_tick - begin_tick);
      scope.flags |= kGpuScopeFlagValid;
    }

    frame.scopes.push_back(std::move(scope));
  }

  for (auto& scope : frame.scopes) {
    if (scope.parent_scope_id != kInvalidScopeId
      && scope.parent_scope_id < frame.scopes.size()) {
      frame.scopes[scope.parent_scope_id].child_scope_ids.push_back(
        scope.scope_id);
    }
  }

  return frame;
}

auto GpuTimelineProfiler::PublishFrame(const GpuTimelineFrame& frame) -> void
{
  std::erase_if(sinks_,
    [&frame](const std::shared_ptr<GpuTimelineSink>& sink) -> bool {
      return sink == nullptr || !sink->ConsumeFrame(frame);
    });
}

auto GpuTimelineProfiler::InternName(const std::string_view name) -> const char*
{
  const auto key = std::string(name);
  const auto [it, inserted] = interned_names_.try_emplace(key, HashName(name));
  static_cast<void>(inserted);
  return it->first.c_str();
}

auto GpuTimelineProfiler::ResolveGraphicsQueue() const
  -> observer_ptr<graphics::CommandQueue>
{
  if (graphics_ == nullptr) {
    return {};
  }
  return graphics_->GetCommandQueue(graphics::QueueRole::kGraphics);
}

auto GpuTimelineProfiler::ResolveTimestampProvider() const
  -> observer_ptr<graphics::TimestampQueryProvider>
{
  if (graphics_ == nullptr) {
    return {};
  }
  return graphics_->GetTimestampQueryProvider();
}

auto GpuTimelineProfiler::AddDiagnostic(std::string code, std::string message)
  -> void
{
  frame_capture_.diagnostics.push_back(GpuTimelineDiagnostic {
    .code = std::move(code),
    .message = std::move(message),
    .frame_sequence = frame_capture_.frame_sequence,
    .used_query_slots = frame_capture_.used_query_slots,
    .max_query_slots = max_scopes_per_frame_ * 2U,
  });
}

} // namespace oxygen::vortex::internal
