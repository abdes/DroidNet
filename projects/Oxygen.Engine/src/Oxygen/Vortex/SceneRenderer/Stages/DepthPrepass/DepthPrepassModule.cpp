//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstdint>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include <Oxygen/Core/Bindless/Generated.RootSignature.D3D12.h>
#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/PipelineState.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Profiling/GpuEventScope.h>
#include <Oxygen/Profiling/ProfileScope.h>
#include <Oxygen/Vortex/Internal/MeshDepthPipeline.h>
#include <Oxygen/Vortex/Internal/ViewportClamp.h>
#include <Oxygen/Vortex/PreparedSceneFrame.h>
#include <Oxygen/Vortex/RenderContext.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/SceneRenderer/DepthPrePassPolicy.h>
#include <Oxygen/Vortex/SceneRenderer/SceneTextures.h>
#include <Oxygen/Vortex/SceneRenderer/Stages/DepthPrepass/DepthPrepassMeshProcessor.h>
#include <Oxygen/Vortex/SceneRenderer/Stages/DepthPrepass/DepthPrepassModule.h>

namespace oxygen::vortex {

namespace {
  namespace bindless_d3d12 = oxygen::bindless::generated::d3d12;

  auto AdoptOrBeginPersistentState(
    graphics::CommandRecorder& recorder, graphics::Texture& texture) -> void
  {
    if (!recorder.IsResourceTracked(texture)
      && !recorder.AdoptKnownResourceState(texture)) {
      auto initial = texture.GetDescriptor().initial_state;
      if (initial == graphics::ResourceStates::kUnknown
        || initial == graphics::ResourceStates::kUndefined) {
        initial = graphics::ResourceStates::kCommon;
      }
      recorder.BeginTrackingResourceState(texture, initial, false);
    }
  }

  auto BuildDepthPrepassFramebuffer(SceneTextures& scene_textures,
    const bool writes_velocity) -> graphics::FramebufferDesc
  {
    auto desc = graphics::FramebufferDesc {};
    if (writes_velocity && scene_textures.GetVelocity() != nullptr) {
      desc.AddColorAttachment({
        .texture = scene_textures.GetVelocityResource(),
        .format = scene_textures.GetVelocity()->GetDescriptor().format,
      });
    }
    desc.SetDepthAttachment({
      .texture = scene_textures.GetSceneDepthResource(),
      .format = scene_textures.GetSceneDepth().GetDescriptor().format,
      .is_read_only = false,
    });
    return desc;
  }

  auto NeedsFramebufferRebuild(
    const std::shared_ptr<graphics::Framebuffer>& framebuffer,
    const SceneTextures& scene_textures, const bool writes_velocity) -> bool
  {
    if (!framebuffer) {
      return true;
    }

    const auto& desc = framebuffer->GetDescriptor();
    const auto expected_color_attachments = writes_velocity ? 1U : 0U;
    if (desc.color_attachments.size() != expected_color_attachments
      || desc.depth_attachment.texture.get()
        != scene_textures.GetSceneDepthResource().get()) {
      return true;
    }

    if (!writes_velocity) {
      return false;
    }

    return scene_textures.GetVelocity() == nullptr
      || desc.color_attachments[0].texture.get()
      != scene_textures.GetVelocityResource().get();
  }

  auto BeginDepthPrepassResourceTracking(graphics::CommandRecorder& recorder,
    SceneTextures& scene_textures, const bool writes_velocity) -> void
  {
    AdoptOrBeginPersistentState(recorder, scene_textures.GetSceneDepth());
    AdoptOrBeginPersistentState(recorder, scene_textures.GetPartialDepth());
    recorder.RequireResourceState(
      scene_textures.GetSceneDepth(), graphics::ResourceStates::kDepthWrite);
    if (writes_velocity && scene_textures.GetVelocity() != nullptr) {
      AdoptOrBeginPersistentState(recorder, *scene_textures.GetVelocity());
      recorder.RequireResourceState(
        *scene_textures.GetVelocity(), graphics::ResourceStates::kRenderTarget);
    }
  }

