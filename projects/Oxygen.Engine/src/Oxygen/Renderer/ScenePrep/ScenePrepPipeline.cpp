//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/Types/ResolvedView.h>
#include <Oxygen/Renderer/ScenePrep/ScenePrepPipeline.h>

namespace oxygen::engine::sceneprep {

auto ScenePrepPipeline::Collect(const scene::Scene& scene,
  std::optional<::oxygen::observer_ptr<const ResolvedView>> view,
  frame::SequenceNumber fseq, ScenePrepState& state, bool reset_state) -> void
{
  DLOG_SCOPE_F(1, fmt::format("ScenePrep Collect f:{}", fseq.get()).c_str());

  // Point prep_state_ at the provided state (non-owning).
  prep_state_.reset(&state);
  // Construct context with optional view (observer_ptr may be nullopt)
  const auto vp = view.has_value()
    ? view.value()
    : ::oxygen::observer_ptr<const ResolvedView>(nullptr);
  ctx_.emplace(fseq, vp, scene);

  // Reset per-frame or per-view state if requested.
  if (reset_state) {
    if (view.has_value()) {
      prep_state_->ResetViewData();
    } else {
      prep_state_->ResetFrameData();
    }
  }

  // If we have an explicit view, iterate the cached global list produced by
  // the Frame-phase traversal. Otherwise traverse the scene node table.
  if (view.has_value()) {
    const auto& nodes = prep_state_->GetFilteredSceneNodes();
    state.ReserveCapacityForItems(nodes.size());
    for (const auto* node_impl : nodes) {
      if (node_impl == nullptr) {
        continue;
      }
      try {
        const auto items_before = state.CollectedCount();
        RenderItemProto item { *node_impl };
        CollectImpl(ctx_, *prep_state_, item);
        // In View-phase we do not repopulate the global list.
        (void)items_before; // silence unused warning
      } catch (const std::exception& ex) {
        LOG_F(ERROR, "-skipped- due to exception: {}", ex.what());
      }
    }
  } else {
    const auto& node_table = scene.GetNodes();
    const auto items = node_table.Items();
    // Reserve an upper bound to minimize reallocations in producer
    state.ReserveCapacityForItems(items.size());

    for (const auto& node_impl : items) {
      if (const auto light_manager = state.GetLightManager()) {
        light_manager->CollectFromNode(node_impl);
      }
      if (!node_impl.HasComponent<scene::detail::RenderableComponent>()) {
        // Skip node if RenderItemProto construction fails (missing components)
        continue;
      }
      DLOG_F(3, "Node: {}", node_impl.GetName());
      try {
        // Track collected count before running collection for this node so we
        // can capture indices/nodes added by the producer.
        RenderItemProto item { node_impl };
        CollectImpl(ctx_, *prep_state_, item);

        // If we are in Frame-phase (no view), cache a reference to the node
        // for faster per-view iteration later. Producers may emit multiple
        // items per node; `AddFilteredSceneNode` deduplicates consecutive
        // inserts to avoid repeating the same node multiple times.
        if (!ctx_->HasView()) {
          prep_state_->AddFilteredSceneNode(&node_impl);
        }

      } catch (const std::exception& ex) {
        LOG_F(ERROR, "-skipped- due to exception: {}", ex.what());
      }
    }
  }
}

auto ScenePrepPipeline::Finalize() -> void
{
  DCHECK_F(ctx_.has_value());
  DCHECK_NOTNULL_F(prep_state_);

  DLOG_SCOPE_F(1,
    fmt::format("ScenePrep Finalize f:{}", ctx_->GetFrameSequenceNumber().get())
      .c_str());
  FinalizeImpl(*prep_state_);
}

} // namespace oxygen::engine::sceneprep
