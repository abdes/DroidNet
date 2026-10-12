//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>

#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/vector_float4.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Bindless/Generated.RootSignature.D3D12.h>
#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Core/Types/ShaderType.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/PipelineState.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Profiling/CpuProfileScope.h>
#include <Oxygen/Profiling/GpuEventScope.h>
#include <Oxygen/Profiling/ProfileScope.h>
#include <Oxygen/Vortex/Diagnostics/Internal/GpuWorkStageTable.h>
#include <Oxygen/Vortex/Internal/BindlessRootBindings.h>
#include <Oxygen/Vortex/Internal/StructuredGpuBuffer.h>
#include <Oxygen/Vortex/PreparedSceneFrame.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/Shadows/Passes/Generated.ShadowPassContracts.h>
#include <Oxygen/Vortex/Shadows/Passes/ShadowReceiverFitPass.h>
#include <Oxygen/Vortex/Shadows/Types/Generated.ShadowContracts.h>
#include <Oxygen/Vortex/Upload/TransientStructuredBuffer.h>

namespace oxygen::vortex::shadows {

namespace {

  namespace bindless_d3d12 = oxygen::bindless::generated::d3d12;

  constexpr auto kDebugName = "Vortex.Stage8.ShadowReceiverFit";
  constexpr auto kAnalysisShader = "Vortex/Services/Shadows/Conventional/"
                                   "ConventionalShadowReceiverAnalysis.hlsl";
  constexpr auto kMaskShader = "Vortex/Services/Shadows/Conventional/"
                               "ConventionalShadowReceiverMask.hlsl";
  constexpr std::uint32_t kLinearGroupSize = 64U;
  constexpr std::uint32_t kScreenGroupSize = 8U;
  constexpr std::uint32_t kBoundsStride = sizeof(ShadowReceiverBounds);

  enum class Kernel : std::uint8_t {
    kClear,
    kDepth,
    kTranslucent,
    kFit,
    kMaskClear,
    kMaskDepth,
    kMaskTranslucent,
    kMaskDilate,
  };
  constexpr std::size_t kKernelCount = 8U;

  struct KernelSource {
    std::string_view shader;
    std::string_view entry_point;
  };

  constexpr auto kKernels = std::array<KernelSource, kKernelCount> {
    KernelSource { kAnalysisShader, "VortexShadowReceiverClearCS" },
    KernelSource { kAnalysisShader, "VortexShadowReceiverDepthCS" },
    KernelSource { kAnalysisShader, "VortexShadowReceiverTranslucentCS" },
    KernelSource { kAnalysisShader, "VortexShadowReceiverFitCS" },
    KernelSource { kMaskShader, "VortexShadowReceiverMaskClearCS" },
    KernelSource { kMaskShader, "VortexShadowReceiverMaskDepthCS" },
    KernelSource { kMaskShader, "VortexShadowReceiverMaskTranslucentCS" },
    KernelSource { kMaskShader, "VortexShadowReceiverMaskDilateCS" },
  };

  auto BuildPipelineDesc(const Kernel kernel) -> graphics::ComputePipelineDesc
  {
    const auto root_bindings = vortex::internal::BuildVortexRootBindings();
    const auto& source = kKernels.at(static_cast<std::size_t>(kernel));
    return graphics::ComputePipelineDesc::Builder()
      .SetComputeShader({
        .stage = ShaderType::kCompute,
        .source_path = std::string { source.shader },
        .entry_point = std::string { source.entry_point },
      })
      .SetRootBindings(std::span(root_bindings))
      .SetDebugName(
        std::string { kDebugName } + "." + std::string { source.entry_point })
      .Build();
  }

  auto GroupCount(const std::uint32_t threads, const std::uint32_t group_size)
    -> std::uint32_t
  {
    return (threads + group_size - 1U) / group_size;
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

  auto TrackTexture(graphics::CommandRecorder& recorder,
    const graphics::Texture& texture) -> void
  {
    if (recorder.IsResourceTracked(texture)
      || recorder.AdoptKnownResourceState(texture)) {
      return;
    }
    recorder.BeginTrackingResourceState(
      texture, graphics::ResourceStates::kCommon, false);
  }

} // namespace

struct ShadowReceiverFitPass::Impl {
  //! One view's fitted records, receiver bounds and masks.
  struct ViewState {
    vortex::internal::StructuredGpuBuffer records {
      std::string { kDebugName } + ".Cascades",
      static_cast<std::uint32_t>(sizeof(ShadowCascadeBinding)),
    };
    vortex::internal::StructuredGpuBuffer bounds {
      std::string { kDebugName } + ".Bounds",
      kBoundsStride,
    };
    vortex::internal::StructuredGpuBuffer mask {
      std::string { kDebugName } + ".Mask",
      static_cast<std::uint32_t>(sizeof(std::uint32_t)),
    };
    std::uint32_t cascade_count { 0U };
  };

