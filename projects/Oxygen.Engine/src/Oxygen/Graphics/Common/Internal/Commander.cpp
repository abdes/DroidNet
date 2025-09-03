//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <format>
#include <map>
#include <ranges>
#include <stdexcept>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Graphics/Common/CommandList.h>
#include <Oxygen/Graphics/Common/CommandQueue.h>
#include <Oxygen/Graphics/Common/Detail/DeferredReclaimer.h>
#include <Oxygen/Graphics/Common/Internal/Commander.h>

namespace oxygen::graphics::internal {

auto Commander::PrepareCommandRecorder(
  std::unique_ptr<graphics::CommandRecorder> recorder,
  std::shared_ptr<graphics::CommandList> command_list, bool immediate)
  -> std::unique_ptr<graphics::CommandRecorder,
    std::function<void(graphics::CommandRecorder*)>>
{
  CHECK_NOTNULL_F(recorder);
  CHECK_NOTNULL_F(command_list);

  LOG_SCOPE_F(1, "CommandRecorder");
  DLOG_F(2, "command list : '{}'", command_list->GetName());
  DLOG_F(2, "target queue : '{}'", recorder->GetTargetQueue()->GetName());
  DLOG_F(2, "mode         : {}", immediate ? "immediate" : "deferred");

  recorder->Begin();

  // Capture `this` so the deleter can access pending_cmd_lists_. The deleter
  // uses scoped logging so submission vs storage is clear in traces.
  return { recorder.release(),
    [this, cmd_list = std::move(command_list), immediate](
      graphics::CommandRecorder* rec) mutable {
      DLOG_SCOPE_F(1, "~CommandRecorder()");
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
          if (immediate) {
            DLOG_SCOPE_F(2, "Immediate submission");
            try {
              target_queue->Submit(completed_cmd);
            } catch (const std::exception& e) {
              LOG_F(
                ERROR, "-failed- '{}' : ", completed_cmd->GetName(), e.what());
              throw;
            }
            cmd_list->OnSubmitted();
            // Register a deferred action to call OnExecuted() for the command
            // list after execution completes. Completion is guaranteed by the
            // engine when a new frame cycle starts with the same frame slot,
            // and so, we can reliably use the DeferredReclaimer.
            RegisterDeferredOnExecute(std::move(cmd_list));
          } else {
            DLOG_F(2, "-> deferred submission");
            std::lock_guard lk(pending_submissions_mutex_);
            pending_submissions_.push_back(
              { std::move(completed_cmd), rec->GetTargetQueue() });
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

auto Commander::UpdateDependencies(
  const std::function<oxygen::Component&(oxygen::TypeId)>&
    get_component) noexcept -> void
{
  // Dependency resolution is guaranteed by the engine's component
  // coordinator; directly resolve and cache the DeferredReclaimer
  // component pointer without defensive checks.
  auto& comp
    = get_component(oxygen::graphics::detail::DeferredReclaimer::ClassTypeId());
  // `comp` is known to actually be a DeferredReclaimer; cast to the concrete
  // type and construct an observer_ptr from the raw pointer.
  auto* dr = static_cast<oxygen::graphics::detail::DeferredReclaimer*>(&comp);
  reclaimer_
    = oxygen::observer_ptr<oxygen::graphics::detail::DeferredReclaimer>(dr);
}

auto Commander::SubmitDeferredCommandLists() -> void
{
  std::vector<DeferredSubmission> submissions;
  {
    std::lock_guard lk(pending_submissions_mutex_);
    if (pending_submissions_.empty()) {
      DLOG_F(4, "No deferred command lists to submit");
      return;
    }
    // Efficient atomic swap of the pending submissions with an empty vector.
    submissions.swap(pending_submissions_);
  }

  DLOG_SCOPE_FUNCTION(2);

  using QueuePtr = observer_ptr<graphics::CommandQueue>;
  using CmdListPtr = std::shared_ptr<graphics::CommandList>;

  // Group by queue using a simple manual loop (more efficient than complex
  // ranges).
  std::map<QueuePtr, std::vector<CmdListPtr>> queue_groups;
  for (auto&& [list, queue] : submissions) {
    if (list && queue) {
      queue_groups[static_cast<QueuePtr>(queue)].emplace_back(
        static_cast<CmdListPtr>(std::move(list)));
    }
  }

  // Submit each queue's commands with streamlined error handling
  std::vector<std::string> errors;

  for (auto&& [queue, command_lists] : queue_groups) {
    try {
      DLOG_F(2, "-> {} command list(s) to queue '{}'", command_lists.size(),
        queue->GetName());
      queue->Submit(std::span { command_lists });
      // Mark all as submitted
      std::ranges::for_each(command_lists, &graphics::CommandList::OnSubmitted);

      // Register a deferred action to call OnExecuted() for each command list
      // after execution completes. Completion is guaranteed by the engine when
      // a new frame cycle starts with the same frame slot, and so, we can
      // reliably use the DeferredReclaimer.
      RegisterDeferredOnExecute(std::move(command_lists));
    } catch (const std::exception& ex) {
      // Efficiently collect errors using ranges transform
      auto queue_errors
        = command_lists | std::views::transform([&ex](const auto& cmd) {
            return std::format("-failed- '{}': {}", cmd->GetName(), ex.what());
          });
      errors.append_range(queue_errors);
    }
  }

  queue_groups.clear();

  // Report errors and throw if any occurred
  if (!errors.empty()) {
    std::ranges::for_each(
      errors, [](const auto& error) { LOG_F(ERROR, "{}", error); });
    throw std::runtime_error(std::format(
      "Failed to submit {} deferred command list(s)", errors.size()));
  }
}

auto Commander::RegisterDeferredOnExecute(
  std::vector<std::shared_ptr<graphics::CommandList>> lists) -> void
{
  reclaimer_->RegisterDeferredAction([moved = std::move(lists)]() mutable {
    DLOG_SCOPE_F(2, "->OnExecuted() Deferred Action");
    for (auto& l : moved) {
      DLOG_F(2, "command list: {}", l->GetName());
      try {
        DCHECK_NOTNULL_F(l);
        l->OnExecuted();
      } catch (const std::exception& e) {
        DLOG_F(WARNING, "-failed- with exception: {}", e.what());
      }
    }
  });
}

auto Commander::RegisterDeferredOnExecute(
  std::shared_ptr<graphics::CommandList> list) -> void
{
  reclaimer_->RegisterDeferredAction([l = std::move(list)]() mutable {
    DLOG_SCOPE_F(2, "->OnExecuted() Deferred Action");
    DLOG_F(2, "command list: {}", l->GetName());
    try {
      DCHECK_NOTNULL_F(l);
      l->OnExecuted();
    } catch (const std::exception& e) {
      DLOG_F(WARNING, "-failed- with exception: {}", e.what());
    }
  });
}

} // namespace oxygen::graphics::internal
