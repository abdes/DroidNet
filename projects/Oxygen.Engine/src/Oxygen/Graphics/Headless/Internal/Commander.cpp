//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Graphics/Common/CommandList.h>
#include <Oxygen/Graphics/Common/CommandQueue.h>
#include <Oxygen/Graphics/Headless/Graphics.h>
#include <Oxygen/Graphics/Headless/Internal/Commander.h>
#include <Oxygen/Graphics/Headless/Internal/QueueManager.h>

namespace oxygen::graphics::headless::internal {

auto Commander::PrepareCommandRecorder(
  std::unique_ptr<graphics::CommandRecorder> recorder,
  std::shared_ptr<graphics::CommandList> command_list,
  bool immediate_submission) -> std::unique_ptr<graphics::CommandRecorder,
  std::function<void(graphics::CommandRecorder*)>>
{
  DCHECK_NOTNULL_F(recorder);
  LOG_SCOPE_F(INFO, "CommandRecorder");
  DLOG_F(2, "command list    : '{}'", command_list->GetName());
  DLOG_F(2, "target queue    : '{}'", recorder->GetTargetQueue()->GetName());
  DLOG_F(
    2, "submission mode : {}", immediate_submission ? "immediate" : "deferred");

  recorder->Begin();

  // Capture `this` so the deleter can access pending_cmd_lists_. The deleter
  // uses scoped logging so submission vs storage is clear in traces.
  return { recorder.release(),
    [this, cmd_list = std::move(command_list), immediate_submission](
      graphics::CommandRecorder* rec) mutable {
      DLOG_SCOPE_F(INFO, "~CommandRecorder()");
      if (!rec) {
        DLOG_F(WARNING, "deleter invoked with null pointer");
        return;
      }
      try {
        if (auto completed_cmd = rec->End(); completed_cmd != nullptr) {
          auto target_queue = rec->GetTargetQueue();
          DCHECK_NOTNULL_F(target_queue);
          DLOG_F(2, "command list : '{}'", completed_cmd->GetName());
          DLOG_F(2, "target queue : '{}'", target_queue->GetName());
          if (immediate_submission) {
            DLOG_SCOPE_F(2, "Immediate submission");
            try {
              target_queue->Submit(*completed_cmd);
            } catch (const std::exception& e) {
              LOG_F(
                ERROR, "-failed- '{}' : ", completed_cmd->GetName(), e.what());
              throw;
            }
            cmd_list->OnSubmitted();
          } else {
            DLOG_F(2, "-> deferred submission");
            std::lock_guard lk(pending_submissions_mutex_);
            pending_submissions_.push_back({
              std::move(completed_cmd),
              rec->GetTargetQueue(),
            });
          }
        } else {
          DLOG_F(2, "no completed command list");
        }
      } catch (const std::exception& ex) {
        LOG_F(ERROR, "exception: {}", ex.what());
      }
      delete rec;
    } };
}

auto Commander::SubmitDeferredCommandLists() -> void
{
  DCHECK_NOTNULL_F(queue_manager_);
  std::vector<DeferredSubmission> submissions;
  {
    std::lock_guard lk(pending_submissions_mutex_);
    submissions.swap(pending_submissions_);
  }

  if (submissions.empty()) {
    DLOG_F(2, "No deferred command lists to submit");
    return;
  }

  auto all_submitted = true;
  for (auto& sub : submissions) {
    DCHECK_NOTNULL_F(sub.list);
    DCHECK_NOTNULL_F(sub.queue);

    DLOG_F(INFO, "submitting command list '{}' to queue '{}'",
      sub.list->GetName(), sub.queue->GetName());
    try {
      sub.queue->Submit(*sub.list);
      sub.list->OnSubmitted();
    } catch (const std::exception& e) {
      LOG_F(ERROR, "-failed- '{}': ", sub.list->GetName(), e.what());
      all_submitted = false;
    }
  }

  if (!all_submitted) {
    throw std::runtime_error("Some deferred command lists failed to submit");
  }
}

auto Commander::UpdateDependencies(
  const std::function<Component&(TypeId)>& get_component) noexcept -> void
{
  // Cache pointer to QueueManager to avoid repeated lookups during submission.
  queue_manager_ = &static_cast<internal::QueueManager&>(
    get_component(internal::QueueManager::ClassTypeId()));
}

} // namespace oxygen::graphics::headless::internal
