//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <exception>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

#include <Oxygen/Base/Compilers.h>
#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Vortex/ScenePrep/RenderItemProto.h>
#include <Oxygen/Vortex/ScenePrep/ScenePrepContext.h>
#include <Oxygen/Vortex/ScenePrep/ScenePrepState.h>
#include <Oxygen/Vortex/ScenePrep/Types.h>
#include <Oxygen/Vortex/api_export.h>

namespace oxygen {
class ResolvedView;
} // namespace oxygen::core

namespace oxygen::vortex::sceneprep {

class ScenePrepPipeline {
public:
  struct FailureStats {
    std::uint64_t total_failures { 0U };
    std::uint64_t logged_failures { 0U };
    std::uint64_t suppressed_failures { 0U };
  };

  ScenePrepPipeline() = default;

  OXYGEN_DEFAULT_COPYABLE(ScenePrepPipeline)
  OXYGEN_DEFAULT_MOVABLE(ScenePrepPipeline)

  virtual ~ScenePrepPipeline() = default;

  //! Runs the frame-shared scene traversal and rebuilds per-frame caches.
  OXGN_VRTX_API auto BeginFrameCollection(const scene::Scene& scene,
    frame::SequenceNumber fseq, ScenePrepState& state) -> void;

  //! Reuses frame-shared caches to build the current view's transient items.
  OXGN_VRTX_API auto PrepareView(const scene::Scene& scene,
    const ResolvedView& view, frame::SequenceNumber fseq, ScenePrepState& state)
    -> void;

  //! Finalizes the current view after PrepareView() populated transient state.
  OXGN_VRTX_API auto FinalizeView(ScenePrepState& state) -> void;

  //! Explicit fused harness/test path for one isolated scene-view traversal.
  //! Traverses the full scene node table once with an active view.
  OXGN_VRTX_API auto CollectSingleView(const scene::Scene& scene,
    ::oxygen::observer_ptr<const ResolvedView> view, frame::SequenceNumber fseq,
    ScenePrepState& state) -> void;

  [[nodiscard]] auto GetFailureStats() const noexcept -> FailureStats
  {
    return failure_stats_;
  }

protected:
  virtual auto CollectImpl(std::optional<ScenePrepContext> ctx,
    ScenePrepState& state, RenderItemProto& item) -> void
    = 0;

  virtual auto FinalizeImpl(ScenePrepState& state) -> void = 0;

  OXGN_VRTX_API auto RecordCollectionFailure(std::string_view stage,
    const std::optional<ScenePrepContext>& ctx,
    const scene::SceneNodeImpl* node, std::string_view message) -> void;

  OXGN_VRTX_API auto RecordCollectionFailure(std::string_view stage,
    const std::optional<ScenePrepContext>& ctx,
    const scene::SceneNodeImpl* node, const std::exception& ex) -> void;

private:
  enum class PreparedViewSource {
    kNone,
    kFrameCollection,
    kSingleView,
  };

  std::optional<ScenePrepContext> ctx_;
  observer_ptr<ScenePrepState> prep_state_;
  const scene::Scene* active_scene_ { nullptr };
  std::optional<frame::SequenceNumber> active_frame_sequence_ {};
  bool frame_collection_ready_ { false };
  PreparedViewSource prepared_view_source_ { PreparedViewSource::kNone };
  FailureStats failure_stats_ {};
  std::unordered_map<std::string, std::uint64_t> failure_occurrences_;
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
    const auto run_stage
      = [&](const std::string_view stage_name, auto&& fn) -> bool {
      try {
        fn();
        return !item.IsDropped();
      } catch (const std::exception& ex) {
        this->RecordCollectionFailure(stage_name, ctx, item.GetNodePtr(), ex);
      }
      item.MarkDropped();
      return false;
    };

    bool seeded_from_frame_cache = false;
    if (ctx && ctx->HasView()) {
      if (const auto* cached = state.TryGetNodeBasics(item.GetNodePtr())) {
        item.SetNodeHandle(cached->node_handle);
        item.SetVisible();
        item.SetCastShadows(cached->cast_shadows);
        item.SetReceiveShadows(cached->receive_shadows);
        item.SetWorldTransform(cached->world_transform);
        item.SetGeometry(cached->geometry);
        item.SetTransformHandle(cached->transform_handle);
        seeded_from_frame_cache = true;
      }
    }

    if constexpr (CollectionCfg::has_pre_filter) {
      if (!seeded_from_frame_cache) {
        if (!run_stage("pre_filter",
              [&]() { collection_.pre_filter(*ctx, state, item); })) {
          return;
        }
      }
    }
    if constexpr (CollectionCfg::has_transform_resolve) {
      if (!item.GetTransformHandle().IsValid()) {
        if (!run_stage("transform_resolve",
              [&]() { collection_.transform_resolve(*ctx, state, item); })) {
          return;
        }
      }
    }
    if constexpr (CollectionCfg::has_mesh_resolver) {
      if (ctx && ctx->HasView()) {
        if (!run_stage("mesh_resolver",
              [&]() { collection_.mesh_resolver(*ctx, state, item); })) {
          return;
        }
      }
    }
    if constexpr (CollectionCfg::has_visibility_filter) {
      if (ctx && ctx->HasView()) {
        if (!run_stage("visibility_filter",
              [&]() { collection_.visibility_filter(*ctx, state, item); })) {
          return;
        }
      }
    }

    // Track how many items were in collected_items before producer
    const auto items_before = state.CollectedCount();

    if constexpr (CollectionCfg::has_producer) {
      if (ctx && ctx->HasView()) {
        if (!run_stage(
              "producer", [&]() { collection_.producer(*ctx, state, item); })) {
          return;
        }
      }
    }

    // Track indices of all new items added by the producer
    const auto items_after = state.CollectedCount();
    for (auto i = items_before; i < items_after; ++i) {
      state.MarkItemRetained(i);
    }
  }

  auto FinalizeImpl(ScenePrepState& state) -> void override
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
  OXYGEN_NO_UNIQUE_ADDRESS CollectionCfg collection_;
  OXYGEN_NO_UNIQUE_ADDRESS FinalizationCfg finalization_;
};

} // namespace oxygen::vortex::sceneprep
