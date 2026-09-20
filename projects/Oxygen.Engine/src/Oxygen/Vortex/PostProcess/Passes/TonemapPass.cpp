//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <bit>
#include <cstring>
#include <limits>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/Bindless/Generated.RootSignature.D3D12.h>
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
#include <Oxygen/Vortex/Internal/PerViewStructuredPublisher.h>
#include <Oxygen/Vortex/Internal/ViewportClamp.h>
#include <Oxygen/Vortex/PostProcess/Passes/TonemapPass.h>
#include <Oxygen/Vortex/RenderContext.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/SceneRenderer/SceneTextures.h>

namespace oxygen::vortex::postprocess {

namespace {

  namespace bindless_d3d12 = oxygen::bindless::generated::d3d12;

  constexpr auto kPassConstantsStride = 256U;
  constexpr auto kPassConstantsSlots = 8U;

  struct alignas(16) TonemapPassConstants {
    std::uint32_t source_texture_index;
    std::uint32_t exposure_buffer_index;
    std::uint32_t bloom_texture_index;
    std::uint32_t tone_mapper;
    float exposure;
    float gamma;
    float bloom_intensity;
    std::uint32_t frame_exposure_srv;
    std::array<float, 3> background_color;
    std::uint32_t background_enabled;
    std::uint32_t fallback_texture_index;
    std::uint32_t conversion_report_index;
    std::array<std::uint32_t, 2> reserved {};
  };

  static_assert(sizeof(TonemapPassConstants) == 64U);
  static_assert(offsetof(TonemapPassConstants, fallback_texture_index) == 48U);
  static_assert(offsetof(TonemapPassConstants, conversion_report_index) == 52U);

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
    if (recorder.IsResourceTracked(texture)) {
      return;
    }
    if (recorder.AdoptKnownResourceState(texture)) {
      return;
    }

    const auto initial = texture.GetDescriptor().initial_state;
    CHECK_F(initial != graphics::ResourceStates::kUnknown
        && initial != graphics::ResourceStates::kUndefined,
      "TonemapPass: cannot track '{}' without a known or declared initial "
      "state",
      texture.GetName());
    recorder.BeginTrackingResourceState(texture, initial, false);
  }

  auto TrackBufferAsShaderReadable(
    graphics::CommandRecorder& recorder, const graphics::Buffer& buffer) -> void
  {
    if (recorder.IsResourceTracked(buffer)
      || recorder.AdoptKnownResourceState(buffer)) {
      return;
    }
    recorder.BeginTrackingResourceState(
      buffer, graphics::ResourceStates::kShaderResource, false);
  }

  auto SetViewportAndScissor(graphics::CommandRecorder& recorder,
    const RenderContext& ctx, const graphics::Framebuffer& target) -> void
  {
    CHECK_F(!target.GetDescriptor().color_attachments.empty()
        && target.GetDescriptor().color_attachments.front().texture != nullptr,
      "TonemapPass: a color attachment is required for Stage 22 visible "
      "output");
    const auto& target_desc = target.GetDescriptor()
                                .color_attachments.front()
                                .texture->GetDescriptor();
    if (ctx.current_view.resolved_view != nullptr) {
      const auto clamped
        = oxygen::vortex::internal::ResolveClampedViewportState(
          ctx.current_view.resolved_view->Viewport(),
          ctx.current_view.resolved_view->Scissor(), target_desc.width,
          target_desc.height);
      recorder.SetViewport(clamped.viewport);
      recorder.SetScissors(clamped.scissors);
      return;
    }

    recorder.SetViewport({
      .top_left_x = 0.0F,
      .top_left_y = 0.0F,
      .width = static_cast<float>(target_desc.width),
      .height = static_cast<float>(target_desc.height),
      .min_depth = 0.0F,
      .max_depth = 1.0F,
    });
    recorder.SetScissors({
      .left = 0,
      .top = 0,
      .right = static_cast<std::int32_t>(target_desc.width),
      .bottom = static_cast<std::int32_t>(target_desc.height),
    });
  }

