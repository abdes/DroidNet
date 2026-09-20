//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Graphics/Common/CommandRecording.h>

#include <exception>
#include <utility>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Graphics/Common/CommandList.h>
#include <Oxygen/Graphics/Common/CommandQueue.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/Detail/DeferredReclaimer.h>
#include <Oxygen/Profiling/CpuProfileScope.h>

namespace oxygen::graphics {

CommandRecording::CommandRecording() noexcept = default;

CommandRecording::CommandRecording(std::unique_ptr<CommandRecorder> recorder,
  const observer_ptr<detail::DeferredReclaimer> reclaimer,
  const SubmissionPolicy policy)
  : recorder_(std::move(recorder))
  , reclaimer_(reclaimer)
  , policy_(policy)
  , state_(State::kRecording)
  , uncaught_exceptions_(std::uncaught_exceptions())
{
  CHECK_NOTNULL_F(recorder_);
  CHECK_NOTNULL_F(reclaimer_);
  command_list_ = recorder_->command_list_;
  CHECK_NOTNULL_F(command_list_);
  recorder_->Begin();
}

CommandRecording::~CommandRecording() noexcept { FinishScope(); }

CommandRecording::CommandRecording(CommandRecording&& other) noexcept
  : recorder_(std::move(other.recorder_))
  , command_list_(std::move(other.command_list_))
  , reclaimer_(other.reclaimer_)
  , policy_(other.policy_)
  , state_(std::exchange(other.state_, State::kEmpty))
  , uncaught_exceptions_(other.uncaught_exceptions_)
  , ended_(other.ended_)
{
}

auto CommandRecording::operator=(CommandRecording&& other) noexcept
  -> CommandRecording&
{
  if (this != &other) {
    FinishScope();
    recorder_ = std::move(other.recorder_);
    command_list_ = std::move(other.command_list_);
    reclaimer_ = other.reclaimer_;
    policy_ = other.policy_;
    state_ = std::exchange(other.state_, State::kEmpty);
    uncaught_exceptions_ = other.uncaught_exceptions_;
    ended_ = other.ended_;
  }
  return *this;
}

CommandRecording::operator bool() const noexcept
{
  return state_ == State::kRecording;
}

auto CommandRecording::operator*() const -> CommandRecorder&
{
  CHECK_F(state_ == State::kRecording, "Recording is already resolved");
  return *recorder_;
}

auto CommandRecording::operator->() const -> CommandRecorder*
{
  return &operator*();
}

void CommandRecording::FinishScope() noexcept
{
  if (state_ != State::kRecording) {
    return;
  }
  if (policy_ == SubmissionPolicy::kOnScopeExit
    && std::uncaught_exceptions() <= uncaught_exceptions_) {
    static_cast<void>(Submit());
  } else {
    Discard();
  }
}

/*!
 Retirement storage is secured before queue submission so an allocation failure
 cannot release submitted native work prematurely. Publication callbacks run
 only after the queue accepts the command list. A failed or discarded recording
 never publishes successful submission.
*/
auto CommandRecording::Submit() noexcept -> bool
{
  if (state_ != State::kRecording) {
    return state_ == State::kSubmitted;
  }
  try {
    profiling::CpuProfileScope cpu_scope("Graphics.FinalizeCommandRecorder",
      profiling::ProfileCategory::kGeneral,
      profiling::Vars(profiling::Var("recording", command_list_->GetName())));
    ended_ = true;
    auto completed = recorder_->End();
    if (!completed) {
      Discard();
      return false;
    }
    reclaimer_->RegisterDeferredAction([list = completed]() -> void {
      if (list->IsSubmitted()) {
        list->OnExecuted();
      } else {
        list->OnFailed();
      }
    });
    recorder_->GetTargetQueue()->Submit(completed);
    completed->OnSubmitted();
    state_ = State::kSubmitted;
    recorder_->ResolveSubmission(SubmissionOutcome::kSubmitted);
    command_list_.reset();
    return true;
  } catch (const std::exception& error) {
    LOG_F(ERROR, "Command recording submission failed: {}", error.what());
  } catch (...) {
    LOG_F(ERROR, "Command recording submission failed with an unknown error");
  }
  Discard();
  return false;
}

void CommandRecording::Discard() noexcept
{
  if (state_ != State::kRecording) {
    return;
  }
  state_ = State::kDiscarded;
  // Close the native list before its pooled allocator/list can be reset.
  if (!ended_) {
    ended_ = true;
    static_cast<void>(recorder_->End());
  }
  command_list_->OnFailed();
  recorder_->ResolveSubmission(SubmissionOutcome::kDiscarded);
  command_list_.reset();
}

} // namespace oxygen::graphics
