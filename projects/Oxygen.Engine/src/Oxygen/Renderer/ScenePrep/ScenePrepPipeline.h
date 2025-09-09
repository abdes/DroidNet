//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <utility>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Renderer/RenderContext.h>
#include <Oxygen/Renderer/ScenePrep/RenderItemProto.h>
#include <Oxygen/Renderer/ScenePrep/ScenePrepState.h>
#include <Oxygen/Renderer/ScenePrep/Types.h>
#include <Oxygen/Scene/Scene.h>

namespace oxygen::engine::sceneprep {

class ScenePrepPipeline {
public:
  ScenePrepPipeline() = default;

  OXYGEN_DEFAULT_COPYABLE(ScenePrepPipeline)
  OXYGEN_DEFAULT_MOVABLE(ScenePrepPipeline)

  virtual ~ScenePrepPipeline() = default;

  virtual auto Collect(scene::Scene& scene, const View& view, uint64_t frame_id,
    ScenePrepState& state, bool reset_state) -> void
    = 0;

  // virtual auto Finalize() -> void = 0;
};

template <typename CollectionCfg>
class ScenePrepPipelineImpl : public ScenePrepPipeline {
public:
  explicit ScenePrepPipelineImpl(CollectionCfg collection_cfg) noexcept
    : collection_(std::move(collection_cfg))
  {
  }

  //! Traverse scene and run collection extractors.
  /*!
   Builds a `ScenePrepContext`, resets per-frame state, visits all nodes,
   and invokes configured extractors in sequence.

   @param scene Source scene graph
   @param view Current view providing camera/frustum
   @param frame_id Monotonic frame identifier
   @param render_context Renderer context (passed into ScenePrepContext)
   @param state ScenePrep working state and output buffers
  */
  auto Collect(scene::Scene& scene, const View& view, uint64_t frame_id,
    ScenePrepState& state, bool reset_state) -> void override
  {
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
      try {
        RenderItemProto item { node_impl };

        if constexpr (CollectionCfg::has_pre_filter) {
          collection_.pre_filter(*ctx_, state, item);
          if (item.IsDropped()) {
            continue;
          }
        }
        if constexpr (CollectionCfg::has_transform_resolve) {
          collection_.transform_resolve(*ctx_, state, item);
          if (item.IsDropped()) {
            continue;
          }
        }
        if constexpr (CollectionCfg::has_mesh_resolver) {
          collection_.mesh_resolver(*ctx_, state, item);
          if (item.IsDropped()) {
            continue;
          }
        }
        if constexpr (CollectionCfg::has_visibility_filter) {
          collection_.visibility_filter(*ctx_, state, item);
          if (item.IsDropped()) {
            continue;
          }
        }

        // Track how many items were in collected_items before producer
        const auto items_before = state.collected_items.size();

        if constexpr (CollectionCfg::has_producer) {
          collection_.producer(*ctx_, state, item);
        }

        // Track indices of all new items added by the producer
        const auto items_after = state.collected_items.size();
        for (auto i = items_before; i < items_after; ++i) {
          state.filtered_indices.push_back(i);
        }
      } catch (const std::exception&) {
        // Skip node if RenderItemProto construction fails (missing components)
      }
    }
  }

private:
  std::optional<ScenePrepContext> ctx_ {};
  observer_ptr<ScenePrepState> prep_state_;
  [[no_unique_address]] CollectionCfg collection_ {};
};

} // namespace oxygen::engine::sceneprep
