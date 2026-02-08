//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include <fmt/format.h>
#include <glm/common.hpp>
#include <glm/geometric.hpp>

#include "PreparedSceneFrame.h"
#include "Resources/DrawMetadataEmitter.h"
#include "Resources/GeometryUploader.h"
#include "Resources/MaterialBinder.h"
#include "Resources/TransformUploader.h"
#include "Types/DrawMetadata.h"
#include "Types/SunState.h"
#include "Upload/StagingProvider.h"
#include "Upload/UploadPolicy.h"
#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Config/RendererConfig.h>
#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Core/Types/ResolvedView.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Core/Types/ViewResolver.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Engine/AsyncEngine.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Graphics/Common/GpuEventScope.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/Queues.h>
#include <Oxygen/Graphics/Common/Surface.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/QueueRole.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/Renderer/Internal/BrdfLutManager.h>
#include <Oxygen/Renderer/Internal/EnvironmentDynamicDataManager.h>
#include <Oxygen/Renderer/Internal/EnvironmentStaticDataManager.h>
#include <Oxygen/Renderer/Internal/GpuDebugManager.h>
#include <Oxygen/Renderer/Internal/IblManager.h>
#include <Oxygen/Renderer/Internal/SceneConstantsManager.h>
#include <Oxygen/Renderer/Internal/SkyAtmosphereLutManager.h>
#include <Oxygen/Renderer/Internal/SunResolver.h>
#include <Oxygen/Renderer/LightManager.h>
#include <Oxygen/Renderer/Passes/CompositingPass.h>
#include <Oxygen/Renderer/Passes/IblComputePass.h>
#include <Oxygen/Renderer/Passes/SkyAtmosphereLutComputePass.h>
#include <Oxygen/Renderer/Passes/SkyCapturePass.h>
#include <Oxygen/Renderer/RenderContext.h>
#include <Oxygen/Renderer/RenderContextPool.h>
#include <Oxygen/Renderer/Renderer.h>
#include <Oxygen/Renderer/RendererTag.h>
#include <Oxygen/Renderer/Resources/TextureBinder.h>
#include <Oxygen/Renderer/ScenePrep/CollectionConfig.h>
#include <Oxygen/Renderer/ScenePrep/FinalizationConfig.h>
#include <Oxygen/Renderer/ScenePrep/ScenePrepPipeline.h>
#include <Oxygen/Renderer/ScenePrep/ScenePrepState.h>
#include <Oxygen/Renderer/Types/CompositingTask.h>
#include <Oxygen/Renderer/Types/EnvironmentDynamicData.h>
#include <Oxygen/Renderer/Types/MaterialConstants.h>
#include <Oxygen/Renderer/Types/SceneConstants.h>
#include <Oxygen/Renderer/Upload/InlineTransfersCoordinator.h>
#include <Oxygen/Renderer/Upload/UploadCoordinator.h>

namespace {
auto ResolveViewOutputTexture(const oxygen::engine::FrameContext& context,
  const oxygen::ViewId view_id) -> std::shared_ptr<oxygen::graphics::Texture>
{
  const auto& view_ctx = context.GetViewContext(view_id);
  if (!view_ctx.output) {
    return {};
  }
  const auto& fb_desc = view_ctx.output->GetDescriptor();
  if (fb_desc.color_attachments.empty()
    || !fb_desc.color_attachments[0].texture) {
    return {};
  }
  return fb_desc.color_attachments[0].texture;
}

auto TrackCompositionFramebuffer(oxygen::graphics::CommandRecorder& recorder,
  const oxygen::graphics::Framebuffer& framebuffer) -> void
{
  const auto& fb_desc = framebuffer.GetDescriptor();
  for (const auto& attachment : fb_desc.color_attachments) {
    if (!attachment.texture) {
      continue;
    }
    auto initial = attachment.texture->GetDescriptor().initial_state;
    if (initial == oxygen::graphics::ResourceStates::kUnknown
      || initial == oxygen::graphics::ResourceStates::kUndefined) {
      initial = oxygen::graphics::ResourceStates::kPresent;
    }
    recorder.BeginTrackingResourceState(*attachment.texture, initial, true);
  }

  if (fb_desc.depth_attachment.texture) {
    recorder.BeginTrackingResourceState(*fb_desc.depth_attachment.texture,
      oxygen::graphics::ResourceStates::kDepthWrite, true);
    recorder.FlushBarriers();
  }
}

auto CopyTextureToRegion(oxygen::graphics::CommandRecorder& recorder,
  oxygen::graphics::Texture& source, oxygen::graphics::Texture& backbuffer,
  const oxygen::ViewPort& viewport) -> void
{
  recorder.BeginTrackingResourceState(
    source, oxygen::graphics::ResourceStates::kCommon);
  recorder.RequireResourceState(
    source, oxygen::graphics::ResourceStates::kCopySource);
  recorder.RequireResourceState(
    backbuffer, oxygen::graphics::ResourceStates::kCopyDest);
  recorder.FlushBarriers();

  const auto& src_desc = source.GetDescriptor();
  const auto& dst_desc = backbuffer.GetDescriptor();

  const uint32_t dst_x = static_cast<uint32_t>(
    std::clamp(viewport.top_left_x, 0.0F, static_cast<float>(dst_desc.width)));
  const uint32_t dst_y = static_cast<uint32_t>(
    std::clamp(viewport.top_left_y, 0.0F, static_cast<float>(dst_desc.height)));

  const uint32_t max_dst_w
    = dst_desc.width > dst_x ? dst_desc.width - dst_x : 0U;
  const uint32_t max_dst_h
    = dst_desc.height > dst_y ? dst_desc.height - dst_y : 0U;

  const uint32_t copy_width = std::min(src_desc.width, max_dst_w);
  const uint32_t copy_height = std::min(src_desc.height, max_dst_h);

  if (copy_width == 0 || copy_height == 0) {
    return;
  }

  const oxygen::graphics::TextureSlice src_slice {
    .x = 0,
    .y = 0,
    .z = 0,
    .width = copy_width,
    .height = copy_height,
    .depth = 1,
  };

  const oxygen::graphics::TextureSlice dst_slice {
    .x = dst_x,
    .y = dst_y,
    .z = 0,
    .width = copy_width,
    .height = copy_height,
    .depth = 1,
  };

  constexpr oxygen::graphics::TextureSubResourceSet subresources {
    .base_mip_level = 0,
    .num_mip_levels = 1,
    .base_array_slice = 0,
    .num_array_slices = 1,
  };

  recorder.CopyTexture(
    source, src_slice, subresources, backbuffer, dst_slice, subresources);
  recorder.RequireResourceState(
    source, oxygen::graphics::ResourceStates::kCommon);
  recorder.FlushBarriers();
}
} // namespace
#include <Oxygen/Scene/Environment/PostProcessVolume.h>
#include <Oxygen/Scene/Environment/SceneEnvironment.h>
#include <Oxygen/Scene/Environment/SkyAtmosphere.h>
#include <Oxygen/Scene/Scene.h>

// Implementation of RendererTagFactory. Provides access to RendererTag
// capability tokens, only from the engine core. When building tests, allow
// tests to override by defining OXYGEN_ENGINE_TESTING.
#if !defined(OXYGEN_ENGINE_TESTING)
namespace oxygen::renderer::internal {
auto RendererTagFactory::Get() noexcept -> RendererTag
{
  return RendererTag {};
}
} // namespace oxygen::renderer::internal
#endif

using oxygen::Graphics;
using oxygen::data::Mesh;
using oxygen::data::detail::IndexType;
using oxygen::engine::MaterialConstants;
using oxygen::engine::Renderer;
using oxygen::graphics::Buffer;
using oxygen::graphics::BufferDesc;
using oxygen::graphics::BufferMemory;
using oxygen::graphics::BufferUsage;
using oxygen::graphics::ResourceStates;
using oxygen::graphics::SingleQueueStrategy;

//===----------------------------------------------------------------------===//
// Renderer Implementation
//===----------------------------------------------------------------------===//

Renderer::Renderer(std::weak_ptr<Graphics> graphics, RendererConfig config)
  : gfx_weak_(std::move(graphics))
  , scene_prep_(std::make_unique<sceneprep::ScenePrepPipelineImpl<
        decltype(sceneprep::CreateBasicCollectionConfig()),
        decltype(sceneprep::CreateStandardFinalizationConfig())>>(
      sceneprep::CreateBasicCollectionConfig(),
      sceneprep::CreateStandardFinalizationConfig()))
{
  LOG_F(
    2, "Renderer::Renderer [this={}] - constructor", static_cast<void*>(this));

  CHECK_F(!gfx_weak_.expired(), "Renderer constructed with expired Graphics");
  auto gfx = gfx_weak_.lock();

  // Require a non-empty upload queue key in the renderer configuration.
  CHECK_F(!config.upload_queue_key.empty(),
    "RendererConfig.upload_queue_key must not be empty");

  // Build upload policy and honour configured upload queue from Renderer
  // configuration.
  auto policy = upload::DefaultUploadPolicy();
  policy.upload_queue_key = graphics::QueueKey { config.upload_queue_key };

  uploader_ = std::make_unique<upload::UploadCoordinator>(
    observer_ptr { gfx.get() }, policy);
  upload_staging_provider_
    = uploader_->CreateRingBufferStaging(frame::kFramesInFlight, 16,
      upload::kDefaultRingBufferStagingSlack, "Renderer.UploadStaging");

  inline_transfers_ = std::make_unique<upload::InlineTransfersCoordinator>(
    observer_ptr { gfx.get() });
  inline_staging_provider_
    = uploader_->CreateRingBufferStaging(frame::kFramesInFlight, 16,
      upload::kDefaultRingBufferStagingSlack, "Renderer.InlineStaging");
  inline_transfers_->RegisterProvider(inline_staging_provider_);

  // Initialize the render-context pool helper used to claim per-frame
  // render contexts during PreRender/Render phases.
  render_context_pool_ = std::make_unique<RenderContextPool>();
}

