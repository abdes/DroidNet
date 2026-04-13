//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Vortex/Internal/CompositionPlanner.h>
#include <Oxygen/Vortex/Internal/CompositionViewImpl.h>
#include <Oxygen/Vortex/SceneRenderer/Internal/FramePlanBuilder.h>

namespace oxygen::vortex::internal {

namespace {

auto ShouldCopyPrimarySceneView(const FrameViewPacket& packet) -> bool
{
  const auto& view = packet.View();
  const auto& desc = view.GetDescriptor();
  return packet.Plan().Intent() == ViewRenderIntent::kSceneAndComposite
    && desc.camera.has_value()
    && desc.z_order == CompositionView::kZOrderScene
    && packet.GetCompositeOpacity() >= 1.0F;
}

} // namespace

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
    if (ShouldCopyPrimarySceneView(packet)) {
      const auto published_view_id = packet.PublishedViewId();
      DLOG_F(2,
        "scene view '{}' planned as copy (published_view_id={} opacity={})",
        packet.View().GetDescriptor().name, published_view_id.get(),
        packet.GetCompositeOpacity());
      planned_composition_tasks.push_back(CompositingTask::MakeCopy(
        published_view_id, packet.GetCompositeViewport()));
      continue;
    }

    DLOG_F(2, "view '{}' planned as texture blend (opacity={})",
      packet.View().GetDescriptor().name, packet.GetCompositeOpacity());
    planned_composition_tasks.push_back(CompositingTask::MakeTextureBlend(
      packet.GetCompositeTexture(), packet.GetCompositeViewport(),
      packet.GetCompositeOpacity()));
  }
}

auto CompositionPlanner::BuildCompositionSubmission(
  std::shared_ptr<graphics::Framebuffer> final_output)
  -> oxygen::vortex::CompositionSubmission
{
  if (!final_output) {
    LOG_F(WARNING, "Vortex: skipping compositing because target is null");
    return {};
  }

  const auto& target_desc = final_output->GetDescriptor();
  if (target_desc.color_attachments.empty()
    || !target_desc.color_attachments[0].texture) {
    LOG_F(WARNING,
      "Vortex: skipping compositing because composite_target has no color "
      "attachment texture");
    return {};
  }

  oxygen::vortex::CompositionSubmission submission;
  submission.composite_target = std::move(final_output);
  submission.tasks = planned_composition_tasks;
  return submission;
}

} // namespace oxygen::vortex::internal
