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
#include <Oxygen/Renderer/api_export.h>
#include <Oxygen/Scene/Scene.h>

namespace oxygen::engine::sceneprep {

class ScenePrepPipeline {
public:
  ScenePrepPipeline() = default;

  OXYGEN_DEFAULT_COPYABLE(ScenePrepPipeline)
  OXYGEN_DEFAULT_MOVABLE(ScenePrepPipeline)

  virtual ~ScenePrepPipeline() = default;

  OXGN_RNDR_API auto Collect(scene::Scene& scene, const View& view,
    uint64_t frame_id, ScenePrepState& state, bool reset_state) -> void;

  // virtual auto Finalize() -> void = 0;

protected:
  virtual auto CollectImpl(std::optional<ScenePrepContext> ctx_,
    ScenePrepState& state, RenderItemProto& item) -> void
    = 0;

private:
  std::optional<ScenePrepContext> ctx_ {};
  observer_ptr<ScenePrepState> prep_state_;
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
  auto CollectImpl(std::optional<ScenePrepContext> ctx, ScenePrepState& state,
    RenderItemProto& item) -> void override
  {
    if constexpr (CollectionCfg::has_pre_filter) {
      collection_.pre_filter(*ctx, state, item);
      if (item.IsDropped()) {
        return;
      }
    }
    if constexpr (CollectionCfg::has_transform_resolve) {
      collection_.transform_resolve(*ctx, state, item);
      if (item.IsDropped()) {
        return;
      }
    }
    if constexpr (CollectionCfg::has_mesh_resolver) {
      collection_.mesh_resolver(*ctx, state, item);
      if (item.IsDropped()) {
        return;
      }
    }
    if constexpr (CollectionCfg::has_visibility_filter) {
      collection_.visibility_filter(*ctx, state, item);
      if (item.IsDropped()) {
        return;
      }
    }

    // Track how many items were in collected_items before producer
    const auto items_before = state.collected_items.size();

    if constexpr (CollectionCfg::has_producer) {
      collection_.producer(*ctx, state, item);
    }

    // Track indices of all new items added by the producer
    const auto items_after = state.collected_items.size();
    for (auto i = items_before; i < items_after; ++i) {
      state.filtered_indices.push_back(i);
    }
  }

private:
  [[no_unique_address]] CollectionCfg collection_ {};
};

} // namespace oxygen::engine::sceneprep
