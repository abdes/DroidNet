//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Composition/Component.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/Internal/DeferredReclaimerComponent.h>
#include <Oxygen/Graphics/Common/api_export.h>

namespace oxygen::graphics::internal {

class Commander : public Component {
  OXYGEN_COMPONENT(Commander)
  // Commander relies on DeferredReclaimer to run deferred actions at frame
  // boundaries so it can complete command list lifecycle transitions after
  // GPU work has been observed as completed.
  OXYGEN_COMPONENT_REQUIRES(
    oxygen::graphics::internal::DeferredReclaimerComponent)

public:
  Commander() = default;
  virtual ~Commander() = default;

  OXYGEN_DEFAULT_COPYABLE(Commander)
  OXYGEN_DEFAULT_MOVABLE(Commander)

  OXGN_GFX_NDAPI auto PrepareCommandRecorder(
    std::unique_ptr<graphics::CommandRecorder> recorder,
    std::shared_ptr<graphics::CommandList> command_list,
    bool immediate_submission) -> std::unique_ptr<graphics::CommandRecorder,
    std::function<void(graphics::CommandRecorder*)>>;

  OXGN_GFX_API auto SubmitDeferredCommandLists() -> void;

private:
  // Register a deferred reclaimer action that will call OnExecuted() on
  // the provided submitted command lists when the frame boundary is
  // reached. The vector of raw pointers is consumed (moved) by this method
  // because the reclaimer only needs to call OnExecuted() and does not
  // require ownership.
  auto RegisterDeferredOnExecute(
    std::vector<std::shared_ptr<graphics::CommandList>> lists) -> void;

  // Overload for a single command list to avoid allocating a temporary
  // vector for immediate submissions.
  auto RegisterDeferredOnExecute(std::shared_ptr<graphics::CommandList> list)
    -> void;

protected:
  // Resolve component dependencies after construction
  OXGN_GFX_API auto UpdateDependencies(
    const std::function<oxygen::Component&(oxygen::TypeId)>&
      get_component) noexcept -> void override;

  // Cached non-owning pointer to the DeferredReclaimer component
  // (resolved in UpdateDependencies). Use observer_ptr to reflect that
  // we do not own the component.
  // NOTE: protected to allow injection of a DeferredReclaimer for testing.
  observer_ptr<oxygen::graphics::detail::DeferredReclaimer> reclaimer_ {
    nullptr
  };

private:
  // Store a command list together with its intended submission metadata so
  // we can submit without querying the list during the drain stage.
  struct DeferredSubmission {
    std::shared_ptr<graphics::CommandList> list;
    // Non-owning pointer to the intended submission queue. Queues are
    // stable for the lifetime of the renderer so storing a pointer avoids
    // an expensive lookup during the drain stage.
    observer_ptr<graphics::CommandQueue> queue;
  };

  std::vector<DeferredSubmission> pending_submissions_;
  // Protects access to pending_submissions_ in multithreaded scenarios.
  mutable std::mutex pending_submissions_mutex_;
};

} // namespace oxygen::graphics::internal
