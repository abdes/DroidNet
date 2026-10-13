//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <utility>

#include <fmt/format.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Bindless/Generated.RootSignature.D3D12.h>
#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Core/Types/ResolvedView.h>
#include <Oxygen/Core/Types/ShaderType.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/PipelineState.h>
#include <Oxygen/Graphics/Common/ReadbackManager.h>
#include <Oxygen/Graphics/Common/ReadbackTypes.h>
#include <Oxygen/Graphics/Common/Shaders.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Profiling/CpuProfileScope.h>
#include <Oxygen/Profiling/GpuEventScope.h>
#include <Oxygen/Profiling/ProfileScope.h>
#include <Oxygen/Vortex/Diagnostics/DiagnosticsService.h>
#include <Oxygen/Vortex/Diagnostics/DiagnosticsTypes.h>
#include <Oxygen/Vortex/Diagnostics/Internal/GpuWorkStageTable.h>
#include <Oxygen/Vortex/Diagnostics/Passes/GpuDebugPrimitivePass.h>
#include <Oxygen/Vortex/Internal/BindlessRootBindings.h>
#include <Oxygen/Vortex/Internal/PerViewStructuredPublisher.h>
#include <Oxygen/Vortex/Internal/StructuredGpuBuffer.h>
#include <Oxygen/Vortex/Internal/ViewportClamp.h>
#include <Oxygen/Vortex/RenderContext.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/Types/DebugFrameBindings.h>

namespace oxygen::vortex {

namespace {

  namespace bindless_d3d12 = oxygen::bindless::generated::d3d12;

  constexpr auto kDebugName = "Vortex.Diagnostics.GpuDebugPrimitives";
  constexpr auto kShaderDir = "Vortex/Services/Diagnostics/";
  constexpr auto kCounterCount = static_cast<std::uint32_t>(
    sizeof(GpuDebugCounters) / sizeof(std::uint32_t));
  //! The `D3D12_DRAW_ARGUMENTS` at the start of the counters.
  constexpr auto kDrawArgumentsSize = std::uint64_t { 16U };

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

  auto TrackTexture(graphics::CommandRecorder& recorder,
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

  auto BuildClearPipeline() -> graphics::ComputePipelineDesc
  {
    const auto root_bindings = internal::BuildVortexRootBindings();
    return graphics::ComputePipelineDesc::Builder()
      .SetComputeShader({
        .stage = ShaderType::kCompute,
        .source_path = std::string { kShaderDir } + "GpuDebugClear.hlsl",
        .entry_point = "VortexGpuDebugClearCS",
      })
      .SetRootBindings(std::span(root_bindings))
      .SetDebugName(std::string { kDebugName } + ".Clear")
      .Build();
  }

  auto BuildDrawPipeline(const graphics::Texture& target)
    -> graphics::GraphicsPipelineDesc
  {
    const auto root_bindings = internal::BuildVortexRootBindings();
    const auto& desc = target.GetDescriptor();
    const auto source = std::string { kShaderDir } + "GpuDebugDraw.hlsl";
    return graphics::GraphicsPipelineDesc::Builder {}
      .SetVertexShader(graphics::ShaderRequest {
        .stage = ShaderType::kVertex,
        .source_path = source,
        .entry_point = "VortexGpuDebugDrawVS",
      })
      .SetPixelShader(graphics::ShaderRequest {
        .stage = ShaderType::kPixel,
        .source_path = source,
        .entry_point = "VortexGpuDebugDrawPS",
      })
      .SetPrimitiveTopology(graphics::PrimitiveType::kLineList)
      .SetRasterizerState(graphics::RasterizerStateDesc::NoCulling())
      .SetDepthStencilState(graphics::DepthStencilStateDesc {
        .depth_test_enable = false,
        .depth_write_enable = false,
        .depth_func = graphics::CompareOp::kAlways,
        .stencil_enable = false,
      })
      .SetFramebufferLayout(graphics::FramebufferLayoutDesc {
        .color_target_formats = { desc.format },
        .depth_stencil_format = std::nullopt,
        .sample_count = desc.sample_count,
        .sample_quality = desc.sample_quality,
      })
      .SetRootBindings(std::span<const graphics::RootBindingItem>(
        root_bindings.data(), root_bindings.size()))
      .SetDebugName(std::string { kDebugName } + ".Draw")
      .Build();
  }

  auto ToStats(const GpuDebugCounters& counters,
    const frame::SequenceNumber frame_sequence,
    const std::uint32_t line_capacity) -> GpuDebugPrimitiveStats
  {
    return GpuDebugPrimitiveStats {
      .frame_sequence = frame_sequence,
      .line_capacity = line_capacity,
      .requested_lines = counters.requested_lines,
      .drawn_lines = counters.drawn_lines,
      .invalid_bindless_accesses = counters.invalid_bindless_accesses,
      .invalid_materials = counters.invalid_materials,
      .last_invalid_slot = counters.last_invalid_slot,
    };
  }

} // namespace

struct GpuDebugPrimitivePass::Impl {
  //! One view's buffers and the counters in flight.
  struct ViewState {
    internal::StructuredGpuBuffer lines {
      std::string { kDebugName } + ".Lines",
      static_cast<std::uint32_t>(sizeof(GpuDebugLine)),
    };
    internal::StructuredGpuBuffer counters {
      std::string { kDebugName } + ".Counters",
      static_cast<std::uint32_t>(sizeof(std::uint32_t)),
      graphics::BufferUsage::kIndirect,
    };
    std::shared_ptr<graphics::GpuBufferReadback> readback;
    //! The frame whose record and reset were recorded.
    std::optional<frame::SequenceNumber> prepared_frame;
    ShaderVisibleIndex record { kInvalidShaderVisibleIndex };
    std::uint32_t line_capacity { 0U };
    //! The frame and capacity of the counters in flight.
    frame::SequenceNumber readback_frame { 0U };
    std::uint32_t readback_capacity { 0U };
    std::optional<GpuDebugPrimitiveStats> stats;
  };

