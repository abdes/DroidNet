//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <limits>
#include <utility>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Content/IAssetLoader.h>
#include <Oxygen/Core/Types/ResolvedView.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/SceneSync/RuntimeMotionProducerModule.h>
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
#include <Oxygen/Vortex/Upload/TransientStructuredBuffer.h>

namespace oxygen::vortex {

namespace {

  constexpr std::size_t kMatrixFloatCount = 16U;

  auto BeginResourceManagerFrame(resources::TextureBinder* const texture_binder,
    upload::TransientStructuredBuffer* const current_skinned_pose_buffer,
    upload::TransientStructuredBuffer* const previous_skinned_pose_buffer,
    upload::TransientStructuredBuffer* const current_morph_buffer,
    upload::TransientStructuredBuffer* const previous_morph_buffer,
    upload::TransientStructuredBuffer* const current_material_wpo_buffer,
    upload::TransientStructuredBuffer* const previous_material_wpo_buffer,
    upload::TransientStructuredBuffer* const
      current_motion_vector_status_buffer,
    upload::TransientStructuredBuffer* const
      previous_motion_vector_status_buffer,
    upload::TransientStructuredBuffer* const velocity_draw_metadata_buffer,
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
    if (current_skinned_pose_buffer != nullptr) {
      current_skinned_pose_buffer->OnFrameStart(
        ctx.frame_sequence, ctx.frame_slot);
    }
    if (previous_skinned_pose_buffer != nullptr) {
      previous_skinned_pose_buffer->OnFrameStart(
        ctx.frame_sequence, ctx.frame_slot);
    }
    if (current_morph_buffer != nullptr) {
      current_morph_buffer->OnFrameStart(ctx.frame_sequence, ctx.frame_slot);
    }
    if (previous_morph_buffer != nullptr) {
      previous_morph_buffer->OnFrameStart(ctx.frame_sequence, ctx.frame_slot);
    }
    if (current_material_wpo_buffer != nullptr) {
      current_material_wpo_buffer->OnFrameStart(
        ctx.frame_sequence, ctx.frame_slot);
    }
    if (previous_material_wpo_buffer != nullptr) {
      previous_material_wpo_buffer->OnFrameStart(
        ctx.frame_sequence, ctx.frame_slot);
    }
    if (current_motion_vector_status_buffer != nullptr) {
      current_motion_vector_status_buffer->OnFrameStart(
        ctx.frame_sequence, ctx.frame_slot);
    }
    if (previous_motion_vector_status_buffer != nullptr) {
      previous_motion_vector_status_buffer->OnFrameStart(
        ctx.frame_sequence, ctx.frame_slot);
    }
    if (velocity_draw_metadata_buffer != nullptr) {
      velocity_draw_metadata_buffer->OnFrameStart(
        ctx.frame_sequence, ctx.frame_slot);
    }
  }

  template <typename PublicationT>
  auto PublishTransientPayload(upload::TransientStructuredBuffer* const buffer,
    std::span<const PublicationT> publications, const char* const label)
    -> ShaderVisibleIndex
  {
    if (buffer == nullptr || publications.empty()) {
      return kInvalidShaderVisibleIndex;
    }

    auto allocation
      = buffer->Allocate(static_cast<std::uint32_t>(publications.size()));
    if (!allocation) {
      LOG_F(ERROR, "InitViewsModule failed to allocate {} payload: {}", label,
        allocation.error().message());
      return kInvalidShaderVisibleIndex;
    }
    if (!allocation->TryWriteRange(publications)) {
      LOG_F(ERROR, "InitViewsModule failed to write {} payload", label);
      return kInvalidShaderVisibleIndex;
    }
    return allocation->srv;
  }

  auto ResolveMotionPublicationCapabilities(
    const scenesync::MaterialMotionCapabilityFlags& capabilities,
    const bool has_runtime_payload) -> std::uint32_t
  {
    std::uint32_t flags = 0U;
    if (capabilities.uses_world_position_offset) {
      flags |= static_cast<std::uint32_t>(
        MotionPublicationCapabilityBits::kUsesWorldPositionOffset);
    }
    if (capabilities.uses_motion_vector_world_offset) {
      flags |= static_cast<std::uint32_t>(
        MotionPublicationCapabilityBits::kUsesMotionVectorWorldOffset);
    }
    if (capabilities.uses_temporal_responsiveness) {
      flags |= static_cast<std::uint32_t>(
        MotionPublicationCapabilityBits::kUsesTemporalResponsiveness);
    }
    if (capabilities.has_pixel_animation) {
      flags |= static_cast<std::uint32_t>(
        MotionPublicationCapabilityBits::kHasPixelAnimation);
    }
    if (has_runtime_payload) {
      flags |= static_cast<std::uint32_t>(
        MotionPublicationCapabilityBits::kHasRuntimePayload);
    }
    return flags;
  }

