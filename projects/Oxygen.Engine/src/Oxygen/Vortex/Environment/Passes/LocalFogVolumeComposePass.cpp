//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

#include <glm/ext/vector_float3.hpp>
#include <glm/geometric.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Bindless/Generated.RootSignature.D3D12.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Core/Types/ShaderType.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/PipelineState.h>
#include <Oxygen/Graphics/Common/Shaders.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Profiling/GpuEventScope.h>
#include <Oxygen/Profiling/ProfileScope.h>
#include <Oxygen/Vortex/Environment/Internal/LocalFogVolumeState.h>
#include <Oxygen/Vortex/Environment/Passes/LocalFogVolumeComposePass.h>
#include <Oxygen/Vortex/Internal/BindlessRootBindings.h>
#include <Oxygen/Vortex/Internal/ViewConstantsGuard.h>
#include <Oxygen/Vortex/Internal/ViewportClamp.h>
#include <Oxygen/Vortex/PostProcess/Passes/ExposurePass.h>
#include <Oxygen/Vortex/RenderContext.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/RendererCapability.h>
#include <Oxygen/Vortex/SceneRenderer/SceneTextures.h>

namespace oxygen::vortex::environment {

namespace {

  namespace bindless_d3d12 = oxygen::bindless::generated::d3d12;

  auto TrackBufferFromKnownOrInitial(
    graphics::CommandRecorder& recorder, const graphics::Buffer& buffer) -> void
  {
    if (recorder.IsResourceTracked(buffer)
      || recorder.AdoptKnownResourceState(buffer)) {
      return;
    }
    recorder.BeginTrackingResourceState(
      buffer, graphics::ResourceStates::kCommon, true);
  }

  auto TrackTextureFromKnownOrInitial(graphics::CommandRecorder& recorder,
    const graphics::Texture& texture) -> void
  {
    if (recorder.IsResourceTracked(texture)
      || recorder.AdoptKnownResourceState(texture)) {
      return;
    }

    auto initial = texture.GetDescriptor().initial_state;
    if (initial == graphics::ResourceStates::kUnknown
      || initial == graphics::ResourceStates::kUndefined) {
      initial = graphics::ResourceStates::kCommon;
    }
    recorder.BeginTrackingResourceState(texture, initial, false);
  }

