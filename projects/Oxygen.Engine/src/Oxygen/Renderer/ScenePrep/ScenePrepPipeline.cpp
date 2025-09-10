//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Renderer/ScenePrep/ScenePrepPipeline.h>

namespace oxygen::engine::sceneprep {

__declspec(noinline) auto ScenePrepPipeline::Collect(scene::Scene& scene,
  const View& view, uint64_t frame_id, ScenePrepState& state, bool reset_state)
  -> void
{
  DLOG_SCOPE_F(2, fmt::format("ScenePrep Collect f:{}", frame_id).c_str());

  prep_state_ = observer_ptr { &state };
  ctx_.emplace(frame_id, view, scene);

  // Reset per-frame state if requested.
  if (reset_state) {
    prep_state_->ResetFrameData();
  }

  const auto& node_table = scene.GetNodes();
  const auto items = node_table.Items();
  // Reserve an upper bound to minimize reallocations in producer
  state.collected_items.reserve(state.collected_items.size() + items.size());
  state.filtered_indices.reserve(items.size());

  for (const auto& node_impl : items) {
    if (!node_impl.HasComponent<scene::detail::RenderableComponent>()) {
      // Skip node if RenderItemProto construction fails (missing components)
      continue;
    }
    DLOG_F(3, "Node: {}", node_impl.GetName());
    try {
      RenderItemProto item { node_impl };

      CollectImpl(ctx_, *prep_state_, item);

    } catch (const std::exception& ex) {
      LOG_F(ERROR, "-skipped- due to exception: {}", ex.what());
    }
  }
}

} // namespace oxygen::engine::sceneprep