  auto BuildMaterialWpoPublication(
    const scenesync::PublishedRuntimeMaterialMotionState& state)
    -> MaterialWpoPublication
  {
    auto publication = MaterialWpoPublication {
      .contract_hash = state.contract_hash,
      .capability_flags = ResolveMotionPublicationCapabilities(
        state.capabilities, state.has_runtime_wpo_input),
    };
    publication.parameter_block0 = state.wpo_parameter_block0;
    return publication;
  }

  auto BuildMotionVectorStatusPublication(
    const scenesync::PublishedRuntimeMaterialMotionState& state)
    -> MotionVectorStatusPublication
  {
    auto publication = MotionVectorStatusPublication {
      .contract_hash = state.contract_hash,
      .capability_flags = ResolveMotionPublicationCapabilities(
        state.capabilities, state.has_runtime_motion_vector_input),
    };
    publication.parameter_block0 = state.motion_vector_parameter_block0;
    return publication;
  }

  auto PublishVelocityPublications(Renderer& renderer,
    const scenesync::PublishedRuntimeMotionSnapshot* const snapshot,
    const observer_ptr<resources::DrawMetadataEmitter> draw_emitter,
    upload::TransientStructuredBuffer* const current_material_wpo_buffer,
    upload::TransientStructuredBuffer* const previous_material_wpo_buffer,
    upload::TransientStructuredBuffer* const
      current_motion_vector_status_buffer,
    upload::TransientStructuredBuffer* const
      previous_motion_vector_status_buffer,
    upload::TransientStructuredBuffer* const velocity_draw_metadata_buffer,
    InitViewsModule::PreparedSceneViewStorage& storage) -> void
  {
    storage.current_skinned_pose_publications.clear();
    storage.previous_skinned_pose_publications.clear();
    storage.current_morph_publications.clear();
    storage.previous_morph_publications.clear();
    // TODO(vortex/skinned-morph): populate these families once the engine
    // exposes real skinned pose / morph runtime payloads. Today the current
    // engine feature envelope closes Phase 3 without those live streams.
    storage.current_material_wpo_publications.clear();
    storage.previous_material_wpo_publications.clear();
    storage.current_motion_vector_status_publications.clear();
    storage.previous_motion_vector_status_publications.clear();
    storage.velocity_draw_metadata.clear();

    auto& prepared_frame = storage.prepared_frame;
    prepared_frame.current_skinned_pose_publications = {};
    prepared_frame.previous_skinned_pose_publications = {};
    prepared_frame.current_morph_publications = {};
    prepared_frame.previous_morph_publications = {};
    prepared_frame.current_material_wpo_publications = {};
    prepared_frame.previous_material_wpo_publications = {};
    prepared_frame.current_motion_vector_status_publications = {};
    prepared_frame.previous_motion_vector_status_publications = {};
    prepared_frame.velocity_draw_metadata = {};
    prepared_frame.bindless_current_skinned_pose_slot
      = kInvalidShaderVisibleIndex;
    prepared_frame.bindless_previous_skinned_pose_slot
      = kInvalidShaderVisibleIndex;
    prepared_frame.bindless_current_morph_slot = kInvalidShaderVisibleIndex;
    prepared_frame.bindless_previous_morph_slot = kInvalidShaderVisibleIndex;
    prepared_frame.bindless_current_material_wpo_slot
      = kInvalidShaderVisibleIndex;
    prepared_frame.bindless_previous_material_wpo_slot
      = kInvalidShaderVisibleIndex;
    prepared_frame.bindless_current_motion_vector_status_slot
      = kInvalidShaderVisibleIndex;
    prepared_frame.bindless_previous_motion_vector_status_slot
      = kInvalidShaderVisibleIndex;
    prepared_frame.bindless_velocity_draw_metadata_slot
      = kInvalidShaderVisibleIndex;

    if (draw_emitter == nullptr) {
      return;
    }

    const auto sources = draw_emitter->GetVelocityPublicationSources();
    if (sources.empty()) {
      return;
    }

    const auto draw_count = sources.size();
    storage.current_material_wpo_publications.assign(draw_count, {});
    storage.previous_material_wpo_publications.assign(draw_count, {});
    storage.current_motion_vector_status_publications.assign(draw_count, {});
    storage.previous_motion_vector_status_publications.assign(draw_count, {});
    storage.velocity_draw_metadata.assign(draw_count, {});

    auto& deformation_history = renderer.GetDeformationHistoryCache();
    for (std::size_t draw_index = 0U; draw_index < draw_count; ++draw_index) {
      const auto& source = sources[draw_index];
      auto& velocity_metadata = storage.velocity_draw_metadata[draw_index];
      velocity_metadata = VelocityDrawMetadata {};

      if (snapshot == nullptr) {
        continue;
      }

      const auto current_state = snapshot->FindMaterialMotionState(
        scenesync::RuntimeMaterialMotionKey {
          .node_handle = source.node_handle,
          .geometry_asset_key = source.geometry_asset_key,
          .lod_index = source.lod_index,
          .submesh_index = source.submesh_index,
        });
      if (current_state == nullptr) {
        continue;
      }

      const auto current_material_wpo
        = BuildMaterialWpoPublication(*current_state);
      const auto current_motion_vector_status
        = BuildMotionVectorStatusPublication(*current_state);

      const auto material_wpo_history
        = deformation_history.TouchCurrentMaterialWpo(
          internal::RenderMotionIdentityKey {
            .node_handle = source.node_handle,
            .geometry_asset_key = source.geometry_asset_key,
            .lod_index = source.lod_index,
            .submesh_index = source.submesh_index,
            .producer_family = VelocityProducerFamily::kMaterialWpo,
            .contract_hash = current_material_wpo.contract_hash,
          },
          current_material_wpo);
      const auto motion_vector_status_history
        = deformation_history.TouchCurrentMotionVectorStatus(
          internal::RenderMotionIdentityKey {
            .node_handle = source.node_handle,
            .geometry_asset_key = source.geometry_asset_key,
            .lod_index = source.lod_index,
            .submesh_index = source.submesh_index,
            .producer_family = VelocityProducerFamily::kMotionVectorStatus,
            .contract_hash = current_motion_vector_status.contract_hash,
          },
          current_motion_vector_status);

      storage.current_material_wpo_publications[draw_index]
        = material_wpo_history.current;
      storage.previous_material_wpo_publications[draw_index]
        = material_wpo_history.previous;
      storage.current_motion_vector_status_publications[draw_index]
        = motion_vector_status_history.current;
      storage.previous_motion_vector_status_publications[draw_index]
        = motion_vector_status_history.previous;

      velocity_metadata.current_material_wpo_index
        = static_cast<std::uint32_t>(draw_index);
      velocity_metadata.previous_material_wpo_index
        = static_cast<std::uint32_t>(draw_index);
      velocity_metadata.current_motion_vector_status_index
        = static_cast<std::uint32_t>(draw_index);
      velocity_metadata.previous_motion_vector_status_index
        = static_cast<std::uint32_t>(draw_index);
      velocity_metadata.publication_flags |= static_cast<std::uint32_t>(
        VelocityDrawPublicationFlagBits::kCurrentMaterialWpoValid);
      velocity_metadata.publication_flags |= static_cast<std::uint32_t>(
        VelocityDrawPublicationFlagBits::kPreviousMaterialWpoValid);
      velocity_metadata.publication_flags |= static_cast<std::uint32_t>(
        VelocityDrawPublicationFlagBits::kCurrentMotionVectorStatusValid);
      velocity_metadata.publication_flags |= static_cast<std::uint32_t>(
        VelocityDrawPublicationFlagBits::kPreviousMotionVectorStatusValid);
      if (material_wpo_history.previous_valid) {
        velocity_metadata.publication_flags |= static_cast<std::uint32_t>(
          VelocityDrawPublicationFlagBits::kMaterialWpoHistoryValid);
      }
      if (motion_vector_status_history.previous_valid) {
        velocity_metadata.publication_flags |= static_cast<std::uint32_t>(
          VelocityDrawPublicationFlagBits::kMotionVectorStatusHistoryValid);
      }
    }

    prepared_frame.current_material_wpo_publications
      = storage.current_material_wpo_publications;
    prepared_frame.previous_material_wpo_publications
      = storage.previous_material_wpo_publications;
    prepared_frame.current_motion_vector_status_publications
      = storage.current_motion_vector_status_publications;
    prepared_frame.previous_motion_vector_status_publications
      = storage.previous_motion_vector_status_publications;
    prepared_frame.velocity_draw_metadata = storage.velocity_draw_metadata;

    prepared_frame.bindless_current_material_wpo_slot
      = PublishTransientPayload(current_material_wpo_buffer,
        std::span<const MaterialWpoPublication>(
          storage.current_material_wpo_publications.data(),
          storage.current_material_wpo_publications.size()),
        "current material WPO");
    prepared_frame.bindless_previous_material_wpo_slot
      = PublishTransientPayload(previous_material_wpo_buffer,
        std::span<const MaterialWpoPublication>(
          storage.previous_material_wpo_publications.data(),
          storage.previous_material_wpo_publications.size()),
        "previous material WPO");
    prepared_frame.bindless_current_motion_vector_status_slot
      = PublishTransientPayload(current_motion_vector_status_buffer,
        std::span<const MotionVectorStatusPublication>(
          storage.current_motion_vector_status_publications.data(),
          storage.current_motion_vector_status_publications.size()),
        "current motion-vector status");
    prepared_frame.bindless_previous_motion_vector_status_slot
      = PublishTransientPayload(previous_motion_vector_status_buffer,
        std::span<const MotionVectorStatusPublication>(
          storage.previous_motion_vector_status_publications.data(),
          storage.previous_motion_vector_status_publications.size()),
        "previous motion-vector status");
    prepared_frame.bindless_velocity_draw_metadata_slot
      = PublishTransientPayload(velocity_draw_metadata_buffer,
        std::span<const VelocityDrawMetadata>(
          storage.velocity_draw_metadata.data(),
          storage.velocity_draw_metadata.size()),
        "velocity draw metadata");
  }

