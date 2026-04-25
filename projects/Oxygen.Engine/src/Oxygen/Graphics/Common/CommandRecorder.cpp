//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <exception>

#include <fmt/format.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Graphics/Common/CommandList.h>
#include <Oxygen/Graphics/Common/CommandQueue.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/Detail/ResourceStateTracker.h>

using oxygen::graphics::Buffer;
using oxygen::graphics::CommandRecorder;
using oxygen::graphics::Texture;

namespace {

constexpr uint8_t kCollectorStateFlagActive = 1U << 0U;

class NativeLabelCollector final
  : public oxygen::graphics::IGpuProfileCollector {
public:
  auto BeginScope(oxygen::graphics::CommandRecorder& recorder,
    const oxygen::graphics::GpuProfileScopeInfo& info,
    oxygen::graphics::GpuProfileCollectorState& state) -> void override
  {
    recorder.BeginEvent(info.formatted_name);
    state.flags = kCollectorStateFlagActive;
  }

  auto EndScope(oxygen::graphics::CommandRecorder& recorder,
    oxygen::graphics::GpuProfileCollectorState& state) -> void override
  {
    if ((state.flags & kCollectorStateFlagActive) == 0U) {
      return;
    }
    recorder.EndEvent();
    state.flags = 0U;
  }
};

} // namespace

auto CommandRecorder::ClearDepthStencilView(const Texture& texture,
  const NativeView& dsv, const ClearFlags clear_flags, const float depth,
  const uint8_t stencil, const std::span<const oxygen::Scissors> /*rects*/)
  -> void
{
  ClearDepthStencilView(texture, dsv, clear_flags, depth, stencil);
}

CommandRecorder::CommandRecorder(std::shared_ptr<CommandList> command_list,
  observer_ptr<CommandQueue> target_queue)
  : command_list_(std::move(command_list))
  , target_queue_(target_queue)
  , resource_state_tracker_(std::make_unique<detail::ResourceStateTracker>())
{
  CHECK_NOTNULL_F(command_list_);
  CHECK_NOTNULL_F(target_queue_);
  scope_records_.reserve(32U);
  scope_stack_.reserve(32U);
}

// Destructor implementation is crucial here because ResourceStateTracker is
// forward-declared in the header
CommandRecorder::~CommandRecorder()
{
  DrainActiveProfileScopes(ScopeCloseKind::kAbort);
  DLOG_F(2, "recorder destroyed");
}

void CommandRecorder::Begin()
{
  DCHECK_NOTNULL_F(command_list_);
  DLOG_F(2, "CommandRecorder::Begin() for: {}", command_list_->GetName());
  DCHECK_EQ_F(command_list_->GetState(), CommandList::State::kFree);
  DrainActiveProfileScopes(ScopeCloseKind::kAbort);
  scope_records_.clear();
  scope_stack_.clear();
  command_list_->OnBeginRecording();
}

auto CommandRecorder::End() noexcept -> std::shared_ptr<CommandList>
{
  DCHECK_NOTNULL_F(command_list_);
  DLOG_F(2, "CommandRecorder::End() for: {}", command_list_->GetName());
  DCHECK_NOTNULL_F(command_list_);
  DrainActiveProfileScopes(ScopeCloseKind::kAbort);
  try {
    // Give a chance to the resource state tracker to restore initial states
    // fore resources that requested to do so. Immediately flush barriers,
    // in case barriers were placed.
    resource_state_tracker_->OnCommandListClosed();
    FlushBarriers();
    auto recorded_states = std::vector<CommandList::RecordedResourceState> {};
    const auto tracker_states = resource_state_tracker_->SnapshotTrackedStates();
    recorded_states.reserve(tracker_states.size());
    for (const auto& tracker_state : tracker_states) {
      recorded_states.push_back(
        { .resource = tracker_state.resource, .state = tracker_state.state });
    }
    command_list_->SetRecordedResourceStates(std::move(recorded_states));

    command_list_->OnEndRecording();
    return std::move(command_list_);
  } catch (const std::exception& e) {
    LOG_F(ERROR, "Recording failed: {}", e.what());
    command_list_->OnFailed(); // noexcept
    command_list_.reset();
    return {};
  }
}

void CommandRecorder::FlushBarriers()
{
  if (!resource_state_tracker_->HasPendingBarriers()) {
    return;
  }

  DLOG_SCOPE_F(2,
    fmt::format("Flushing barriers for: `{}`", command_list_->GetName())
      .c_str());

  // Execute the pending barriers by calling an abstract method to be
  // implemented by the backend-specific Commander
  ExecuteBarriers(resource_state_tracker_->GetPendingBarriers());

  // Clear the pending barriers
  resource_state_tracker_->ClearPendingBarriers();
}

void CommandRecorder::RecordQueueSignal(uint64_t value)
{
  if (command_list_ != nullptr) {
    command_list_->QueueSubmitSignal(value);
    return;
  }
  LOG_F(WARNING, "RecordQueueSignal: command list is null");
}

