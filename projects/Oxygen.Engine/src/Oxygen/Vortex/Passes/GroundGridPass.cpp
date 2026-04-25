//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Vortex/Passes/GroundGridPass.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <vector>

#include <glm/glm.hpp>

#include <Oxygen/Core/Bindless/Generated.RootSignature.D3D12.h>
#include <Oxygen/Core/Detail/FormatUtils.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/PipelineState.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Profiling/GpuEventScope.h>
#include <Oxygen/Vortex/Internal/ViewportClamp.h>
#include <Oxygen/Vortex/RenderContext.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/SceneRenderer/SceneTextures.h>
#include <Oxygen/Vortex/Types/GroundGridConfig.h>

namespace oxygen::vortex {

namespace {

  namespace bindless_d3d12 = oxygen::bindless::generated::d3d12;

  constexpr auto kPassConstantsStride = 256U;
  constexpr auto kPassConstantsSlots = 8U;
  constexpr double kMinSpacing = 1e-4;
  constexpr double kMinSmoothTime = 0.001;
  constexpr double kTeleportThreshold = 1000.0;
  constexpr double kCritDampCoeff1 = 0.48;
  constexpr double kCritDampCoeff2 = 0.235;

  struct alignas(16) GroundGridPassConstants {
    glm::mat4 inv_view_proj { 1.0F };
    float plane_height { 0.0F };
    float spacing { 1.0F };
    float major_every { 10.0F };
    float fade_start { 0.0F };
    float line_thickness { 0.02F };
    float major_thickness { 0.04F };
    float axis_thickness { 0.06F };
    float fade_power { 2.0F };
    float origin_x { 0.0F };
    float origin_y { 0.0F };
    float horizon_boost { 0.35F };
    float pad0 { 0.0F };
    float grid_offset_x { 0.0F };
    float grid_offset_y { 0.0F };
    std::uint32_t reserved0 { 0U };
    std::uint32_t reserved1 { 0U };
    glm::vec4 minor_color { 0.16F, 0.16F, 0.16F, 1.0F };
    glm::vec4 major_color { 0.20F, 0.20F, 0.20F, 1.0F };
    glm::vec4 axis_color_x { 0.7F, 0.23F, 0.23F, 1.0F };
    glm::vec4 axis_color_y { 0.23F, 0.7F, 0.23F, 1.0F };
    glm::vec4 origin_color { 1.0F, 1.0F, 1.0F, 1.0F };
  };

  static_assert(sizeof(GroundGridPassConstants) <= kPassConstantsStride);

  auto RangeTypeToViewType(const bindless_d3d12::RangeType type)
    -> graphics::ResourceViewType
  {
    using graphics::ResourceViewType;

    switch (type) {
    case bindless_d3d12::RangeType::SRV:
      return ResourceViewType::kRawBuffer_SRV;
    case bindless_d3d12::RangeType::Sampler:
      return ResourceViewType::kSampler;
    case bindless_d3d12::RangeType::UAV:
      return ResourceViewType::kRawBuffer_UAV;
    default:
      return ResourceViewType::kNone;
    }
  }

  auto BuildVortexRootBindings() -> std::vector<graphics::RootBindingItem>
  {
    std::vector<graphics::RootBindingItem> bindings;
    bindings.reserve(bindless_d3d12::kRootParamTableCount);

    for (std::uint32_t index = 0; index < bindless_d3d12::kRootParamTableCount;
      ++index) {
      const auto& desc = bindless_d3d12::kRootParamTable.at(index);
      graphics::RootBindingDesc binding {};
      binding.binding_slot_desc.register_index = desc.shader_register;
      binding.binding_slot_desc.register_space = desc.register_space;
      binding.visibility = graphics::ShaderStageFlags::kAll;

      switch (desc.kind) {
      case bindless_d3d12::RootParamKind::DescriptorTable: {
        graphics::DescriptorTableBinding table {};
        if (desc.ranges_count > 0U && desc.ranges.data() != nullptr) {
          const auto& range = desc.ranges.front();
          table.view_type = RangeTypeToViewType(
            static_cast<bindless_d3d12::RangeType>(range.range_type));
          table.base_index = range.base_register;
          table.count = range.num_descriptors
              == (std::numeric_limits<std::uint32_t>::max)()
            ? (std::numeric_limits<std::uint32_t>::max)()
            : range.num_descriptors;
        }
        binding.data = table;
        break;
      }
      case bindless_d3d12::RootParamKind::CBV:
        binding.data = graphics::DirectBufferBinding {};
        break;
      case bindless_d3d12::RootParamKind::RootConstants:
        binding.data
          = graphics::PushConstantsBinding { .size = desc.constants_count };
        break;
      }

      bindings.emplace_back(binding);
    }

    return bindings;
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
    recorder.BeginTrackingResourceState(texture, initial);
  }