Renderer::~Renderer()
{
  sky_capture_pass_.reset();
  sky_atmo_lut_compute_pass_.reset();
  ibl_compute_pass_.reset();
  env_dynamic_manager_.reset();
  brdf_lut_manager_.reset();
  env_static_manager_.reset();
  scene_const_manager_.reset();
  scene_prep_state_.reset();
  uploader_.reset();
  upload_staging_provider_.reset();
  inline_transfers_.reset();
  inline_staging_provider_.reset();
}

auto Renderer::OnAttached(observer_ptr<AsyncEngine> engine) noexcept -> bool
{
  DCHECK_NOTNULL_F(engine);

  asset_loader_ = engine->GetAssetLoader();
  if (!asset_loader_) {
    LOG_F(ERROR, "AssetLoader unavailable; cannot initialize TextureBinder");
    return false;
  }

  if (!scene_prep_state_) {
    auto gfx = gfx_weak_.lock();
    if (!gfx) {
      LOG_F(ERROR, "Graphics expired during Renderer::OnAttached");
      return false;
    }

    auto geom_uploader
      = std::make_unique<renderer::resources::GeometryUploader>(
        observer_ptr { gfx.get() }, observer_ptr { uploader_.get() },
        observer_ptr { upload_staging_provider_.get() },
        observer_ptr { asset_loader_.get() });
    auto xform_uploader
      = std::make_unique<renderer::resources::TransformUploader>(
        observer_ptr { gfx.get() },
        observer_ptr { inline_staging_provider_.get() },
        observer_ptr { inline_transfers_.get() });

    auto texture_binder = std::make_unique<renderer::resources::TextureBinder>(
      observer_ptr { gfx.get() },
      observer_ptr { upload_staging_provider_.get() },
      observer_ptr { uploader_.get() }, observer_ptr { asset_loader_.get() });

    auto mat_binder = std::make_unique<renderer::resources::MaterialBinder>(
      observer_ptr { gfx.get() }, observer_ptr { uploader_.get() },
      observer_ptr { upload_staging_provider_.get() },
      observer_ptr { texture_binder.get() });

    auto emitter = std::make_unique<renderer::resources::DrawMetadataEmitter>(
      observer_ptr { gfx.get() },
      observer_ptr { inline_staging_provider_.get() },
      observer_ptr { geom_uploader.get() }, observer_ptr { mat_binder.get() },
      observer_ptr { inline_transfers_.get() });

    auto light_manager
      = std::make_unique<renderer::LightManager>(observer_ptr { gfx.get() },
        observer_ptr { inline_staging_provider_.get() },
        observer_ptr { inline_transfers_.get() });

    scene_prep_state_ = std::make_unique<sceneprep::ScenePrepState>(
      std::move(geom_uploader), std::move(xform_uploader),
      std::move(mat_binder), std::move(emitter), std::move(light_manager));
    texture_binder_ = std::move(texture_binder);

    // Initialize scene constants manager for per-view, per-slot Upload heap
    // buffers.
    scene_const_manager_ = std::make_unique<internal::SceneConstantsManager>(
      observer_ptr { gfx.get() },
      static_cast<std::uint32_t>(sizeof(SceneConstants::GpuData)));

    // Initialize environment dynamic data manager for b3 CBV (cluster slots,
    // exposure, etc.).
    env_dynamic_manager_
      = std::make_unique<internal::EnvironmentDynamicDataManager>(
        observer_ptr { gfx.get() });

    // Precompute and bind BRDF integration LUTs (bindless SRV slot provider).
    brdf_lut_manager_ = std::make_unique<internal::BrdfLutManager>(
      observer_ptr { gfx.get() }, observer_ptr { uploader_.get() },
      observer_ptr { upload_staging_provider_.get() });

    // Create sky atmosphere LUT manager for transmittance and sky-view LUTs.
    sky_atmo_lut_manager_ = std::make_unique<internal::SkyAtmosphereLutManager>(
      observer_ptr { gfx.get() }, observer_ptr { uploader_.get() },
      observer_ptr { upload_staging_provider_.get() });

    // Create sky capture pass
    sky_capture_pass_config_ = std::make_shared<SkyCapturePassConfig>();
    sky_capture_pass_config_->resolution = 128u;
    sky_capture_pass_ = std::make_unique<SkyCapturePass>(
      observer_ptr { gfx.get() }, sky_capture_pass_config_);

    // Create sky atmosphere LUT compute pass (explicitly executed before sky
    // capture so the capture never runs against stale LUTs).
    sky_atmo_lut_compute_pass_config_
      = std::make_shared<SkyAtmosphereLutComputePassConfig>();
    sky_atmo_lut_compute_pass_config_->lut_manager
      = observer_ptr { sky_atmo_lut_manager_.get() };
    sky_atmo_lut_compute_pass_ = std::make_unique<SkyAtmosphereLutComputePass>(
      observer_ptr { gfx.get() }, sky_atmo_lut_compute_pass_config_);

    // Initialize IBL Manager
    ibl_manager_
      = std::make_unique<internal::IblManager>(observer_ptr { gfx.get() });

    // Initialize environment static data single-owner manager (bindless SRV).
    // TextureBinder is passed directly to the constructor for cubemap
    // resolution.
    env_static_manager_
      = std::make_unique<internal::EnvironmentStaticDataManager>(
        observer_ptr { gfx.get() }, observer_ptr { texture_binder_.get() },
        observer_ptr { brdf_lut_manager_.get() },
        observer_ptr { ibl_manager_.get() },
        observer_ptr { sky_atmo_lut_manager_.get() },
        observer_ptr { sky_capture_pass_.get() });

    ibl_compute_pass_ = std::make_unique<IblComputePass>("IblComputePass");

    // Initialize GPU debug manager.
    gpu_debug_manager_
      = std::make_unique<internal::GpuDebugManager>(observer_ptr { gfx.get() });
  }
  return true;
}

auto Renderer::OnShutdown() noexcept -> void
{
  {
    std::lock_guard lock(composition_mutex_);
    composition_submission_.reset();
    composition_surface_.reset();
  }

  compositing_pass_.reset();
  compositing_pass_config_.reset();
}

auto Renderer::GetGraphics() -> std::shared_ptr<Graphics>
{
  auto graphics_ptr = gfx_weak_.lock();
  if (!graphics_ptr) {
    throw std::runtime_error("Graphics expired in Renderer::GetGraphics");
  }
  return graphics_ptr;
}

auto Renderer::GetStagingProvider() -> upload::StagingProvider&
{
  DCHECK_NOTNULL_F(
    inline_staging_provider_, "StagingProvider is not initialized");
  return *inline_staging_provider_;
}

auto Renderer::GetInlineTransfersCoordinator()
  -> upload::InlineTransfersCoordinator&
{
  DCHECK_NOTNULL_F(
    inline_transfers_, "InlineTransfersCoordinator is not initialized");
  return *inline_transfers_;
}

auto Renderer::GetLightManager() const noexcept
  -> observer_ptr<renderer::LightManager>
{
  if (!scene_prep_state_) {
    return nullptr;
  }
  return scene_prep_state_->GetLightManager();
}

auto Renderer::GetSkyAtmosphereLutManager() const noexcept
  -> observer_ptr<internal::SkyAtmosphereLutManager>
{
  return observer_ptr { sky_atmo_lut_manager_.get() };
}

auto Renderer::GetEnvironmentStaticDataManager() const noexcept
  -> observer_ptr<internal::EnvironmentStaticDataManager>
{
  return observer_ptr { env_static_manager_.get() };
}

auto Renderer::GetIblManager() const noexcept
  -> observer_ptr<internal::IblManager>
{
  return observer_ptr { ibl_manager_.get() };
}

auto Renderer::GetIblComputePass() const noexcept
  -> observer_ptr<IblComputePass>
{
  return observer_ptr { ibl_compute_pass_.get() };
}

auto Renderer::DumpEstimatedTextureMemory(const std::size_t top_n) const -> void
{
  if (!texture_binder_) {
    LOG_F(
      WARNING, "TextureBinder is not initialized; cannot dump texture memory");
    return;
  }

  texture_binder_->DumpEstimatedTextureMemory(top_n);
}

