//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <optional>
#include <ranges>

#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Vortex/RenderContext.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/RendererCapability.h>
#include <Oxygen/Vortex/SceneRenderer/SceneRenderer.h>
#include <Oxygen/Vortex/SceneRenderer/Stages/BasePass/BasePassModule.h>
#include <Oxygen/Vortex/SceneRenderer/Stages/DepthPrepass/DepthPrepassModule.h>
#include <Oxygen/Vortex/SceneRenderer/Stages/InitViews/InitViewsModule.h>

namespace oxygen::vortex {

namespace {

  constexpr SceneRenderer::StageOrder kAuthoredStageOrder {
    1,
    2,
    3,
    4,
    5,
    6,
    7,
    8,
    9,
    10,
    11,
    12,
    13,
    14,
    15,
    16,
    17,
    18,
    19,
    20,
    21,
    22,
    23,
  };

  auto ResolveViewportExtent(const engine::ViewContext& view)
    -> std::optional<glm::uvec2>
  {
    if (!view.view.viewport.IsValid()) {
      return std::nullopt;
    }

    return glm::uvec2 {
      std::max(1U, static_cast<std::uint32_t>(view.view.viewport.width)),
      std::max(1U, static_cast<std::uint32_t>(view.view.viewport.height)),
    };
  }

  auto ResolveFrameViewportExtent(const engine::FrameContext& frame)
    -> std::optional<glm::uvec2>
  {
    // Preserve the future multi-view shell by sizing against the maximum
    // scene-view envelope. Only fall back to non-scene views when no scene
    // views exist.
    auto max_scene_extent = std::optional<glm::uvec2> {};
    auto max_non_scene_extent = std::optional<glm::uvec2> {};

    const auto accumulate = [](std::optional<glm::uvec2>& current,
                              const glm::uvec2 candidate) -> void {
      if (!current.has_value()) {
        current = candidate;
        return;
      }
      current->x = std::max(current->x, candidate.x);
      current->y = std::max(current->y, candidate.y);
    };

    for (const auto& view_ref : frame.GetViews()) {
      const auto& view = view_ref.get();
      const auto viewport_extent = ResolveViewportExtent(view);
      if (!viewport_extent.has_value()) {
        continue;
      }

      if (view.metadata.is_scene_view) {
        accumulate(max_scene_extent, *viewport_extent);
      } else {
        accumulate(max_non_scene_extent, *viewport_extent);
      }
    }

    if (max_scene_extent.has_value()) {
      return max_scene_extent;
    }
    return max_non_scene_extent;
  }

  auto ResolveDepthSrvFormat(const Format texture_format) -> Format
  {
    switch (texture_format) {
    case Format::kDepth32:
    case Format::kDepth32Stencil8:
    case Format::kDepth24Stencil8:
      return Format::kR32Float;
    case Format::kDepth16:
      return Format::kR16UNorm;
    default:
      return texture_format;
    }
  }

  auto ResolveStencilSrvFormat(const Format texture_format) -> Format
  {
    switch (texture_format) {
    case Format::kDepth24Stencil8:
      return Format::kDepth24Stencil8;
    case Format::kDepth32Stencil8:
      return Format::kDepth32Stencil8;
    default:
      return texture_format;
    }
  }

  auto MakeSrvDesc(const graphics::Texture& texture, const Format format)
    -> graphics::TextureViewDescription
  {
    return {
      .view_type = graphics::ResourceViewType::kTexture_SRV,
      .visibility = graphics::DescriptorVisibility::kShaderVisible,
      .format = format,
      .dimension = texture.GetDescriptor().texture_type,
      .sub_resources = graphics::TextureSubResourceSet::EntireTexture(),
    };
  }

  auto MakeUavDesc(const graphics::Texture& texture, const Format format)
    -> graphics::TextureViewDescription
  {
    return {
      .view_type = graphics::ResourceViewType::kTexture_UAV,
      .visibility = graphics::DescriptorVisibility::kShaderVisible,
      .format = format,
      .dimension = texture.GetDescriptor().texture_type,
      .sub_resources = graphics::TextureSubResourceSet::EntireTexture(),
    };
  }