  auto BuildTonemapPipelineDesc(const graphics::Framebuffer& target)
    -> graphics::GraphicsPipelineDesc
  {
    CHECK_F(!target.GetDescriptor().color_attachments.empty()
        && target.GetDescriptor().color_attachments.front().texture != nullptr,
      "TonemapPass: a color attachment is required for Stage 22 visible "
      "output");
    const auto& target_desc = target.GetDescriptor()
                                .color_attachments.front()
                                .texture->GetDescriptor();
    auto root_bindings = BuildVortexRootBindings();
    return graphics::GraphicsPipelineDesc::Builder {}
      .SetVertexShader(graphics::ShaderRequest {
        .stage = ShaderType::kVertex,
        .source_path = "Vortex/Services/PostProcess/Tonemap.hlsl",
        .entry_point = "VortexTonemapVS",
      })
      .SetPixelShader(graphics::ShaderRequest {
        .stage = ShaderType::kPixel,
        .source_path = "Vortex/Services/PostProcess/Tonemap.hlsl",
        .entry_point = "VortexTonemapPS",
      })
      .SetPrimitiveTopology(graphics::PrimitiveType::kTriangleList)
      .SetRasterizerState(graphics::RasterizerStateDesc::NoCulling())
      .SetDepthStencilState(graphics::DepthStencilStateDesc::Disabled())
      .SetBlendState({})
      .SetFramebufferLayout(graphics::FramebufferLayoutDesc {
        .color_target_formats = { target_desc.format },
        .sample_count = target_desc.sample_count,
        .sample_quality = target_desc.sample_quality,
      })
      .SetRootBindings(std::span<const graphics::RootBindingItem>(
        root_bindings.data(), root_bindings.size()))
      .SetDebugName("Vortex.PostProcess.Tonemap")
      .Build();
  }

} // namespace

TonemapPass::TonemapPass(Renderer& renderer)
  : renderer_(renderer)
{
}

TonemapPass::~TonemapPass() = default;

auto TonemapPass::Record(RenderContext& ctx,
  graphics::CommandRecorder& recorder, const Inputs& inputs) -> RecordingState
{
  auto state = RecordingState {
    .requested
    = inputs.scene_signal != nullptr && inputs.post_target != nullptr,
  };
  if (!state.requested) {
    return state;
  }

  CHECK_F(!inputs.post_target->GetDescriptor().color_attachments.empty()
      && inputs.post_target->GetDescriptor().color_attachments.front().texture
        != nullptr,
    "TonemapPass: Stage 22 requires a valid post target color attachment");

  auto& target_color
    = *inputs.post_target->GetDescriptor().color_attachments.front().texture;
  renderer_.GetDiagnosticsService().AttachGpuTimelineCollector(recorder);

  TrackTextureFromKnownOrInitial(recorder, *inputs.scene_signal);
  const bool checked_resolve = inputs.conversion_report != nullptr;
  CHECK_F(checked_resolve == inputs.conversion_report_srv.IsValid()
    && checked_resolve == (inputs.scene_fallback != nullptr)
    && checked_resolve == inputs.scene_fallback_srv.IsValid());
  if (checked_resolve) {
    const auto& source = inputs.scene_signal->GetDescriptor();
    const auto& fallback = inputs.scene_fallback->GetDescriptor();
    CHECK_F(source.format == Format::kRGBA16Float
      && fallback.format == Format::kRGBA32Float
      && source.width == fallback.width && source.height == fallback.height);
    TrackTextureFromKnownOrInitial(recorder, *inputs.scene_fallback);
    TrackBufferAsShaderReadable(recorder, *inputs.conversion_report);
    recorder.RequireResourceState(
      *inputs.scene_fallback, graphics::ResourceStates::kShaderResource);
    recorder.RequireResourceState(
      *inputs.conversion_report, graphics::ResourceStates::kShaderResource);
  }
  TrackTextureFromKnownOrInitial(recorder, target_color);
  if (inputs.exposure_buffer != nullptr) {
    TrackBufferAsShaderReadable(recorder, *inputs.exposure_buffer);
  }
  recorder.RequireResourceState(
    *inputs.scene_signal, graphics::ResourceStates::kShaderResource);
  if (inputs.exposure_buffer != nullptr) {
    recorder.RequireResourceState(
      *inputs.exposure_buffer, graphics::ResourceStates::kShaderResource);
  }
  CHECK_F((inputs.frame_exposure_buffer != nullptr)
    == inputs.frame_exposure_srv.IsValid());
  if (inputs.frame_exposure_buffer) {
    TrackBufferAsShaderReadable(recorder, *inputs.frame_exposure_buffer);
    recorder.RequireResourceState(
      *inputs.frame_exposure_buffer, graphics::ResourceStates::kShaderResource);
  }
  recorder.RequireResourceState(
    target_color, graphics::ResourceStates::kRenderTarget);
  recorder.FlushBarriers();
  recorder.BindFrameBuffer(*inputs.post_target);
  SetViewportAndScissor(recorder, ctx, *inputs.post_target);
  graphics::GpuEventScope pass_scope(recorder, "Vortex.PostProcess.Tonemap",
    profiling::ProfileGranularity::kTelemetry,
    profiling::ProfileCategory::kPass);
  recorder.SetPipelineState(BuildTonemapPipelineDesc(*inputs.post_target));
  const auto pass_constants_index = UpdatePassConstants(ctx, inputs);
  recorder.SetGraphicsRoot32BitConstant(
    static_cast<std::uint32_t>(bindless_d3d12::RootParam::kRootConstants), 0U,
    0U);
  recorder.SetGraphicsRoot32BitConstant(
    static_cast<std::uint32_t>(bindless_d3d12::RootParam::kRootConstants),
    pass_constants_index.get(), 1U);
  recorder.Draw(3U, 1U, 0U, 0U);
  recorder.RequireResourceState(
    *inputs.scene_signal, graphics::ResourceStates::kShaderResource);
  recorder.RequireResourceState(
    target_color, graphics::ResourceStates::kRenderTarget);

  state.recorded = true;
  state.writes_visible_output = true;
  return state;
}

