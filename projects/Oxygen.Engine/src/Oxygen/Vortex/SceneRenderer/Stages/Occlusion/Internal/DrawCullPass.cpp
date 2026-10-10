//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/vector_float4.hpp>
#include <glm/gtc/matrix_access.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Bindless/Generated.RootSignature.D3D12.h>
#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Core/Types/ShaderType.h>
#include <Oxygen/Graphics/Common/Buffer.h>
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

  // Mirror the OCCLUSION_CULL_* flags of OcclusionCull.hlsl.
  constexpr std::uint32_t kOcclusionEnabledFlag = 1U << 0U;
  constexpr std::uint32_t kHistoryValidFlag = 1U << 1U;
  constexpr std::uint32_t kPyramidValidFlag = 1U << 2U;

  enum class Kernel : std::uint8_t {
    kStatsClear,
    kPhase1,
    kPhase2,
  };
  constexpr std::size_t kKernelCount = 3U;

  auto EntryPoint(const Kernel kernel) -> std::string_view
  {
    switch (kernel) {
    case Kernel::kStatsClear:
      return "VortexOcclusionStatsClearCS";
    case Kernel::kPhase1:
      return "VortexOcclusionPhase1CS";
    case Kernel::kPhase2:
      return "VortexOcclusionPhase2CS";
    }
    return {};
  }

  auto BuildPipelineDesc(const Kernel kernel, const std::string& debug_name)
    -> graphics::ComputePipelineDesc
  {
    const auto root_bindings = vortex::internal::BuildVortexRootBindings();
    const auto entry_point = EntryPoint(kernel);
    return graphics::ComputePipelineDesc::Builder()
      .SetComputeShader({
        .stage = ShaderType::kCompute,
        .source_path = "Vortex/Stages/Occlusion/OcclusionCull.hlsl",
        .entry_point = std::string { entry_point },
      })
      .SetRootBindings(std::span(root_bindings))
      .SetDebugName(debug_name + "." + std::string { entry_point })
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

  //! Projection terms P[2][2], P[3][2], P[2][3], P[3][3] (column, row) that
  //! turn device depth back into view depth.
  auto DepthTerms(const glm::mat4& projection) -> glm::vec4
  {
    const auto z_column = glm::column(projection, 2);
    const auto w_column = glm::column(projection, 3);
    return glm::vec4 { z_column.z, w_column.z, z_column.w, w_column.w };
  }

  auto MakePassConstants(const DrawCullInputs& inputs,
    const ShaderVisibleIndex visibility_uav, const std::uint32_t draw_count)
    -> OcclusionCullPassConstants
  {
    const auto& frame = *inputs.prepared_frame;
    const auto& projection = inputs.projection_matrix;
    auto flags = std::uint32_t { 0U };
    flags |= inputs.occlusion_enabled ? kOcclusionEnabledFlag : 0U;
    flags |= inputs.history.valid ? kHistoryValidFlag : 0U;
    return OcclusionCullPassConstants {
      .view_projection = projection * inputs.view_matrix,
      .viewport
      = glm::vec4 { inputs.viewport.top_left_x, inputs.viewport.top_left_y,
        inputs.viewport.width, inputs.viewport.height },
      .clip_rect = glm::vec4 { static_cast<float>(inputs.scissors.left),
        static_cast<float>(inputs.scissors.top),
        static_cast<float>(inputs.scissors.right),
        static_cast<float>(inputs.scissors.bottom) },
      .depth_terms = DepthTerms(projection),
      .draw_metadata_srv = frame.bindless_draw_metadata_slot,
      .cull_records_srv = frame.bindless_draw_cull_records_slot,
      .worlds_srv = frame.bindless_worlds_slot,
      .visibility_uav = visibility_uav,
      .history_slots_srv = inputs.history.slots_srv,
      .history_uav = inputs.history.history_uav,
      .stats_uav = inputs.history.stats_uav,
      .draw_count = draw_count,
      .flags = flags,
      .depth_bias = inputs.depth_bias,
    };
  }

  auto GroupCount(const std::uint32_t draw_count) -> std::uint32_t
  {
    return (draw_count + kThreadGroupSize - 1U) / kThreadGroupSize;
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
  //! One pipeline per `Kernel`.
  std::array<std::optional<graphics::ComputePipelineDesc>, kKernelCount>
    pipelines;
  //! One visibility buffer per phase 1 of the current frame.
  std::vector<std::unique_ptr<vortex::internal::StructuredGpuBuffer>>
    visibility_buffers;
  std::optional<frame::SequenceNumber> frame;
  std::size_t next_buffer { 0U };

  auto Pipeline(const Kernel kernel) -> const graphics::ComputePipelineDesc&
  {
    auto& pipeline = pipelines.at(static_cast<std::size_t>(kernel));
    if (!pipeline.has_value()) {
      pipeline = BuildPipelineDesc(kernel, debug_name);
    }
    return *pipeline;
  }

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

  //! The visibility buffer of a phase 1 recorded this frame.
  [[nodiscard]] auto FindVisibilityBuffer(
    const DrawVisibilityProducts& products) const
    -> vortex::internal::StructuredGpuBuffer*
  {
    const auto used = std::span(visibility_buffers).first(next_buffer);
    const auto found
      = std::ranges::find_if(used, [&](const auto& buffer) -> bool {
          return buffer->GetBuffer() == products.buffer.get();
        });
    return found == used.end() ? nullptr : found->get();
  }

  //! Publishes one phase's constants; nothing when the upload fails.
  auto PublishConstants(
    const DrawCullInputs& inputs, const OcclusionCullPassConstants& constants)
    -> std::optional<ShaderVisibleIndex>
  {
    if (!pass_constants.has_value()) {
      auto gfx = renderer->GetGraphics();
      pass_constants.emplace(observer_ptr { gfx.get() },
        renderer->GetStagingProvider(),
        static_cast<std::uint32_t>(sizeof(OcclusionCullPassConstants)),
        observer_ptr { &renderer->GetInlineTransfersCoordinator() },
        debug_name + ".PassConstants");
    }
    pass_constants->OnFrameStart(inputs.frame_sequence, inputs.frame_slot);
    const auto allocation = pass_constants->Allocate(1U);
    if (!allocation.has_value() || !allocation->TryWriteObject(constants)) {
      LOG_F(ERROR, "{}: failed to publish the pass constants", debug_name);
      return std::nullopt;
    }
    return allocation->srv;
  }

  auto Dispatch(graphics::CommandRecorder& recorder, const Kernel kernel,
    const ShaderVisibleIndex constants_srv, const std::uint32_t group_count)
    -> void
  {
    recorder.SetPipelineState(Pipeline(kernel));
    const auto root_constants
      = static_cast<std::uint32_t>(bindless_d3d12::RootParam::kRootConstants);
    recorder.SetComputeRoot32BitConstant(root_constants, 0U, 0U);
    recorder.SetComputeRoot32BitConstant(
      root_constants, constants_srv.get(), 1U);
    recorder.Dispatch(group_count, 1U, 1U);
  }

  //! Puts the history and stats buffers in UAV state, with UAV barriers
  //! between the dispatches that share them.
  static auto BeginHistoryAccess(
    graphics::CommandRecorder& recorder, const DrawCullHistory& history) -> void
  {
    for (const auto* buffer : { history.history.get(), history.stats.get() }) {
      if (buffer == nullptr) {
        continue;
      }
      TrackFromKnownOrCommon(recorder, *buffer);
      recorder.EnableAutoMemoryBarriers(*buffer);
      recorder.RequireResourceState(
        *buffer, graphics::ResourceStates::kUnorderedAccess);
    }
  }

  static auto EndHistoryAccess(
    graphics::CommandRecorder& recorder, const DrawCullHistory& history) -> void
  {
    for (const auto* buffer : { history.history.get(), history.stats.get() }) {
      if (buffer != nullptr) {
        recorder.DisableAutoMemoryBarriers(*buffer);
      }
    }
  }
};

DrawCullPass::DrawCullPass(
  Renderer& renderer, const std::string_view debug_name)
  : impl_(std::make_unique<Impl>(renderer, debug_name))
{
}

DrawCullPass::~DrawCullPass() = default;

auto DrawCullPass::RunPhase1(graphics::CommandRecorder& recorder,
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
  const auto constants = impl_->PublishConstants(
    inputs, MakePassConstants(inputs, visibility.GetUav(), draw_count));
  if (!constants.has_value()) {
    return {};
  }

  graphics::GpuEventScope scope(recorder, impl_->debug_name + ".Phase1",
    profiling::ProfileGranularity::kTelemetry,
    profiling::ProfileCategory::kPass);
  auto& buffer = *visibility.GetBuffer();
  TrackFromKnownOrCommon(recorder, buffer);
  recorder.RequireResourceState(
    buffer, graphics::ResourceStates::kUnorderedAccess);
  Impl::BeginHistoryAccess(recorder, inputs.history);
  recorder.FlushBarriers();

  if (inputs.history.stats != nullptr) {
    impl_->Dispatch(recorder, Kernel::kStatsClear, *constants, 1U);
    recorder.RequireResourceState(
      *inputs.history.stats, graphics::ResourceStates::kUnorderedAccess);
    recorder.FlushBarriers();
  }
  impl_->Dispatch(
    recorder, Kernel::kPhase1, *constants, GroupCount(draw_count));
  Impl::EndHistoryAccess(recorder, inputs.history);

  recorder.RequireResourceState(
    buffer, graphics::ResourceStates::kShaderResource);
  return DrawVisibilityProducts {
    .buffer = observer_ptr<const graphics::Buffer> { &buffer },
    .srv = visibility.GetSrv(),
    .draw_count = draw_count,
    .phase2 = false,
  };
}

auto DrawCullPass::RunPhase2(graphics::CommandRecorder& recorder,
  const DrawCullInputs& inputs, const DrawVisibilityProducts& phase1,
  const std::optional<OcclusionPyramidBinding>& pyramid)
  -> DrawVisibilityProducts
{
  auto* visibility
    = phase1.IsValid() ? impl_->FindVisibilityBuffer(phase1) : nullptr;
  if (visibility == nullptr) {
    return phase1;
  }
  auto constants_value
    = MakePassConstants(inputs, visibility->GetUav(), phase1.draw_count);
  if (pyramid.has_value() && pyramid->srv.IsValid()) {
    constants_value.flags |= kPyramidValidFlag;
    constants_value.pyramid_srv = pyramid->srv;
    constants_value.pyramid_source = glm::uvec4 {
      pyramid->origin_x,
      pyramid->origin_y,
      pyramid->width,
      pyramid->height,
    };
  }
  const auto constants = impl_->PublishConstants(inputs, constants_value);
  if (!constants.has_value()) {
    return phase1;
  }

  graphics::GpuEventScope scope(recorder, impl_->debug_name + ".Phase2",
    profiling::ProfileGranularity::kTelemetry,
    profiling::ProfileCategory::kPass);
  auto& buffer = *visibility->GetBuffer();
  recorder.RequireResourceState(
    buffer, graphics::ResourceStates::kUnorderedAccess);
  if (pyramid.has_value() && pyramid->srv.IsValid()
    && pyramid->texture != nullptr) {
    recorder.RequireResourceState(
      *pyramid->texture, graphics::ResourceStates::kShaderResource);
  }
  Impl::BeginHistoryAccess(recorder, inputs.history);
  recorder.FlushBarriers();
  impl_->Dispatch(
    recorder, Kernel::kPhase2, *constants, GroupCount(phase1.draw_count));
  Impl::EndHistoryAccess(recorder, inputs.history);

  recorder.RequireResourceState(
    buffer, graphics::ResourceStates::kShaderResource);
  auto products = phase1;
  products.phase2 = true;
  return products;
}

} // namespace oxygen::vortex::occlusion::internal
