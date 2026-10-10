//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Bindless/Generated.RootSignature.D3D12.h>
#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Core/Types/ShaderType.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/PipelineState.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Profiling/GpuEventScope.h>
#include <Oxygen/Profiling/ProfileScope.h>
#include <Oxygen/Vortex/Internal/BindlessRootBindings.h>
#include <Oxygen/Vortex/Internal/StructuredGpuBuffer.h>
#include <Oxygen/Vortex/PreparedSceneFrame.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/SceneRenderer/Stages/Occlusion/Internal/DrawCullPass.h>
#include <Oxygen/Vortex/SceneRenderer/Stages/Occlusion/Types/DrawVisibility.h>
#include <Oxygen/Vortex/Upload/TransientStructuredBuffer.h>

namespace oxygen::vortex::occlusion::internal {

namespace {

  namespace bindless_d3d12 = oxygen::bindless::generated::d3d12;

  constexpr std::uint32_t kThreadGroupSize = 64U;

  auto BuildPipelineDesc(const std::string& debug_name)
    -> graphics::ComputePipelineDesc
  {
    const auto root_bindings = vortex::internal::BuildVortexRootBindings();
    return graphics::ComputePipelineDesc::Builder()
      .SetComputeShader({
        .stage = ShaderType::kCompute,
        .source_path = "Vortex/Stages/Occlusion/OcclusionCull.hlsl",
        .entry_point = "VortexOcclusionCullCS",
      })
      .SetRootBindings(std::span(root_bindings))
      .SetDebugName(debug_name)
      .Build();
  }

  auto TrackFromKnownOrCommon(
    graphics::CommandRecorder& recorder, const graphics::Buffer& buffer) -> void
  {
    if (recorder.IsResourceTracked(buffer)
      || recorder.AdoptKnownResourceState(buffer)) {
      return;
    }
    recorder.BeginTrackingResourceState(
      buffer, graphics::ResourceStates::kCommon, true);
  }

  auto MakePassConstants(const DrawCullInputs& inputs,
    const ShaderVisibleIndex visibility_uav, const std::uint32_t draw_count)
    -> OcclusionCullPassConstants
  {
    const auto& frame = *inputs.prepared_frame;
    return OcclusionCullPassConstants {
      .view_projection = inputs.view_projection,
      .viewport
      = glm::vec4 { inputs.viewport.top_left_x, inputs.viewport.top_left_y,
        inputs.viewport.width, inputs.viewport.height },
      .clip_rect = glm::vec4 { static_cast<float>(inputs.scissors.left),
        static_cast<float>(inputs.scissors.top),
        static_cast<float>(inputs.scissors.right),
        static_cast<float>(inputs.scissors.bottom) },
      .draw_metadata_srv = frame.bindless_draw_metadata_slot,
      .cull_records_srv = frame.bindless_draw_cull_records_slot,
      .worlds_srv = frame.bindless_worlds_slot,
      .visibility_uav = visibility_uav,
      .draw_count = draw_count,
    };
  }

} // namespace

struct DrawCullPass::Impl {
  Impl(Renderer& renderer_in, const std::string_view debug_name_in)
    : renderer(&renderer_in)
    , debug_name(debug_name_in)
  {
  }

  observer_ptr<Renderer> renderer;
  std::string debug_name;
  std::optional<upload::TransientStructuredBuffer> pass_constants;
  std::optional<graphics::ComputePipelineDesc> pipeline_desc;
  //! One visibility buffer per `Run` of the current frame.
  std::vector<std::unique_ptr<vortex::internal::StructuredGpuBuffer>>
    visibility_buffers;
  std::optional<frame::SequenceNumber> frame;
  std::size_t next_buffer { 0U };