//=== Debug Overrides ===-----------------------------------------------------//

auto Renderer::RequestIblRegeneration() noexcept -> void
{
  if (!ibl_compute_pass_) {
    return;
  }

  ibl_compute_pass_->RequestRegenerationOnce();
}

auto Renderer::RequestSkyCapture() noexcept -> void
{
  sky_capture_requested_ = true;
  if (sky_capture_pass_) {
    sky_capture_pass_->MarkDirty();
  }
}

auto Renderer::SetAtmosphereDebugFlags(const uint32_t flags) -> void
{
  atmosphere_debug_flags_ = flags;

  // Forward flags to LUT manager so the sky-view LUT can use them during
  // compute dispatch. The LUT manager will mark itself dirty if flags change.
  if (sky_atmo_lut_manager_) {
    sky_atmo_lut_manager_->SetAtmosphereFlags(flags);
  }
}

auto Renderer::GetAtmosphereDebugFlags() const noexcept -> uint32_t
{
  return atmosphere_debug_flags_;
}

/*!
 Returns the active render context for the current frame.

 @return Observer pointer to the active render context, or null if the
   renderer is outside a frame scope.

### Performance Characteristics

 - Time Complexity: O(1)
 - Memory: None
 - Optimization: None
*/
auto Renderer::OverrideMaterialUvTransform(const data::MaterialAsset& material,
  const glm::vec2 uv_scale, const glm::vec2 uv_offset) -> bool
{
  if (!scene_prep_state_) {
    return false;
  }

  const auto materials = scene_prep_state_->GetMaterialBinder();
  if (!materials) {
    return false;
  }

  return materials->OverrideUvTransform(material, uv_scale, uv_offset);
}

auto Renderer::RegisterView(
  ViewId view_id, ViewResolver resolver, RenderGraphFactory factory) -> void
{
  std::unique_lock lock(view_registration_mutex_);
  view_resolvers_.insert_or_assign(view_id, std::move(resolver));
  render_graphs_.insert_or_assign(view_id, std::move(factory));
  DLOG_F(1, "RegisterView: view_id={}, total_views={}", view_id.get(),
    render_graphs_.size());
}

auto Renderer::UnregisterView(ViewId view_id) -> void
{
  std::size_t removed_resolver = 0;
  std::size_t removed_graph = 0;
  {
    std::unique_lock lock(view_registration_mutex_);
    removed_resolver = view_resolvers_.erase(view_id);
    removed_graph = render_graphs_.erase(view_id);
  }

  DLOG_F(1,
    "UnregisterView: view_id={}, removed_resolver={}, removed_factory={}",
    view_id.get(), removed_resolver, removed_graph);

  std::size_t pending_size = 0;
  {
    std::lock_guard lock(pending_cleanup_mutex_);
    pending_cleanup_.insert(view_id);
    pending_size = pending_cleanup_.size();
  }

  DLOG_F(1, "UnregisterView: pending_cleanup_count={}", pending_size);

  {
    std::unique_lock state_lock(view_state_mutex_);
    view_ready_states_.erase(view_id);
  }
}

auto Renderer::RegisterComposition(CompositionSubmission submission,
  std::shared_ptr<graphics::Surface> target_surface) -> void
{
  std::lock_guard lock(composition_mutex_);
  composition_submission_ = std::move(submission);
  composition_surface_ = std::move(target_surface);
}

auto Renderer::IsViewReady(ViewId view_id) const -> bool
{
  std::shared_lock lock(view_state_mutex_);
  const auto it = view_ready_states_.find(view_id);
  return it != view_ready_states_.end() && it->second;
}

auto Renderer::OnPreRender(observer_ptr<FrameContext> context) -> co::Co<>
{
  LOG_SCOPE_FUNCTION(2);

  const auto dt = context->GetModuleTimingData().game_delta_time;
  const auto dt_ns = dt.get();
  const auto dt_seconds
    = std::chrono::duration_cast<std::chrono::duration<float>>(dt_ns).count();
  if (dt_seconds > 0.0F) {
    last_frame_dt_seconds_ = dt_seconds;
  } else {
    last_frame_dt_seconds_ = 1.0F / 60.0F;
  }

  DrainPendingViewCleanup("OnPreRender");

  {
    std::shared_lock lock(view_registration_mutex_);
    if (render_graphs_.empty()) {
      DLOG_F(WARNING, "no render graphs registered; skipping");
      co_return;
    }

    if (view_resolvers_.empty()) {
      DLOG_F(WARNING, "no view resolvers registered; skipping");
      co_return;
    }
  }

  // Failing to acquire a slot will throw, and drop the frame.
  render_context_
    = observer_ptr { &render_context_pool_->Acquire(context->GetFrameSlot()) };

  {
    auto graphics_p = gfx_weak_.lock();
    if (!graphics_p) {
      LOG_F(ERROR, "Graphics expired during OnPreRender");
      co_return;
    }
    render_context_->SetRenderer(this, graphics_p.get());
  }

  render_context_->scene = observer_ptr<const scene::Scene> {
    context->GetScene().get(),
  };

  // Clear the per-frame and per-view state (per-frame caches are refreshed
  // at the start of PreRender). Deferred cleanup of unregistered views is
  // performed at frame end (OnFrameEnd) to avoid destroying entries while
  // other modules may add registrations during the frame start.
  {
    std::unique_lock state_lock(view_state_mutex_);
    view_ready_states_.clear();
  }
  resolved_views_.clear();
  prepared_frames_.clear();
  per_view_storage_.clear();

  // Build and publish environment static data once per frame.
  if (env_static_manager_) {
    auto tag = oxygen::renderer::internal::RendererTagFactory::Get();
    env_static_manager_->UpdateIfNeeded(tag, *render_context_);
    scene_const_cpu_.SetBindlessEnvironmentStaticSlot(
      BindlessEnvironmentStaticSlot(env_static_manager_->GetSrvIndex()),
      SceneConstants::kRenderer);
  }

  // Iterate all views registered in FrameContext and prepare each one
  auto views_range = context->GetViews();
  bool first = true;

  for (const auto& view_ref : views_range) {
    const auto& view_ctx = view_ref.get();
    DLOG_SCOPE_F(2,
      fmt::format(
        "View {} ({})", nostd::to_string(view_ctx.id), view_ctx.metadata.name)
        .c_str());
    try {
      ViewResolver resolver_copy;
      {
        std::shared_lock lock(view_registration_mutex_);
        const auto resolver_it = view_resolvers_.find(view_ctx.id);
        if (resolver_it == view_resolvers_.end()) {
          LOG_F(2, "View {} has no resolver; skipping", view_ctx.id.get());
          continue;
        }
        resolver_copy = resolver_it->second; // copy function
      }

      // Invoke resolver outside of the registration lock to avoid locking
      // user-provided code paths.
      const auto resolved = resolver_copy(view_ctx);

      // Cache the resolved view for use in OnRender
      resolved_views_.insert_or_assign(view_ctx.id, resolved);

      // Build frame data for this view (scene prep, culling, draw list)
      [[maybe_unused]] const auto draw_count
        = RunScenePrep(view_ctx.id, resolved, *context, first);
      first = false;

      DLOG_F(2, "view prepared with {} draws", draw_count);

      // Mark view as ready for rendering
      // Note: ViewId not directly available from GetViews() range
      // Apps should use ViewContext metadata to identify views if needed

    } catch (const std::exception& ex) {
      LOG_F(WARNING, "-failed- : {}", ex.what());
    }
  }

  LOG_SCOPE_F(2, "Populating renderer-level scene constants");

  if (const auto transforms = scene_prep_state_->GetTransformUploader()) {
    const auto worlds_srv = transforms->GetWorldsSrvIndex();
    const auto normals_srv = transforms->GetNormalsSrvIndex();
    DLOG_F(3, "Worlds: {}", worlds_srv);
    DLOG_F(3, "Normals: {}", normals_srv);
    scene_const_cpu_.SetBindlessWorldsSlot(
      BindlessWorldsSlot(worlds_srv), SceneConstants::kRenderer);
    scene_const_cpu_.SetBindlessNormalMatricesSlot(
      BindlessNormalsSlot(normals_srv), SceneConstants::kRenderer);
  }

  if (const auto materials = scene_prep_state_->GetMaterialBinder()) {
    const auto materials_srv = materials->GetMaterialsSrvIndex();
    DLOG_F(3, "Materials: {}", materials_srv);
    scene_const_cpu_.SetBindlessMaterialConstantsSlot(
      BindlessMaterialConstantsSlot(materials_srv), SceneConstants::kRenderer);
  }

  if (auto emitter = scene_prep_state_->GetDrawMetadataEmitter()) {
    const auto draw_metadata_srv = emitter->GetDrawMetadataSrvIndex();
    DLOG_F(3, "Draw Metadata: {}", draw_metadata_srv);
    scene_const_cpu_.SetBindlessDrawMetadataSlot(
      BindlessDrawMetadataSlot(draw_metadata_srv), SceneConstants::kRenderer);

    // Set instance data slot for GPU instancing
    const auto instance_data_srv = emitter->GetInstanceDataSrvIndex();
    DLOG_F(3, "Instance Data: {}", instance_data_srv);
    scene_const_cpu_.SetBindlessInstanceDataSlot(
      BindlessInstanceDataSlot(instance_data_srv), SceneConstants::kRenderer);
  }

  if (auto light_manager = scene_prep_state_->GetLightManager()) {
    const auto dir_srv = light_manager->GetDirectionalLightsSrvIndex();
    const auto dir_shadows_srv = light_manager->GetDirectionalShadowsSrvIndex();
    const auto pos_srv = light_manager->GetPositionalLightsSrvIndex();

    DLOG_F(3, "Directional Lights: {}", dir_srv);
    DLOG_F(3, "Directional Shadows: {}", dir_shadows_srv);
    DLOG_F(3, "Positional Lights: {}", pos_srv);

    scene_const_cpu_.SetBindlessDirectionalLightsSlot(
      BindlessDirectionalLightsSlot(dir_srv), SceneConstants::kRenderer);
    scene_const_cpu_.SetBindlessDirectionalShadowsSlot(
      BindlessDirectionalShadowsSlot(dir_shadows_srv),
      SceneConstants::kRenderer);
    scene_const_cpu_.SetBindlessPositionalLightsSlot(
      BindlessPositionalLightsSlot(pos_srv), SceneConstants::kRenderer);
  }

  co_return;
}

