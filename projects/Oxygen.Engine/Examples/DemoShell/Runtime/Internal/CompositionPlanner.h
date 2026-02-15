//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Renderer/Types/CompositingTask.h>

namespace oxygen::examples::internal {

class FramePlanBuilder;

class CompositionPlanner {
public:
  explicit CompositionPlanner(observer_ptr<FramePlanBuilder> frame_plan_builder)
    : frame_plan_builder_(frame_plan_builder)
  {
  }
  ~CompositionPlanner();
  OXYGEN_MAKE_NON_COPYABLE(CompositionPlanner)
  OXYGEN_MAKE_NON_MOVABLE(CompositionPlanner)

  void PlanCompositingTasks();
  auto BuildCompositionSubmission(
    std::shared_ptr<graphics::Framebuffer> final_output)
    -> oxygen::engine::CompositionSubmission;

private:
  observer_ptr<FramePlanBuilder> frame_plan_builder_;
  engine::CompositingTaskList planned_composition_tasks;
};

} // namespace oxygen::examples::internal
