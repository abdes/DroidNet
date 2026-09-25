//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <exception>
#include <utility>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Graphics/Common/BackendLifetime.h>
#include <Oxygen/Graphics/Common/CommandList.h>
#include <Oxygen/Graphics/Common/CommandQueue.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/CommandRecording.h>
#include <Oxygen/Graphics/Common/Detail/DeferredReclaimer.h>
#include <Oxygen/Profiling/CpuProfileScope.h>

namespace oxygen::graphics {

CommandRecording::CommandRecording() noexcept = default;

CommandRecording::CommandRecording(std::unique_ptr<CommandRecorder> recorder,
  const observer_ptr<detail::DeferredReclaimer> reclaimer,
  const SubmissionPolicy policy, std::shared_ptr<Graphics> backend_owner,
  std::shared_ptr<BackendLifetime> lifetime)
  : backend_owner_(std::move(backend_owner))
  , lifetime_(std::move(lifetime))
  , recorder_(std::move(recorder))
  , reclaimer_(reclaimer)
  , policy_(policy)
  , state_(State::kRecording)
  , uncaught_exceptions_(std::uncaught_exceptions())
{
  CHECK_NOTNULL_F(recorder_);
  CHECK_NOTNULL_F(reclaimer_);
  command_list_ = recorder_->command_list_;
  CHECK_NOTNULL_F(command_list_);
  try {
    command_list_->BindBackend(
      lifetime_, recorder_->GetTargetQueue()->Identity());
    recorder_->Begin();
    if (lifetime_) {
      lifetime_->RetainRecording();
    }
  } catch (...) {
    command_list_->Invalidate();
    recorder_->ResolveSubmission(SubmissionOutcome::kDiscarded);
    throw;
  }
}

CommandRecording::~CommandRecording() noexcept { FinishScope(); }

CommandRecording::CommandRecording(CommandRecording&& other) noexcept
  : backend_owner_(std::move(other.backend_owner_))
  , lifetime_(std::move(other.lifetime_))
  , recorder_(std::move(other.recorder_))
  , command_list_(std::move(other.command_list_))
  , reclaimer_(other.reclaimer_)
  , policy_(other.policy_)
  , state_(std::exchange(other.state_, State::kEmpty))
  , uncaught_exceptions_(other.uncaught_exceptions_)
  , ended_(other.ended_)
  , result_(other.result_)
{
}

auto CommandRecording::operator=(CommandRecording&& other) noexcept
  -> CommandRecording&
{
  if (this != &other) {
    FinishScope();
    // Destroy old recorder/list before releasing their backend owner.
    recorder_.reset();
    command_list_.reset();
    backend_owner_ = std::move(other.backend_owner_);
    lifetime_ = std::move(other.lifetime_);
    recorder_ = std::move(other.recorder_);
    command_list_ = std::move(other.command_list_);
    reclaimer_ = other.reclaimer_;
    policy_ = other.policy_;
    state_ = std::exchange(other.state_, State::kEmpty);
    uncaught_exceptions_ = other.uncaught_exceptions_;
    ended_ = other.ended_;
    result_ = other.result_;
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
  return Finalize(false).outcome == SubmissionOutcome::kSubmitted;
}

auto CommandRecording::SubmitWithReceipt() noexcept -> SubmissionResult
{
  return Finalize(true);
}

auto CommandRecording::Finalize(bool receipt) noexcept -> SubmissionResult
{
  if (state_ != State::kRecording) {
    return result_;
  }
  try {
    // Preserve view-owner attribution without allocating the static label on
    // every submit. Observer/Tracy consumers read the description at entry.
    thread_local profiling::CpuProfileScopeDesc finalize_profile {
      .label = "Graphics.FinalizeCommandRecorder",
      .variables = profiling::Vars(profiling::Var("recording", std::string_view {})),
      .category = profiling::ProfileCategory::kGeneral};
    finalize_profile.variables[0].value.assign(command_list_->GetName());
    const profiling::CpuProfileScope cpu_scope(finalize_profile);

    std::shared_ptr<CommandList> completed;
    detail::DeferredReclaimer::PreparedDeferredAction retirement;
    {
      const auto admission
        = lifetime_ ? lifetime_->AcquireOperation() : BackendOperation {};
      ended_ = true;
      completed = recorder_->End();
      if (!completed) {
        command_list_->Invalidate();
        throw std::runtime_error("Command recording could not be closed");
      }
      receipt = receipt || completed->Uses().NeedsCompletion();
      if (!receipt) {
        retirement = reclaimer_->PrepareDeferredAction([list = completed] {
          if (list->IsSubmitted()) {
            list->OnExecuted();
          } else {
            list->OnFailed();
          }
        });
      }
    }
    if (receipt) {
      result_ = recorder_->GetTargetQueue()->SubmitWithReceipt(completed);
    } else {
      recorder_->GetTargetQueue()->Submit(completed);
      // Test queues may implement Submit directly; production queues finalize
      // their state inside the common issue transaction.
      if (!completed->IsSubmitted()) {
        completed->OnSubmitted();
      }
      result_.outcome = SubmissionOutcome::kSubmitted;
      reclaimer_->CommitDeferredAction(std::move(retirement));
    }
  } catch (const SubmissionException& error) {
    result_ = error.Result();
  } catch (...) {
    // End/preparation failures have not issued native commands.
  }
  if (result_.outcome == SubmissionOutcome::kDiscarded) {
    Discard();
  } else {
    state_ = result_.outcome == SubmissionOutcome::kSubmitted
      ? State::kSubmitted
      : State::kExecutionUncertain;
    ResolveAndRelease(result_.outcome);
  }
  return result_;
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
    try {
      if (!recorder_->End()) {
        command_list_->Invalidate();
      }
    } catch (...) {
      command_list_->Invalidate();
    }
  }
  command_list_->Uses().Release(UseReleaseReason::kDiscarded);
  command_list_->OnFailed();
  ResolveAndRelease(SubmissionOutcome::kDiscarded);
}

void CommandRecording::ResolveAndRelease(
  const SubmissionOutcome outcome) noexcept
{
  // Detach before invoking user observers. Keep the backend alive until all
  // recorder/list cleanup has returned, outside the lifecycle admission lock.
  auto backend = std::move(backend_owner_);
  auto lifetime = std::move(lifetime_);
  auto recorder = std::move(recorder_);
  auto list = std::move(command_list_);
  reclaimer_ = nullptr;
  if (lifetime) {
    lifetime->ReleaseRecording();
  }
  recorder->ResolveSubmission(outcome);
}

} // namespace oxygen::graphics