auto Renderer::OnRender(observer_ptr<FrameContext> context) -> co::Co<>
{
  LOG_SCOPE_FUNCTION(2);

  // Early exit if no render render context
  if (!render_context_) {
    DLOG_F(WARNING, "no render context available; skipping");
    co_return;
  }

  auto graphics_ptr = gfx_weak_.lock();
  if (!graphics_ptr) {
    LOG_F(WARNING, "Graphics expired; skipping");
    co_return;
  }
  auto& graphics = *graphics_ptr;

  // Iterate all views and execute their registered render graphs.
  // Take a snapshot of the registered factories under lock so
  // UnregisterView() can safely mutate the underlying containers
  // without invalidating our iteration.
  std::vector<std::pair<ViewId, RenderGraphFactory>> graphs_snapshot;
  {
    std::shared_lock lock(view_registration_mutex_);
    graphs_snapshot.reserve(render_graphs_.size());
    for (const auto& kv : render_graphs_) {
      graphs_snapshot.emplace_back(kv.first, kv.second);
    }
  }

  bool ran_ibl_compute_this_frame = false;
  bool ran_sky_capture_this_frame = false;
  bool ran_atmo_lut_compute_this_frame = false;

  for (const auto& [view_id, factory] : graphs_snapshot) {
    DLOG_SCOPE_F(2, fmt::format("View {}", nostd::to_string(view_id)).c_str());

    // Mark view as not ready initially
    {
      std::unique_lock state_lock(view_state_mutex_);
      view_ready_states_[view_id] = false;
    }

    try {
      // Get the ViewContext for this view to access output framebuffer
      const auto& view_ctx = context->GetViewContext(view_id);

      // Skip if no output framebuffer assigned
      if (!view_ctx.output) {
        LOG_F(WARNING, "View {} has no output framebuffer; skipping",
          view_id.get());
        continue;
      }

      // Acquire command recorder for this view
      auto recorder_ptr = AcquireRecorderForView(view_id, graphics);
      if (!recorder_ptr) {
        LOG_F(ERROR, "Could not acquire recorder for view {}; skipping",
          view_id.get());
        continue;
      }
      auto recorder = recorder_ptr.get();

      graphics::GpuEventScope view_scope(
        *recorder, fmt::format("View {}", view_id.get()));

      auto update_view_state = [&](ViewId view_id, bool success) -> void {
        std::unique_lock state_lock(view_state_mutex_);
        view_ready_states_[view_id] = success;
      };

      // --- STEP 1: Wire all constants and context data ---
      // This MUST happen before any pass (SkyCapture, IBL, or Graph) runs.
      if (!PrepareAndWireSceneConstantsForView(
            view_id, *context, *render_context_)) {
        // Failure already logged inside helper; mark the view failed and
        // skip this view's render graph.
        update_view_state(view_id, false);
        continue;
      }

      // --- STEP 2: Run environment update passes ---
      // Regenerate atmosphere LUTs first (if needed) so sky capture and IBL
      // can run in the same frame against the freshly swapped LUT buffers.
      if (!ran_atmo_lut_compute_this_frame && sky_atmo_lut_compute_pass_
        && sky_atmo_lut_manager_) {
        bool atmo_enabled = false;
        if (const auto scene = render_context_->scene) {
          if (const auto env = scene->GetEnvironment()) {
            if (const auto atmo
              = env->TryGetSystem<scene::environment::SkyAtmosphere>();
              atmo && atmo->IsEnabled()) {
              atmo_enabled = true;
            }
          }
        }

        if (atmo_enabled
          && (sky_atmo_lut_manager_->IsDirty()
            || !sky_atmo_lut_manager_->HasBeenGenerated())) {
          try {
            graphics::GpuEventScope lut_scope(
              *recorder, "Atmosphere LUT Compute");
            co_await sky_atmo_lut_compute_pass_->PrepareResources(
              *render_context_, *recorder);
            co_await sky_atmo_lut_compute_pass_->Execute(
              *render_context_, *recorder);
          } catch (const std::exception& ex) {
            LOG_F(ERROR, "SkyAtmosphereLutComputePass failed: {}", ex.what());
          }
        }

        ran_atmo_lut_compute_this_frame = true;
      }

      // Check if sky needs re-capture.
      // Run this BEFORE SetupFramebufferForView to avoid touching main scene
      // targets while performing the internal capture.
      if (!ran_sky_capture_this_frame && sky_capture_pass_) {
        bool needs_capture
          = sky_capture_requested_ || !sky_capture_pass_->IsCaptured();

        if (sky_atmo_lut_manager_) {
          const auto current_atmo_gen = sky_atmo_lut_manager_->GetGeneration();
          if (current_atmo_gen != last_atmo_generation_) {
            needs_capture = true;
          }

          // If atmosphere is enabled, we MUST have LUTs generated/clean
          // before capturing the sky. LUT compute is executed just above in
          // the same step to make interactive sky updates stable.
          bool atmo_enabled = false;
          if (const auto scene = render_context_->scene) {
            if (const auto env = scene->GetEnvironment()) {
              if (const auto atmo
                = env->TryGetSystem<scene::environment::SkyAtmosphere>();
                atmo && atmo->IsEnabled()) {
                atmo_enabled = true;
              }
            }
          }

          if (atmo_enabled && needs_capture
            && (sky_atmo_lut_manager_->IsDirty()
              || !sky_atmo_lut_manager_->HasBeenGenerated())) {
            needs_capture = false;
          }
        }

        bool capture_completed = false;
        if (needs_capture) {
          try {
            graphics::GpuEventScope capture_scope(*recorder, "Sky Capture");
            co_await sky_capture_pass_->PrepareResources(
              *render_context_, *recorder);
            co_await sky_capture_pass_->Execute(*render_context_, *recorder);
            capture_completed = true;

            // Force IBL to re-filter the new capture.
            if (ibl_compute_pass_) {
              ibl_compute_pass_->RequestRegenerationOnce();
            }
          } catch (const std::exception& ex) {
            LOG_F(ERROR, "SkyCapturePass failed: {}", ex.what());
          }
        }
        if (capture_completed) {
          sky_capture_requested_ = false;
          if (sky_atmo_lut_manager_) {
            last_atmo_generation_ = sky_atmo_lut_manager_->GetGeneration();
          }
        }
        ran_sky_capture_this_frame = true;
      }

      if (!ran_ibl_compute_this_frame && ibl_compute_pass_) {
        bool needs_ibl = false;
        if (auto env_static = GetEnvironmentStaticDataManager()) {
          // If the manager flagged a param change, or a capture just happened.
          if (env_static->IsIblRegenerationRequested()) {
            needs_ibl = true;
            env_static->MarkIblRegenerationClean();
          }
        }

        if (needs_ibl) {
          ibl_compute_pass_->RequestRegenerationOnce();
        }

        try {
          graphics::GpuEventScope ibl_scope(*recorder, "IBL Compute");
          co_await ibl_compute_pass_->PrepareResources(
            *render_context_, *recorder);
          co_await ibl_compute_pass_->Execute(*render_context_, *recorder);
        } catch (const std::exception& ex) {
          LOG_F(ERROR, "IblComputePass failed: {}", ex.what());
        }
        ran_ibl_compute_this_frame = true;
      }

      // --- STEP 3: Setup main scene framebuffer ---
      // This starts tracking the depth and color buffers for the actual view.
      if (!SetupFramebufferForView(
            *context, view_id, *recorder, *render_context_)) {
        LOG_F(ERROR, "Failed to setup framebuffer for view {}; skipping",
          view_id.get());
        continue;
      }

      // --- STEP 4: Execute RenderGraph ---
      graphics::GpuEventScope graph_scope(*recorder, "RenderGraph");
      const bool rv = co_await ExecuteRenderGraphForView(
        view_id, factory, *render_context_, *recorder);

      // Finalize state and instrumentation
      update_view_state(view_id, rv);
    } catch (const std::exception& ex) {
      LOG_F(ERROR, "Failed to render view {}: {}", view_id.get(), ex.what());
      std::unique_lock state_lock(view_state_mutex_);
      view_ready_states_[view_id] = false;
    }
  }

  // Return the pooled context for this slot to a clean state and clear the
  // debug in-use marker.
  render_context_pool_->Release(context->GetFrameSlot());
  render_context_.reset();

  co_return;
}

