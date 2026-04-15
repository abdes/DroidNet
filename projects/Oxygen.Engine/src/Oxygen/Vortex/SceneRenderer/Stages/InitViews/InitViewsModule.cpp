//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <optional>
#include <utility>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Types/ResolvedView.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Vortex/RenderContext.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/ScenePrep/CollectionConfig.h>
#include <Oxygen/Vortex/ScenePrep/Extractors.h>
#include <Oxygen/Vortex/ScenePrep/FinalizationConfig.h>
#include <Oxygen/Vortex/ScenePrep/Finalizers.h>
#include <Oxygen/Vortex/ScenePrep/ScenePrepPipeline.h>
#include <Oxygen/Vortex/SceneRenderer/SceneTextures.h>
#include <Oxygen/Vortex/SceneRenderer/Stages/InitViews/InitViewsModule.h>

namespace oxygen::vortex {

namespace {

  auto BeginFrameCollection(sceneprep::ScenePrepPipeline& scene_prep,
    const scene::Scene& scene, const frame::SequenceNumber frame_id,
    sceneprep::ScenePrepState& state) -> void
  {
    scene_prep.Collect(scene, std::nullopt, frame_id, state, true);
  }

  auto PrepareView(sceneprep::ScenePrepPipeline& scene_prep,
    const scene::Scene& scene, const ResolvedView& resolved_view,
    const frame::SequenceNumber frame_id, sceneprep::ScenePrepState& state)
    -> void
  {
    scene_prep.Collect(scene,
      std::optional(observer_ptr<const ResolvedView> { &resolved_view }),
      frame_id, state, true);
  }

  auto FinalizeView(sceneprep::ScenePrepPipeline& scene_prep) -> void
  {
    scene_prep.Finalize();
  }

  auto PublishPreparedSceneFrame(const sceneprep::ScenePrepState& state,
    std::vector<sceneprep::RenderItemData>& render_items,
    PreparedSceneFrame& prepared_frame) -> void
  {
    const auto collected_items = state.CollectedItems();
    render_items.assign(collected_items.begin(), collected_items.end());

    prepared_frame = {};
    prepared_frame.render_items
      = std::span<const sceneprep::RenderItemData>(
        render_items.data(), render_items.size());
  }

} // namespace

InitViewsModule::InitViewsModule(Renderer& renderer)
  : renderer_(renderer)
{
  auto collection_cfg = sceneprep::CreateBasicCollectionConfig();
  auto finalization_cfg = sceneprep::CreateStandardFinalizationConfig();
  scene_prep_ = std::make_unique<
    sceneprep::ScenePrepPipelineImpl<decltype(collection_cfg),
      decltype(finalization_cfg)>>(
    std::move(collection_cfg), std::move(finalization_cfg));
}

InitViewsModule::~InitViewsModule() = default;

void InitViewsModule::Execute(
  RenderContext& ctx, SceneTextures& scene_textures)
{
  (void)scene_textures;
  (void)renderer_;

  for (auto& [_, storage] : prepared_views_) {
    storage.published = false;
    storage.render_items.clear();
    storage.prepared_frame = {};
  }

  const auto scene = ctx.GetScene();
  if (scene == nullptr || scene_prep_ == nullptr) {
    return;
  }

  BeginFrameCollection(*scene_prep_, *scene, ctx.frame_sequence,
    scene_prep_state_);

  for (const auto& view_entry : ctx.frame_views) {
    if (!view_entry.is_scene_view || view_entry.resolved_view == nullptr) {
      continue;
    }

    auto [it, _] = prepared_views_.try_emplace(view_entry.view_id);
    auto& storage = it->second;

    PrepareView(*scene_prep_, *scene, *view_entry.resolved_view,
      ctx.frame_sequence, scene_prep_state_);
    FinalizeView(*scene_prep_);
    PublishPreparedSceneFrame(
      scene_prep_state_, storage.render_items, storage.prepared_frame);
    storage.published = true;
  }
}

auto InitViewsModule::GetPreparedSceneFrame(const ViewId view_id) const
  -> const PreparedSceneFrame*
{
  const auto it = prepared_views_.find(view_id);
  if (it == prepared_views_.end() || !it->second.published) {
    return nullptr;
  }

  return &it->second.prepared_frame;
}

} // namespace oxygen::vortex