auto TonemapPass::UpdatePassConstants(RenderContext& ctx, const Inputs& inputs)
  -> ShaderVisibleIndex
{
  if (!constants_publisher_) {
    auto gfx = renderer_.GetGraphics();
    CHECK_NOTNULL_F(gfx.get());
    constants_publisher_
      = std::make_unique<::oxygen::vortex::internal::PerViewStructuredPublisher<
        std::array<std::uint32_t, 16U>>>(observer_ptr { gfx.get() },
        renderer_.GetStagingProvider(),
        observer_ptr { &renderer_.GetInlineTransfersCoordinator() },
        "Vortex.PostProcess.Tonemap.Constants");
  }
  if (constants_frame_ != ctx.frame_sequence) {
    constants_publisher_->OnFrameStart(ctx.frame_sequence, ctx.frame_slot);
    constants_frame_ = ctx.frame_sequence;
  }

  const auto background_color = inputs.background_color.value_or(Vec3 { 0.0F });
  const auto constants = TonemapPassConstants {
    .source_texture_index = inputs.scene_signal_srv.get(),
    .exposure_buffer_index = inputs.exposure_buffer_srv.get(),
    .bloom_texture_index = inputs.bloom_texture_srv.get(),
    .tone_mapper = static_cast<std::uint32_t>(inputs.tone_mapper),
    .exposure = std::max(inputs.exposure_value, 0.0F),
    .gamma = std::max(inputs.gamma, 1.0e-4F),
    .bloom_intensity = std::max(inputs.bloom_intensity, 0.0F),
    .frame_exposure_srv = inputs.frame_exposure_srv.get(),
    .background_color = {
      background_color.x,
      background_color.y,
      background_color.z,
    },
    .background_enabled = inputs.background_color.has_value() ? 1U : 0U,
    .fallback_texture_index = inputs.scene_fallback_srv.get(),
    .conversion_report_index = inputs.conversion_report_srv.get(),
  };

  const auto slot = constants_publisher_->Publish(ctx.current_view.view_id,
    std::bit_cast<std::array<std::uint32_t, 16U>>(constants));
  CHECK_F(slot.IsValid(), "Tonemap constants publication failed");
  return slot;
}

} // namespace oxygen::vortex::postprocess