auto Renderer::OnCompositing(observer_ptr<FrameContext> context) -> co::Co<>
{
  std::optional<CompositionSubmission> submission;
  std::shared_ptr<graphics::Surface> target_surface;
  {
    std::lock_guard lock(composition_mutex_);
    if (!composition_submission_) {
      co_return;
    }
    submission = std::move(composition_submission_);
    composition_submission_.reset();
    target_surface = std::move(composition_surface_);
    composition_surface_.reset();
  }

  CHECK_F(submission.has_value(), "Compositing submission required");
  auto& payload = *submission;
  if (payload.tasks.empty()) {
    co_return;
  }

  CHECK_F(static_cast<bool>(payload.target_framebuffer),
    "Compositing requires a target framebuffer");

  auto gfx = GetGraphics();
  CHECK_F(static_cast<bool>(gfx), "Graphics required for compositing");

  const auto queue_key = gfx->QueueKeyFor(graphics::QueueRole::kGraphics);
  auto recorder_ptr
    = gfx->AcquireCommandRecorder(queue_key, "Renderer Compositing");
  CHECK_F(
    static_cast<bool>(recorder_ptr), "Compositing recorder acquisition failed");
  auto& recorder = *recorder_ptr;
  auto& target_fb = *payload.target_framebuffer;
  TrackCompositionFramebuffer(recorder, target_fb);

  const auto& fb_desc = target_fb.GetDescriptor();
  CHECK_F(!fb_desc.color_attachments.empty(),
    "Compositing requires a color attachment");
  CHECK_F(static_cast<bool>(fb_desc.color_attachments[0].texture),
    "Compositing target missing color texture");
  auto& backbuffer = *fb_desc.color_attachments[0].texture;
  const auto& back_desc = backbuffer.GetDescriptor();
  DLOG_F(1,
    "Log compositing target ptr={} size={}x{} fmt={} samples={} name={}",
    fmt::ptr(&backbuffer), back_desc.width, back_desc.height, back_desc.format,
    back_desc.sample_count, back_desc.debug_name);

  RenderContext comp_context {};
  comp_context.SetRenderer(this, gfx.get());
  comp_context.framebuffer = observer_ptr { payload.target_framebuffer.get() };
  comp_context.frame_slot = frame_slot_;
  comp_context.frame_sequence = frame_seq_num;

  if (!compositing_pass_) {
    compositing_pass_config_ = std::make_shared<CompositingPassConfig>();
    compositing_pass_config_->debug_name = "CompositingPass";
    compositing_pass_
      = std::make_shared<CompositingPass>(compositing_pass_config_);
  }

  for (const auto& task : payload.tasks) {
    switch (task.type) {
    case CompositingTaskType::kCopy: {
      if (!IsViewReady(task.copy.source_view)) {
        DLOG_F(1, "Skip copy: view {} not ready", task.copy.source_view.get());
        continue;
      }
      auto source = ResolveViewOutputTexture(*context, task.copy.source_view);
      if (!source) {
        DLOG_F(1, "Skip copy: missing source texture for view {}",
          task.copy.source_view.get());
        continue;
      }
      const auto& src_desc = source->GetDescriptor();
      DLOG_F(2, "Log copy: view={} ptr={} size={}x{} fmt={} samples={}",
        task.copy.source_view.get(), fmt::ptr(source.get()), src_desc.width,
        src_desc.height, src_desc.format, src_desc.sample_count);
      DLOG_F(2, "Log copy viewport: ({}, {}) {}x{}",
        task.copy.viewport.top_left_x, task.copy.viewport.top_left_y,
        task.copy.viewport.width, task.copy.viewport.height);
      if (source->GetDescriptor().format != backbuffer.GetDescriptor().format) {
        DLOG_F(1, "Fallback to blend: format mismatch for view {}",
          task.copy.source_view.get());
        CHECK_NOTNULL_F(
          compositing_pass_config_.get(), "CompositingPass config missing");
        compositing_pass_config_->source_texture = source;
        compositing_pass_config_->viewport = task.copy.viewport;
        compositing_pass_config_->alpha = 1.0F;

        co_await compositing_pass_->PrepareResources(comp_context, recorder);
        co_await compositing_pass_->Execute(comp_context, recorder);
        break;
      }
      CopyTextureToRegion(recorder, *source, backbuffer, task.copy.viewport);
      break;
    }
    case CompositingTaskType::kBlend: {
      if (!IsViewReady(task.blend.source_view)) {
        DLOG_F(
          1, "Skip blend: view {} not ready", task.blend.source_view.get());
        continue;
      }
      auto source = ResolveViewOutputTexture(*context, task.blend.source_view);
      if (!source) {
        DLOG_F(1, "Skip blend: missing source texture for view {}",
          task.blend.source_view.get());
        continue;
      }
      const auto& src_desc = source->GetDescriptor();
      DLOG_F(2, "Blend view={} ptr={} size={}x{} fmt={} samples={}",
        task.blend.source_view.get(), fmt::ptr(source.get()), src_desc.width,
        src_desc.height, src_desc.format, src_desc.sample_count);
      DLOG_F(2, "Blend viewport=({}, {}) {}x{} alpha={}",
        task.blend.viewport.top_left_x, task.blend.viewport.top_left_y,
        task.blend.viewport.width, task.blend.viewport.height,
        task.blend.alpha);

      CHECK_NOTNULL_F(
        compositing_pass_config_.get(), "CompositingPass config missing");
      compositing_pass_config_->source_texture = source;
      compositing_pass_config_->viewport = task.blend.viewport;
      compositing_pass_config_->alpha = task.blend.alpha;

      co_await compositing_pass_->PrepareResources(comp_context, recorder);
      co_await compositing_pass_->Execute(comp_context, recorder);
      break;
    }
    case CompositingTaskType::kBlendTexture: {
      if (!task.texture_blend.source_texture) {
        DLOG_F(1, "Skip blend texture: missing source texture");
        continue;
      }
      const auto& src_desc = task.texture_blend.source_texture->GetDescriptor();
      DLOG_F(2, "Blend texture ptr={} size={}x{} fmt={} samples={} name={}",
        fmt::ptr(task.texture_blend.source_texture.get()), src_desc.width,
        src_desc.height, src_desc.format, src_desc.sample_count,
        src_desc.debug_name);
      DLOG_F(2, "Blend texture viewport=({}, {}) {}x{} alpha={}",
        task.texture_blend.viewport.top_left_x,
        task.texture_blend.viewport.top_left_y,
        task.texture_blend.viewport.width, task.texture_blend.viewport.height,
        task.texture_blend.alpha);

      CHECK_NOTNULL_F(
        compositing_pass_config_.get(), "CompositingPass config missing");
      compositing_pass_config_->source_texture
        = task.texture_blend.source_texture;
      compositing_pass_config_->viewport = task.texture_blend.viewport;
      compositing_pass_config_->alpha = task.texture_blend.alpha;

      co_await compositing_pass_->PrepareResources(comp_context, recorder);
      co_await compositing_pass_->Execute(comp_context, recorder);
      break;
    }
    case CompositingTaskType::kTaa:
    default:
      DLOG_F(1, "Skip compositing: task type not implemented");
      break;
    }
  }

  recorder.RequireResourceStateFinal(
    backbuffer, graphics::ResourceStates::kPresent);
  recorder.FlushBarriers();

  if (target_surface) {
    const auto surfaces = context->GetSurfaces();
    for (size_t i = 0; i < surfaces.size(); ++i) {
      if (surfaces[i].get() == target_surface.get()) {
        context->SetSurfacePresentable(i, true);
        break;
      }
    }
  }

  co_return;
}

auto Renderer::OnFrameEnd(observer_ptr<FrameContext> /*context*/) -> void
{
  LOG_SCOPE_FUNCTION(2);

  texture_binder_->OnFrameEnd();
  DrainPendingViewCleanup("OnFrameEnd");
}