void CommandRecorder::RecordQueueWait(uint64_t value)
{
  if (command_list_ != nullptr) {
    command_list_->QueueSubmitWait(value);
    return;
  }
  LOG_F(WARNING, "RecordQueueWait: command list is null");
}

/*!
 Begins a GPU debug event scope.

 The common implementation is a no-op so that backends which do not support
 GPU debug markers (or builds that do not enable them) can safely ignore these
 calls.

 @param name The event name to display in GPU debuggers.
*/
auto CommandRecorder::BeginEvent(std::string_view /*name*/) -> void { }

/*!
 Ends the most recently begun GPU debug event scope.

 The common implementation is a no-op.
*/
auto CommandRecorder::EndEvent() -> void { }

/*!
 Emits an instantaneous GPU debug marker.

 The common implementation is a no-op.

 @param name The marker name to display in GPU debuggers.
*/
auto CommandRecorder::SetMarker(std::string_view /*name*/) -> void { }

auto CommandRecorder::BeginProfileScope(
  const oxygen::profiling::GpuProfileScopeDesc& desc,
  const std::source_location callsite) -> GpuProfileScopeToken
{
  if (desc.label.empty()) {
    return {};
  }

  ScopeRecord record {};
  scope_records_.reserve(scope_records_.size() + 1U);
  scope_stack_.reserve(scope_stack_.size() + 1U);
  record.base_label = desc.label;
  record.formatted_name = oxygen::profiling::FormatScopeName(desc);
  if (telemetry_collector_ != nullptr
    && desc.granularity == oxygen::profiling::ProfileGranularity::kTelemetry) {
    record.telemetry_collector = telemetry_collector_;
  }
  if (trace_collector_ != nullptr) {
    record.trace_collector = trace_collector_;
  }
  record.active = true;

  const GpuProfileScopeInfo info {
    .desc = desc,
    .base_label = record.base_label,
    .formatted_name = record.formatted_name,
    .callsite = callsite,
  };

  try {
    NativeLabelCollector native_labels {};
    native_labels.BeginScope(*this, info, record.native_label_state);

    if (record.telemetry_collector != nullptr) {
      record.telemetry_collector->BeginScope(*this, info, record.telemetry_state);
    }

    if (record.trace_collector != nullptr) {
      record.trace_collector->BeginScope(*this, info, record.trace_state);
    }

    const auto scope_id = static_cast<uint32_t>(scope_records_.size());
    scope_records_.push_back(std::move(record));
    scope_stack_.push_back(scope_id);

    return GpuProfileScopeToken {
      .scope_id = scope_id,
      .stream_id = 0U,
      .flags = kGpuScopeTokenFlagActive,
    };
  } catch (...) {
    try {
      CloseScopeRecord(record, ScopeCloseKind::kAbort);
    } catch (const std::exception& e) {
      LOG_F(ERROR, "Failed to abort partially opened GPU profile scope '{}': {}",
        record.base_label, e.what());
    } catch (...) {
      LOG_F(ERROR,
        "Failed to abort partially opened GPU profile scope '{}': unknown "
        "exception",
        record.base_label);
    }
    throw;
  }
}

auto CommandRecorder::EndProfileScope(const GpuProfileScopeToken& token) -> void
{
  if ((token.flags & kGpuScopeTokenFlagActive) == 0U) {
    return;
  }
  if (token.scope_id >= scope_records_.size()) {
    return;
  }

  auto& record = scope_records_[token.scope_id];
  if (!record.active) {
    return;
  }

  CloseScopeRecord(record, ScopeCloseKind::kEnd);

  if (!scope_stack_.empty()) {
    if (scope_stack_.back() == token.scope_id) {
      scope_stack_.pop_back();
    } else if (const auto it
      = std::find(scope_stack_.begin(), scope_stack_.end(), token.scope_id);
      it != scope_stack_.end()) {
      scope_stack_.erase(it);
    }
  }

  if (scope_stack_.empty()) {
    scope_records_.clear();
  }
}

auto CommandRecorder::CloseScopeRecord(
  ScopeRecord& record, const ScopeCloseKind close_kind) -> void
{
  if (!record.active) {
    return;
  }

  std::exception_ptr first_failure {};
  const auto close_collector = [&](const observer_ptr<IGpuProfileCollector> collector,
                                 GpuProfileCollectorState& state) {
    if (collector == nullptr) {
      return;
    }
    try {
      if (close_kind == ScopeCloseKind::kAbort) {
        collector->AbortScope(*this, state);
      } else {
        collector->EndScope(*this, state);
      }
    } catch (...) {
      if (first_failure == nullptr) {
        first_failure = std::current_exception();
      }
    }
  };

  close_collector(record.trace_collector, record.trace_state);
  close_collector(record.telemetry_collector, record.telemetry_state);

  NativeLabelCollector native_labels {};
  try {
    if (close_kind == ScopeCloseKind::kAbort) {
      native_labels.AbortScope(*this, record.native_label_state);
    } else {
      native_labels.EndScope(*this, record.native_label_state);
    }
  } catch (...) {
    if (first_failure == nullptr) {
      first_failure = std::current_exception();
    }
  }

  record.active = false;

  if (first_failure != nullptr) {
    std::rethrow_exception(first_failure);
  }
}

