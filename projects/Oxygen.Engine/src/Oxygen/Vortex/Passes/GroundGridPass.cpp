//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/vector_double2.hpp>
#include <glm/ext/vector_float4.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/matrix_access.hpp>
#include <glm/matrix.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Bindless/Generated.RootSignature.D3D12.h>
#include <Oxygen/Core/Detail/FormatUtils.h>
#include <Oxygen/Core/Types/ShaderType.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/PipelineState.h>
#include <Oxygen/Graphics/Common/Shaders.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Profiling/GpuEventScope.h>
#include <Oxygen/Profiling/ProfileScope.h>
#include <Oxygen/Vortex/Diagnostics/Internal/GpuWorkStageTable.h>
#include <Oxygen/Vortex/Internal/BindlessRootBindings.h>
#include <Oxygen/Vortex/Internal/PerViewStructuredPublisher.h>
#include <Oxygen/Vortex/Internal/ViewConstantsGuard.h>
#include <Oxygen/Vortex/Internal/ViewportClamp.h>
#include <Oxygen/Vortex/Passes/GroundGridPass.h>
#include <Oxygen/Vortex/RenderContext.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/SceneRenderer/SceneTextures.h>
#include <Oxygen/Vortex/Types/GroundGridConfig.h>

namespace oxygen::vortex {

namespace {

  namespace bindless_d3d12 = oxygen::bindless::generated::d3d12;

  constexpr double kMinSpacing = 1e-4;
  constexpr double kMinSmoothTime = 0.001;
  constexpr double kTeleportThreshold = 1000.0;
  // Below this fraction of the grid spacing, in offset and in the distance the
  // velocity covers over one smoothing time, the grid is at rest: the eased
  // offset snaps to its target so the spring's tail does not keep the view
  // rendering for invisible motion.
  constexpr double kRestFraction = 1e-3;
  constexpr double kCritDampCoeff1 = 0.48;
  constexpr double kCritDampCoeff2 = 0.235;

  struct alignas(16) GroundGridPassConstants {
    glm::mat4 inv_view_proj {};
    float plane_height {};
    float spacing {};
    float major_every {};
    float fade_start {};
    float line_thickness {};
    float major_thickness {};
    float axis_thickness {};
    float fade_power {};
    float origin_x {};
    float origin_y {};
    float horizon_boost {};
    float pad0 {};
    float grid_offset_x {};
    float grid_offset_y {};
    std::uint32_t reserved0 {};
    std::uint32_t reserved1 {};
    glm::vec4 minor_color {};
    glm::vec4 major_color {};
    glm::vec4 axis_color_x {};
    glm::vec4 axis_color_y {};
    glm::vec4 origin_color {};
  };

  static_assert(sizeof(GroundGridPassConstants) == 208U);

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