auto Renderer::DrainPendingViewCleanup(std::string_view reason) -> void
{
  std::unordered_set<ViewId> pending;
  {
    std::lock_guard lock(pending_cleanup_mutex_);
    if (pending_cleanup_.empty()) {
      return;
    }
    pending.swap(pending_cleanup_);
  }

  DLOG_F(1, "Process pending cleanup: {} views ({})", pending.size(), reason);

  for (const auto& id : pending) {
    resolved_views_.erase(id);
    prepared_frames_.erase(id);
    per_view_storage_.erase(id);
  }

  {
    std::unique_lock state_lock(view_state_mutex_);
    for (const auto& id : pending) {
      view_ready_states_.erase(id);
    }
  }
}

//===----------------------------------------------------------------------===//
// PreExecute helper implementations
//===----------------------------------------------------------------------===//

// Removed legacy draw-metadata helpers; lifecycle now handled by
// DrawMetadataEmitter via ScenePrepState

auto Renderer::WireContext(RenderContext& render_context,
  const std::shared_ptr<graphics::Buffer>& scene_consts) -> void
{
  DLOG_SCOPE_FUNCTION(3);

  render_context.scene_constants = scene_consts;
  render_context.frame_slot = frame_slot_;
  render_context.frame_sequence = frame_seq_num;
  render_context.gpu_debug_manager.reset(gpu_debug_manager_.get());

  const auto graphics_ptr = gfx_weak_.lock();
  if (!graphics_ptr) {
    throw std::runtime_error("Graphics expired in Renderer::WireContext");
  }
  render_context.SetRenderer(this, graphics_ptr.get());
}

auto Renderer::AcquireRecorderForView(ViewId view_id, Graphics& gfx)
  -> std::shared_ptr<oxygen::graphics::CommandRecorder>
{
  DLOG_SCOPE_FUNCTION(3);

  const auto queue_key
    = gfx.QueueKeyFor(oxygen::graphics::QueueRole::kGraphics);
  return gfx.AcquireCommandRecorder(
    queue_key, std::string("View_") + std::to_string(view_id.get()));
}

auto Renderer::SetupFramebufferForView(const FrameContext& frame_context,
  ViewId view_id, graphics::CommandRecorder& recorder,
  RenderContext& render_context) -> bool
{
  DLOG_SCOPE_FUNCTION(3);

  const auto& view_ctx = frame_context.GetViewContext(view_id);

  if (!view_ctx.output) {
    LOG_F(WARNING, "View {} has no output", view_id.get());
    return false;
  }

  const auto& fb_desc = view_ctx.output->GetDescriptor();
  for (const auto& attachment : fb_desc.color_attachments) {
    if (attachment.texture) {
      // Use the texture's own descriptor initial_state when available.
      // Previously we assumed swapchain backbuffers for all color
      // attachments and used kPresent which breaks for render-to-texture
      // targets (e.g. EditorView). Honoring the texture descriptor avoids
      // conflicting initial states being tracked and prevents invalid
      // barrier sequences.
      auto initial = attachment.texture->GetDescriptor().initial_state;
      if (initial == graphics::ResourceStates::kUnknown
        || initial == graphics::ResourceStates::kUndefined) {
        initial = graphics::ResourceStates::kPresent;
      }
      recorder.BeginTrackingResourceState(*attachment.texture, initial, true);
      recorder.RequireResourceState(
        *attachment.texture, graphics::ResourceStates::kRenderTarget);
    }
  }

  if (fb_desc.depth_attachment.texture) {
    auto initial
      = fb_desc.depth_attachment.texture->GetDescriptor().initial_state;
    if (initial == graphics::ResourceStates::kUnknown
      || initial == graphics::ResourceStates::kUndefined) {
      initial = graphics::ResourceStates::kDepthWrite;
    }
    recorder.BeginTrackingResourceState(
      *fb_desc.depth_attachment.texture, initial, true);
    recorder.RequireResourceState(
      *fb_desc.depth_attachment.texture, graphics::ResourceStates::kDepthWrite);
    recorder.FlushBarriers();
  }

  recorder.BindFrameBuffer(*view_ctx.output);
  render_context.framebuffer = view_ctx.output;
  return true;
}

auto Renderer::PrepareAndWireSceneConstantsForView(ViewId view_id,
  const FrameContext& frame_context, RenderContext& render_context) -> bool
{
  DLOG_SCOPE_FUNCTION(3);

  auto resolved_it = resolved_views_.find(view_id);
  auto prepared_it = prepared_frames_.find(view_id);

  if (resolved_it == resolved_views_.end()
    || prepared_it == prepared_frames_.end()) {
    LOG_F(2, "No cached data for view {} (resolved={}, prepared={})",
      view_id.get(), resolved_it != resolved_views_.end(),
      prepared_it != prepared_frames_.end());
    return false;
  }

  // Create a per-view scene constants snapshot based on the last frame-level
  // scene_const_cpu_ and per-view SRV indices captured during RunScenePrep.
  SceneConstants view_scene_consts = scene_const_cpu_;
  const auto& prepared = prepared_it->second;
  DLOG_F(3, "   worlds: {}", prepared.bindless_worlds_slot);
  DLOG_F(3, "  normals: {}", prepared.bindless_normals_slot);
  DLOG_F(3, "materials: {}", prepared.bindless_materials_slot);
  DLOG_F(3, " metadata: {}", prepared.bindless_draw_metadata_slot);
  DLOG_F(3, " instance: {}", prepared.bindless_instance_data_slot);

  view_scene_consts.SetBindlessWorldsSlot(
    BindlessWorldsSlot(prepared.bindless_worlds_slot),
    SceneConstants::kRenderer);
  view_scene_consts.SetBindlessNormalMatricesSlot(
    BindlessNormalsSlot(prepared.bindless_normals_slot),
    SceneConstants::kRenderer);
  view_scene_consts.SetBindlessMaterialConstantsSlot(
    BindlessMaterialConstantsSlot(prepared.bindless_materials_slot),
    SceneConstants::kRenderer);
  view_scene_consts.SetBindlessDrawMetadataSlot(
    BindlessDrawMetadataSlot(prepared.bindless_draw_metadata_slot),
    SceneConstants::kRenderer);
  view_scene_consts.SetBindlessInstanceDataSlot(
    BindlessInstanceDataSlot(prepared.bindless_instance_data_slot),
    SceneConstants::kRenderer);

  if (gpu_debug_manager_) {
    view_scene_consts.SetBindlessGpuDebugLineSlot(
      BindlessGpuDebugLineSlot(
        ShaderVisibleIndex(gpu_debug_manager_->GetLineBufferSrvIndex())),
      SceneConstants::kRenderer);
    view_scene_consts.SetBindlessGpuDebugCounterSlot(
      BindlessGpuDebugCounterSlot(
        ShaderVisibleIndex(gpu_debug_manager_->GetCounterBufferUavIndex())),
      SceneConstants::kRenderer);

    static bool logged_gpu_debug_slots = false;
    if (!logged_gpu_debug_slots) {
      LOG_F(WARNING,
        "Renderer: bindless GPU debug slots set (line_srv={}, counter_uav={})",
        gpu_debug_manager_->GetLineBufferSrvIndex(),
        gpu_debug_manager_->GetCounterBufferUavIndex());
      logged_gpu_debug_slots = true;
    }
  }

  const auto& proj_matrix = resolved_it->second.ProjectionMatrix();
  view_scene_consts.SetViewMatrix(resolved_it->second.ViewMatrix())
    .SetProjectionMatrix(proj_matrix)
    .SetCameraPosition(resolved_it->second.CameraPosition())
    .SetFrameSlot(frame_context.GetFrameSlot(), SceneConstants::kRenderer)
    .SetFrameSequenceNumber(
      frame_context.GetFrameSequenceNumber(), SceneConstants::kRenderer);

  // Write constants into per-view mapped buffer
  const auto& snapshot = view_scene_consts.GetSnapshot();
  auto buffer_info = scene_const_manager_->WriteSceneConstants(
    view_id, &snapshot, sizeof(SceneConstants::GpuData));
  if (!buffer_info.buffer) {
    LOG_F(ERROR, "Failed to write scene constants for view {}", view_id);
    return false;
  }

  WireContext(render_context, buffer_info.buffer);
  render_context.env_dynamic_manager.reset(env_dynamic_manager_.get());

  // Populate render_context.current_view
  render_context.current_view.view_id = view_id;
  render_context.current_view.resolved_view.reset(&resolved_it->second);
  render_context.current_view.prepared_frame.reset(&prepared_it->second);

  return true;
}

