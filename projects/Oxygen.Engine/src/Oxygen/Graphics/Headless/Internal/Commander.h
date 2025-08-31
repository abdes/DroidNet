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

#include <Oxygen/Composition/Component.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>

namespace oxygen::graphics::headless {

class Graphics;

namespace internal {

  class Commander final : public Component {
    OXYGEN_COMPONENT(Commander)

  public:
    Commander() = default;
    ~Commander() = default;

    OXYGEN_DEFAULT_COPYABLE(Commander)
    OXYGEN_DEFAULT_MOVABLE(Commander)

    auto PrepareCommandRecorder(
      std::unique_ptr<graphics::CommandRecorder> recorder,
      std::shared_ptr<graphics::CommandList> command_list,
      bool immediate_submission) -> std::unique_ptr<graphics::CommandRecorder,
      std::function<void(graphics::CommandRecorder*)>>;

    auto SubmitDeferredCommandLists() -> void;

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

} // namespace internal
} // namespace oxygen::graphics::headless
