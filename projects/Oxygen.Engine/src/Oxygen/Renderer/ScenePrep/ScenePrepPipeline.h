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

  OXGN_RNDR_API auto Collect(const scene::Scene& scene, const View& view,
    uint64_t frame_id, ScenePrepState& state, bool reset_state) -> void;

  OXGN_RNDR_API auto Finalize() -> void;

protected:
  virtual auto CollectImpl(std::optional<ScenePrepContext> ctx,
    ScenePrepState& state, RenderItemProto& item) -> void
    = 0;

  virtual auto FinalizeImpl(ScenePrepState& state) -> void = 0;

private:
  std::optional<ScenePrepContext> ctx_ {};
  observer_ptr<ScenePrepState> prep_state_;
};

template <typename CollectionCfg, typename FinalizationCfg>
class ScenePrepPipelineImpl : public ScenePrepPipeline {
public:
  explicit ScenePrepPipelineImpl(
    CollectionCfg collect_cfg, FinalizationCfg finalize_cfg) noexcept
    : collection_(std::move(collect_cfg))
    , finalization_(std::move(finalize_cfg))
  {
  }

  //! Traverse scene and run collection extractors.
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
    const auto items_before = state.CollectedCount();

    if constexpr (CollectionCfg::has_producer) {
      collection_.producer(*ctx, state, item);
    }

    // Track indices of all new items added by the producer
    const auto items_after = state.CollectedCount();
    for (auto i = items_before; i < items_after; ++i) {
      state.MarkItemRetained(i);
    }
  }

  auto FinalizeImpl(ScenePrepState& state) -> void
  {
    if constexpr (FinalizationCfg::has_geometry_upload) {
      finalization_.geometry_upload(state);
    }

    if constexpr (FinalizationCfg::has_transform_upload) {
      finalization_.transform_upload(state);
    }

    if constexpr (FinalizationCfg::has_material_upload) {
      finalization_.material_upload(state);
    }

    // Draw metadata emission per retained item
    if constexpr (FinalizationCfg::has_draw_md_emit) {
      for (const auto& item : state.RetainedItems()) {
        finalization_.draw_md_emit(state, item);
      }
    }

    // Sorting and partitioning
    if constexpr (FinalizationCfg::has_draw_md_sorter) {
      finalization_.draw_md_sort(state);
    }

    // Upload draw metadata (includes potential SRV resolution)
    if constexpr (FinalizationCfg::has_draw_md_upload) {
      finalization_.draw_md_upload(state);
    }
  }

private:
  [[no_unique_address]] CollectionCfg collection_;
  [[no_unique_address]] FinalizationCfg finalization_;
};

} // namespace oxygen::engine::sceneprep