auto Renderer::UpdateViewExposure(
  ViewId view_id, const scene::Scene& scene, const SunState& sun_state) -> float
{
  float exposure = 1.0F;
  float exposure_key = 1.0F;
  std::optional<float> camera_ev100 {};

  if (const auto resolved_it = resolved_views_.find(view_id);
    resolved_it != resolved_views_.end()) {
    camera_ev100 = resolved_it->second.CameraEv100();
  }

  // Manual and auto exposure use the post-process volume.
  if (const auto env = scene.GetEnvironment()) {
    if (const auto pp
      = env->TryGetSystem<scene::environment::PostProcessVolume>();
      pp && pp->IsEnabled()) {
      if (!pp->GetExposureEnabled()) {
        env_dynamic_manager_->SetExposure(view_id, exposure);
        return exposure;
      }
      exposure_key = std::max(1e-4F, pp->GetExposureKey());
      const float compensation_ev = pp->GetExposureCompensationEv();
      if (pp->GetExposureMode() == scene::environment::ExposureMode::kManual
        || pp->GetExposureMode()
          == scene::environment::ExposureMode::kManualCamera) {
        const float ev100 = pp->GetExposureMode()
            == scene::environment::ExposureMode::kManualCamera
          ? camera_ev100.value_or(pp->GetManualExposureEv100())
          : pp->GetManualExposureEv100();
        // Physically calibrated manual exposure (ISO 2720 reflected-light
        // calibration constant K = 12.5).
        exposure = (1.0F / 12.5F) * std::exp2(compensation_ev - ev100);
      } else {
        const float min_ev = pp->GetAutoExposureMinEv();
        const float max_ev = pp->GetAutoExposureMaxEv();

        // Estimate scene EV100 from sun illuminance (lux) using a diffuse
        // Lambertian assumption (L = E / pi) and standard calibration.
        const float illuminance_lux = std::max(0.0F, sun_state.illuminance);
        constexpr float kCalibration = 12.5F;
        constexpr float kPi = 3.14159265359F;
        const float luminance = illuminance_lux / kPi;
        const float ev100
          = std::log2(std::max(1e-4F, luminance * 100.0F / kCalibration));
        const float target_ev = std::clamp(ev100, min_ev, max_ev);

        float resolved_ev = target_ev;
        auto it = auto_exposure_ev100_.find(view_id);
        if (it != auto_exposure_ev100_.end()) {
          const float prev_ev = it->second;
          const bool brightening = target_ev < prev_ev;
          const float speed = brightening ? pp->GetAutoExposureSpeedUp()
                                          : pp->GetAutoExposureSpeedDown();
          const float dt = std::max(0.0F, last_frame_dt_seconds_);
          const float alpha = 1.0F - std::exp(-speed * dt);
          resolved_ev = prev_ev + (target_ev - prev_ev) * alpha;
        }
        auto_exposure_ev100_[view_id] = resolved_ev;

        exposure = std::exp2(compensation_ev - resolved_ev);
      }
    }
  }

  exposure *= exposure_key;
  env_dynamic_manager_->SetExposure(view_id, exposure);
  return exposure;
}

auto Renderer::ExecuteRenderGraphForView(ViewId view_id,
  const RenderGraphFactory& factory, RenderContext& render_context,
  graphics::CommandRecorder& recorder) -> co::Co<bool>
{
  DLOG_SCOPE_FUNCTION(3);

  try {
    co_await factory(view_id, render_context, recorder);
    co_return true;
  } catch (const std::exception& ex) {
    LOG_F(ERROR, "RenderGraph execution for view {} failed: {}", view_id,
      ex.what());
    co_return false;
  } catch (...) {
    LOG_F(ERROR, "RenderGraph execution for view {} failed: unknown error",
      view_id.get());
    co_return false;
  }
}

auto Renderer::RunScenePrep(ViewId view_id, const ResolvedView& view,
  const FrameContext& frame_context, bool run_frame_phase) -> std::size_t
{
  DLOG_SCOPE_FUNCTION(3);

  auto scene_ptr = frame_context.GetScene();
  CHECK_NOTNULL_F(scene_ptr, "FrameContext.scene is null in RunScenePrep");
  auto& scene = *scene_ptr;

  // Get or create the prepared frame for this specific view
  auto& prepared_frame = prepared_frames_[view_id];

  auto frame_seq = frame_context.GetFrameSequenceNumber();

  if (run_frame_phase) {
    DLOG_SCOPE_F(3,
      fmt::format("frame-phase for frame seq {}", nostd::to_string(frame_seq))
        .c_str());
    scene_prep_->Collect(
      scene, std::nullopt, frame_seq, *scene_prep_state_, true);
    scene_prep_->Finalize();
  }

  ::oxygen::observer_ptr<const ::oxygen::ResolvedView> view_ptr(&view);
  DLOG_SCOPE_F(3,
    fmt::format("view-phase for view {}", nostd::to_string(view_id)).c_str());
  {
    scene_prep_->Collect(scene,
      std::optional<::oxygen::observer_ptr<const ::oxygen::ResolvedView>>(
        view_ptr),
      frame_seq, *scene_prep_state_,
      run_frame_phase); // Only reset on first view
    scene_prep_->Finalize();

    // CRITICAL: Capture bindless SRV indices IMMEDIATELY after Finalize
    // These indices are valid only for THIS view's finalization and will be
    // overwritten when the next view calls Finalize. Store them in THIS view's
    // prepared_frame so OnRender can use the correct indices.
    if (const auto transforms = scene_prep_state_->GetTransformUploader()) {
      prepared_frame.bindless_worlds_slot = transforms->GetWorldsSrvIndex();
      DLOG_F(3, " captured worlds: {}", prepared_frame.bindless_worlds_slot);
      prepared_frame.bindless_normals_slot = transforms->GetNormalsSrvIndex();
      DLOG_F(3, "captured normals: {}", prepared_frame.bindless_normals_slot);
    }
    if (const auto materials = scene_prep_state_->GetMaterialBinder()) {
      prepared_frame.bindless_materials_slot
        = materials->GetMaterialsSrvIndex();
    }
    if (auto emitter = scene_prep_state_->GetDrawMetadataEmitter()) {
      prepared_frame.bindless_draw_metadata_slot
        = emitter->GetDrawMetadataSrvIndex();
      prepared_frame.bindless_instance_data_slot
        = emitter->GetInstanceDataSrvIndex();
    }

    if (env_dynamic_manager_) {
      const oxygen::observer_ptr<renderer::LightManager> light_mgr
        = scene_prep_state_->GetLightManager();
      if (light_mgr) {
        const SunState scene_sun = internal::ResolveSunForView(
          scene, light_mgr->GetDirectionalLights());
        env_dynamic_manager_->SetSunState(view_id, scene_sun);
        UpdateViewExposure(view_id, scene, scene_sun);

        // Populate SkyAtmosphere per-view context. Defaults stay conservative
        // until LUT precompute is wired; analytic fallback stays enabled.
        float aerial_distance_scale = 1.0F;
        float aerial_scattering_strength = 1.0F;
        std::uint32_t atmosphere_flags = 0u;
        // Planet center positioned below Z=0 ground plane so camera at Z>=0
        // is on/above surface. Default radius places center at Z=-6360km.
        float planet_radius_m = 6'360'000.0F;
        glm::vec3 planet_center_ws { 0.0F, 0.0F, -planet_radius_m };
        glm::vec3 planet_up_ws { 0.0F, 0.0F, 1.0F };
        float camera_altitude_m = 0.0F;
        float sky_view_lut_slice = 0.0F;
        float planet_to_sun_cos_zenith = 0.0F;

        // Compute effective sun state: use override if enabled, else scene sun.
        SunState effective_sun
          = sun_override_.enabled ? sun_override_ : scene_sun;

        if (auto env = scene.GetEnvironment()) {
          if (const auto atmo
            = env->TryGetSystem<scene::environment::SkyAtmosphere>();
            atmo && atmo->IsEnabled()) {
            aerial_distance_scale = atmo->GetAerialPerspectiveDistanceScale();
            aerial_scattering_strength = atmo->GetAerialScatteringStrength();
            planet_radius_m = atmo->GetPlanetRadiusMeters();

            // Update planet center to keep Z=0 as ground level.
            planet_center_ws = glm::vec3(0.0F, 0.0F, -planet_radius_m);

            // LUT availability is checked later when merging with debug flags.
            // The debug UI controls whether aerial perspective is enabled.

            const auto camera_pos = view.CameraPosition();
            camera_altitude_m = glm::max(
              glm::length(camera_pos - planet_center_ws) - planet_radius_m,
              0.0F);
            // Use effective sun's cos_zenith for atmosphere.
            planet_to_sun_cos_zenith
              = effective_sun.enabled ? effective_sun.cos_zenith : 0.0F;
          }
        }

        env_dynamic_manager_->SetAtmosphereScattering(
          view_id, aerial_distance_scale, aerial_scattering_strength);
        // Note: planet_radius_m is in EnvironmentStaticData, not passed here.
        env_dynamic_manager_->SetAtmosphereFrameContext(view_id,
          planet_center_ws, planet_up_ws, camera_altitude_m, sky_view_lut_slice,
          planet_to_sun_cos_zenith);

        // Merge debug flags with computed atmosphere flags.
        // Debug flags control whether aerial perspective is enabled at all.
        const uint32_t debug_flags = atmosphere_debug_flags_;

        // Only enable LUT mode if:
        // 1. The debug UI has kUseLut set (user wants it enabled), AND
        // 2. LUTs are actually available
        if ((debug_flags & static_cast<uint32_t>(AtmosphereFlags::kUseLut))
          != 0) {
          if (sky_atmo_lut_manager_
            && sky_atmo_lut_manager_->HasBeenGenerated()) {
            atmosphere_flags |= static_cast<uint32_t>(AtmosphereFlags::kUseLut);
          }
        }
        // If user chose force analytic mode
        if ((debug_flags
              & static_cast<uint32_t>(AtmosphereFlags::kForceAnalytic))
          != 0) {
          // Debug override: force analytic, clear LUT flag.
          atmosphere_flags &= ~static_cast<uint32_t>(AtmosphereFlags::kUseLut);
          atmosphere_flags
            |= static_cast<uint32_t>(AtmosphereFlags::kForceAnalytic);
        }
        if ((debug_flags
              & static_cast<uint32_t>(AtmosphereFlags::kVisualizeLut))
          != 0) {
          atmosphere_flags
            |= static_cast<uint32_t>(AtmosphereFlags::kVisualizeLut);
        }
        if (sun_override_.enabled) {
          atmosphere_flags
            |= static_cast<uint32_t>(AtmosphereFlags::kOverrideSun);
        }

        env_dynamic_manager_->SetAtmosphereFlags(view_id, atmosphere_flags);
        env_dynamic_manager_->SetAtmosphereSunOverride(view_id, sun_override_);

        // Update LUT manager with effective sun for regeneration.
        if (sky_atmo_lut_manager_) {
          sky_atmo_lut_manager_->UpdateSunState(effective_sun);
        }
      }
    }
  }

  PublishPreparedFrameSpans(view_id, prepared_frame);
  UpdateSceneConstantsFromView(view);

  const auto draw_count
    = prepared_frame.draw_metadata_bytes.size() / sizeof(DrawMetadata);

  DLOG_F(3, "draw count: {}", draw_count);
  return draw_count;
}