  auto AcquireVisibilityBuffer(const frame::SequenceNumber sequence)
    -> vortex::internal::StructuredGpuBuffer&
  {
    if (frame != sequence) {
      frame = sequence;
      next_buffer = 0U;
    }
    if (next_buffer == visibility_buffers.size()) {
      visibility_buffers.push_back(
        std::make_unique<vortex::internal::StructuredGpuBuffer>(
          debug_name + ".Visibility" + std::to_string(next_buffer),
          static_cast<std::uint32_t>(sizeof(std::uint32_t))));
    }
    return *visibility_buffers.at(next_buffer++);
  }
};

DrawCullPass::DrawCullPass(
  Renderer& renderer, const std::string_view debug_name)
  : impl_(std::make_unique<Impl>(renderer, debug_name))
{
}

DrawCullPass::~DrawCullPass() = default;

auto DrawCullPass::Run(graphics::CommandRecorder& recorder,
  const DrawCullInputs& inputs) -> DrawVisibilityProducts
{
  if (inputs.prepared_frame == nullptr
    || inputs.frame_slot == frame::kInvalidSlot) {
    return {};
  }
  const auto& prepared_frame = *inputs.prepared_frame;
  const auto draw_count
    = static_cast<std::uint32_t>(prepared_frame.GetDrawMetadata().size());
  if (draw_count == 0U || !prepared_frame.bindless_draw_metadata_slot.IsValid()
    || !prepared_frame.bindless_draw_cull_records_slot.IsValid()) {
    return {};
  }

  auto gfx = impl_->renderer->GetGraphics();
  if (gfx == nullptr) {
    return {};
  }
  auto& visibility = impl_->AcquireVisibilityBuffer(inputs.frame_sequence);
  if (!visibility.Ensure(gfx, draw_count)) {
    return {};
  }

  if (!impl_->pass_constants.has_value()) {
    impl_->pass_constants.emplace(observer_ptr { gfx.get() },
      impl_->renderer->GetStagingProvider(),
      static_cast<std::uint32_t>(sizeof(OcclusionCullPassConstants)),
      observer_ptr { &impl_->renderer->GetInlineTransfersCoordinator() },
      impl_->debug_name + ".PassConstants");
  }
  impl_->pass_constants->OnFrameStart(inputs.frame_sequence, inputs.frame_slot);
  const auto constants = impl_->pass_constants->Allocate(1U);
  if (!constants.has_value()
    || !constants->TryWriteObject(
      MakePassConstants(inputs, visibility.GetUav(), draw_count))) {
    LOG_F(ERROR, "{}: failed to publish the pass constants", impl_->debug_name);
    return {};
  }

  if (!impl_->pipeline_desc.has_value()) {
    impl_->pipeline_desc = BuildPipelineDesc(impl_->debug_name);
  }

  graphics::GpuEventScope scope(recorder, impl_->debug_name,
    profiling::ProfileGranularity::kTelemetry,
    profiling::ProfileCategory::kPass);
  auto& buffer = *visibility.GetBuffer();
  TrackFromKnownOrCommon(recorder, buffer);
  recorder.RequireResourceState(
    buffer, graphics::ResourceStates::kUnorderedAccess);
  recorder.FlushBarriers();

  recorder.SetPipelineState(*impl_->pipeline_desc);
  const auto root_constants
    = static_cast<std::uint32_t>(bindless_d3d12::RootParam::kRootConstants);
  recorder.SetComputeRoot32BitConstant(root_constants, 0U, 0U);
  recorder.SetComputeRoot32BitConstant(
    root_constants, constants->srv.get(), 1U);
  recorder.Dispatch(
    (draw_count + kThreadGroupSize - 1U) / kThreadGroupSize, 1U, 1U);

  recorder.RequireResourceState(
    buffer, graphics::ResourceStates::kShaderResource);
  return DrawVisibilityProducts {
    .buffer = observer_ptr<const graphics::Buffer> { &buffer },
    .srv = visibility.GetSrv(),
    .draw_count = draw_count,
  };
}

} // namespace oxygen::vortex::occlusion::internal