  explicit Impl(Renderer& renderer_in)
    : renderer(&renderer_in)
  {
  }

  auto BeginFrame(const RenderContext& ctx) -> void
  {
    if (!records) {
      auto gfx = renderer->GetGraphics();
      records = std::make_unique<
        internal::PerViewStructuredPublisher<DebugFrameBindings>>(
        observer_ptr { gfx.get() }, renderer->GetStagingProvider(),
        observer_ptr { &renderer->GetInlineTransfersCoordinator() },
        std::string { kDebugName } + ".Bindings");
    }
    if (frame != ctx.frame_sequence) {
      records->OnFrameStart(ctx.frame_sequence, ctx.frame_slot);
      frame = ctx.frame_sequence;
    }
  }

  //! Takes the counters of an earlier frame when their readback is done, and
  //! reports what they show.
  auto Collect(const ViewId view_id, ViewState& view) const -> void
  {
    if (view.readback == nullptr
      || view.readback->GetState() == graphics::ReadbackState::kIdle) {
      return;
    }
    const auto ready = view.readback->IsReady();
    if (ready.has_value() && !*ready) {
      return;
    }
    // A failed readback is dropped; the next frame counts again.
    if (ready.has_value()) {
      if (const auto mapped = view.readback->TryMap(); mapped.has_value()
        && mapped->Bytes().size() >= sizeof(GpuDebugCounters)) {
        auto counters = GpuDebugCounters {};
        std::memcpy(&counters, mapped->Bytes().data(), sizeof(counters));
        view.stats
          = ToStats(counters, view.readback_frame, view.readback_capacity);
      }
    }
    view.readback->Reset();
    if (view.stats.has_value()) {
      Report(view_id, *view.stats);
    }
  }

  auto Report(const ViewId view_id, const GpuDebugPrimitiveStats& stats) const
    -> void
  {
    auto& diagnostics = renderer->GetDiagnosticsService();
    if (stats.requested_lines > stats.line_capacity) {
      diagnostics.ReportIssue(MakeDiagnosticsIssue(
        DiagnosticsIssueCode::kGpuDebugOverflow, DiagnosticsSeverity::kWarning,
        fmt::format("view {} emitted {} GPU debug lines; {} were drawn",
          view_id.get(), stats.requested_lines, stats.drawn_lines)));
    }
    if (stats.invalid_bindless_accesses != 0U
      || stats.invalid_materials != 0U) {
      diagnostics.ReportIssue(
        MakeDiagnosticsIssue(DiagnosticsIssueCode::kGpuDebugInvalidAccess,
          DiagnosticsSeverity::kError,
          fmt::format("view {}: {} invalid bindless loads, {} draws without "
                      "their material; last slot {}",
            view_id.get(), stats.invalid_bindless_accesses,
            stats.invalid_materials, stats.last_invalid_slot)));
    }
  }

