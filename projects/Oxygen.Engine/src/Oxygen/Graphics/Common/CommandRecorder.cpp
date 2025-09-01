//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Graphics/Common/CommandList.h>
#include <Oxygen/Graphics/Common/CommandQueue.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/Detail/ResourceStateTracker.h>

using oxygen::graphics::Buffer;
using oxygen::graphics::CommandRecorder;
using oxygen::graphics::Texture;

CommandRecorder::CommandRecorder(std::shared_ptr<CommandList> command_list,
  observer_ptr<CommandQueue> target_queue)
  : command_list_(std::move(command_list))
  , target_queue_(target_queue)
  , resource_state_tracker_(std::make_unique<detail::ResourceStateTracker>())
{
  CHECK_NOTNULL_F(command_list_);
  CHECK_NOTNULL_F(target_queue_);
}

// Destructor implementation is crucial here because ResourceStateTracker is
// forward-declared in the header
CommandRecorder::~CommandRecorder() { DLOG_F(2, "recorder destroyed"); }

void CommandRecorder::Begin()
{
  DLOG_F(2, "CommandRecorder::Begin()");
  DCHECK_EQ_F(command_list_->GetState(), CommandList::State::kFree);
  command_list_->OnBeginRecording();
}

auto CommandRecorder::End() -> std::shared_ptr<CommandList>
{
  DLOG_F(2, "CommandRecorder::End()");
  DCHECK_NOTNULL_F(command_list_);
  try {
    // Give a chance to the resource state tracker to restore initial states
    // fore resources that requested to do so. Immediately flush barriers,
    // in case barriers were placed.
    resource_state_tracker_->OnCommandListClosed();
    FlushBarriers();

    command_list_->OnEndRecording();
    return std::move(command_list_);
  } catch (const std::exception& e) {
    LOG_F(ERROR, "Recording failed: {}", e.what());
    command_list_.reset();
    return {};
  }
}

void CommandRecorder::FlushBarriers()
{
  if (!resource_state_tracker_->HasPendingBarriers()) {
    return;
  }

  LOG_SCOPE_F(4, "Flushing barriers");

  // Execute the pending barriers by calling an abstract method to be
  // implemented by the backend-specific Commander
  ExecuteBarriers(resource_state_tracker_->GetPendingBarriers());

  // Clear the pending barriers
  resource_state_tracker_->ClearPendingBarriers();
}

void CommandRecorder::RecordQueueSignal(uint64_t value)
{
  // Default common implementation: if a target queue exists, forward the
  // signal request as a queue-side command. Backends may override this to
  // instead record a backend-specific command into their command stream.
  if (target_queue_) {
    target_queue_->QueueSignalCommand(value);
  } else {
    LOG_F(WARNING, "RecordQueueSignal: target queue is null");
  }
}

void CommandRecorder::RecordQueueWait(uint64_t value)
{
  // Default common implementation: forward to the target queue's GPU-side
  // wait command. Backends can override to enqueue a recorded command
  // instead, but the default is a direct forward for simplicity.
  if (target_queue_) {
    target_queue_->QueueWaitCommand(value);
  } else {
    LOG_F(WARNING, "RecordQueueWait: target queue is null");
  }
}

// -- Private non-template dispatch method implementations for Buffer

// ReSharper disable once CppMemberFunctionMayBeConst
void CommandRecorder::DoBeginTrackingResourceState(const Buffer& resource,
  const ResourceStates initial_state, const bool keep_initial_state)
{
  LOG_F(4, "buffer: begin tracking state 0x{} initial={} {}",
    nostd::to_string(resource.GetNativeResource()).c_str(),
    nostd::to_string(initial_state).c_str(),
    keep_initial_state ? " (preserve it)" : "");
  resource_state_tracker_->BeginTrackingResourceState(
    resource, initial_state, keep_initial_state);
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
void CommandRecorder::DoBeginTrackingResourceState(const Texture& resource,
  const ResourceStates initial_state, const bool keep_initial_state)
{
  LOG_F(4, "texture: begin tracking state 0x{} initial={} {}",
    nostd::to_string(resource.GetNativeResource()).c_str(),
    nostd::to_string(initial_state).c_str(),
    keep_initial_state ? " (preserve it)" : "");
  resource_state_tracker_->BeginTrackingResourceState(
    resource, initial_state, keep_initial_state);
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