  explicit Impl(Renderer& renderer_in)
    : renderer(&renderer_in)
  {
  }

  auto EnsureUploads() -> void
  {
    if (inputs.has_value()) {
      return;
    }
    auto gfx = renderer->GetGraphics();
    const auto transfers
      = observer_ptr { &renderer->GetInlineTransfersCoordinator() };
    inputs.emplace(observer_ptr { gfx.get() }, renderer->GetStagingProvider(),
      static_cast<std::uint32_t>(sizeof(ShadowReceiverFitInputs)), transfers,
      std::string { kDebugName } + ".Inputs");
    loose.emplace(observer_ptr { gfx.get() }, renderer->GetStagingProvider(),
      static_cast<std::uint32_t>(sizeof(ShadowCascadeBinding)), transfers,
      std::string { kDebugName } + ".LooseCascades");
    if (frame.has_value()) {
      inputs->OnFrameStart(*frame, slot);
      loose->OnFrameStart(*frame, slot);
    }
  }

  auto Pipeline(const Kernel kernel) -> const graphics::ComputePipelineDesc&
  {
    auto& pipeline = pipelines.at(static_cast<std::size_t>(kernel));
    if (!pipeline.has_value()) {
      pipeline = BuildPipelineDesc(kernel);
    }
    return *pipeline;
  }