  auto HasPublishedGBufferDebugInputs(
    const SceneTextureBindings& bindings) -> bool
  {
    return std::ranges::all_of(bindings.gbuffer_srvs,
      [](const std::uint32_t index) -> bool {
        return index != SceneTextureBindings::kInvalidIndex;
      });
  }

} // namespace

SceneRenderer::SceneRenderer(Renderer& renderer, Graphics& gfx,
  const SceneTexturesConfig config, const ShadingMode default_shading_mode)
  : renderer_(renderer)
  , gfx_(gfx)
  , scene_textures_(gfx, config)
  , default_shading_mode_(default_shading_mode)
{
  if (renderer_.HasCapability(RendererCapabilityFamily::kScenePreparation)) {
    init_views_ = std::make_unique<InitViewsModule>(renderer_);
  }
  if (renderer_.HasCapability(RendererCapabilityFamily::kScenePreparation)
    && renderer_.HasCapability(RendererCapabilityFamily::kDeferredShading)) {
    depth_prepass_
      = std::make_unique<DepthPrepassModule>(renderer_, scene_textures_.GetConfig());
  }
  if (renderer_.HasCapability(RendererCapabilityFamily::kScenePreparation)
    && renderer_.HasCapability(RendererCapabilityFamily::kDeferredShading)) {
    base_pass_
      = std::make_unique<BasePassModule>(renderer_, scene_textures_.GetConfig());
  }
}

SceneRenderer::~SceneRenderer() = default;

void SceneRenderer::OnFrameStart(const engine::FrameContext& frame)
{
  if (const auto frame_extent = ResolveFrameViewportExtent(frame);
    frame_extent.has_value() && *frame_extent != scene_textures_.GetExtent()) {
    scene_textures_.Resize(*frame_extent);
  }

  setup_mode_.Reset();
  scene_texture_bindings_.Invalidate();
  ResetExtractArtifacts();
  InvalidatePublishedViewFrameBindings();
}

void SceneRenderer::OnPreRender(const engine::FrameContext& /*frame*/) { }

void SceneRenderer::OnRender(RenderContext& ctx)
{
  [[maybe_unused]] const auto shading_mode
    = ResolveShadingModeForCurrentView(ctx);

  // Stage 2: InitViews
  ctx.current_view.prepared_frame.reset(nullptr);
  if (init_views_ != nullptr) {
    init_views_->Execute(ctx, scene_textures_);
    if (ctx.current_view.view_id != kInvalidViewId) {
      ctx.current_view.prepared_frame
        = observer_ptr<const PreparedSceneFrame> {
          init_views_->GetPreparedSceneFrame(ctx.current_view.view_id)
        };
    }
  }

  // Stage 3: Depth prepass + early velocity
  if (depth_prepass_ != nullptr) {
    depth_prepass_->SetConfig(DepthPrepassConfig {
      .mode = ctx.current_view.depth_prepass_mode,
      .write_velocity = scene_textures_.GetVelocity() != nullptr,
    });
    depth_prepass_->Execute(ctx, scene_textures_);
    ctx.current_view.depth_prepass_completeness
      = depth_prepass_->GetCompleteness();
  } else {
    ctx.current_view.depth_prepass_completeness
      = DepthPrePassCompleteness::kDisabled;
  }
  if (ctx.current_view.depth_prepass_completeness
    == DepthPrePassCompleteness::kComplete) {
    ApplyStage3DepthPrepassState();
  }

  // Stage 4: reserved - GeometryVirtualizationService

  // Stage 5: Occlusion / HZB

  // Stage 6: Forward light data / light grid

  // Stage 7: reserved - MaterialCompositionService::PreBasePass

  // Stage 8: Shadow depth

  // Stage 9: Base pass
  if (base_pass_ != nullptr) {
    base_pass_->SetConfig(BasePassConfig {
      .write_velocity = scene_textures_.GetVelocity() != nullptr,
      .early_z_pass_done = ctx.current_view.IsEarlyDepthComplete(),
      .shading_mode = shading_mode,
    });
    base_pass_->Execute(ctx, scene_textures_);
    if (base_pass_->HasPublishedBasePassProducts()
      && base_pass_->HasCompletedVelocityForDynamicGeometry()) {
      ApplyStage9BasePassState();
    }
  }

  // Stage 10: Rebuild scene textures with GBuffers
  if (base_pass_ != nullptr && base_pass_->HasPublishedBasePassProducts()) {
    ApplyStage10RebuildState();
  }

  // Stage 11: reserved - MaterialCompositionService::PostBasePass

  // Stage 12: Deferred direct lighting
  RenderDeferredLighting(ctx, scene_textures_);

  // Stage 13: reserved - IndirectLightingService

  // Stage 14: reserved - EnvironmentLightingService volumetrics

  // Stage 15: Sky / atmosphere / fog

  // Stage 16: reserved - WaterService

  // Stage 17: reserved - post-opaque extensions

  // Stage 18: Translucency

  // Stage 19: reserved - DistortionModule

  // Stage 20: reserved - LightShaftBloomModule

  // Stage 21: Resolve scene color
  ResolveSceneColor(ctx);

  // Stage 22: Post processing
  ApplyStage22PostProcessState();

  // Stage 23: Post-render cleanup / extraction
  PostRenderCleanup(ctx);
}

void SceneRenderer::OnCompositing(RenderContext& /*ctx*/)
{
  // Phase 2 explicitly preserves the seam while Renderer retains composition
  // planning, queueing, target resolution, and presentation ownership.
}

void SceneRenderer::OnFrameEnd(const engine::FrameContext& /*frame*/) { }

void SceneRenderer::ApplyStage3DepthPrepassState()
{
  auto flags = SceneTextureSetupMode::Flag::kSceneDepth
    | SceneTextureSetupMode::Flag::kPartialDepth;
  if (scene_textures_.GetVelocity() != nullptr) {
    flags = flags | SceneTextureSetupMode::Flag::kSceneVelocity;
  }
  setup_mode_.SetFlags(flags);
  RefreshSceneTextureBindings();
}

void SceneRenderer::ApplyStage9BasePassState()
{
  // Stage 9 writes deferred attachments, but Stage 10 remains the first
  // bindless publication boundary for SceneColor and the active GBuffers.
  if (scene_textures_.GetVelocity() == nullptr) {
    return;
  }

  setup_mode_.Set(SceneTextureSetupMode::Flag::kSceneVelocity);
  RefreshSceneTextureBindings();
}

void SceneRenderer::ApplyStage10RebuildState()
{
  scene_textures_.RebuildWithGBuffers();
  setup_mode_.SetFlags(SceneTextureSetupMode::Flag::kGBuffers
    | SceneTextureSetupMode::Flag::kSceneColor
    | SceneTextureSetupMode::Flag::kStencil);
  RefreshSceneTextureBindings();
  CHECK_F(HasPublishedGBufferDebugInputs(scene_texture_bindings_),
    "SceneRenderer: Stage 10 must publish GBuffer bindings before deferred "
    "lighting or GBuffer debug inspection");
}

void SceneRenderer::ApplyStage22PostProcessState()
{
  if (scene_textures_.GetCustomDepth() != nullptr) {
    setup_mode_.Set(SceneTextureSetupMode::Flag::kCustomDepth);
  }
  RefreshSceneTextureBindings();
}

void SceneRenderer::ApplyStage23ExtractionState()
{
  // Stage 23 is an extraction boundary only; scene-texture bindless
  // availability remains defined by the prior setup milestones.
}

auto SceneRenderer::GetSceneTextures() const -> const SceneTextures&
{
  return scene_textures_;
}

auto SceneRenderer::GetSceneTextures() -> SceneTextures&
{
  return scene_textures_;
}

auto SceneRenderer::GetSceneTextureBindings() const
  -> const SceneTextureBindings&
{
  return scene_texture_bindings_;
}

auto SceneRenderer::GetSceneTextureExtracts() const
  -> const SceneTextureExtracts&
{
  return scene_texture_extracts_;
}

auto SceneRenderer::GetDefaultShadingMode() const -> ShadingMode
{
  return default_shading_mode_;
}

auto SceneRenderer::GetEffectiveShadingMode(const RenderContext& ctx) const
  -> ShadingMode
{
  return ResolveShadingModeForCurrentView(ctx);
}

auto SceneRenderer::GetAuthoredStageOrder() -> const StageOrder&
{
  return kAuthoredStageOrder;
}

auto SceneRenderer::GetPublishedViewFrameBindings() const
  -> const ViewFrameBindings&
{
  return published_view_frame_bindings_;
}

auto SceneRenderer::GetPublishedViewFrameBindingsSlot() const
  -> ShaderVisibleIndex
{
  return published_view_frame_bindings_slot_;
}

auto SceneRenderer::GetPublishedViewId() const -> ViewId
{
  return published_view_id_;
}

void SceneRenderer::PublishViewFrameBindings(const ViewId view_id,
  const ViewFrameBindings& bindings, const ShaderVisibleIndex slot)
{
  published_view_id_ = view_id;
  published_view_frame_bindings_ = bindings;
  published_view_frame_bindings_slot_ = slot;
}

void SceneRenderer::InvalidatePublishedViewFrameBindings()
{
  published_view_id_ = kInvalidViewId;
  published_view_frame_bindings_ = {};
  published_view_frame_bindings_slot_ = kInvalidShaderVisibleIndex;
}

void SceneRenderer::RefreshSceneTextureBindings()
{
  scene_texture_bindings_.Invalidate();
  if (setup_mode_.GetFlags() == 0U) {
    return;
  }

  scene_texture_bindings_.valid_flags = setup_mode_.GetFlags();

  if (setup_mode_.IsSet(SceneTextureSetupMode::Flag::kSceneDepth)) {
    scene_texture_bindings_.scene_depth_srv
      = RegisterSceneTextureView(scene_textures_.GetSceneDepth(),
        MakeSrvDesc(scene_textures_.GetSceneDepth(),
          ResolveDepthSrvFormat(
            scene_textures_.GetSceneDepth().GetDescriptor().format)));
  }

  if (setup_mode_.IsSet(SceneTextureSetupMode::Flag::kPartialDepth)) {
    scene_texture_bindings_.partial_depth_srv
      = RegisterSceneTextureView(scene_textures_.GetPartialDepth(),
        MakeSrvDesc(scene_textures_.GetPartialDepth(),
          scene_textures_.GetPartialDepth().GetDescriptor().format));
  }

  if (setup_mode_.IsSet(SceneTextureSetupMode::Flag::kSceneVelocity)
    && scene_textures_.GetVelocity() != nullptr) {
    scene_texture_bindings_.velocity_srv
      = RegisterSceneTextureView(*scene_textures_.GetVelocity(),
        MakeSrvDesc(*scene_textures_.GetVelocity(),
          scene_textures_.GetVelocity()->GetDescriptor().format));
  }

  if (setup_mode_.IsSet(SceneTextureSetupMode::Flag::kSceneColor)) {
    scene_texture_bindings_.scene_color_srv
      = RegisterSceneTextureView(scene_textures_.GetSceneColor(),
        MakeSrvDesc(scene_textures_.GetSceneColor(),
          scene_textures_.GetSceneColor().GetDescriptor().format));
    scene_texture_bindings_.scene_color_uav
      = RegisterSceneTextureView(scene_textures_.GetSceneColor(),
        MakeUavDesc(scene_textures_.GetSceneColor(),
          scene_textures_.GetSceneColor().GetDescriptor().format));
  }

  if (setup_mode_.IsSet(SceneTextureSetupMode::Flag::kStencil)) {
    const auto stencil_view = scene_textures_.GetStencil();
    if (stencil_view.IsValid()) {
      scene_texture_bindings_.stencil_srv
        = RegisterSceneTextureView(*stencil_view.texture,
          MakeSrvDesc(*stencil_view.texture,
            ResolveStencilSrvFormat(
              stencil_view.texture->GetDescriptor().format)));
    }
  }

  if (setup_mode_.IsSet(SceneTextureSetupMode::Flag::kCustomDepth)
    && scene_textures_.GetCustomDepth() != nullptr) {
    scene_texture_bindings_.custom_depth_srv
      = RegisterSceneTextureView(*scene_textures_.GetCustomDepth(),
        MakeSrvDesc(*scene_textures_.GetCustomDepth(),
          ResolveDepthSrvFormat(
            scene_textures_.GetCustomDepth()->GetDescriptor().format)));

    const auto custom_stencil = scene_textures_.GetCustomStencil();
    if (custom_stencil.IsValid()) {
      scene_texture_bindings_.custom_stencil_srv
        = RegisterSceneTextureView(*custom_stencil.texture,
          MakeSrvDesc(*custom_stencil.texture,
            ResolveStencilSrvFormat(
              custom_stencil.texture->GetDescriptor().format)));
    }
  }

  if (setup_mode_.IsSet(SceneTextureSetupMode::Flag::kGBuffers)) {
    scene_texture_bindings_.gbuffer_srvs[0]
      = RegisterSceneTextureView(scene_textures_.GetGBufferNormal(),
        MakeSrvDesc(scene_textures_.GetGBufferNormal(),
          scene_textures_.GetGBufferNormal().GetDescriptor().format));
    scene_texture_bindings_.gbuffer_srvs[1]
      = RegisterSceneTextureView(scene_textures_.GetGBufferMaterial(),
        MakeSrvDesc(scene_textures_.GetGBufferMaterial(),
          scene_textures_.GetGBufferMaterial().GetDescriptor().format));
    scene_texture_bindings_.gbuffer_srvs[2]
      = RegisterSceneTextureView(scene_textures_.GetGBufferBaseColor(),
        MakeSrvDesc(scene_textures_.GetGBufferBaseColor(),
          scene_textures_.GetGBufferBaseColor().GetDescriptor().format));
    scene_texture_bindings_.gbuffer_srvs[3]
      = RegisterSceneTextureView(scene_textures_.GetGBufferCustomData(),
        MakeSrvDesc(scene_textures_.GetGBufferCustomData(),
          scene_textures_.GetGBufferCustomData().GetDescriptor().format));
  }
}

void SceneRenderer::ResetExtractArtifacts()
{
  scene_texture_extracts_.Reset();
  resolved_scene_color_artifact_.texture.reset();
  resolved_scene_depth_artifact_.texture.reset();
  prev_scene_depth_artifact_.texture.reset();
  prev_velocity_artifact_.texture.reset();
}

auto SceneRenderer::EnsureArtifactTexture(ExtractArtifact& artifact,
  std::string_view debug_name, const graphics::Texture& source)
  -> graphics::Texture*
{
  const auto& source_desc = source.GetDescriptor();
  const auto requires_reallocation = [&]() -> bool {
    if (artifact.texture == nullptr) {
      return true;
    }
    const auto& current_desc = artifact.texture->GetDescriptor();
    return current_desc.width != source_desc.width
      || current_desc.height != source_desc.height
      || current_desc.format != source_desc.format
      || current_desc.sample_count != source_desc.sample_count;
  }();

  if (requires_reallocation) {
    auto artifact_desc = source_desc;
    artifact_desc.debug_name = std::string(debug_name);
    artifact.texture = gfx_.CreateTexture(artifact_desc);
  }

  return artifact.texture.get();
}

auto SceneRenderer::ResolveVelocitySourceTexture() const
  -> const graphics::Texture*
{
  return scene_textures_.GetVelocity();
}

auto SceneRenderer::RegisterSceneTextureView(graphics::Texture& texture,
  const graphics::TextureViewDescription& desc) -> std::uint32_t
{
  auto& registry = gfx_.GetResourceRegistry();
  if (const auto existing_index
    = registry.FindShaderVisibleIndex(texture, desc);
    existing_index.has_value()) {
    return existing_index->get();
  }

  auto& allocator = gfx_.GetDescriptorAllocator();
  auto handle = allocator.AllocateRaw(desc.view_type, desc.visibility);
  CHECK_F(handle.IsValid(),
    "SceneRenderer: failed to allocate a {} descriptor for '{}'",
    graphics::to_string(desc.view_type), texture.GetName());

  const auto view = registry.RegisterView(texture, std::move(handle), desc);
  CHECK_F(view->IsValid(),
    "SceneRenderer: failed to register a {} descriptor for '{}'",
    graphics::to_string(desc.view_type), texture.GetName());

  const auto index = registry.FindShaderVisibleIndex(texture, desc);
  CHECK_F(index.has_value(),
    "SceneRenderer: {} descriptor registration for '{}' did not yield a "
    "shader-visible index",
    graphics::to_string(desc.view_type), texture.GetName());
  return index->get();
}

auto SceneRenderer::ResolveShadingModeForCurrentView(
  const RenderContext& ctx) const -> ShadingMode
{
  if (const auto* view = ctx.GetCurrentCompositionView();
    view != nullptr && view->GetShadingMode().has_value()) {
    return view->GetShadingMode().value();
  }
  if (ctx.current_view.shading_mode_override.has_value()) {
    return ctx.current_view.shading_mode_override.value();
  }
  return default_shading_mode_;
}

void SceneRenderer::RenderDeferredLighting(
  RenderContext& /*ctx*/, const SceneTextures& /*scene_textures*/)
{
  if (!HasPublishedGBufferDebugInputs(scene_texture_bindings_)) {
    return;
  }
}

} // namespace oxygen::vortex
