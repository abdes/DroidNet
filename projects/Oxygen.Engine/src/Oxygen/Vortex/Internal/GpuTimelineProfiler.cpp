//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstring>
#include <fstream>
#include <limits>
#include <thread>

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

auto WriteJsonFrame(std::ostream& out,
  const oxygen::vortex::internal::GpuTimelineFrame& frame) -> void
{
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
    out << fmt::format("      \"end_query_slot\": {},\n", scope.end_query_slot);
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
    WriteJsonFrame(out, frame);
  }

  std::filesystem::path output_path_;
  bool completed_ { false };
};

class RecordingExportSink final
  : public oxygen::vortex::internal::GpuTimelineSink {
public:
  RecordingExportSink(const std::filesystem::path& path,
    const uint64_t first_frame, const uint32_t frame_count)
    : first_frame_(first_frame)
    , seen_(frame_count, false)
  {
    if (!path.parent_path().empty()) {
      std::filesystem::create_directories(path.parent_path());
    }
    output_.exceptions(std::ios::badbit | std::ios::failbit);
    output_.open(path, std::ios::binary | std::ios::trunc);
    output_ << fmt::format(
      "{{\n\"version\": 2,\n\"first_frame_seq\": {},\n"
      "\"requested_frames\": {},\n\"serialization\": \"worker\",\n"
      "\"queue_capacity_frames\": {},\n\"frames\": [\n",
      first_frame, frame_count, kMaximumQueuedFrames);
    worker_ = std::jthread([this]() { WriteFrames(); });
  }

  ~RecordingExportSink() override
  {
    {
      std::scoped_lock lock(queue_mutex_);
      producer_done_ = true;
    }
    queue_ready_.notify_one();
    worker_.join();
  }

  OXYGEN_MAKE_NON_COPYABLE(RecordingExportSink)
  OXYGEN_MAKE_NON_MOVABLE(RecordingExportSink)

  auto ConsumeFrame(const oxygen::vortex::internal::GpuTimelineFrame& frame)
    -> bool override
  {
    if (write_failed_.load(std::memory_order_acquire)) {
      return false;
    }
    if (frame.frame_sequence < first_frame_
      || frame.frame_sequence - first_frame_ >= seen_.size()) {
      return true;
    }
    const auto index
      = static_cast<std::size_t>(frame.frame_sequence - first_frame_);
    if (seen_[index]) {
      ++duplicate_frames_;
      return true;
    }
    try {
      std::scoped_lock lock(queue_mutex_);
      if (pending_.size() == kMaximumQueuedFrames) {
        cancelled_frame_ = frame.frame_sequence;
        cancel_reason_ = "export_queue_full";
        return false;
      }
      pending_.push_back(frame);
      seen_[index] = true;
      ++received_frames_;
    } catch (const std::exception& ex) {
      cancelled_frame_ = frame.frame_sequence;
      cancel_reason_ = "export_queue_allocation_failed";
      LOG_F(ERROR, "GPU timeline recording enqueue failed: {}", ex.what());
      return false;
    }
    queue_ready_.notify_one();
    return received_frames_ != seen_.size();
  }

private:
  auto WriteFrames() noexcept -> void
  {
    try {
      for (;;) {
        auto frame = oxygen::vortex::internal::GpuTimelineFrame {};
        {
          auto lock = std::unique_lock(queue_mutex_);
          queue_ready_.wait(
            lock, [this]() { return producer_done_ || !pending_.empty(); });
          if (pending_.empty()) {
            break;
          }
          frame = std::move(pending_.front());
          pending_.pop_front();
        }
        if (written_frames_ != 0U) {
          output_ << ",\n";
        }
        WriteJsonFrame(output_, frame);
        ++written_frames_;
        timing_valid_ = timing_valid_ && frame.profiling_enabled
          && !frame.overflowed && frame.timestamp_frequency_hz != 0U
          && !frame.scopes.empty() && frame.diagnostics.empty()
          && std::ranges::all_of(
            frame.scopes, [](const auto& scope) { return scope.valid; });
      }
      const auto complete = written_frames_ == seen_.size();
      output_ << fmt::format(
        "\n],\n\"written_frames\": {},\n\"duplicate_frames\": {},\n"
        "\"complete\": {},\n\"timing_valid\": {},\n"
        "\"cancel_reason\": \"{}\",\n\"cancelled_frame_seq\": {}\n}}\n",
        written_frames_, duplicate_frames_, complete,
        complete && timing_valid_ && duplicate_frames_ == 0U, cancel_reason_,
        cancelled_frame_.value_or(0U));
      output_.close();
    } catch (const std::exception& ex) {
      write_failed_.store(true, std::memory_order_release);
      LOG_F(ERROR, "GPU timeline recording write failed: {}", ex.what());
    }
  }

  static constexpr auto kMaximumQueuedFrames = std::size_t { 8U };
  std::ofstream output_;
  uint64_t first_frame_ { 0U };
  std::vector<bool> seen_;
  uint32_t received_frames_ { 0U };
  uint32_t written_frames_ { 0U };
  uint64_t duplicate_frames_ { 0U };
  bool timing_valid_ { true };
  std::optional<uint64_t> cancelled_frame_;
  std::string_view cancel_reason_;
  std::mutex queue_mutex_;
  std::condition_variable queue_ready_;
  std::deque<oxygen::vortex::internal::GpuTimelineFrame> pending_;
  bool producer_done_ { false };
  std::atomic<bool> write_failed_ { false };
  std::jthread worker_;
};

} // namespace