  auto BuildGroundGridFramebuffer(const graphics::Framebuffer& target,
    const SceneTextures& scene_textures) -> graphics::FramebufferDesc
  {
    auto desc = graphics::FramebufferDesc {};
    CHECK_F(!target.GetDescriptor().color_attachments.empty()
        && target.GetDescriptor().color_attachments.front().texture != nullptr,
      "GroundGridPass: target framebuffer requires a valid color attachment");
    desc.AddColorAttachment(target.GetDescriptor().color_attachments.front());
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

  auto BuildGroundGridPipelineDesc(const SceneTextures& scene_textures,
    const graphics::Framebuffer& target) -> graphics::GraphicsPipelineDesc
  {
    auto root_bindings = vortex::internal::BuildVortexRootBindings();
    const auto alpha_blend = graphics::BlendTargetDesc {
      .blend_enable = true,
      .src_blend = graphics::BlendFactor::kSrcAlpha,
      .dest_blend = graphics::BlendFactor::kInvSrcAlpha,
      .blend_op = graphics::BlendOp::kAdd,
      .src_blend_alpha = graphics::BlendFactor::kZero,
      .dest_blend_alpha = graphics::BlendFactor::kOne,
      .blend_op_alpha = graphics::BlendOp::kAdd,
      .write_mask = graphics::ColorWriteMask::kAll,
    };
    CHECK_F(!target.GetDescriptor().color_attachments.empty()
        && target.GetDescriptor().color_attachments.front().texture != nullptr,
      "GroundGridPass: target framebuffer requires a color attachment");
    const auto& target_color_desc = target.GetDescriptor()
                                      .color_attachments.front()
                                      .texture->GetDescriptor();
    auto pixel_defines = std::vector<graphics::ShaderDefine> {};
    if (graphics::detail::IsHdr(target_color_desc.format)) {
      pixel_defines.push_back({ .name = "OXYGEN_HDR_OUTPUT", .value = "1" });
    }
    // Depth-tested against reverse-Z scene depth, never written.
    auto depth_state = graphics::DepthStencilStateDesc {};
    depth_state.depth_test_enable = true;
    depth_state.depth_write_enable = false;
    depth_state.depth_func = graphics::CompareOp::kGreaterOrEqual;
    depth_state.stencil_enable = false;
    return graphics::GraphicsPipelineDesc::Builder {}
    .SetVertexShader(graphics::ShaderRequest {
      .stage = ShaderType::kVertex,
      .source_path = "Vortex/Services/PostProcess/GroundGrid.hlsl",
      .entry_point = "VortexGroundGridVS",
    })
    .SetPixelShader(graphics::ShaderRequest {
      .stage = ShaderType::kPixel,
      .source_path = "Vortex/Services/PostProcess/GroundGrid.hlsl",
      .entry_point = "VortexGroundGridPS",
      .defines = std::move(pixel_defines),
    })
    .SetPrimitiveTopology(graphics::PrimitiveType::kTriangleList)
    .SetRasterizerState(graphics::RasterizerStateDesc::NoCulling())
    .SetDepthStencilState(depth_state)
    .SetBlendState({ alpha_blend })
    .SetFramebufferLayout(graphics::FramebufferLayoutDesc {
      .color_target_formats = {
        target_color_desc.format,
      },
      .depth_stencil_format = scene_textures.GetSceneDepth().GetDescriptor().format,
      .sample_count = target_color_desc.sample_count,
      .sample_quality = target_color_desc.sample_quality,
    })
    .SetRootBindings(std::span<const graphics::RootBindingItem>(
      root_bindings.data(), root_bindings.size()))
    .SetDebugName("Vortex.Stage20.GroundGrid")
    .Build();
  }

} // namespace

struct GroundGridPass::PassConstants : GroundGridPassConstants { };

GroundGridPass::GroundGridPass(Renderer& renderer)
  : renderer_(renderer)
{
}

GroundGridPass::~GroundGridPass() = default;

auto GroundGridPass::Record(RenderContext& ctx,
  graphics::CommandRecorder& recorder, const SceneTextures& scene_textures,
  const observer_ptr<const graphics::Framebuffer> target) -> RecordState
{
  const auto& config = renderer_.GetGroundGridConfig();
  auto state = RecordState {
    .requested = config.enabled && ctx.current_view.view_id != kInvalidViewId
      && ctx.current_view.feature_mask.Has(
        CompositionView::ViewFeatureMask::kGroundGrid)
      && ctx.current_view.resolved_view != nullptr && target != nullptr,
  };
  if (!state.requested) {
    return state;
  }

  auto gfx = renderer_.GetGraphics();
  if (gfx == nullptr) {
    return state;
  }

  auto framebuffer = gfx->CreateFramebuffer(
    BuildGroundGridFramebuffer(*target, scene_textures));
  if (!framebuffer) {
    return state;
  }

  if (!internal::HasViewConstants(ctx, "Vortex.Stage20.GroundGrid")) {
    state.executed = false;
    return state;
  }
  graphics::GpuEventScope pass_scope(recorder, gpu_work::kGroundGridLabel,
    profiling::ProfileGranularity::kTelemetry,
    profiling::ProfileCategory::kPass);
  auto& target_color
    = *target->GetDescriptor().color_attachments.front().texture;
  TrackTextureFromKnownOrInitial(recorder, target_color);
  TrackTextureFromKnownOrInitial(recorder, scene_textures.GetSceneDepth());
  recorder.RequireResourceState(
    target_color, graphics::ResourceStates::kRenderTarget);
  recorder.RequireResourceState(
    scene_textures.GetSceneDepth(), graphics::ResourceStates::kDepthRead);
  recorder.FlushBarriers();
  recorder.BindFrameBuffer(*framebuffer);
  SetViewportAndScissor(recorder, ctx, scene_textures);
  recorder.SetPipelineState(
    BuildGroundGridPipelineDesc(scene_textures, *target));
  recorder.SetGraphicsRootConstantBufferView(
    static_cast<std::uint32_t>(bindless_d3d12::RootParam::kViewConstants),
    ctx.view_constants.GpuAddress());
  const auto pass_constants_index = UpdatePassConstants(ctx, state.settling);
  if (!pass_constants_index.IsValid()) {
    return state;
  }
  recorder.SetGraphicsRoot32BitConstant(
    static_cast<std::uint32_t>(bindless_d3d12::RootParam::kRootConstants), 0U,
    0U);
  recorder.SetGraphicsRoot32BitConstant(
    static_cast<std::uint32_t>(bindless_d3d12::RootParam::kRootConstants),
    pass_constants_index.get(), 1U);
  recorder.Draw(3U, 1U, 0U, 0U);
  recorder.RequireResourceState(
    target_color, graphics::ResourceStates::kRenderTarget);
  recorder.RequireResourceState(
    scene_textures.GetSceneDepth(), graphics::ResourceStates::kDepthRead);
  gfx->RegisterDeferredRelease(std::move(framebuffer));

  state.executed = true;
  state.wrote_scene_color = false;
  state.sampled_scene_depth = true;
  state.draw_count = 1U;
  return state;
}

auto GroundGridPass::UpdatePassConstants(
  const RenderContext& ctx, bool& settling) -> ShaderVisibleIndex
{
  if (!constants_publisher_) {
    auto gfx = renderer_.GetGraphics();
    CHECK_NOTNULL_F(gfx.get());
    constants_publisher_
      = std::make_unique<internal::PerViewStructuredPublisher<PassConstants>>(
        observer_ptr { gfx.get() }, renderer_.GetStagingProvider(),
        observer_ptr { &renderer_.GetInlineTransfersCoordinator() },
        "Vortex.Stage20.GroundGrid.PassConstants");
  }
  if (constants_frame_ != ctx.frame_sequence) {
    constants_publisher_->OnFrameStart(ctx.frame_sequence, ctx.frame_slot);
    constants_frame_ = ctx.frame_sequence;
  }
  PassConstants constants {};
  constants.inv_view_proj = ComputeInvViewProj(ctx);
  FillConstants(constants);
  settling = ComputeGridOffset(constants, ctx);
  return internal::SlotOrInvalid(
    constants_publisher_->Publish(ctx.current_view.view_id, constants),
    ctx.current_view.view_id, "ground grid pass constants");
}

auto GroundGridPass::ComputeInvViewProj(const RenderContext& ctx) const
  -> glm::mat4
{
  if (ctx.current_view.resolved_view == nullptr) {
    return glm::mat4 { 1.0F };
  }

  const auto& view = ctx.current_view.resolved_view->ViewMatrix();
  // Camera-relative: the grid shader works around the view origin.
  const auto view_no_trans
    = glm::column(view, 3, glm::vec4(0.0F, 0.0F, 0.0F, 1.0F));
  const auto& proj = ctx.current_view.resolved_view->ProjectionMatrix();
  return glm::inverse(proj * view_no_trans);
}

auto GroundGridPass::ComputeGridOffset(
  PassConstants& constants, const RenderContext& ctx) -> bool
{
  if (ctx.current_view.resolved_view == nullptr) {
    return false;
  }

  const auto& config = renderer_.GetGroundGridConfig();
  const auto camera_pos_f = ctx.current_view.resolved_view->CameraPosition();
  const glm::dvec2 camera_pos { camera_pos_f.x, camera_pos_f.y };
  const double spacing = std::max<double>(config.spacing, kMinSpacing);
  const double major_every = std::max<double>(config.major_every, 1.0);
  const double period = spacing * major_every;
  const glm::dvec2 origin(config.origin.x, config.origin.y);

  auto wrap = [period](double value) -> double {
    auto wrapped = std::fmod(value, period);
    if (wrapped < 0.0) {
      wrapped += period;
    }
    return wrapped;
  };
  auto shortest_wrapped_delta
    = [period](double current, double target) -> double {
    auto delta = target - current;
    if (delta > period * 0.5) {
      delta -= period;
    } else if (delta < -period * 0.5) {
      delta += period;
    }
    return delta;
  };

  auto snap_to_spacing = [spacing](double value) -> double {
    return std::floor(value / spacing) * spacing;
  };

  const glm::dvec2 snapped_grid_offset {
    wrap(snap_to_spacing(camera_pos.x - origin.x)),
    wrap(snap_to_spacing(camera_pos.y - origin.y)),
  };
  glm::dvec2 effective_grid_offset = snapped_grid_offset;
  bool settling = false;
  auto& smooth_state = smooth_states_by_view_[ctx.current_view.view_id];

  if (config.smooth_motion) {
    if (smooth_state.first_frame) {
      smooth_state.grid_offset = snapped_grid_offset;
      smooth_state.velocity = glm::dvec2(0.0);
      smooth_state.first_frame = false;
    } else {
      const auto dt = static_cast<double>(ctx.delta_time);
      const double smooth_time
        = std::max<double>(config.smooth_time, kMinSmoothTime);
      const double omega = 2.0 / smooth_time;
      const double x = omega * dt;
      const double exp = 1.0
        / (1.0 + x + (kCritDampCoeff1 * x * x) + (kCritDampCoeff2 * x * x * x));

      glm::dvec2 target = snapped_grid_offset;
      target.x = smooth_state.grid_offset.x
        + shortest_wrapped_delta(
          smooth_state.grid_offset.x, snapped_grid_offset.x);
      target.y = smooth_state.grid_offset.y
        + shortest_wrapped_delta(
          smooth_state.grid_offset.y, snapped_grid_offset.y);

      const glm::dvec2 change = smooth_state.grid_offset - target;
      const glm::dvec2 temp = (smooth_state.velocity + omega * change) * dt;
      smooth_state.velocity = (smooth_state.velocity - omega * temp) * exp;
      smooth_state.grid_offset = target + (change + temp) * exp;

      const double rest_distance = spacing * kRestFraction;
      const bool at_rest
        = glm::length(target - smooth_state.grid_offset) <= rest_distance
        && glm::length(smooth_state.velocity) * smooth_time <= rest_distance;
      if (glm::length(change) > kTeleportThreshold || at_rest) {
        smooth_state.grid_offset = snapped_grid_offset;
        smooth_state.velocity = glm::dvec2(0.0);
      } else {
        settling = true;
      }

      smooth_state.grid_offset.x = wrap(smooth_state.grid_offset.x);
      smooth_state.grid_offset.y = wrap(smooth_state.grid_offset.y);
    }
    effective_grid_offset = smooth_state.grid_offset;
  } else {
    smooth_state.grid_offset = snapped_grid_offset;
    smooth_state.velocity = glm::dvec2(0.0);
    smooth_state.first_frame = true;
  }

  constants.grid_offset_x = static_cast<float>(effective_grid_offset.x);
  constants.grid_offset_y = static_cast<float>(effective_grid_offset.y);
  return settling;
}

auto GroundGridPass::FillConstants(PassConstants& constants) const -> void
{
  const auto& config = renderer_.GetGroundGridConfig();
  constants.plane_height = 0.0F;
  constants.spacing = config.spacing;
  constants.major_every
    = static_cast<float>((std::max)(config.major_every, 1U));
  constants.fade_start = config.fade_start;
  constants.line_thickness = config.line_thickness;
  constants.major_thickness = config.major_thickness;
  constants.axis_thickness = config.axis_thickness;
  constants.fade_power = (std::max)(config.fade_power, 0.0F);
  constants.origin_x = config.origin.x;
  constants.origin_y = config.origin.y;
  constants.horizon_boost = (std::max)(config.horizon_boost, 0.0F);
  constants.minor_color = glm::vec4(config.minor_color.r, config.minor_color.g,
    config.minor_color.b, config.minor_color.a);
  constants.major_color = glm::vec4(config.major_color.r, config.major_color.g,
    config.major_color.b, config.major_color.a);
  constants.axis_color_x = glm::vec4(config.axis_color_x.r,
    config.axis_color_x.g, config.axis_color_x.b, config.axis_color_x.a);
  constants.axis_color_y = glm::vec4(config.axis_color_y.r,
    config.axis_color_y.g, config.axis_color_y.b, config.axis_color_y.a);
  constants.origin_color = glm::vec4(config.origin_color.r,
    config.origin_color.g, config.origin_color.b, config.origin_color.a);
}

} // namespace oxygen::vortex