  observer_ptr<Renderer> renderer;
  std::optional<upload::TransientStructuredBuffer> inputs;
  std::optional<upload::TransientStructuredBuffer> loose;
  std::optional<frame::SequenceNumber> frame;
  frame::Slot slot { frame::kInvalidSlot };
  std::array<std::optional<graphics::ComputePipelineDesc>, kKernelCount>
    pipelines;
  std::unordered_map<ViewId, ViewState> views;
};

ShadowReceiverFitPass::ShadowReceiverFitPass(Renderer& renderer)
  : impl_(std::make_unique<Impl>(renderer))
{
}

ShadowReceiverFitPass::~ShadowReceiverFitPass() = default;

auto ShadowReceiverFitPass::OnFrameStart(
  const frame::SequenceNumber sequence, const frame::Slot slot) -> void
{
  impl_->frame = sequence;
  impl_->slot = slot;
  if (impl_->inputs.has_value()) {
    impl_->inputs->OnFrameStart(sequence, slot);
    impl_->loose->OnFrameStart(sequence, slot);
  }
}

auto ShadowReceiverFitPass::Record(graphics::CommandRecorder& recorder,
  const ViewId view_id, const PreparedSceneFrame& prepared_frame,
  const ShadowReceiverView& receivers,
  const std::span<const ShadowCascadeBinding> loose)
  -> std::optional<FittedCascades>
{
  auto gfx = impl_->renderer->GetGraphics();
  if (loose.empty() || gfx == nullptr || receivers.scene_depth == nullptr
    || !receivers.scene_depth_srv.IsValid() || receivers.viewport.z < 1.0F
    || receivers.viewport.w < 1.0F || !impl_->frame.has_value()) {
    return std::nullopt;
  }
  profiling::CpuProfileScope cpu_scope(
    "Vortex.Shadows.ReceiverFit", profiling::ProfileCategory::kPass);
  impl_->EnsureUploads();
  const auto cascade_count = static_cast<std::uint32_t>(loose.size());
  auto& view = impl_->views[view_id];
  view.cascade_count = 0U;
  if (!view.records.Ensure(gfx, cascade_count)
    || !view.bounds.Ensure(gfx, cascade_count)
    || !view.mask.Ensure(gfx, cascade_count * kShadowReceiverMaskBlock)) {
    LOG_F(WARNING, "{}: view {} keeps its loose cascades", kDebugName,
      view_id.get());
    return std::nullopt;
  }
  const auto loose_records = impl_->loose->Allocate(cascade_count);
  if (!loose_records.has_value() || !loose_records->srv.IsValid()
    || !loose_records->TryWriteRange(loose)) {
    LOG_F(WARNING, "{}: no upload space for the loose cascades of view {}",
      kDebugName, view_id.get());
    return std::nullopt;
  }
  const auto draw_count
    = static_cast<std::uint32_t>(prepared_frame.GetDrawMetadata().size());
  const auto record = ShadowReceiverFitInputs {
    .inverse_view_projection = receivers.inverse_view_projection,
    .view_matrix = receivers.view_matrix,
    .viewport = receivers.viewport,
    .scene_depth_srv = receivers.scene_depth_srv,
    .cascade_records_uav = view.records.GetUav(),
    .loose_cascade_records_srv = loose_records->srv,
    .cascade_count = cascade_count,
    .bounds_uav = view.bounds.GetUav(),
    .mask_uav = view.mask.GetUav(),
    .draw_cull_records_srv = prepared_frame.bindless_draw_cull_records_slot,
    .draw_metadata_srv = prepared_frame.bindless_draw_metadata_slot,
    .worlds_srv = prepared_frame.bindless_worlds_slot,
    .draw_count = draw_count,
    .near_plane = receivers.near_plane,
  };
  const auto inputs = impl_->inputs->Allocate(1U);
  if (!inputs.has_value() || !inputs->srv.IsValid()
    || !inputs->TryWriteObject(record)) {
    LOG_F(WARNING, "{}: no upload space for the inputs of view {}", kDebugName,
      view_id.get());
    return std::nullopt;
  }

  graphics::GpuEventScope scope(recorder, gpu_work::kShadowReceiverFitLabel,
    profiling::ProfileGranularity::kTelemetry,
    profiling::ProfileCategory::kPass);
  const auto buffers = std::array {
    view.records.GetBuffer(),
    view.bounds.GetBuffer(),
    view.mask.GetBuffer(),
  };
  for (auto* buffer : buffers) {
    TrackFromKnownOrCommon(recorder, *buffer);
    recorder.EnableAutoMemoryBarriers(*buffer);
    recorder.RequireResourceState(
      *buffer, graphics::ResourceStates::kUnorderedAccess);
  }
  TrackTexture(recorder, *receivers.scene_depth);
  recorder.RequireResourceState(
    *receivers.scene_depth, graphics::ResourceStates::kShaderResource);
  recorder.FlushBarriers();

  const auto root_constants
    = static_cast<std::uint32_t>(bindless_d3d12::RootParam::kRootConstants);
  const auto dispatch = [&](const Kernel kernel, const std::uint32_t x,
                          const std::uint32_t y) -> void {
    if (x == 0U || y == 0U) {
      return;
    }
    recorder.SetPipelineState(impl_->Pipeline(kernel));
    recorder.SetComputeRoot32BitConstant(root_constants, 0U, 0U);
    recorder.SetComputeRoot32BitConstant(root_constants, inputs->srv.get(), 1U);
    recorder.Dispatch(x, y, 1U);
  };
  // Each step reads what the previous one wrote.
  const auto barrier = [&]() -> void {
    for (auto* buffer : buffers) {
      recorder.RequireResourceState(
        *buffer, graphics::ResourceStates::kUnorderedAccess);
    }
    recorder.FlushBarriers();
  };
  const auto screen_x = GroupCount(
    static_cast<std::uint32_t>(receivers.viewport.z), kScreenGroupSize);
  const auto screen_y = GroupCount(
    static_cast<std::uint32_t>(receivers.viewport.w), kScreenGroupSize);
  const auto draw_groups = GroupCount(draw_count, kLinearGroupSize);

  dispatch(Kernel::kClear, GroupCount(cascade_count, kLinearGroupSize), 1U);
  dispatch(Kernel::kMaskClear,
    GroupCount(cascade_count * kShadowReceiverMaskBlock, kLinearGroupSize), 1U);
  barrier();
  dispatch(Kernel::kDepth, screen_x, screen_y);
  dispatch(Kernel::kTranslucent, draw_groups, 1U);
  barrier();
  dispatch(Kernel::kFit, GroupCount(cascade_count, kLinearGroupSize), 1U);
  barrier();
  dispatch(Kernel::kMaskDepth, screen_x, screen_y);
  dispatch(Kernel::kMaskTranslucent, draw_groups, 1U);
  barrier();
  dispatch(Kernel::kMaskDilate,
    GroupCount(cascade_count * kShadowReceiverMaskTiles, kLinearGroupSize), 1U);

  for (auto* buffer : buffers) {
    recorder.DisableAutoMemoryBarriers(*buffer);
  }
  recorder.RequireResourceState(
    *view.records.GetBuffer(), graphics::ResourceStates::kShaderResource);
  recorder.RequireResourceState(
    *view.mask.GetBuffer(), graphics::ResourceStates::kShaderResource);
  recorder.FlushBarriers();
  view.cascade_count = cascade_count;
  return FittedCascades {
    .records_srv = view.records.GetSrv(),
    .receiver_mask_srv = view.mask.GetSrv(),
  };
}

auto ShadowReceiverFitPass::Inspect(const ViewId view_id) const
  -> std::optional<Inspection>
{
  const auto found = impl_->views.find(view_id);
  if (found == impl_->views.end() || found->second.cascade_count == 0U) {
    return std::nullopt;
  }
  return Inspection {
    .records = observer_ptr<const graphics::Buffer> {
      found->second.records.GetBuffer(),
    },
    .receiver_mask = observer_ptr<const graphics::Buffer> {
      found->second.mask.GetBuffer(),
    },
    .cascade_count = found->second.cascade_count,
  };
}

auto ShadowReceiverFitPass::RemoveView(const ViewId view_id) -> void
{
  impl_->views.erase(view_id);
}

} // namespace oxygen::vortex::shadows