  auto TransitionDepthPrepassOutputs(graphics::CommandRecorder& recorder,
    SceneTextures& scene_textures, const bool writes_velocity) -> void
  {
    recorder.RequireResourceState(
      scene_textures.GetSceneDepth(), graphics::ResourceStates::kDepthRead);
    recorder.RequireResourceState(scene_textures.GetPartialDepth(),
      graphics::ResourceStates::kShaderResource);
    if (writes_velocity && scene_textures.GetVelocity() != nullptr) {
      recorder.RequireResourceState(*scene_textures.GetVelocity(),
        graphics::ResourceStates::kShaderResource);
    }
  }

  auto SetViewportAndScissor(graphics::CommandRecorder& recorder,
    const RenderContext& ctx, const SceneTextures& scene_textures) -> void
  {
    const auto extent = scene_textures.GetExtent();
    if (ctx.current_view.resolved_view != nullptr) {
      const auto clamped
        = oxygen::vortex::internal::ResolveClampedViewportState(
          ctx.current_view.resolved_view->Viewport(),
          ctx.current_view.resolved_view->Scissor(), extent.x, extent.y);
      recorder.SetViewport(clamped.viewport);
      recorder.SetScissors(clamped.scissors);
      return;
    }

    recorder.SetViewport({
      .top_left_x = 0.0F,
      .top_left_y = 0.0F,
      .width = static_cast<float>(extent.x),
      .height = static_cast<float>(extent.y),
      .min_depth = 0.0F,
      .max_depth = 1.0F,
    });
    recorder.SetScissors({
      .left = 0,
      .top = 0,
      .right = static_cast<std::int32_t>(extent.x),
      .bottom = static_cast<std::int32_t>(extent.y),
    });
  }

  auto ResolveRasterState(const PreparedSceneFrame& prepared_frame,
    const DrawCommand& draw_command) -> internal::MeshRasterState
  {
    return internal::ResolveMeshRasterState(
      prepared_frame.GetDrawMetadata(), draw_command.draw_index);
  }

