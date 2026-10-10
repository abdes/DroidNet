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
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/SceneRenderer/Stages/Occlusion/Internal/IndirectListBuilder.h>
#include <Oxygen/Vortex/SceneRenderer/Stages/Occlusion/Types/DrawVisibility.h>
#include <Oxygen/Vortex/Upload/TransientStructuredBuffer.h>

namespace oxygen::vortex::occlusion::internal {

namespace {

  namespace bindless_d3d12 = oxygen::bindless::generated::d3d12;

  constexpr std::uint32_t kGroupSize = 256U;
  constexpr std::uint32_t kCommandUints
    = sizeof(IndirectDrawCommand) / sizeof(std::uint32_t);

  //! Mirrors IndirectCandidate in ListCompaction.hlsl.
  struct GpuIndirectCandidate {
    std::uint32_t draw_index { 0U };
    std::uint32_t vertex_count { 0U };
    std::uint32_t instance_count { 0U };
    std::uint32_t segment { 0U };
  };
  static_assert(sizeof(GpuIndirectCandidate) == 16U);

  //! Mirrors IndirectSegment in ListCompaction.hlsl.
  struct GpuIndirectSegment {
    std::uint32_t first_candidate { 0U };
    std::uint32_t candidate_count { 0U };
    std::uint32_t _pad0 { 0U };
    std::uint32_t _pad1 { 0U };
  };
  static_assert(sizeof(GpuIndirectSegment) == 16U);