auto Renderer::PublishPreparedFrameSpans(
  ViewId view_id, PreparedSceneFrame& prepared_frame) -> void
{
  DLOG_SCOPE_FUNCTION(3);

  // Ensure per-view backing storage exists
  auto& storage = per_view_storage_[view_id];

  const auto transforms = scene_prep_state_->GetTransformUploader();
  const auto world_span = transforms->GetWorldMatrices();

  // Copy matrix floats into per-view storage so spans stay valid
  storage.world_matrix_storage.assign(
    reinterpret_cast<const float*>(world_span.data()),
    reinterpret_cast<const float*>(world_span.data())
      + world_span.size() * 16u);

  prepared_frame.world_matrices = std::span<const float>(
    storage.world_matrix_storage.data(), storage.world_matrix_storage.size());

  const auto normal_span = transforms->GetNormalMatrices();
  storage.normal_matrix_storage.assign(
    reinterpret_cast<const float*>(normal_span.data()),
    reinterpret_cast<const float*>(normal_span.data())
      + normal_span.size() * 16u);

  prepared_frame.normal_matrices = std::span<const float>(
    storage.normal_matrix_storage.data(), storage.normal_matrix_storage.size());

  // Publish draw metadata bytes and partitions from emitter accessors
  if (auto emitter = scene_prep_state_->GetDrawMetadataEmitter()) {
    const auto src_bytes = emitter->GetDrawMetadataBytes();
    storage.draw_metadata_storage.assign(src_bytes.begin(), src_bytes.end());

    prepared_frame.draw_metadata_bytes
      = std::span<const std::byte>(storage.draw_metadata_storage.data(),
        storage.draw_metadata_storage.size());

    using PR = oxygen::engine::PreparedSceneFrame::PartitionRange;
    const auto parts = emitter->GetPartitions();
    storage.partition_storage.assign(parts.begin(), parts.end());

    prepared_frame.partitions = std::span<const PR>(
      storage.partition_storage.data(), storage.partition_storage.size());
  } else {
    // No emitter -> empty spans
    prepared_frame.draw_metadata_bytes = {};
    prepared_frame.partitions = {};
  }
}

auto Renderer::UpdateSceneConstantsFromView(const ResolvedView& view) -> void
{
  // Update scene constants from the provided view snapshot
  scene_const_cpu_.SetViewMatrix(view.ViewMatrix())
    .SetProjectionMatrix(view.ProjectionMatrix())
    .SetCameraPosition(view.CameraPosition());
}

auto Renderer::OnFrameStart(observer_ptr<FrameContext> context) -> void
{
  DLOG_SCOPE_FUNCTION(2);

  {
    std::lock_guard lock(composition_mutex_);
    composition_submission_.reset();
    composition_surface_.reset();
  }

  if (!scene_prep_state_ || !texture_binder_) {
    LOG_F(
      ERROR, "Renderer OnFrameStart called before OnAttached initialization");
    return;
  }

  auto tag = oxygen::renderer::internal::RendererTagFactory::Get();
  auto frame_slot = context->GetFrameSlot();
  auto frame_sequence = context->GetFrameSequenceNumber();

  // Store frame lifecycle state for RenderContext propagation
  frame_slot_ = frame_slot;
  frame_seq_num = frame_sequence;

  // Initialize Upload Coordinator and its staging providers for the new frame
  // slot BEFORE any uploaders start allocating from them.
  inline_transfers_->OnFrameStart(tag, frame_slot);
  uploader_->OnFrameStart(tag, frame_slot);
  // then uploaders and scene constants manager
  texture_binder_->OnFrameStart();
  scene_const_manager_->OnFrameStart(frame_slot);
  env_dynamic_manager_->OnFrameStart(frame_slot);
  if (env_static_manager_) {
    env_static_manager_->OnFrameStart(tag, frame_slot);
  }
  scene_prep_state_->GetTransformUploader()->OnFrameStart(
    tag, frame_sequence, frame_slot);
  scene_prep_state_->GetGeometryUploader()->OnFrameStart(tag, frame_slot);
  scene_prep_state_->GetMaterialBinder()->OnFrameStart(tag, frame_slot);
  if (auto emitter = scene_prep_state_->GetDrawMetadataEmitter()) {
    emitter->OnFrameStart(tag, frame_sequence, frame_slot);
  }
  if (auto light_manager = scene_prep_state_->GetLightManager()) {
    light_manager->OnFrameStart(tag, frame_sequence, frame_slot);
  }
}

/*!
 Executes the scene transform propagation phase.

 Flow
 1. Acquire non-owning scene pointer from the frame context.
 2. If absent: early return (benign no-op, keeps frame deterministic).
 3. Call Scene::Update() which performs:
    - Pass 1: Dense linear scan processing dirty node flags (non-transform).
    - Pass 2: Pre-order filtered traversal (DirtyTransformFilter) resolving
      world transforms only along dirty chains (parent first).
 4. Return; no extra state retained by this module.

 Invariants / Guarantees
 - Invoked exactly once per frame in kTransformPropagation phase.
 - Parent world matrix valid before any child transform recompute.
 - Clean descendants of a dirty ancestor incur only an early-out check.
 - kIgnoreParentTransform subtrees intentionally skipped per design.
 - No scene graph structural mutation occurs here.
 - No GPU resource mutation or uploads here (CPU authoritative only).

 Never Do
 - Do not reparent / create / destroy nodes here.
 - Do not call Scene::Update() more than once per frame.
 - Do not cache raw pointers across frames.
 - Do not allocate large transient buffers (Scene owns traversal memory).
 - Do not introduce side-effects dependent on sibling visitation order.

 Performance Characteristics
 - Time: O(F + T) where F = processed dirty flags, T = visited transform
   chain nodes (<= total nodes, typically sparse).
 - Memory: No steady-state allocations.
 - Optimization: Early-exit for clean transforms; dense flag pass for cache
 locality.

 Future Improvement (Parallel Chains)
 - The scene's root hierarchies are independent for transform propagation.
 - A future optimization can collect the subset of root hierarchies that have
   at least one dirty descendant and dispatch each qualifying root subtree to
   a worker task (parent-first order preserved inside each task, no sharing).
 - Synchronize (join) all tasks before proceeding to later phases to maintain
   frame determinism. Skip parallel dispatch below a configurable dirty-node
   threshold to avoid overhead on small scenes.
 - This preserves all existing invariants (no graph mutation, parent-first,
   single update per node) while offering scalable speedups on large scenes.

 @note Dirty flag semantics, traversal filtering, and no-mutation policy are
       deliberate and should be preserved.
 @see oxygen::scene::Scene::Update
 @see oxygen::scene::SceneTraversal::UpdateTransforms
 @see oxygen::scene::DirtyTransformFilter
*/
auto Renderer::OnTransformPropagation(observer_ptr<FrameContext> context)
  -> co::Co<>
{
  LOG_SCOPE_FUNCTION(2);

  // Acquire scene pointer (non-owning). If absent, log once per frame in debug.
  auto scene_ptr = context->GetScene();
  if (!scene_ptr) {
    DLOG_F(WARNING,
      "No active scene set in FrameContext; skipping transform propagation");
    co_return; // Nothing to update
  }

  // Perform hierarchy propagation & world matrix updates.
  scene_ptr->Update();

  co_return;
}