  auto BuildGroundGridFramebuffer(const graphics::Framebuffer& target,
    const SceneTextures& scene_textures)
    -> graphics::FramebufferDesc
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
    const graphics::Framebuffer& target)
    -> graphics::GraphicsPipelineDesc
  {
    auto root_bindings = BuildVortexRootBindings();
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
    const auto& target_color_desc
      = target.GetDescriptor().color_attachments.front().texture->GetDescriptor();
    auto pixel_defines = std::vector<graphics::ShaderDefine> {};
    if (graphics::detail::IsHdr(
          target_color_desc.format)) {
      pixel_defines.push_back({ .name = "OXYGEN_HDR_OUTPUT", .value = "1" });
    }
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
    .SetDepthStencilState(graphics::DepthStencilStateDesc {
      .depth_test_enable = true,
      .depth_write_enable = false,
      .depth_func = graphics::CompareOp::kGreaterOrEqual,
      .stencil_enable = false,
      .stencil_read_mask = 0xFF,
      .stencil_write_mask = 0xFF,
    })
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
  pass_constants_indices_.fill(kInvalidShaderVisibleIndex);
}

GroundGridPass::~GroundGridPass() { ReleasePassConstantsBuffer(); }

auto GroundGridPass::Record(
  RenderContext& ctx, const SceneTextures& scene_textures,
  const observer_ptr<const graphics::Framebuffer> target) -> RecordState
{
  const auto& config = renderer_.GetGroundGridConfig();
  auto state = RecordState {
    .requested = config.enabled && ctx.current_view.view_id != kInvalidViewId
      && ctx.current_view.resolved_view != nullptr
      && target != nullptr,
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

  const auto queue_key = gfx->QueueKeyFor(graphics::QueueRole::kGraphics);
  auto recorder = gfx->AcquireCommandRecorder(queue_key, "Vortex GroundGrid");
  if (!recorder) {
    gfx->RegisterDeferredRelease(std::move(framebuffer));
    return state;
  }

  graphics::GpuEventScope pass_scope(*recorder, "Vortex.Stage20.GroundGrid",
    profiling::ProfileGranularity::kDiagnostic,
    profiling::ProfileCategory::kPass);
  auto& target_color
    = *target->GetDescriptor().color_attachments.front().texture;
  TrackTextureFromKnownOrInitial(*recorder, target_color);
  TrackTextureFromKnownOrInitial(*recorder, scene_textures.GetSceneDepth());
  recorder->RequireResourceState(
    target_color, graphics::ResourceStates::kRenderTarget);
  recorder->RequireResourceState(
    scene_textures.GetSceneDepth(), graphics::ResourceStates::kDepthRead);
  recorder->FlushBarriers();
  recorder->BindFrameBuffer(*framebuffer);
  SetViewportAndScissor(*recorder, ctx, scene_textures);
  recorder->SetPipelineState(BuildGroundGridPipelineDesc(scene_textures, *target));
  if (ctx.view_constants != nullptr) {
    recorder->SetGraphicsRootConstantBufferView(
      static_cast<std::uint32_t>(bindless_d3d12::RootParam::kViewConstants),
      ctx.view_constants->GetGPUVirtualAddress());
  }
  const auto pass_constants_index = UpdatePassConstants(ctx);
  recorder->SetGraphicsRoot32BitConstant(
    static_cast<std::uint32_t>(bindless_d3d12::RootParam::kRootConstants), 0U,
    0U);
  recorder->SetGraphicsRoot32BitConstant(
    static_cast<std::uint32_t>(bindless_d3d12::RootParam::kRootConstants),
    pass_constants_index.get(), 1U);
  recorder->Draw(3U, 1U, 0U, 0U);
  recorder->RequireResourceStateFinal(
    target_color, graphics::ResourceStates::kRenderTarget);
  recorder->RequireResourceStateFinal(
    scene_textures.GetSceneDepth(), graphics::ResourceStates::kDepthRead);
  gfx->RegisterDeferredRelease(std::move(framebuffer));

  state.executed = true;
  state.wrote_scene_color = false;
  state.sampled_scene_depth = true;
  state.draw_count = 1U;
  return state;
}

auto GroundGridPass::EnsurePassConstantsBuffer() -> void
{
  if (pass_constants_buffer_ != nullptr
    && pass_constants_indices_[0].IsValid()) {
    return;
  }

  auto gfx = renderer_.GetGraphics();
  CHECK_NOTNULL_F(gfx.get());

  auto& registry = gfx->GetResourceRegistry();
  auto& allocator = gfx->GetDescriptorAllocator();
  const auto desc = graphics::BufferDesc {
    .size_bytes = kPassConstantsStride * kPassConstantsSlots,
    .usage = graphics::BufferUsage::kConstant,
    .memory = graphics::BufferMemory::kUpload,
    .debug_name = "Vortex.Stage20.GroundGrid.PassConstants",
  };

  pass_constants_buffer_ = gfx->CreateBuffer(desc);
  CHECK_NOTNULL_F(pass_constants_buffer_.get(),
    "GroundGridPass: failed to create pass constants buffer");
  pass_constants_buffer_->SetName(desc.debug_name);
  pass_constants_mapped_ptr_
    = static_cast<std::byte*>(pass_constants_buffer_->Map(0, desc.size_bytes));
  CHECK_NOTNULL_F(pass_constants_mapped_ptr_,
    "GroundGridPass: failed to map pass constants buffer");

  pass_constants_indices_.fill(kInvalidShaderVisibleIndex);
  registry.Register(pass_constants_buffer_);
  for (std::size_t slot = 0; slot < kPassConstantsSlots; ++slot) {
    auto handle
      = allocator.AllocateRaw(graphics::ResourceViewType::kConstantBuffer,
        graphics::DescriptorVisibility::kShaderVisible);
    CHECK_F(handle.IsValid(),
      "GroundGridPass: failed to allocate pass constants descriptor");
    pass_constants_indices_[slot] = allocator.GetShaderVisibleIndex(handle);

    const auto offset = static_cast<std::uint32_t>(slot * kPassConstantsStride);
    const auto view_desc = graphics::BufferViewDescription {
      .view_type = graphics::ResourceViewType::kConstantBuffer,
      .visibility = graphics::DescriptorVisibility::kShaderVisible,
      .range = { offset, kPassConstantsStride },
    };
    const auto view = registry.RegisterView(
      *pass_constants_buffer_, std::move(handle), view_desc);
    CHECK_F(view->IsValid(),
      "GroundGridPass: failed to register pass constants view");
  }
}

auto GroundGridPass::ReleasePassConstantsBuffer() -> void
{
  if (pass_constants_buffer_ == nullptr) {
    pass_constants_mapped_ptr_ = nullptr;
    pass_constants_indices_.fill(kInvalidShaderVisibleIndex);
    pass_constants_slot_ = 0U;
    return;
  }

  if (pass_constants_buffer_->IsMapped()) {
    pass_constants_buffer_->UnMap();
  }

  if (auto gfx = renderer_.GetGraphics(); gfx != nullptr) {
    auto& registry = gfx->GetResourceRegistry();
    if (registry.Contains(*pass_constants_buffer_)) {
      registry.UnRegisterResource(*pass_constants_buffer_);
    }
  }

  pass_constants_mapped_ptr_ = nullptr;
  pass_constants_buffer_.reset();
  pass_constants_indices_.fill(kInvalidShaderVisibleIndex);
  pass_constants_slot_ = 0U;
}

auto GroundGridPass::UpdatePassConstants(const RenderContext& ctx)
  -> ShaderVisibleIndex
{
  EnsurePassConstantsBuffer();
  CHECK_NOTNULL_F(pass_constants_mapped_ptr_);

  PassConstants constants {};
  constants.inv_view_proj = ComputeInvViewProj(ctx);
  FillConstants(constants);
  ComputeGridOffset(constants, ctx);

  const auto slot = pass_constants_slot_ % kPassConstantsSlots;
  pass_constants_slot_++;
  std::memcpy(pass_constants_mapped_ptr_ + (slot * kPassConstantsStride),
    &constants, sizeof(constants));
  return pass_constants_indices_[slot];
}

auto GroundGridPass::ComputeInvViewProj(const RenderContext& ctx) const
  -> glm::mat4
{
  if (ctx.current_view.resolved_view == nullptr) {
    return glm::mat4 { 1.0F };
  }

  const auto& view = ctx.current_view.resolved_view->ViewMatrix();
  auto view_no_trans = view;
  view_no_trans[3] = glm::vec4(0.0F, 0.0F, 0.0F, 1.0F);
  const auto& proj = ctx.current_view.resolved_view->ProjectionMatrix();
  return glm::inverse(proj * view_no_trans);
}

auto GroundGridPass::ComputeGridOffset(
  PassConstants& constants, const RenderContext& ctx) -> void
{
  if (ctx.current_view.resolved_view == nullptr) {
    return;
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
  auto shortest_wrapped_delta = [period](double current, double target) -> double {
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

  if (config.smooth_motion) {
    if (first_frame_) {
      smooth_grid_offset_ = snapped_grid_offset;
      smooth_grid_velocity_ = glm::dvec2(0.0);
      first_frame_ = false;
    } else {
      const auto dt = static_cast<double>(ctx.delta_time);
      const double smooth_time
        = std::max<double>(config.smooth_time, kMinSmoothTime);
      const double omega = 2.0 / smooth_time;
      const double x = omega * dt;
      const double exp = 1.0
        / (1.0 + x + kCritDampCoeff1 * x * x + kCritDampCoeff2 * x * x * x);

      glm::dvec2 target = snapped_grid_offset;
      target.x = smooth_grid_offset_.x
        + shortest_wrapped_delta(
          smooth_grid_offset_.x, snapped_grid_offset.x);
      target.y = smooth_grid_offset_.y
        + shortest_wrapped_delta(
          smooth_grid_offset_.y, snapped_grid_offset.y);

      const glm::dvec2 change = smooth_grid_offset_ - target;
      const glm::dvec2 temp = (smooth_grid_velocity_ + omega * change) * dt;
      smooth_grid_velocity_ = (smooth_grid_velocity_ - omega * temp) * exp;
      smooth_grid_offset_ = target + (change + temp) * exp;

      if (glm::length(change) > kTeleportThreshold) {
        smooth_grid_offset_ = snapped_grid_offset;
        smooth_grid_velocity_ = glm::dvec2(0.0);
      }

      smooth_grid_offset_.x = wrap(smooth_grid_offset_.x);
      smooth_grid_offset_.y = wrap(smooth_grid_offset_.y);
    }
    effective_grid_offset = smooth_grid_offset_;
  } else {
    smooth_grid_offset_ = snapped_grid_offset;
    smooth_grid_velocity_ = glm::dvec2(0.0);
    first_frame_ = true;
  }

  constants.grid_offset_x = static_cast<float>(effective_grid_offset.x);
  constants.grid_offset_y = static_cast<float>(effective_grid_offset.y);
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