  auto BuildFramebuffer(const SceneTextures& scene_textures)
    -> graphics::FramebufferDesc
  {
    auto desc = graphics::FramebufferDesc {};
    desc.AddColorAttachment({
      .texture = scene_textures.GetSceneColorResource(),
      .format = scene_textures.GetSceneColor().GetDescriptor().format,
    });
    desc.SetDepthAttachment({
      .texture = scene_textures.GetSceneDepthResource(),
      .format = scene_textures.GetSceneDepth().GetDescriptor().format,
      .is_read_only = true,
    });
    return desc;
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

  auto BuildPipelineDesc(const SceneTextures& scene_textures,
    const bool reverse_z) -> graphics::GraphicsPipelineDesc
  {
    auto root_bindings = vortex::internal::BuildVortexRootBindings();
    const auto fog_blend = graphics::BlendTargetDesc {
      .blend_enable = true,
      .src_blend = graphics::BlendFactor::kOne,
      .dest_blend = graphics::BlendFactor::kInvSrcAlpha,
      .blend_op = graphics::BlendOp::kAdd,
      .src_blend_alpha = graphics::BlendFactor::kOne,
      .dest_blend_alpha = graphics::BlendFactor::kInvSrcAlpha,
      .blend_op_alpha = graphics::BlendOp::kAdd,
      .write_mask = graphics::ColorWriteMask::kAll,
    };
    return graphics::GraphicsPipelineDesc::Builder {}
    .SetVertexShader(graphics::ShaderRequest {
      .stage = ShaderType::kVertex,
      .source_path = "Vortex/Services/Environment/LocalFogVolumeCompose.hlsl",
      .entry_point = "VortexLocalFogVolumeComposeVS",
    })
    .SetPixelShader(graphics::ShaderRequest {
      .stage = ShaderType::kPixel,
      .source_path = "Vortex/Services/Environment/LocalFogVolumeCompose.hlsl",
      .entry_point = "VortexLocalFogVolumeComposePS",
    })
    .SetPrimitiveTopology(graphics::PrimitiveType::kTriangleList)
    .SetRasterizerState(graphics::RasterizerStateDesc::NoCulling())
    .SetDepthStencilState(graphics::DepthStencilStateDesc {
      .depth_test_enable = true,
      .depth_write_enable = false,
      .depth_func = reverse_z ? graphics::CompareOp::kGreaterOrEqual
                              : graphics::CompareOp::kLessOrEqual,
      .stencil_enable = false,
    })
    .SetBlendState({ fog_blend })
    .SetFramebufferLayout(graphics::FramebufferLayoutDesc {
      .color_target_formats = {
        scene_textures.GetSceneColor().GetDescriptor().format,
      },
      .depth_stencil_format = scene_textures.GetSceneDepth().GetDescriptor().format,
      .sample_count = scene_textures.GetSceneColor().GetDescriptor().sample_count,
      .sample_quality = scene_textures.GetSceneColor().GetDescriptor().sample_quality,
    })
    .SetRootBindings(std::span<const graphics::RootBindingItem>(
      root_bindings.data(), root_bindings.size()))
    .SetDebugName("Vortex.Environment.LocalFogCompose")
    .Build();
  }

  //! Distances, lengths and clip w at or below this are degenerate.
  constexpr float kDegenerateEpsilon = 1.0e-6F;

  auto ComputeLocalFogStartDepthZ(const ResolvedView& resolved_view,
    const float global_start_distance_meters) -> float
  {
    if (global_start_distance_meters <= kDegenerateEpsilon) {
      return resolved_view.ReverseZ() ? 1.0F : 0.0F;
    }

    const auto view_space_corner
      = resolved_view.InverseProjection() * glm::vec4(1.0F, 1.0F, 1.0F, 1.0F);
    const auto corner = glm::vec3(view_space_corner);
    const auto corner_length = glm::length(corner);
    if (corner_length <= kDegenerateEpsilon) {
      return 0.0F;
    }

    const auto ratio = std::abs(view_space_corner.z) / corner_length;
    const auto view_space_start_fog_point
      = glm::vec4(0.0F, 0.0F, -global_start_distance_meters * ratio, 1.0F);
    const auto clip_space
      = resolved_view.ProjectionMatrix() * view_space_start_fog_point;
    if (std::abs(clip_space.w) <= kDegenerateEpsilon) {
      return 0.0F;
    }

    return std::clamp(clip_space.z / clip_space.w, 0.0F, 1.0F);
  }

} // namespace

LocalFogVolumeComposePass::LocalFogVolumeComposePass(Renderer& renderer)
  : renderer_(renderer)
{
}

LocalFogVolumeComposePass::~LocalFogVolumeComposePass() = default;

auto LocalFogVolumeComposePass::OnFrameStart(
  const frame::SequenceNumber sequence, const frame::Slot slot) -> void
{
  if (const auto buffer = EnsurePassConstantsBuffer(); buffer != nullptr) {
    buffer->OnFrameStart(sequence, slot);
  }
}

auto LocalFogVolumeComposePass::EnsurePassConstantsBuffer()
  -> observer_ptr<upload::TransientStructuredBuffer>
{
  if (pass_constants_buffer_.has_value()) {
    return observer_ptr { &*pass_constants_buffer_ };
  }
  auto gfx = renderer_.GetGraphics();
  if (gfx == nullptr) {
    return nullptr;
  }
  return observer_ptr { &pass_constants_buffer_.emplace(
    observer_ptr { gfx.get() }, renderer_.GetStagingProvider(),
    static_cast<std::uint32_t>(sizeof(PassConstants)),
    observer_ptr { &renderer_.GetInlineTransfersCoordinator() },
    "Environment.LocalFogCompose.PassConstants") };
}

auto LocalFogVolumeComposePass::IsRequested(const RenderContext& ctx,
  const internal::LocalFogVolumeState::ViewProducts& products) const -> bool
{
  return renderer_.GetLocalFogEnabled() && ctx.current_view.with_local_fog
    && products.buffer_ready && products.tile_data_ready
    && products.occupied_tile_buffer_slot.IsValid()
    && products.instance_count > 0U
    && products.occupied_tile_draw_args_buffer != nullptr;
}

auto LocalFogVolumeComposePass::PublishConstants(RenderContext& ctx,
  const SceneTextures& scene_textures,
  const internal::LocalFogVolumeState::ViewProducts& products)
  -> ShaderVisibleIndex
{
  if (!IsRequested(ctx, products)
    || !renderer_.HasCapability(
      RendererCapabilityFamily::kEnvironmentLighting)) {
    return kInvalidShaderVisibleIndex;
  }
  const auto constants_buffer = EnsurePassConstantsBuffer();
  if (constants_buffer == nullptr) {
    return kInvalidShaderVisibleIndex;
  }

  const auto tile_pixel_size = renderer_.GetLocalFogTilePixelSize();
  const auto extent = scene_textures.GetExtent();
  auto view_width = extent.x;
  auto view_height = extent.y;
  const auto global_start_distance_meters
    = renderer_.GetLocalFogGlobalStartDistanceMeters();
  auto start_depth_z = 0.0F;
  if (const auto* resolved_view = ctx.current_view.resolved_view.get();
    resolved_view != nullptr) {
    const auto viewport = resolved_view->Viewport();
    if (viewport.IsValid()) {
      view_width
        = (std::max)(static_cast<std::uint32_t>(std::ceil(viewport.width)), 1U);
      view_height
        = (std::max)(static_cast<std::uint32_t>(std::ceil(viewport.height)),
          1U);
    }
    start_depth_z = ComputeLocalFogStartDepthZ(
      *resolved_view, global_start_distance_meters);
  }

  const auto constants = PassConstants {
    .instance_buffer_slot = products.instance_buffer_slot.get(),
    .tile_data_texture_slot = products.tile_data_texture_slot.get(),
    .occupied_tile_buffer_slot = products.occupied_tile_buffer_slot.get(),
    .tile_resolution_x = products.tile_resolution_x,
    .tile_resolution_y = products.tile_resolution_y,
    .max_instances_per_tile = products.max_instances_per_tile,
    .instance_count = products.instance_count,
    .tile_pixel_size = tile_pixel_size,
    .view_width = view_width,
    .view_height = view_height,
    .global_start_distance_meters = global_start_distance_meters,
    .start_depth_z = start_depth_z,
  };
  auto constants_alloc = constants_buffer->Allocate(1U);
  if (!constants_alloc.has_value()
    || !constants_alloc->TryWriteObject(constants)) {
    return kInvalidShaderVisibleIndex;
  }
  return constants_alloc->srv;
}

auto LocalFogVolumeComposePass::Record(RenderContext& ctx,
  graphics::CommandRecorder& recorder, const SceneTextures& scene_textures,
  const internal::LocalFogVolumeState::ViewProducts& products) -> RecordState
{
  auto state = RecordState {
    .requested = IsRequested(ctx, products),
    .executed = false,
  };
  if (!state.requested
    || !renderer_.HasCapability(
      RendererCapabilityFamily::kEnvironmentLighting)) {
    return state;
  }

  auto gfx = renderer_.GetGraphics();
  if (gfx == nullptr
    || !vortex::internal::HasViewConstants(ctx, "Vortex.Stage15.LocalFog")) {
    return state;
  }
  const auto constants_srv = PublishConstants(ctx, scene_textures, products);
  if (!constants_srv.IsValid()) {
    return state;
  }
  const auto* resolved_view = ctx.current_view.resolved_view.get();
  const auto reverse_z = resolved_view == nullptr || resolved_view->ReverseZ();

  auto framebuffer = gfx->CreateFramebuffer(BuildFramebuffer(scene_textures));

  TrackTextureFromKnownOrInitial(recorder, scene_textures.GetSceneColor());
  TrackTextureFromKnownOrInitial(recorder, scene_textures.GetSceneDepth());
  TrackBufferFromKnownOrInitial(
    recorder, *products.occupied_tile_draw_args_buffer);

  graphics::GpuEventScope pass_scope(recorder, "Vortex.Stage15.LocalFog",
    profiling::ProfileGranularity::kTelemetry,
    profiling::ProfileCategory::kPass);
  recorder.RequireResourceState(
    scene_textures.GetSceneColor(), graphics::ResourceStates::kRenderTarget);
  recorder.RequireResourceState(
    scene_textures.GetSceneDepth(), graphics::ResourceStates::kDepthRead);
  recorder.RequireResourceState(*products.occupied_tile_draw_args_buffer,
    graphics::ResourceStates::kIndirectArgument);
  if (const auto& frame = ctx.current_view.frame_exposure) {
    const auto& status = *frame->current_state->status_buffer;
    if (!recorder.IsResourceTracked(status)
      && !recorder.AdoptKnownResourceState(status)) {
      recorder.BeginTrackingResourceState(
        status, graphics::ResourceStates::kCommon, false);
    }
    recorder.RequireResourceState(
      status, graphics::ResourceStates::kUnorderedAccess);
  }
  recorder.FlushBarriers();

  recorder.BindFrameBuffer(*framebuffer);
  SetViewportAndScissor(recorder, ctx, scene_textures);
  recorder.SetPipelineState(BuildPipelineDesc(scene_textures, reverse_z));
  recorder.SetGraphicsRootConstantBufferView(
    static_cast<std::uint32_t>(bindless_d3d12::RootParam::kViewConstants),
    ctx.view_constants.GpuAddress());
  recorder.SetGraphicsRoot32BitConstant(
    static_cast<std::uint32_t>(bindless_d3d12::RootParam::kRootConstants), 0U,
    0U);
  recorder.SetGraphicsRoot32BitConstant(
    static_cast<std::uint32_t>(bindless_d3d12::RootParam::kRootConstants),
    constants_srv.get(), 1U);
  recorder.ExecuteIndirect(*products.occupied_tile_draw_args_buffer,
    graphics::CommandRecorder::IndirectCommandDesc {
      .kind = graphics::CommandRecorder::IndirectCommandKind::kDraw,
    },
    graphics::CommandRecorder::IndirectExecutionDesc {
      .argument_buffer_range = { 0U, 0U },
      .command_count = graphics::CommandRecorder::IndirectCommandCount { 1U },
    });

  LOG_F(INFO,
    "local_fog_indirect_draw_valid=true tile_capacity={} instances={}",
    products.tile_capacity, products.instance_count);

  recorder.RequireResourceState(
    scene_textures.GetSceneColor(), graphics::ResourceStates::kRenderTarget);
  recorder.RequireResourceState(
    scene_textures.GetSceneDepth(), graphics::ResourceStates::kDepthRead);
  recorder.RequireResourceState(*products.occupied_tile_draw_args_buffer,
    graphics::ResourceStates::kIndirectArgument);
  gfx->RegisterDeferredRelease(std::move(framebuffer));

  state.executed = true;
  state.draw_count = 1U;
  state.bound_scene_color = true;
  state.sampled_scene_depth = true;
  state.consumed_instance_buffer = true;
  return state;
}

} // namespace oxygen::vortex::environment
