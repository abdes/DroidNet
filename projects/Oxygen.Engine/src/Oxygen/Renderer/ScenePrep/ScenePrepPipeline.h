//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <utility>

#include <Oxygen/Renderer/RenderContext.h>
#include <Oxygen/Renderer/ScenePrep/CollectionConfig.h>
#include <Oxygen/Renderer/ScenePrep/RenderItemProto.h>
#include <Oxygen/Renderer/ScenePrep/ScenePrepState.h>
#include <Oxygen/Renderer/ScenePrep/Types.h>
#include <Oxygen/Renderer/Types/View.h>
#include <Oxygen/Scene/Scene.h>
#include <stdexcept>

namespace oxygen::engine::sceneprep {

//! Collection-only pipeline that orchestrates ScenePrep extractors.
/*!
 Applies configured collection stages over all scene nodes and produces
 `RenderItemData` entries in `ScenePrepState::collected_items`.

 Stages are invoked with compile-time gating via presence flags on the
 `CollectionConfig` type. Dropped items short-circuit downstream stages.
 */
template <typename CollectionCfg> class ScenePrepPipelineCollection {
public:
  explicit ScenePrepPipelineCollection(CollectionCfg cfg) noexcept
    : collection_(std::move(cfg))
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
    ScenePrepState& state) const -> void
  {
    ScenePrepContext ctx { frame_id, view, scene };
    state.ResetFrameData();

    const auto& node_table = scene.GetNodes();
    const auto items = node_table.Items();
    // Reserve an upper bound to minimize reallocations in producer
    state.collected_items.reserve(state.collected_items.size() + items.size());

    for (const auto& node_impl : items) {
      try {
        RenderItemProto item { node_impl };

        if constexpr (CollectionCfg::has_pre_filter) {
          collection_.pre_filter(ctx, state, item);
          if (item.IsDropped()) {
            continue;
          }
        }
        if constexpr (CollectionCfg::has_transform_resolve) {
          collection_.transform_resolve(ctx, state, item);
          if (item.IsDropped()) {
            continue;
          }
        }
        if constexpr (CollectionCfg::has_mesh_resolver) {
          collection_.mesh_resolver(ctx, state, item);
          if (item.IsDropped()) {
            continue;
          }
        }
        if constexpr (CollectionCfg::has_visibility_filter) {
          collection_.visibility_filter(ctx, state, item);
          if (item.IsDropped()) {
            continue;
          }
        }
        if constexpr (CollectionCfg::has_producer) {
          collection_.producer(ctx, state, item);
        }
      } catch (const std::exception&) {
        // Skip node if RenderItemProto construction fails (missing components)
      }
    }
  }

private:
  CollectionCfg collection_ {};
};

} // namespace oxygen::engine::sceneprep
