//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Graphics/Common/CommandList.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/Detail/ResourceStateTracker.h>

using oxygen::graphics::Buffer;
using oxygen::graphics::CommandRecorder;
using oxygen::graphics::Texture;

CommandRecorder::CommandRecorder(CommandList* command_list, CommandQueue* target_queue, SubmissionMode mode)
    : submission_mode_(mode)
    , command_list_(command_list)
    , target_queue_(target_queue)
    , resource_state_tracker_(std::make_unique<detail::ResourceStateTracker>())
{
    CHECK_NOTNULL_F(command_list_);
    CHECK_NOTNULL_F(target_queue_);
}

// Destructor implementation is crucial here because ResourceStateTracker is
// forward-declared in the header
CommandRecorder::~CommandRecorder() = default;

void CommandRecorder::Begin()
{
    DCHECK_EQ_F(command_list_->GetState(), CommandList::State::kFree);
    command_list_->OnBeginRecording();
}

auto CommandRecorder::End() -> CommandList*
{
    try {
        // Give a chance to the resource state tracker to restore initial states
        // fore resources that requested to do so. Immediately flush barriers,
        // in case barriers were placed.
        resource_state_tracker_->OnCommandListClosed();
        FlushBarriers();

        command_list_->OnEndRecording();
        return command_list_;
    } catch (const std::exception& e) {
        LOG_F(ERROR, "Recording failed: {}", e.what());
        return {};
    }
}

void CommandRecorder::OnSubmitted()
{
    DCHECK_NOTNULL_F(command_list_);
    command_list_->OnSubmitted();
    resource_state_tracker_->OnCommandListSubmitted();
}

void CommandRecorder::FlushBarriers()
{
    if (!resource_state_tracker_->HasPendingBarriers()) {
        return;
    }

    // Execute the pending barriers by calling an abstract method to be
    // implemented by the backend-specific Commander
    ExecuteBarriers(resource_state_tracker_->GetPendingBarriers());

    // Clear the pending barriers
    resource_state_tracker_->ClearPendingBarriers();
}

// -- Private non-template dispatch method implementations for Buffer

// ReSharper disable once CppMemberFunctionMayBeConst
void CommandRecorder::DoBeginTrackingResourceState(
    const Buffer& resource,
    const ResourceStates initial_state,
    const bool keep_initial_state)
{
    resource_state_tracker_->BeginTrackingResourceState(resource, initial_state, keep_initial_state);
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
void CommandRecorder::DoRequireResourceState(const Buffer& resource, const ResourceStates state)
{
    resource_state_tracker_->RequireResourceState(resource, state);
}

// ReSharper disable once CppMemberFunctionMayBeConst
void CommandRecorder::DoRequireResourceStateFinal(const Buffer& resource, const ResourceStates state)
{
    resource_state_tracker_->RequireResourceStateFinal(resource, state);
}

// -- Private non-template dispatch method implementations for Texture

// ReSharper disable once CppMemberFunctionMayBeConst
void CommandRecorder::DoBeginTrackingResourceState(
    const Texture& resource,
    const ResourceStates initial_state,
    const bool keep_initial_state)
{
    resource_state_tracker_->BeginTrackingResourceState(resource, initial_state, keep_initial_state);
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
void CommandRecorder::DoRequireResourceState(const Texture& resource, const ResourceStates state)
{
    resource_state_tracker_->RequireResourceState(resource, state);
}

// ReSharper disable once CppMemberFunctionMayBeConst
void CommandRecorder::DoRequireResourceStateFinal(const Texture& resource, const ResourceStates state)
{
    resource_state_tracker_->RequireResourceStateFinal(resource, state);
}
