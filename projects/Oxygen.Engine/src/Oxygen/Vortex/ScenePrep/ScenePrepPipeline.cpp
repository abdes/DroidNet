//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/Types/ResolvedView.h>
#include <Oxygen/Scene/SceneTraversal.h>
#include <Oxygen/Vortex/ScenePrep/ScenePrepPipeline.h>

namespace oxygen::vortex::sceneprep {

auto ScenePrepPipeline::RecordCollectionFailure(const std::string_view stage,
  const std::optional<ScenePrepContext>& ctx, const scene::SceneNodeImpl* node,
  const std::string_view message) -> void
{
  ++failure_stats_.total_failures;

  const auto* const phase
    = ctx.has_value() && ctx->HasView() ? "view" : "frame";
  const auto node_name = node != nullptr ? node->GetName() : "<unknown>";
  const auto frame_sequence
    = ctx.has_value() ? ctx->GetFrameSequenceNumber().get() : 0U;
  const auto key = std::string(phase) + "|" + std::string(stage) + "|"
    + std::string(node_name) + "|" + std::string(message);

  const auto [it, inserted] = failure_occurrences_.try_emplace(key, 0U);
  ++it->second;

  if (!inserted) {
    ++failure_stats_.suppressed_failures;
    return;
  }

  ++failure_stats_.logged_failures;
  LOG_F(ERROR,
    "ScenePrep {}-phase stage '{}' failed for node '{}' (frame={}): {}", phase,
    stage, node_name, frame_sequence, message);
}

auto ScenePrepPipeline::RecordCollectionFailure(const std::string_view stage,
  const std::optional<ScenePrepContext>& ctx, const scene::SceneNodeImpl* node,
  const std::exception& ex) -> void
{
  RecordCollectionFailure(stage, ctx, node, ex.what());
}

auto ScenePrepPipeline::BeginFrameCollection(const scene::Scene& scene,
  frame::SequenceNumber fseq, ScenePrepState& state) -> void
{
  CHECK_F(prepared_view_source_ == PreparedViewSource::kNone,
    "BeginFrameCollection requires FinalizeView before starting a new frame");

  DLOG_SCOPE_F(
    1, fmt::format("ScenePrep BeginFrameCollection f:{}", fseq.get()).c_str());

  prep_state_.reset(&state);
  active_scene_ = &scene;
  active_frame_sequence_ = fseq;
  frame_collection_ready_ = true;
  prepared_view_source_ = PreparedViewSource::kNone;
  ctx_.emplace(
    fseq, ::oxygen::observer_ptr<const ResolvedView>(nullptr), scene);
  prep_state_->ResetFrameData();

  const auto& node_table = scene.GetNodes();
  const auto items = node_table.Items();
  state.ReserveCapacityForItems(items.size());

  auto traversal = scene::SceneTraversal(scene.shared_from_this());
  static_cast<void>(traversal.Traverse(
    [&](const auto& visited, bool /*dry_run*/) -> scene::VisitResult {
      const auto& node_impl = *visited.node_impl;
      if (!node_impl
            .template HasComponent<scene::detail::RenderableComponent>()) {
        return scene::VisitResult::kContinue;
      }
      DLOG_F(3, "Node: {}", node_impl.GetName());
      try {
        RenderItemProto item { node_impl, visited.handle };
        CollectImpl(ctx_, *prep_state_, item);

        if (!item.IsDropped()) {
          prep_state_->CacheNodeBasics(&node_impl,
            ScenePrepState::CachedNodeBasics {
              .node_handle = item.GetNodeHandle(),
              .cast_shadows = item.CastsShadows(),
              .receive_shadows = item.ReceivesShadows(),
              .world_transform = item.GetWorldTransform(),
              .geometry = item.Geometry(),
              .transform_handle = item.GetTransformHandle(),
            });
          prep_state_->AddFilteredSceneNode(&node_impl);
        }

      } catch (const std::exception& ex) {
        RecordCollectionFailure("node_collect", ctx_, &node_impl, ex);
      }
      return scene::VisitResult::kContinue;
    }));
}

auto ScenePrepPipeline::PrepareView(const scene::Scene& scene,
  const ResolvedView& view, frame::SequenceNumber fseq, ScenePrepState& state)
  -> void
{
  CHECK_F(frame_collection_ready_,
    "PrepareView requires BeginFrameCollection for the current frame");
  CHECK_F(prepared_view_source_ == PreparedViewSource::kNone,
    "PrepareView requires FinalizeView before preparing another view");
  CHECK_F(prep_state_.get() == &state,
    "PrepareView requires the same ScenePrepState used for "
    "BeginFrameCollection");
  CHECK_F(active_scene_ == &scene,
    "PrepareView requires the same scene passed to BeginFrameCollection");
  CHECK_F(active_frame_sequence_.has_value() && *active_frame_sequence_ == fseq,
    "PrepareView requires the same frame sequence passed to "
    "BeginFrameCollection");

  DLOG_SCOPE_F(
    1, fmt::format("ScenePrep PrepareView f:{}", fseq.get()).c_str());

  prep_state_.reset(&state);
  ctx_.emplace(fseq, ::oxygen::observer_ptr<const ResolvedView>(&view), scene);
  prep_state_->ResetViewData();
  prepared_view_source_ = PreparedViewSource::kFrameCollection;

  const auto& nodes = prep_state_->GetFilteredSceneNodes();
  state.ReserveCapacityForItems(nodes.size());
  for (const auto* node_impl : nodes) {
    if (node_impl == nullptr) {
      continue;
    }
    try {
      RenderItemProto item { *node_impl };
      CollectImpl(ctx_, *prep_state_, item);
    } catch (const std::exception& ex) {
      RecordCollectionFailure("node_collect", ctx_, node_impl, ex);
    }
  }
}

auto ScenePrepPipeline::CollectSingleView(const scene::Scene& scene,
  ::oxygen::observer_ptr<const ResolvedView> view, frame::SequenceNumber fseq,
  ScenePrepState& state) -> void
{
  CHECK_F(prepared_view_source_ == PreparedViewSource::kNone,
    "CollectSingleView requires FinalizeView before preparing another view");

  DLOG_SCOPE_F(
    1, fmt::format("ScenePrep CollectSingleView f:{}", fseq.get()).c_str());

  CHECK_F(static_cast<bool>(view),
    "CollectSingleView requires a non-null resolved view");

  prep_state_.reset(&state);
  active_scene_ = &scene;
  active_frame_sequence_ = fseq;
  frame_collection_ready_ = false;
  prepared_view_source_ = PreparedViewSource::kSingleView;
  ctx_.emplace(fseq, view, scene);
  prep_state_->ResetFrameData();

  const auto& node_table = scene.GetNodes();
  const auto items = node_table.Items();
  state.ReserveCapacityForItems(items.size());

  auto traversal = scene::SceneTraversal(scene.shared_from_this());
  static_cast<void>(traversal.Traverse(
    [&](const auto& visited, bool /*dry_run*/) -> scene::VisitResult {
      const auto& node_impl = *visited.node_impl;
      if (!node_impl
            .template HasComponent<scene::detail::RenderableComponent>()) {
        return scene::VisitResult::kContinue;
      }
      DLOG_F(3, "Node: {}", node_impl.GetName());
      try {
        RenderItemProto item { node_impl, visited.handle };
        CollectImpl(ctx_, *prep_state_, item);
      } catch (const std::exception& ex) {
        RecordCollectionFailure("node_collect", ctx_, &node_impl, ex);
      }
      return scene::VisitResult::kContinue;
    }));
}

auto ScenePrepPipeline::FinalizeView(ScenePrepState& state) -> void
{
  CHECK_F(prepared_view_source_ != PreparedViewSource::kNone,
    "FinalizeView requires PrepareView or CollectSingleView before "
    "finalization");
  CHECK_F(prep_state_.get() == &state,
    "FinalizeView requires the same ScenePrepState used to prepare the current "
    "view");
  CHECK_F(ctx_.has_value());
  CHECK_F(ctx_->HasView(),
    "FinalizeView requires a current view prepared via PrepareView");
  CHECK_NOTNULL_F(prep_state_);

  DLOG_SCOPE_F(1,
    fmt::format(
      "ScenePrep FinalizeView f:{}", ctx_->GetFrameSequenceNumber().get())
      .c_str());
  FinalizeImpl(*prep_state_);

  const auto prepared_view_source = prepared_view_source_;
  prepared_view_source_ = PreparedViewSource::kNone;
  if (prepared_view_source == PreparedViewSource::kFrameCollection) {
    DCHECK_F(active_scene_ != nullptr);
    DCHECK_F(active_frame_sequence_.has_value());
    ctx_.emplace(*active_frame_sequence_,
      ::oxygen::observer_ptr<const ResolvedView>(nullptr), *active_scene_);
  } else {
    ctx_.reset();
    prep_state_.reset();
    active_scene_ = nullptr;
    active_frame_sequence_.reset();
    frame_collection_ready_ = false;
  }
}

} // namespace oxygen::vortex::sceneprep
