//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Renderer/Pipeline/Internal/CompositionPlanner.h>
#include <Oxygen/Renderer/Pipeline/Internal/FramePlanBuilder.h>

namespace oxygen::renderer::internal {

CompositionPlanner::~CompositionPlanner() = default;

void CompositionPlanner::PlanCompositingTasks()
{
  DCHECK_NOTNULL_F(frame_plan_builder_.get());
  planned_composition_tasks.clear();
  const auto& frame_view_packets = frame_plan_builder_->GetFrameViewPackets();
  planned_composition_tasks.reserve(frame_view_packets.size());
  for (const auto& packet : frame_view_packets) {
    if (!packet.HasCompositeTexture()) {
      continue;
    }
    planned_composition_tasks.push_back(
      oxygen::engine::CompositingTask::MakeTextureBlend(
        packet.GetCompositeTexture(), packet.GetCompositeViewport(),
        packet.GetCompositeOpacity()));
  }
}

auto CompositionPlanner::BuildCompositionSubmission(
  std::shared_ptr<graphics::Framebuffer> final_output)
  -> oxygen::engine::CompositionSubmission
{
  if (!final_output) {
    LOG_F(
      WARNING, "ForwardPipeline: skipping compositing because target is null");
    return {};
  }

  const auto& target_desc = final_output->GetDescriptor();
  if (target_desc.color_attachments.empty()
    || !target_desc.color_attachments[0].texture) {
    LOG_F(WARNING,
      "ForwardPipeline: skipping compositing because composite_target has no "
      "color attachment texture");
    return {};
  }

  oxygen::engine::CompositionSubmission submission;
  submission.composite_target = std::move(final_output);
  submission.tasks = planned_composition_tasks;
  return submission;
}

} // namespace oxygen::renderer::internal
