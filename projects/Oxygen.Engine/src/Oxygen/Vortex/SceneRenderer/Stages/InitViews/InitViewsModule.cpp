//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <optional>
#include <utility>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Types/ResolvedView.h>
#include <Oxygen/Content/IAssetLoader.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Vortex/RenderContext.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/RendererTag.h>
#include <Oxygen/Vortex/Resources/DrawMetadataEmitter.h>
#include <Oxygen/Vortex/Resources/GeometryUploader.h>
#include <Oxygen/Vortex/Resources/MaterialBinder.h>
#include <Oxygen/Vortex/Resources/TextureBinder.h>
#include <Oxygen/Vortex/Resources/TransformUploader.h>
#include <Oxygen/Vortex/ScenePrep/CollectionConfig.h>
#include <Oxygen/Vortex/ScenePrep/Extractors.h>
#include <Oxygen/Vortex/ScenePrep/FinalizationConfig.h>
#include <Oxygen/Vortex/ScenePrep/Finalizers.h>
#include <Oxygen/Vortex/ScenePrep/ScenePrepPipeline.h>
#include <Oxygen/Vortex/SceneRenderer/SceneTextures.h>
#include <Oxygen/Vortex/SceneRenderer/Stages/InitViews/InitViewsModule.h>

namespace oxygen::vortex {

namespace {

  constexpr std::size_t kMatrixFloatCount = 16U;

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

  auto BeginResourceManagerFrame(resources::TextureBinder* const texture_binder,
    sceneprep::ScenePrepState& state, const RenderContext& ctx) -> void
  {
    const auto tag = internal::RendererTagFactory::Get();
    if (texture_binder != nullptr) {
      texture_binder->OnFrameStart();
    }
    if (const auto geometry_uploader = state.GetGeometryUploader();
      geometry_uploader != nullptr) {
      geometry_uploader->OnFrameStart(tag, ctx.frame_slot);
    }
    if (const auto transform_uploader = state.GetTransformUploader();
      transform_uploader != nullptr) {
      transform_uploader->OnFrameStart(tag, ctx.frame_sequence, ctx.frame_slot);
    }
    if (const auto material_binder = state.GetMaterialBinder();
      material_binder != nullptr) {
      material_binder->OnFrameStart(tag, ctx.frame_slot);
    }
    if (const auto draw_emitter = state.GetDrawMetadataEmitter();
      draw_emitter != nullptr) {
      draw_emitter->OnFrameStart(tag, ctx.frame_sequence, ctx.frame_slot);
    }
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

    if (const auto draw_emitter = state.GetDrawMetadataEmitter();
      draw_emitter != nullptr) {
      prepared_frame.draw_metadata_bytes = draw_emitter->GetDrawMetadataBytes();
      prepared_frame.partitions = draw_emitter->GetPartitions();
      prepared_frame.draw_bounding_spheres
        = draw_emitter->GetDrawBoundingSpheres();
      prepared_frame.bindless_draw_metadata_slot
        = draw_emitter->GetDrawMetadataSrvIndex();
      prepared_frame.bindless_draw_bounds_slot
        = draw_emitter->GetDrawBoundingSpheresSrvIndex();
      prepared_frame.bindless_instance_data_slot
        = draw_emitter->GetInstanceDataSrvIndex();
    }

    if (const auto transform_uploader = state.GetTransformUploader();
      transform_uploader != nullptr) {
      const auto world_matrices = transform_uploader->GetWorldMatrices();
      if (!world_matrices.empty()) {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
        const auto* world_data = reinterpret_cast<const float*>(world_matrices.data());
        prepared_frame.world_matrices = {
          world_data,
          world_matrices.size() * kMatrixFloatCount,
        };
      }
      const auto normal_matrices = transform_uploader->GetNormalMatrices();
      if (!normal_matrices.empty()) {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
        const auto* normal_data = reinterpret_cast<const float*>(normal_matrices.data());
        prepared_frame.normal_matrices = {
          normal_data,
          normal_matrices.size() * kMatrixFloatCount,
        };
      }
      prepared_frame.bindless_worlds_slot
        = transform_uploader->GetWorldsSrvIndex();
      prepared_frame.bindless_normals_slot
        = transform_uploader->GetNormalsSrvIndex();
    }

    if (const auto material_binder = state.GetMaterialBinder();
      material_binder != nullptr) {
      prepared_frame.bindless_material_shading_slot
        = material_binder->GetMaterialShadingSrvIndex();
    }
  }

} // namespace

InitViewsModule::InitViewsModule(Renderer& renderer)
  : renderer_(renderer)
{
  if (const auto gfx = renderer_.GetGraphics();
    gfx != nullptr && renderer_.GetAssetLoader() != nullptr) {
    auto asset_loader = renderer_.GetAssetLoader();
    auto& uploader = renderer_.GetUploadCoordinator();
    auto& staging_provider = renderer_.GetStagingProvider();
    auto& inline_transfers = renderer_.GetInlineTransfersCoordinator();

    texture_binder_ = std::make_unique<resources::TextureBinder>(
      observer_ptr { gfx.get() }, observer_ptr { &staging_provider },
      observer_ptr { &uploader }, asset_loader);

    auto geometry_uploader = std::make_unique<resources::GeometryUploader>(
      observer_ptr { gfx.get() }, observer_ptr { &uploader },
      observer_ptr { &staging_provider }, asset_loader);
    auto transform_uploader = std::make_unique<resources::TransformUploader>(
      observer_ptr { gfx.get() }, observer_ptr { &staging_provider },
      observer_ptr { &inline_transfers });
    auto material_binder = std::make_unique<resources::MaterialBinder>(
      observer_ptr { gfx.get() }, observer_ptr { &uploader },
      observer_ptr { &staging_provider }, observer_ptr { texture_binder_.get() },
      asset_loader);
    auto draw_metadata_emitter = std::make_unique<resources::DrawMetadataEmitter>(
      observer_ptr { gfx.get() }, observer_ptr { &staging_provider },
      observer_ptr { geometry_uploader.get() },
      observer_ptr { material_binder.get() }, observer_ptr { &inline_transfers });

    scene_prep_state_.AttachResourceManagers(std::move(geometry_uploader),
      std::move(transform_uploader), std::move(material_binder),
      std::move(draw_metadata_emitter));
  }

  auto collection_cfg = sceneprep::CreateBasicCollectionConfig();
  auto finalization_cfg = sceneprep::CreateStandardFinalizationConfig();
  scene_prep_ = std::make_unique<
    sceneprep::ScenePrepPipelineImpl<decltype(collection_cfg),
      decltype(finalization_cfg)>>(
    collection_cfg, finalization_cfg);
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

  BeginResourceManagerFrame(texture_binder_.get(), scene_prep_state_, ctx);
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