namespace oxygen::vortex::internal {

GpuTimelineProfiler::GpuTimelineProfiler(
  const observer_ptr<Graphics> graphics, const bool record_frame_span)
  : graphics_(graphics)
{
  frame_capture_.scopes.reserve(max_scopes_per_frame_);
  scope_stack_.reserve(max_scopes_per_frame_);
  if (record_frame_span) {
    frame_scope_state_ = std::make_unique<graphics::GpuProfileCollectorState>();
  }
}

GpuTimelineProfiler::~GpuTimelineProfiler() = default;

auto GpuTimelineProfiler::SetEnabled(const bool enabled) -> void
{
  enabled_ = enabled;
}

auto GpuTimelineProfiler::SetMaxScopesPerFrame(const uint32_t max_scopes)
  -> void
{
  constexpr auto max_safe_scopes
    = std::numeric_limits<uint32_t>::max() / (2U * kCaptureSlots);
  max_scopes_per_frame_ = std::clamp(max_scopes, 1U, max_safe_scopes);
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
  if (frame_capture_.resolve_submitted) {
    pending_frames_.push_back(std::move(frame_capture_));
    frame_capture_ = {};
  } else if (!frame_capture_.diagnostics.empty()
    || !recording_sink_.expired()) {
    if (frame_capture_.diagnostics.empty()) {
      AddDiagnostic("gpu.timestamp.unavailable",
        frame_capture_.profiling_enabled
          ? "frame has no resolved telemetry scopes"
          : "timing collection was disabled or unavailable for this frame");
    }
    // Missing samples are explicit, including a full capture ring. Never
    // silently turn a delayed or failed frame into a faster distribution.
    PublishFrame(BuildTimelineFrame(frame_capture_, {}));
  }
  ConsumePreviousFrame();
  if (!reusable_captures_.empty()) {
    frame_capture_ = std::move(reusable_captures_.back());
    reusable_captures_.pop_back();
  }
  ResetForFrame(frame_sequence);
  if (frame_scope_state_ && frame_capture_.profiling_enabled) {
    *frame_scope_state_ = {};
    auto recorder = graphics_->AcquireCommandRecorder(
      graphics_->QueueKeyFor(graphics::QueueRole::kGraphics),
      "GpuTimestamp.FrameBegin");
    if (!recorder) {
      AddDiagnostic("gpu.timestamp.frame_begin_failed",
        "failed to acquire the graphics-frame timestamp recorder");
      frame_capture_.profiling_enabled = false;
      return;
    }
    const auto desc = profiling::GpuProfileScopeDesc {
      .label = "Vortex.Frame",
      .granularity = profiling::ProfileGranularity::kTelemetry,
      .category = profiling::ProfileCategory::kPass,
    };
    BeginScope(*recorder,
      { .desc = desc, .base_label = desc.label, .formatted_name = desc.label },
      *frame_scope_state_);
  }
}

auto GpuTimelineProfiler::OnFrameRecordTailResolve() -> void
{
  if (frame_capture_.resolve_submitted) {
    return;
  }

  if (frame_capture_.used_query_slots == 0U) {
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

    if (frame_scope_state_) {
      EndScope(*recorder, *frame_scope_state_);
    }
    CloseIncompleteScopes();
    if (frame_capture_.profiling_enabled
      && !provider->RecordResolve(*recorder, frame_capture_.used_query_slots,
        frame_capture_.query_offset)) {
      AddDiagnostic("gpu.timestamp.resolve_failed",
        "backend failed to record timestamp resolve");
      frame_capture_.profiling_enabled = false;
      // Keep the timestamp allocation alive through the submitted fence even
      // when no valid timing can be produced from this resolve.
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
  const auto begin_slot
    = frame_capture_.query_offset + frame_capture_.used_query_slots++;
  const auto end_slot
    = frame_capture_.query_offset + frame_capture_.used_query_slots++;
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

auto GpuTimelineProfiler::RequestRecording(
  const std::filesystem::path& path, const uint32_t frame_count) -> bool
{
  constexpr auto max_recording_frames = 1'000'000U;
  if (path.empty() || frame_count == 0U || frame_count > max_recording_frames
    || !frame_capture_.profiling_enabled || !recording_sink_.expired()
    || frame_capture_.frame_sequence
      > std::numeric_limits<uint64_t>::max() - (frame_count - 1U)) {
    return false;
  }
  try {
    auto sink = std::make_shared<RecordingExportSink>(
      path, frame_capture_.frame_sequence, frame_count);
    recording_sink_ = sink;
    AddSink(std::move(sink));
    return true;
  } catch (const std::exception& ex) {
    LOG_F(ERROR, "GPU timeline recording failed for '{}': {}", path.string(),
      ex.what());
    return false;
  }
}

auto GpuTimelineProfiler::GetLastPublishedFrame() const
  -> std::optional<GpuTimelineFrame>
{
  std::scoped_lock lock(published_frame_mutex_);
  return last_published_frame_;
}

auto GpuTimelineProfiler::ConsumePreviousFrame() -> void
{
  auto queue = ResolveGraphicsQueue();
  auto provider = ResolveTimestampProvider();
  if (queue == nullptr || provider == nullptr)
    return;

  const auto completed = queue->GetCompletedValue();
  while (!pending_frames_.empty()
    && pending_frames_.front().resolve_fence_value <= completed) {
    auto& capture = pending_frames_.front();
    if (!sinks_.empty() || retain_latest_frame_) {
      const auto frame
        = BuildTimelineFrame(capture, provider->GetResolvedTicks());
      PublishFrame(frame);
      std::scoped_lock lock(published_frame_mutex_);
      last_published_frame_ = frame;
    }
    capture.scopes.clear();
    capture.diagnostics.clear();
    reusable_captures_.push_back(std::move(capture));
    pending_frames_.pop_front();
  }
}

auto GpuTimelineProfiler::ResetForFrame(
  const frame::SequenceNumber frame_sequence) -> void
{
  frame_capture_.frame_sequence = frame_sequence.get();
  frame_capture_.resolve_fence_value = 0U;
  frame_capture_.timestamp_frequency_hz = 0U;
  frame_capture_.used_query_slots = 0U;
  frame_capture_.overflowed = false;
  frame_capture_.resolve_submitted = false;
  frame_capture_.profiling_enabled = false;
  frame_capture_.diagnostics.clear();
  frame_capture_.scopes.clear();
  scope_stack_.clear();
  if (pending_frames_.empty())
    interned_names_.clear();

  auto queue = ResolveGraphicsQueue();
  auto provider = ResolveTimestampProvider();
  if (!enabled_ || queue == nullptr || provider == nullptr)
    return;

  const auto requested_stride = max_scopes_per_frame_ * 2U;
  if (requested_stride > query_stride_) {
    // Growing the backend replaces its query heap/readback allocation. Wait
    // for ordinary completion before reconfiguration, without a CPU/GPU stall.
    if (!pending_frames_.empty()) {
      AddDiagnostic("gpu.timestamp.reconfigure_pending",
        "query capacity change deferred until pending captures complete");
      return;
    }
    if (!provider->EnsureCapacity(requested_stride * kCaptureSlots)) {
      AddDiagnostic("gpu.timestamp.backend_unavailable",
        "timestamp backend failed to provision query capacity");
      return;
    }
    query_stride_ = requested_stride;
  }

  auto slot = uint32_t { 0U };
  for (; slot < kCaptureSlots; ++slot) {
    const auto offset = slot * query_stride_;
    if (std::ranges::none_of(pending_frames_, [offset](const auto& capture) {
          return capture.query_offset == offset;
        }))
      break;
  }
  if (slot == kCaptureSlots) {
    AddDiagnostic("gpu.timestamp.capture_backlog",
      "all timestamp ranges remain in flight; frame timing unavailable");
    return;
  }
  frame_capture_.query_offset = slot * query_stride_;

  if (!queue->TryGetTimestampFrequency(frame_capture_.timestamp_frequency_hz)
    || frame_capture_.timestamp_frequency_hz == 0U) {
    AddDiagnostic("gpu.timestamp.unsupported",
      "graphics queue does not expose a timestamp frequency");
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

auto GpuTimelineProfiler::BuildTimelineFrame(const GpuFrameCapture& capture,
  const std::span<const uint64_t>& ticks) const -> GpuTimelineFrame
{
  GpuTimelineFrame frame {};
  frame.frame_sequence = capture.frame_sequence;
  frame.timestamp_frequency_hz = capture.timestamp_frequency_hz;
  frame.profiling_enabled = capture.profiling_enabled;
  frame.overflowed = capture.overflowed;
  frame.used_query_slots = capture.used_query_slots;
  frame.diagnostics = capture.diagnostics;
  frame.scopes.reserve(capture.scopes.size());

  uint64_t frame_origin_tick = std::numeric_limits<uint64_t>::max();
  for (const auto& record : capture.scopes) {
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

  const auto ticks_to_ms = [&capture](const uint64_t tick_delta) -> float {
    if (capture.timestamp_frequency_hz == 0U) {
      return 0.0F;
    }
    return static_cast<float>(
      (static_cast<double>(tick_delta) * kMillisecondsPerSecond)
      / static_cast<double>(capture.timestamp_frequency_hz));
  };

  for (uint32_t scope_id = 0U; scope_id < capture.scopes.size(); ++scope_id) {
    const auto& record = capture.scopes[scope_id];
    const bool complete = (record.flags & kGpuScopeFlagComplete) != 0U;
    const uint64_t begin_tick = record.begin_query_slot < ticks.size()
      ? ticks[record.begin_query_slot]
      : 0U;
    const uint64_t end_tick = record.end_query_slot < ticks.size()
      ? ticks[record.end_query_slot]
      : 0U;
    const bool valid = capture.profiling_enabled
      && capture.timestamp_frequency_hz != 0U && complete
      && record.begin_query_slot < ticks.size()
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
  std::erase_if(
    sinks_, [&frame](const std::shared_ptr<GpuTimelineSink>& sink) -> bool {
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