  auto PublishPreparedSceneFrame(const sceneprep::ScenePrepState& state,
    std::vector<sceneprep::RenderItemData>& render_items,
    PreparedSceneFrame& prepared_frame) -> void
  {
    const auto collected_items = state.CollectedItems();
    render_items.assign(collected_items.begin(), collected_items.end());

    prepared_frame = {};
    prepared_frame.render_items = std::span<const sceneprep::RenderItemData>(
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
        const auto* world_data
          = reinterpret_cast<const float*>(world_matrices.data());
        prepared_frame.world_matrices = {
          world_data,
          world_matrices.size() * kMatrixFloatCount,
        };
      }
      const auto normal_matrices = transform_uploader->GetNormalMatrices();
      if (!normal_matrices.empty()) {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
        const auto* normal_data
          = reinterpret_cast<const float*>(normal_matrices.data());
        prepared_frame.normal_matrices = {
          normal_data,
          normal_matrices.size() * kMatrixFloatCount,
        };
      }
      const auto previous_world_matrices
        = transform_uploader->GetPreviousWorldMatrices();
      if (!previous_world_matrices.empty()) {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
        const auto* previous_world_data
          = reinterpret_cast<const float*>(previous_world_matrices.data());
        prepared_frame.previous_world_matrices = {
          previous_world_data,
          previous_world_matrices.size() * kMatrixFloatCount,
        };
      }
      prepared_frame.bindless_worlds_slot
        = transform_uploader->GetWorldsSrvIndex();
      prepared_frame.bindless_previous_worlds_slot
        = transform_uploader->GetPreviousWorldsSrvIndex();
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
    auto material_binder
      = std::make_unique<resources::MaterialBinder>(observer_ptr { gfx.get() },
        observer_ptr { &uploader }, observer_ptr { &staging_provider },
        observer_ptr { texture_binder_.get() }, asset_loader);
    auto draw_metadata_emitter
      = std::make_unique<resources::DrawMetadataEmitter>(
        observer_ptr { gfx.get() }, observer_ptr { &staging_provider },
        observer_ptr { geometry_uploader.get() },
        observer_ptr { material_binder.get() },
        observer_ptr { &inline_transfers });
    current_skinned_pose_buffer_
      = std::make_unique<upload::TransientStructuredBuffer>(
        observer_ptr { gfx.get() }, staging_provider,
        static_cast<std::uint32_t>(sizeof(SkinnedPosePublication)),
        observer_ptr { &inline_transfers }, "InitViews.CurrentSkinnedPose");
    previous_skinned_pose_buffer_
      = std::make_unique<upload::TransientStructuredBuffer>(
        observer_ptr { gfx.get() }, staging_provider,
        static_cast<std::uint32_t>(sizeof(SkinnedPosePublication)),
        observer_ptr { &inline_transfers }, "InitViews.PreviousSkinnedPose");
    current_morph_buffer_ = std::make_unique<upload::TransientStructuredBuffer>(
      observer_ptr { gfx.get() }, staging_provider,
      static_cast<std::uint32_t>(sizeof(MorphPublication)),
      observer_ptr { &inline_transfers }, "InitViews.CurrentMorph");
    previous_morph_buffer_
      = std::make_unique<upload::TransientStructuredBuffer>(
        observer_ptr { gfx.get() }, staging_provider,
        static_cast<std::uint32_t>(sizeof(MorphPublication)),
        observer_ptr { &inline_transfers }, "InitViews.PreviousMorph");
    current_material_wpo_buffer_
      = std::make_unique<upload::TransientStructuredBuffer>(
        observer_ptr { gfx.get() }, staging_provider,
        static_cast<std::uint32_t>(sizeof(MaterialWpoPublication)),
        observer_ptr { &inline_transfers }, "InitViews.CurrentMaterialWpo");
    previous_material_wpo_buffer_
      = std::make_unique<upload::TransientStructuredBuffer>(
        observer_ptr { gfx.get() }, staging_provider,
        static_cast<std::uint32_t>(sizeof(MaterialWpoPublication)),
        observer_ptr { &inline_transfers }, "InitViews.PreviousMaterialWpo");
    current_motion_vector_status_buffer_
      = std::make_unique<upload::TransientStructuredBuffer>(
        observer_ptr { gfx.get() }, staging_provider,
        static_cast<std::uint32_t>(sizeof(MotionVectorStatusPublication)),
        observer_ptr { &inline_transfers },
        "InitViews.CurrentMotionVectorStatus");
    previous_motion_vector_status_buffer_
      = std::make_unique<upload::TransientStructuredBuffer>(
        observer_ptr { gfx.get() }, staging_provider,
        static_cast<std::uint32_t>(sizeof(MotionVectorStatusPublication)),
        observer_ptr { &inline_transfers },
        "InitViews.PreviousMotionVectorStatus");
    velocity_draw_metadata_buffer_
      = std::make_unique<upload::TransientStructuredBuffer>(
        observer_ptr { gfx.get() }, staging_provider,
        static_cast<std::uint32_t>(sizeof(VelocityDrawMetadata)),
        observer_ptr { &inline_transfers }, "InitViews.VelocityDrawMetadata");

    scene_prep_state_.AttachResourceManagers(std::move(geometry_uploader),
      std::move(transform_uploader), std::move(material_binder),
      std::move(draw_metadata_emitter));
    scene_prep_state_.SetRigidTransformHistory(
      observer_ptr { &renderer_.GetRigidTransformHistoryCache() });
  }