  //! Mirrors ListCompactionPassConstants in ListCompaction.hlsl.
  struct ListCompactionPassConstants {
    ShaderVisibleIndex candidates_srv { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex segments_srv { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex visibility_srv { kInvalidShaderVisibleIndex };
    std::uint32_t predicate_mask { 0U };
    std::uint32_t candidate_count { 0U };
    std::uint32_t group_count { 0U };
    ShaderVisibleIndex scan_uav { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex arguments_uav { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex counts_uav { kInvalidShaderVisibleIndex };
    std::uint32_t group_prefix_base { 0U };
    std::uint32_t _pad0 { 0U };
    std::uint32_t _pad1 { 0U };
  };
  static_assert(
    sizeof(ListCompactionPassConstants) == 48U); // NOLINT(*-magic-numbers)

  enum class Kernel : std::uint8_t { kScan, kGroups, kScatter };

  auto BuildPipelineDesc(const Kernel kernel, const std::string& debug_name)
    -> graphics::ComputePipelineDesc
  {
    const auto entry_point = [kernel] -> std::string_view {
      switch (kernel) {
      case Kernel::kScan:
        return "VortexListCompactionScanCS";
      case Kernel::kGroups:
        return "VortexListCompactionGroupsCS";
      case Kernel::kScatter:
        return "VortexListCompactionScatterCS";
      }
      return {};
    }();
    const auto root_bindings = vortex::internal::BuildVortexRootBindings();
    return graphics::ComputePipelineDesc::Builder()
      .SetComputeShader({
        .stage = ShaderType::kCompute,
        .source_path = "Vortex/Stages/Occlusion/ListCompaction.hlsl",
        .entry_point = std::string(entry_point),
      })
      .SetRootBindings(std::span(root_bindings))
      .SetDebugName(debug_name + "." + std::string(entry_point))
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

  //! Cuts `candidates` into runs of equal raster state.
  auto MakeSegments(std::span<const IndirectDrawCandidate> candidates)
    -> std::vector<IndirectDrawSegment>
  {
    auto segments = std::vector<IndirectDrawSegment> {};
    for (const auto& [position, candidate] :
      std::views::enumerate(candidates)) {
      if (segments.empty()
        || segments.back().raster_state != candidate.raster_state) {
        segments.push_back(IndirectDrawSegment {
          .raster_state = candidate.raster_state,
          .first_candidate = static_cast<std::uint32_t>(position),
          .candidate_count = 0U,
        });
      }
      ++segments.back().candidate_count;
    }
    return segments;
  }

  //! The scan and argument buffers of one `Build`.
  struct ListBuffers {
    explicit ListBuffers(const std::string& name)
      : scan(name + ".Scan", sizeof(std::uint32_t))
      , arguments(name + ".Arguments", sizeof(std::uint32_t),
          graphics::BufferUsage::kIndirect)
      , counts(name + ".Counts", sizeof(std::uint32_t),
          graphics::BufferUsage::kIndirect)
    {
    }

    vortex::internal::StructuredGpuBuffer scan;
    vortex::internal::StructuredGpuBuffer arguments;
    //! One count per segment.
    vortex::internal::StructuredGpuBuffer counts;
  };

} // namespace

struct IndirectListBuilder::Impl {
  Impl(Renderer& renderer_in, const std::string_view debug_name_in)
    : renderer(&renderer_in)
    , debug_name(debug_name_in)
  {
  }

  observer_ptr<Renderer> renderer;
  std::string debug_name;
  std::optional<upload::TransientStructuredBuffer> pass_constants;
  std::optional<upload::TransientStructuredBuffer> candidate_upload;
  std::optional<upload::TransientStructuredBuffer> segment_upload;
  std::array<std::optional<graphics::ComputePipelineDesc>, 3> pipelines;
  //! One buffer set per `Build` of the current frame.
  std::vector<std::unique_ptr<ListBuffers>> buffers;
  std::optional<frame::SequenceNumber> frame;
  std::size_t next_buffers { 0U };

  auto EnsureUploads(Graphics& gfx) -> void
  {
    if (pass_constants.has_value()) {
      return;
    }
    const auto make
      = [&](const std::uint32_t stride,
          const char* suffix) -> upload::TransientStructuredBuffer {
      return { observer_ptr { &gfx }, renderer->GetStagingProvider(), stride,
        observer_ptr { &renderer->GetInlineTransfersCoordinator() },
        debug_name + suffix };
    };
    pass_constants.emplace(
      make(static_cast<std::uint32_t>(sizeof(ListCompactionPassConstants)),
        ".PassConstants"));
    candidate_upload.emplace(make(
      static_cast<std::uint32_t>(sizeof(GpuIndirectCandidate)), ".Candidates"));
    segment_upload.emplace(make(
      static_cast<std::uint32_t>(sizeof(GpuIndirectSegment)), ".Segments"));
  }

  auto AcquireBuffers(const frame::SequenceNumber sequence) -> ListBuffers&
  {
    if (frame != sequence) {
      frame = sequence;
      next_buffers = 0U;
    }
    if (next_buffers == buffers.size()) {
      buffers.push_back(std::make_unique<ListBuffers>(
        debug_name + ".List" + std::to_string(next_buffers)));
    }
    return *buffers.at(next_buffers++);
  }

  auto Pipeline(const Kernel kernel) -> const graphics::ComputePipelineDesc&
  {
    auto& pipeline = pipelines.at(static_cast<std::size_t>(kernel));
    if (!pipeline.has_value()) {
      pipeline = BuildPipelineDesc(kernel, debug_name);
    }
    return *pipeline;
  }

  //! Uploads the candidates and segments, then the pass constants that name
  //! them; the constants' SRV, invalid on failure.
  auto Upload(const frame::SequenceNumber sequence, const frame::Slot slot,
    const std::span<const IndirectDrawCandidate> candidates,
    const std::span<const IndirectDrawSegment> segments,
    ListCompactionPassConstants& constants) -> ShaderVisibleIndex
  {
    auto gpu_candidates = std::vector<GpuIndirectCandidate> {};
    gpu_candidates.reserve(candidates.size());
    auto gpu_segments = std::vector<GpuIndirectSegment> {};
    gpu_segments.reserve(segments.size());
    for (const auto& [index, segment] : std::views::enumerate(segments)) {
      gpu_segments.push_back(GpuIndirectSegment {
        .first_candidate = segment.first_candidate,
        .candidate_count = segment.candidate_count,
      });
      for (const auto& candidate :
        candidates.subspan(segment.first_candidate, segment.candidate_count)) {
        gpu_candidates.push_back(GpuIndirectCandidate {
          .draw_index = candidate.draw_index,
          .vertex_count = candidate.vertex_count,
          .instance_count = candidate.instance_count,
          .segment = static_cast<std::uint32_t>(index),
        });
      }
    }

    if (!pass_constants.has_value() || !candidate_upload.has_value()
      || !segment_upload.has_value()) {
      return kInvalidShaderVisibleIndex;
    }
    pass_constants->OnFrameStart(sequence, slot);
    candidate_upload->OnFrameStart(sequence, slot);
    segment_upload->OnFrameStart(sequence, slot);
    const auto candidate_alloc = candidate_upload->Allocate(
      static_cast<std::uint32_t>(candidates.size()));
    const auto segment_alloc
      = segment_upload->Allocate(static_cast<std::uint32_t>(segments.size()));
    if (!candidate_alloc.has_value() || !segment_alloc.has_value()
      || !candidate_alloc->TryWriteRange(std::span(gpu_candidates))
      || !segment_alloc->TryWriteRange(std::span(gpu_segments))) {
      return kInvalidShaderVisibleIndex;
    }
    constants.candidates_srv = candidate_alloc->srv;
    constants.segments_srv = segment_alloc->srv;
    const auto constants_alloc = pass_constants->Allocate(1U);
    if (!constants_alloc.has_value()
      || !constants_alloc->TryWriteObject(constants)) {
      return kInvalidShaderVisibleIndex;
    }
    return constants_alloc->srv;
  }

  //! Records the scan, group-prefix and scatter dispatches.
  auto RecordCompaction(graphics::CommandRecorder& recorder,
    ListBuffers& list_buffers, const ShaderVisibleIndex constants_srv,
    const std::uint32_t group_count) -> void
  {
    auto& scan = *list_buffers.scan.GetBuffer();
    auto& arguments = *list_buffers.arguments.GetBuffer();
    auto& counts = *list_buffers.counts.GetBuffer();
    TrackFromKnownOrCommon(recorder, scan);
    TrackFromKnownOrCommon(recorder, arguments);
    TrackFromKnownOrCommon(recorder, counts);
    // Makes each later UAV transition of the scan buffer a UAV barrier, so a
    // dispatch reads what the previous one wrote.
    recorder.EnableAutoMemoryBarriers(scan);
    recorder.RequireResourceState(
      scan, graphics::ResourceStates::kUnorderedAccess);
    recorder.RequireResourceState(
      arguments, graphics::ResourceStates::kUnorderedAccess);
    recorder.RequireResourceState(
      counts, graphics::ResourceStates::kUnorderedAccess);
    recorder.FlushBarriers();

    const auto root_constants
      = static_cast<std::uint32_t>(bindless_d3d12::RootParam::kRootConstants);
    const auto dispatch
      = [&](const Kernel kernel, const std::uint32_t groups) -> void {
      recorder.SetPipelineState(Pipeline(kernel));
      recorder.SetComputeRoot32BitConstant(root_constants, 0U, 0U);
      recorder.SetComputeRoot32BitConstant(
        root_constants, constants_srv.get(), 1U);
      recorder.Dispatch(groups, 1U, 1U);
    };
    dispatch(Kernel::kScan, group_count);
    recorder.RequireResourceState(
      scan, graphics::ResourceStates::kUnorderedAccess);
    recorder.FlushBarriers();
    dispatch(Kernel::kGroups, 1U);
    recorder.RequireResourceState(
      scan, graphics::ResourceStates::kUnorderedAccess);
    recorder.FlushBarriers();
    dispatch(Kernel::kScatter, group_count);

    recorder.DisableAutoMemoryBarriers(scan);
    recorder.RequireResourceState(
      arguments, graphics::ResourceStates::kIndirectArgument);
    recorder.RequireResourceState(
      counts, graphics::ResourceStates::kIndirectArgument);
    recorder.FlushBarriers();
  }
};

IndirectListBuilder::IndirectListBuilder(
  Renderer& renderer, const std::string_view debug_name)
  : impl_(std::make_unique<Impl>(renderer, debug_name))
{
}

IndirectListBuilder::~IndirectListBuilder() = default;

auto IndirectListBuilder::Build(graphics::CommandRecorder& recorder,
  const frame::SequenceNumber frame_sequence, const frame::Slot frame_slot,
  const std::span<const IndirectDrawCandidate> candidates,
  const DrawVisibilityProducts& visibility,
  const DrawVisibilityPredicate predicate) -> IndirectDrawList
{
  if (candidates.empty()) {
    return {};
  }
  if (frame_slot == frame::kInvalidSlot) {
    LOG_F(ERROR, "{}: no frame slot; {} candidates not drawn",
      impl_->debug_name, candidates.size());
    return {};
  }
  auto gfx = impl_->renderer->GetGraphics();
  if (gfx == nullptr) {
    return {};
  }

  auto segments = MakeSegments(candidates);
  const auto candidate_count = static_cast<std::uint32_t>(candidates.size());
  const auto segment_count = static_cast<std::uint32_t>(segments.size());
  const auto group_count = (candidate_count + kGroupSize - 1U) / kGroupSize;

  auto& buffers = impl_->AcquireBuffers(frame_sequence);
  if (!buffers.scan.Ensure(gfx, candidate_count + group_count)
    || !buffers.arguments.Ensure(gfx, candidate_count * kCommandUints)
    || !buffers.counts.Ensure(gfx, segment_count)) {
    return {};
  }

  impl_->EnsureUploads(*gfx);
  const auto keeps_all = predicate.KeepsAll() || !visibility.IsValid();
  auto constants = ListCompactionPassConstants {
    .visibility_srv = keeps_all ? kInvalidShaderVisibleIndex : visibility.srv,
    .predicate_mask = keeps_all ? 0U : predicate.Mask(),
    .candidate_count = candidate_count,
    .group_count = group_count,
    .scan_uav = buffers.scan.GetUav(),
    .arguments_uav = buffers.arguments.GetUav(),
    .counts_uav = buffers.counts.GetUav(),
    .group_prefix_base = candidate_count,
  };
  const auto constants_srv = impl_->Upload(
    frame_sequence, frame_slot, candidates, segments, constants);
  if (!constants_srv.IsValid()) {
    LOG_F(ERROR, "{}: failed to upload the list", impl_->debug_name);
    return {};
  }

  graphics::GpuEventScope scope(recorder, impl_->debug_name,
    profiling::ProfileGranularity::kDiagnostic,
    profiling::ProfileCategory::kPass);
  if (!keeps_all) {
    TrackFromKnownOrCommon(recorder, *visibility.buffer);
    recorder.RequireResourceState(
      *visibility.buffer, graphics::ResourceStates::kShaderResource);
  }
  impl_->RecordCompaction(recorder, buffers, constants_srv, group_count);

  return IndirectDrawList {
    .arguments
    = observer_ptr<const graphics::Buffer> { buffers.arguments.GetBuffer() },
    .counts
    = observer_ptr<const graphics::Buffer> { buffers.counts.GetBuffer() },
    .segments = std::move(segments),
  };
}

auto IndirectListBuilder::Draw(graphics::CommandRecorder& recorder,
  const IndirectDrawList& list, const BindPipeline& bind_pipeline) -> void
{
  if (list.IsEmpty()) {
    return;
  }
  const auto command_desc = graphics::CommandRecorder::IndirectCommandDesc {
    .kind = graphics::CommandRecorder::IndirectCommandKind::kDraw,
    .push_constants = graphics::CommandRecorder::IndirectPushConstantsDesc {
      .binding_slot_desc = graphics::BindingSlotDesc {
        .register_index = bindless_d3d12::kRootConstantsRegister,
        .register_space = bindless_d3d12::kRootConstantsSpace,
      },
      .dest_offset_in_32bit_values = 0U,
      .value_count = 1U,
    },
  };
  for (const auto& [segment_index, segment] :
    std::views::enumerate(list.segments)) {
    bind_pipeline(segment.raster_state);
    recorder.ExecuteIndirect(*list.arguments, command_desc,
      graphics::CommandRecorder::IndirectExecutionDesc {
        .argument_buffer_range = graphics::BufferRange {
          static_cast<std::uint64_t>(segment.first_candidate)
            * sizeof(IndirectDrawCommand),
          static_cast<std::uint64_t>(segment.candidate_count)
            * sizeof(IndirectDrawCommand),
        },
        .command_count = graphics::CommandRecorder::IndirectCommandCount {
          segment.candidate_count },
        .count_buffer = list.counts,
        .count_buffer_range = graphics::BufferRange {
          static_cast<std::uint64_t>(segment_index) * sizeof(std::uint32_t),
          sizeof(std::uint32_t) },
      });
  }
}

} // namespace oxygen::vortex::occlusion::internal