  observer_ptr<Renderer> renderer;
  std::unique_ptr<internal::PerViewStructuredPublisher<DebugFrameBindings>>
    records;
  std::optional<frame::SequenceNumber> frame;
  std::optional<graphics::ComputePipelineDesc> clear_pipeline;
  std::unordered_map<ViewId, ViewState> views;
};

GpuDebugPrimitivePass::GpuDebugPrimitivePass(Renderer& renderer)
  : impl_(std::make_unique<Impl>(renderer))
{
}

GpuDebugPrimitivePass::~GpuDebugPrimitivePass() = default;

auto GpuDebugPrimitivePass::Prepare(
  RenderContext& ctx, graphics::CommandRecorder& recorder) -> ShaderVisibleIndex
{
  const auto view_id = ctx.current_view.view_id;
  auto& diagnostics = impl_->renderer->GetDiagnosticsService();
  auto gfx = impl_->renderer->GetGraphics();
  if (!diagnostics.IsGpuDebugPrimitivesEnabled() || view_id == kInvalidViewId
    || gfx == nullptr) {
    impl_->views.erase(view_id);
    return kInvalidShaderVisibleIndex;
  }

  profiling::CpuProfileScope cpu_scope(
    "Vortex.Diagnostics.GpuDebugPrimitives.Prepare",
    profiling::ProfileCategory::kPass);
  impl_->BeginFrame(ctx);
  auto& view = impl_->views[view_id];
  view.prepared_frame.reset();
  impl_->Collect(view_id, view);

  const auto capacity = diagnostics.GetGpuDebugLineCapacity();
  if (!view.lines.Ensure(gfx, capacity)
    || !view.counters.Ensure(gfx, kCounterCount)) {
    LOG_F(WARNING, "{}: view {} renders without GPU debug buffers", kDebugName,
      view_id.get());
    return kInvalidShaderVisibleIndex;
  }
  const auto record = internal::SlotOrInvalid(
    impl_->records->Publish(view_id,
      DebugFrameBindings {
        .line_buffer_srv_slot = view.lines.GetSrv(),
        .line_buffer_uav_slot = view.lines.GetUav(),
        .counter_buffer_uav_slot = view.counters.GetUav(),
        .line_capacity = capacity,
      }),
    view_id, "GPU debug bindings");
  if (!record.IsValid()) {
    return kInvalidShaderVisibleIndex;
  }

  graphics::GpuEventScope scope(recorder,
    gpu_work::kGpuDebugPrimitivesClearLabel,
    profiling::ProfileGranularity::kTelemetry,
    profiling::ProfileCategory::kPass);
  auto& counters = *view.counters.GetBuffer();
  auto& lines = *view.lines.GetBuffer();
  TrackFromKnownOrCommon(recorder, counters);
  TrackFromKnownOrCommon(recorder, lines);
  recorder.EnableAutoMemoryBarriers(counters);
  recorder.RequireResourceState(
    counters, graphics::ResourceStates::kUnorderedAccess);
  recorder.RequireResourceState(
    lines, graphics::ResourceStates::kUnorderedAccess);
  recorder.FlushBarriers();
  if (!impl_->clear_pipeline.has_value()) {
    impl_->clear_pipeline = BuildClearPipeline();
  }
  recorder.SetPipelineState(*impl_->clear_pipeline);
  const auto root_constants
    = static_cast<std::uint32_t>(bindless_d3d12::RootParam::kRootConstants);
  recorder.SetComputeRoot32BitConstant(root_constants, 0U, 0U);
  recorder.SetComputeRoot32BitConstant(root_constants, record.get(), 1U);
  recorder.Dispatch(1U, 1U, 1U);
  // Producers append after the reset.
  recorder.RequireResourceState(
    counters, graphics::ResourceStates::kUnorderedAccess);
  recorder.FlushBarriers();
  recorder.DisableAutoMemoryBarriers(counters);

  view.prepared_frame = ctx.frame_sequence;
  view.record = record;
  view.line_capacity = capacity;
  ctx.current_view.gpu_debug_bindings_slot = record;
  return record;
}

auto GpuDebugPrimitivePass::Record(RenderContext& ctx,
  graphics::CommandRecorder& recorder, const graphics::Framebuffer& target)
  -> bool
{
  const auto found = impl_->views.find(ctx.current_view.view_id);
  auto gfx = impl_->renderer->GetGraphics();
  if (found == impl_->views.end() || gfx == nullptr
    || found->second.prepared_frame != ctx.frame_sequence
    || ctx.current_view.resolved_view == nullptr
    || !ctx.view_constants.IsValid()
    || target.GetDescriptor().color_attachments.empty()) {
    return false;
  }
  auto& view = found->second;
  const auto& attachment = target.GetDescriptor().color_attachments.front();
  const auto target_color = attachment.texture;
  if (target_color == nullptr) {
    return false;
  }
  auto output = gfx->CreateFramebuffer(
    graphics::FramebufferDesc {}.AddColorAttachment(attachment));
  if (!output) {
    return false;
  }

  graphics::GpuEventScope scope(recorder,
    gpu_work::kGpuDebugPrimitivesDrawLabel,
    profiling::ProfileGranularity::kTelemetry,
    profiling::ProfileCategory::kPass);
  auto& counters = *view.counters.GetBuffer();
  auto& lines = *view.lines.GetBuffer();
  TrackTexture(recorder, *target_color);
  recorder.RequireResourceState(
    *target_color, graphics::ResourceStates::kRenderTarget);
  recorder.RequireResourceState(
    lines, graphics::ResourceStates::kShaderResource);
  recorder.RequireResourceState(
    counters, graphics::ResourceStates::kIndirectArgument);
  recorder.FlushBarriers();
  recorder.BindFrameBuffer(*output);
  const auto& view_desc = *ctx.current_view.resolved_view;
  const auto& target_desc = target_color->GetDescriptor();
  const auto clamped
    = internal::ResolveClampedViewportState(view_desc.Viewport(),
      view_desc.Scissor(), target_desc.width, target_desc.height);
  recorder.SetViewport(clamped.viewport);
  recorder.SetScissors(clamped.scissors);
  recorder.SetPipelineState(BuildDrawPipeline(*target_color));
  recorder.SetGraphicsRootConstantBufferView(
    static_cast<std::uint32_t>(bindless_d3d12::RootParam::kViewConstants),
    ctx.view_constants.GpuAddress());
  const auto root_constants
    = static_cast<std::uint32_t>(bindless_d3d12::RootParam::kRootConstants);
  recorder.SetGraphicsRoot32BitConstant(root_constants, 0U, 0U);
  recorder.SetGraphicsRoot32BitConstant(root_constants, view.record.get(), 1U);
  recorder.ExecuteIndirect(counters,
    graphics::CommandRecorder::IndirectCommandDesc {
      .kind = graphics::CommandRecorder::IndirectCommandKind::kDraw,
    },
    graphics::CommandRecorder::IndirectExecutionDesc {
      .argument_buffer_range = { 0U, kDrawArgumentsSize },
      .command_count = graphics::CommandRecorder::IndirectCommandCount { 1U },
    });
  gfx->RegisterDeferredRelease(std::move(output));

  if (view.readback == nullptr) {
    if (const auto manager = gfx->GetReadbackManager(); manager != nullptr) {
      view.readback = manager->CreateBufferReadback(
        std::string { kDebugName } + ".Counters");
    }
  }
  if (view.readback != nullptr
    && view.readback->GetState() == graphics::ReadbackState::kIdle) {
    if (view.readback
          ->EnqueueCopy(recorder, counters,
            graphics::BufferRange { 0U, sizeof(GpuDebugCounters) })
          .has_value()) {
      view.readback_frame = ctx.frame_sequence;
      view.readback_capacity = view.line_capacity;
    } else {
      LOG_F(WARNING, "{}: counter readback failed", kDebugName);
    }
  }
  return true;
}

auto GpuDebugPrimitivePass::GetStats(const ViewId view_id) const
  -> std::optional<GpuDebugPrimitiveStats>
{
  const auto found = impl_->views.find(view_id);
  if (found == impl_->views.end()) {
    return std::nullopt;
  }
  return found->second.stats;
}

auto GpuDebugPrimitivePass::RemoveView(const ViewId view_id) -> void
{
  impl_->views.erase(view_id);
}

} // namespace oxygen::vortex