  auto collection_cfg = sceneprep::CreateBasicCollectionConfig();
  auto finalization_cfg = sceneprep::CreateStandardFinalizationConfig();
  scene_prep_ = std::make_unique<sceneprep::ScenePrepPipelineImpl<
    decltype(collection_cfg), decltype(finalization_cfg)>>(
    collection_cfg, finalization_cfg);
}

InitViewsModule::~InitViewsModule() = default;

void InitViewsModule::Execute(RenderContext& ctx, SceneTextures& scene_textures)
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

  BeginResourceManagerFrame(texture_binder_.get(),
    current_skinned_pose_buffer_.get(), previous_skinned_pose_buffer_.get(),
    current_morph_buffer_.get(), previous_morph_buffer_.get(),
    current_material_wpo_buffer_.get(), previous_material_wpo_buffer_.get(),
    current_motion_vector_status_buffer_.get(),
    previous_motion_vector_status_buffer_.get(),
    velocity_draw_metadata_buffer_.get(), scene_prep_state_, ctx);
  scene_prep_->BeginFrameCollection(
    *scene, ctx.frame_sequence, scene_prep_state_);
  const auto runtime_motion_producer
    = renderer_.GetRuntimeMotionProducerModule();
  const auto* runtime_motion_snapshot = runtime_motion_producer != nullptr
    ? runtime_motion_producer->GetPublishedSnapshot(
        observer_ptr { scene.get() })
    : nullptr;

  for (const auto& view_entry : ctx.frame_views) {
    if (!view_entry.is_scene_view || view_entry.resolved_view == nullptr) {
      continue;
    }

    auto [it, _] = prepared_views_.try_emplace(view_entry.view_id);
    auto& storage = it->second;

    scene_prep_->PrepareView(
      *scene, *view_entry.resolved_view, ctx.frame_sequence, scene_prep_state_);
    scene_prep_->FinalizeView(scene_prep_state_);
    PublishPreparedSceneFrame(
      scene_prep_state_, storage.render_items, storage.prepared_frame);
    PublishVelocityPublications(renderer_, runtime_motion_snapshot,
      scene_prep_state_.GetDrawMetadataEmitter(),
      current_material_wpo_buffer_.get(), previous_material_wpo_buffer_.get(),
      current_motion_vector_status_buffer_.get(),
      previous_motion_vector_status_buffer_.get(),
      velocity_draw_metadata_buffer_.get(), storage);
    storage.published = true;
  }

  std::erase_if(prepared_views_,
    [](const auto& entry) -> bool { return !entry.second.published; });
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