auto CommandRecorder::DrainActiveProfileScopes(
  const ScopeCloseKind close_kind) noexcept -> void
{
  for (auto it = scope_stack_.rbegin(); it != scope_stack_.rend(); ++it) {
    if (*it >= scope_records_.size()) {
      continue;
    }

    auto& record = scope_records_[*it];
    try {
      CloseScopeRecord(record, close_kind);
    } catch (const std::exception& e) {
      LOG_F(ERROR, "Failed to drain GPU profile scope '{}': {}", record.base_label,
        e.what());
    } catch (...) {
      LOG_F(ERROR, "Failed to drain GPU profile scope '{}': unknown exception",
        record.base_label);
    }
  }

  scope_stack_.clear();
  scope_records_.clear();
}

// -- Private non-template dispatch method implementations for Buffer

// ReSharper disable once CppMemberFunctionMayBeConst
bool CommandRecorder::DoIsResourceTracked(const Buffer& resource) const
{
  return resource_state_tracker_->IsResourceTracked(resource);
}

// ReSharper disable once CppMemberFunctionMayBeConst
void CommandRecorder::DoBeginTrackingResourceState(const Buffer& resource,
  const ResourceStates initial_state, const bool keep_initial_state)
{
  DLOG_F(4, "buffer: begin tracking state `{}` initial={}{}",
    resource.GetName(), initial_state,
    keep_initial_state ? " (preserve it)" : "");
  resource_state_tracker_->BeginTrackingResourceState(
    resource, initial_state, keep_initial_state);
}

void CommandRecorder::DoAdoptKnownResourceState(const Buffer& resource,
  const ResourceStates current_state, const bool keep_initial_state)
{
  DLOG_F(4, "buffer: adopt known state `{}` current={}{}",
    resource.GetName(), current_state,
    keep_initial_state ? " (preserve it)" : "");
  resource_state_tracker_->AdoptResourceState(
    resource, current_state, keep_initial_state);
}

// ReSharper disable once CppMemberFunctionMayBeConst
void CommandRecorder::DoEnableAutoMemoryBarriers(const Buffer& resource)
{
  resource_state_tracker_->EnableAutoMemoryBarriers(resource);
}

// ReSharper disable once CppMemberFunctionMayBeConst
void CommandRecorder::DoDisableAutoMemoryBarriers(const Buffer& resource)
{
  resource_state_tracker_->DisableAutoMemoryBarriers(resource);
}

// ReSharper disable once CppMemberFunctionMayBeConst
void CommandRecorder::DoRequireResourceState(
  const Buffer& resource, const ResourceStates state)
{
  resource_state_tracker_->RequireResourceState(resource, state);
}

// ReSharper disable once CppMemberFunctionMayBeConst
void CommandRecorder::DoRequireResourceStateFinal(
  const Buffer& resource, const ResourceStates state)
{
  resource_state_tracker_->RequireResourceStateFinal(resource, state);
}

// -- Private non-template dispatch method implementations for Texture

// ReSharper disable once CppMemberFunctionMayBeConst
bool CommandRecorder::DoIsResourceTracked(const Texture& resource) const
{
  return resource_state_tracker_->IsResourceTracked(resource);
}

// ReSharper disable once CppMemberFunctionMayBeConst
void CommandRecorder::DoBeginTrackingResourceState(const Texture& resource,
  const ResourceStates initial_state, const bool keep_initial_state)
{
  DLOG_F(4, "texture: begin tracking state `{}` initial={}{}",
    resource.GetName(), initial_state,
    keep_initial_state ? " (preserve it)" : "");
  resource_state_tracker_->BeginTrackingResourceState(
    resource, initial_state, keep_initial_state);
}

void CommandRecorder::DoAdoptKnownResourceState(const Texture& resource,
  const ResourceStates current_state, const bool keep_initial_state)
{
  DLOG_F(4, "texture: adopt known state `{}` current={}{}",
    resource.GetName(), current_state,
    keep_initial_state ? " (preserve it)" : "");
  resource_state_tracker_->AdoptResourceState(
    resource, current_state, keep_initial_state);
}

// ReSharper disable once CppMemberFunctionMayBeConst
void CommandRecorder::DoEnableAutoMemoryBarriers(const Texture& resource)
{
  resource_state_tracker_->EnableAutoMemoryBarriers(resource);
}

// ReSharper disable once CppMemberFunctionMayBeConst
void CommandRecorder::DoDisableAutoMemoryBarriers(const Texture& resource)
{
  resource_state_tracker_->DisableAutoMemoryBarriers(resource);
}

// ReSharper disable once CppMemberFunctionMayBeConst
void CommandRecorder::DoRequireResourceState(
  const Texture& resource, const ResourceStates state)
{
  resource_state_tracker_->RequireResourceState(resource, state);
}

// ReSharper disable once CppMemberFunctionMayBeConst
void CommandRecorder::DoRequireResourceStateFinal(
  const Texture& resource, const ResourceStates state)
{
  resource_state_tracker_->RequireResourceStateFinal(resource, state);
}