  auto CopySceneDepthToPartialDepth(
    graphics::CommandRecorder& recorder, SceneTextures& scene_textures) -> void
  {
    recorder.RequireResourceState(
      scene_textures.GetSceneDepth(), graphics::ResourceStates::kCopySource);
    recorder.RequireResourceState(
      scene_textures.GetPartialDepth(), graphics::ResourceStates::kCopyDest);
    recorder.FlushBarriers();

    recorder.CopyTexture(scene_textures.GetSceneDepth(),
      graphics::TextureSlice {},
      graphics::TextureSubResourceSet::EntireTexture(),
      scene_textures.GetPartialDepth(), graphics::TextureSlice {},
      graphics::TextureSubResourceSet::EntireTexture());
  }

} // namespace

DepthPrepassModule::DepthPrepassModule(
  Renderer& renderer, const SceneTexturesConfig& scene_textures_config)
  : renderer_(renderer)
  , mesh_processor_(std::make_unique<DepthPrepassMeshProcessor>(renderer))
{
  std::ignore = scene_textures_config;
}

DepthPrepassModule::~DepthPrepassModule() = default;

void DepthPrepassModule::Execute(RenderContext& ctx,
  graphics::CommandRecorder& recorder, SceneTextures& scene_textures)
{
  auto empty_prepared_frame = PreparedSceneFrame {};
  has_published_depth_products_ = false;
  has_valid_depth_product_ = false;
  completeness_ = DepthPrePassCompleteness::kIncomplete;
  if (config_.mode == DepthPrePassMode::kDisabled) {
    completeness_ = DepthPrePassCompleteness::kDisabled;
    return;
  }

  const auto has_current_view_payload = mesh_processor_ != nullptr
    && ctx.current_view.prepared_frame != nullptr
    && ctx.current_view.prepared_frame->IsValid();
  const auto writes_velocity
    = config_.write_velocity && scene_textures.GetVelocity() != nullptr;
  if (ctx.view_constants == nullptr) {
    return;
  }

  if (mesh_processor_ != nullptr) {
    mesh_processor_->BuildDrawCommands(has_current_view_payload
        ? *ctx.current_view.prepared_frame
        : empty_prepared_frame,
      ctx.current_view.resolved_view.get(),
      config_.mode == DepthPrePassMode::kOpaqueAndMasked);
  }

  auto* gfx = renderer_.GetGraphics().get();
  if (gfx == nullptr) {
    return;
  }

  graphics::GpuEventScope stage_scope(recorder, "Vortex.Stage3.DepthPrepass",
    profiling::ProfileGranularity::kTelemetry,
    profiling::ProfileCategory::kPass);

  const auto reverse_z = ctx.current_view.resolved_view == nullptr
    || ctx.current_view.resolved_view->ReverseZ();
  BeginDepthPrepassResourceTracking(recorder, scene_textures, writes_velocity);

  auto& framebuffer
    = writes_velocity ? depth_velocity_framebuffer_ : depth_framebuffer_;
  if (NeedsFramebufferRebuild(framebuffer, scene_textures, writes_velocity)) {
    framebuffer = gfx->CreateFramebuffer(
      BuildDepthPrepassFramebuffer(scene_textures, writes_velocity));
  }

  recorder.FlushBarriers();
  recorder.ClearFramebuffer(
    *framebuffer, std::nullopt, reverse_z ? 0.0F : 1.0F, 0U);
  recorder.BindFrameBuffer(*framebuffer);
  SetViewportAndScissor(recorder, ctx, scene_textures);
  const auto root_constants_param
    = static_cast<std::uint32_t>(bindless_d3d12::RootParam::kRootConstants);
  const auto view_constants_param
    = static_cast<std::uint32_t>(bindless_d3d12::RootParam::kViewConstants);

  auto current_raster_state = std::optional<internal::MeshRasterState> {};
  for (const auto& draw_command : mesh_processor_->GetDrawCommands()) {
    const auto raster_state
      = ResolveRasterState(*ctx.current_view.prepared_frame, draw_command);
    if (!current_raster_state.has_value()
      || current_raster_state.value() != raster_state) {
      recorder.SetPipelineState(internal::BuildMeshDepthPipeline(
        scene_textures.GetSceneDepth().GetDescriptor(),
        writes_velocity ? scene_textures.GetVelocity()->GetDescriptor().format
                        : Format::kUnknown,
        raster_state, reverse_z));
      recorder.SetGraphicsRootConstantBufferView(
        view_constants_param, ctx.view_constants->GetGPUVirtualAddress());
      recorder.SetGraphicsRoot32BitConstant(
        root_constants_param, kInvalidShaderVisibleIndex.get(), 1U);
      current_raster_state = raster_state;
    }

    recorder.SetGraphicsRoot32BitConstant(
      root_constants_param, draw_command.draw_index, 0U);
    recorder.Draw(draw_command.index_count, draw_command.instance_count, 0U,
      draw_command.start_instance);
  }

  CopySceneDepthToPartialDepth(recorder, scene_textures);
  TransitionDepthPrepassOutputs(recorder, scene_textures, writes_velocity);

  has_valid_depth_product_ = true;
  completeness_ = has_current_view_payload
    ? DepthPrePassCompleteness::kComplete
    : DepthPrePassCompleteness::kIncomplete;
  has_published_depth_products_
    = completeness_ == DepthPrePassCompleteness::kComplete;
}

void DepthPrepassModule::SetConfig(const DepthPrepassConfig& config)
{
  config_ = config;
}

auto DepthPrepassModule::GetCompleteness() const -> DepthPrePassCompleteness
{
  return completeness_;
}

auto DepthPrepassModule::HasValidDepthProduct() const -> bool
{
  return has_valid_depth_product_;
}

auto DepthPrepassModule::HasPublishedDepthProducts() const -> bool
{
  return has_published_depth_products_;
}

} // namespace oxygen::vortex
