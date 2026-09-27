//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <unordered_map>
#include <utility>

#include <glm/gtc/matrix_access.hpp>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Bindless/Generated.BindlessAbi.h>
#include <Oxygen/Core/Bindless/Generated.RootSignature.D3D12.h>
#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Core/Types/ShaderType.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Graphics/Common/AllocationBudget.h>
#include <Oxygen/Graphics/Common/AllocationBudgetTag.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/PipelineState.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Graphics/Common/Types/DescriptorVisibility.h>
#include <Oxygen/Graphics/Common/Types/QueueRole.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Graphics/Common/Types/ResourceViewType.h>
#include <Oxygen/Profiling/GpuEventScope.h>
#include <Oxygen/Profiling/ProfileScope.h>
#include <Oxygen/Vortex/Internal/BindlessRootBindings.h>
#include <Oxygen/Vortex/Internal/GpuFeedback.h>
#include <Oxygen/Vortex/Lighting/Internal/LightGridBuilder.h>
#include <Oxygen/Vortex/Lighting/Internal/SpatialLightGrid.h>
#include <Oxygen/Vortex/Lighting/Types/ClusterLightRange.h>
#include <Oxygen/Vortex/Lighting/Types/LightGridBuildStatus.h>
#include <Oxygen/Vortex/Lighting/Types/LightGridPassConstants.h>
#include <Oxygen/Vortex/Lighting/Types/LightGridResources.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/Types/LightingFrameBindings.h>
#include <Oxygen/Vortex/Upload/TransientStructuredBuffer.h>

namespace oxygen::vortex::lighting::internal {
namespace {
  struct GridBuffer {
    std::shared_ptr<graphics::Buffer> buffer;
    ShaderVisibleIndex srv { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex uav { kInvalidShaderVisibleIndex };
    std::uint32_t count { 0U };
  };

  auto MakePipeline() -> graphics::ComputePipelineDesc
  {
    const auto roots = vortex::internal::BuildVortexRootBindings();
    return graphics::ComputePipelineDesc::Builder {}
      .SetComputeShader({
        .stage = ShaderType::kCompute,
        .source_path = "Vortex/Services/Lighting/SpatialLightGrid.hlsl",
        .entry_point = "SpatialLightGridCS",
      })
      .SetRootBindings(roots)
      .SetDebugName("Vortex.Lighting.SpatialGrid")
      .Build();
  }
} // namespace

struct SpatialLightGrid::Impl {
  struct FrameResources {
    GridBuffer ranges;
    GridBuffer indices;
    GridBuffer counts;
    std::array<GridBuffer, 2> scan;
    GridBuffer status;
    vortex::internal::GpuFeedbackReservation feedback;
    frame::SequenceNumber feedback_sequence { 0U };
  };
  struct ViewResources {
    frame::SequenceNumber last_used { 0U };
    std::unique_ptr<vortex::internal::GpuFeedbackPool> feedback_pool;
    std::unordered_map<frame::Slot, FrameResources> frames;
    std::uint64_t measured_demand { 0U };
    frame::SequenceNumber feedback_sequence { 0U };
    bool have_demand { false };
    std::optional<CompletedLightGridBuild> completed;
  };
  observer_ptr<Renderer> renderer;
  frame::SequenceNumber sequence { 0U };
  frame::Slot slot { frame::kInvalidSlot };
  std::uint32_t active_view_count { 1U };
  std::unordered_map<ViewId, ViewResources> views;
  upload::TransientStructuredBuffer constants;

  explicit Impl(Renderer& owner)
    : renderer(make_observer(&owner))
    , constants(observer_ptr { owner.GetGraphics().get() },
        owner.GetLightingStagingProvider(), sizeof(LightGridPassConstants),
        observer_ptr { &owner.GetInlineTransfersCoordinator() },
        "LightingService.SpatialGrid.Constants")
  {
  }

  void Retire(GridBuffer& resource) const
  {
    auto gfx = renderer->GetGraphics();
    if (resource.buffer && gfx) {
      gfx->GetDeferredReclaimer().RegisterDeferredAction(
        [owner = gfx.get(),
          buffer = std::move(resource.buffer)] mutable -> void {
          owner->ForgetKnownResourceState(*buffer);
          auto& registry = owner->GetResourceRegistry();
          if (registry.Contains(*buffer)) {
            registry.UnRegisterResource(*buffer);
          }
          buffer.reset();
        });
    }
    resource = {};
  }

  void Retire(ViewResources& view) const
  {
    for (auto& [frame_slot, resources] : view.frames) {
      Retire(resources.ranges);
      Retire(resources.indices);
      Retire(resources.counts);
      Retire(resources.scan.at(0));
      Retire(resources.scan.at(1));
      Retire(resources.status);
    }
  }

  void ReadDemand(ViewResources& view)
  {
    for (auto& [frame_slot, resources] : view.frames) {
      if (!resources.feedback) {
        continue;
      }
      const auto packet = resources.feedback.Poll<LightGridBuildStatus>(
        vortex::internal::FeedbackPayloadSize::kAtLeast);
      if (packet && !packet->has_value()) {
        continue;
      }
      if (packet && packet->has_value()) {
        const auto& status = **packet;
        if (!view.completed
          || resources.feedback_sequence > view.completed->sequence) {
          view.completed = CompletedLightGridBuild {
            .sequence = resources.feedback_sequence,
            .status = status,
          };
        }
        if (status.state == kLightGridBuildValid
          && (!view.have_demand
            || resources.feedback_sequence > view.feedback_sequence)) {
          view.measured_demand = status.required_index_count.at(0)
            | (static_cast<std::uint64_t>(status.required_index_count.at(1))
              << 32U);
          view.feedback_sequence = resources.feedback_sequence;
          view.have_demand = true;
        }
      }
      resources.feedback.Reset();
    }
  }

  auto EnsureCompactIndices(GridBuffer& indices, std::uint32_t capacity) const
    -> std::uint32_t
  {
    if (capacity <= indices.count) {
      return indices.count;
    }
    try {
      Ensure(indices, capacity, sizeof(std::uint32_t),
        "LightingService.GridIndices",
        graphics::AllocationCategory::kCompactIndices);
    } catch (const graphics::AllocationBudgetExceeded&) {
      // A smaller existing allocation, or complete-list cells, preserves every
      // light.
      return indices.count;
    }
    return indices.count;
  }

  void Ensure(GridBuffer& target, std::uint32_t count, std::uint32_t stride,
    const char* name,
    graphics::AllocationCategory category
    = graphics::AllocationCategory::kGeneral) const
  {
    if (target.buffer && target.count >= count) {
      return;
    }
    auto gfx = renderer->GetGraphics();
    if (!gfx || count == 0U) {
      throw std::runtime_error("Invalid spatial grid allocation");
    }
    auto candidate = GridBuffer {};
    candidate.count = count;
    candidate.buffer = gfx->CreateBuffer({
      .size_bytes = static_cast<std::uint64_t>(count) * stride,
      .usage = graphics::BufferUsage::kStorage,
      .memory = graphics::BufferMemory::kDeviceLocal,
      .debug_name = name,
      .allocation_budget = { .owner = renderer->GetLightingAllocationBudget(),
        .category = category, },
    });
    if (!candidate.buffer) {
      throw std::runtime_error("Spatial grid buffer creation failed");
    }
    auto& registry = gfx->GetResourceRegistry();
    registry.Register(candidate.buffer);
    try {
      auto& allocator = gfx->GetDescriptorAllocator();
      for (auto type : {
             graphics::ResourceViewType::kStructuredBuffer_SRV,
             graphics::ResourceViewType::kStructuredBuffer_UAV,
           }) {
        auto handle = type == graphics::ResourceViewType::kStructuredBuffer_SRV
          ? allocator.AllocateBindless(
              bindless::generated::kGlobalSrvDomain, type)
          : allocator.AllocateRaw(
              type, graphics::DescriptorVisibility::kShaderVisible);
        if (!handle.IsValid()) {
          throw std::runtime_error("Spatial grid descriptor allocation failed");
        }
        const auto index = allocator.GetShaderVisibleIndex(handle);
        auto view = registry.RegisterView(*candidate.buffer, std::move(handle),
          graphics::BufferViewDescription {
            .view_type = type,
            .visibility = graphics::DescriptorVisibility::kShaderVisible,
            .range = { 0U, static_cast<std::uint64_t>(count) * stride },
            .stride = stride,
          });
        if (!view->IsValid()) {
          throw std::runtime_error("Spatial grid view creation failed");
        }
        (type == graphics::ResourceViewType::kStructuredBuffer_SRV
            ? candidate.srv
            : candidate.uav) = index;
      }
    } catch (...) {
      registry.UnRegisterResource(*candidate.buffer);
      throw;
    }
    Retire(target);
    target = std::move(candidate);
  }
};

SpatialLightGrid::SpatialLightGrid(Renderer& renderer)
  : impl_(std::make_unique<Impl>(renderer))
{
}

SpatialLightGrid::~SpatialLightGrid()
{
  for (auto& [id, view] : impl_->views) {
    impl_->Retire(view);
  }
}

void SpatialLightGrid::OnFrameStart(
  frame::SequenceNumber sequence, frame::Slot slot)
{
  if (sequence != impl_->sequence) {
    for (auto it = impl_->views.begin(); it != impl_->views.end();) {
      if (it->second.last_used != impl_->sequence) {
        impl_->Retire(it->second);
        it = impl_->views.erase(it);
      } else {
        impl_->ReadDemand(it->second);
        ++it;
      }
    }
  }
  impl_->sequence = sequence;
  impl_->slot = slot;
  impl_->constants.OnFrameStart(sequence, slot);
}

void SpatialLightGrid::SetActiveViewCount(std::uint32_t count)
{
  impl_->active_view_count = (std::max)(count, 1U);
}

auto SpatialLightGrid::Prepare(
  const BuiltLightGridView& view, LightingFrameBindings& bindings) -> bool
{
  auto& state = impl_->views.try_emplace(view.view_id).first->second;
  state.last_used = impl_->sequence;
  auto& resources = state.frames.try_emplace(impl_->slot).first->second;
  const auto count = bindings.cluster_count;
  impl_->Ensure(resources.ranges, count, sizeof(ClusterLightRange),
    "LightingService.GridRanges");
  impl_->Ensure(resources.counts, count, sizeof(std::uint32_t),
    "LightingService.GridCounts");
  impl_->Ensure(resources.scan.at(0), count, 2U * sizeof(std::uint32_t),
    "LightingService.GridScanA");
  impl_->Ensure(resources.scan.at(1), count, 2U * sizeof(std::uint32_t),
    "LightingService.GridScanB");
  impl_->Ensure(resources.status, 1U, sizeof(LightGridBuildStatus),
    "LightingService.GridStatus");
  // Cold allocations reserve a small occupancy estimate. Completed GPU counts
  // drive growth; a scene-count worst case must not consume every slot's
  // budget.
  const auto maximum = static_cast<std::uint64_t>(count) * bindings.local_count;
  const auto seed
    = static_cast<std::uint64_t>(count) * (std::min)(bindings.local_count, 4U);
  const auto measured = state.have_demand
    ? (std::min)(state.measured_demand, maximum)
      + ((std::min)(state.measured_demand, maximum) / 4U)
    : seed;
  const auto desired = (std::min)(maximum, (std::max)(seed, measured));
  const auto budget
    = impl_->renderer->GetLightingAllocationBudget()->Snapshot();
  const auto share = budget.limits.compact_indices.get() / sizeof(std::uint32_t)
    / impl_->active_view_count / frame::kFramesInFlight.get();
  if (resources.indices.count > share
    || (state.have_demand && resources.indices.count > desired * 4U)) {
    impl_->Retire(resources.indices);
  }
  const auto available = budget.limits.compact_indices.get()
    - (std::min)(budget.compact_indices.get(),
      budget.limits.compact_indices.get());
  const auto capacity = static_cast<std::uint32_t>((std::min)({
    desired,
    share,
    available / sizeof(std::uint32_t),
    static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max()),
  }));
  const auto admitted_capacity
    = impl_->EnsureCompactIndices(resources.indices, capacity);
  bindings.cluster_ranges_srv = resources.ranges.srv;
  bindings.local_indices_srv = resources.indices.srv;
  bindings.index_capacity = admitted_capacity;
  bindings.build_status_srv = resources.status.srv;
  if (!state.feedback_pool) {
    if (auto graphics = impl_->renderer->GetGraphics();
      graphics && graphics->GetReadbackManager()) {
      state.feedback_pool = std::make_unique<vortex::internal::GpuFeedbackPool>(
        graphics, frame::kFramesInFlight.get(), "Lighting.GridDemand");
    }
  }
  return true;
}

auto SpatialLightGrid::InspectCompleted(ViewId view)
  -> std::optional<CompletedLightGridBuild>
{
  const auto found = impl_->views.find(view);
  if (found == impl_->views.end()) {
    return std::nullopt;
  }
  impl_->ReadDemand(found->second);
  return found->second.completed;
}

auto SpatialLightGrid::Inspect(ViewId view) const -> LightGridResources
{
  const auto found = impl_->views.find(view);
  if (found == impl_->views.end()) {
    return {};
  }
  const auto slot = found->second.frames.find(impl_->slot);
  if (slot == found->second.frames.end()) {
    return {};
  }
  return {
    .status = slot->second.status.buffer,
    .ranges = slot->second.ranges.buffer,
    .indices = slot->second.indices.buffer,
  };
}

auto SpatialLightGrid::Record(
  const BuiltLightGridView& view, ShaderVisibleIndex header) -> bool
{
  auto& state = impl_->views.at(view.view_id);
  auto& resources = state.frames.at(impl_->slot);
  auto gfx = impl_->renderer->GetGraphics();
  auto recording = gfx->AcquireCommandRecorder(
    gfx->QueueKeyFor(graphics::QueueRole::kGraphics),
    "LightingService.SpatialGrid");
  if (!recording) {
    return false;
  }
  impl_->renderer->GetDiagnosticsService().AttachGpuTimelineCollector(
    *recording);
  auto pending_feedback = vortex::internal::GpuFeedbackReservation {};
  {
    graphics::GpuEventScope scope(*recording, "Vortex.Stage6.SpatialLightGrid",
      profiling::ProfileGranularity::kTelemetry,
      profiling::ProfileCategory::kPass);
    const auto buffers = std::array {
      &resources.ranges,
      &resources.indices,
      &resources.counts,
      resources.scan.data(),
      &resources.scan.at(1),
      &resources.status,
    };
    for (auto* item : buffers) {
      if (item->buffer && !recording->AdoptKnownResourceState(*item->buffer)) {
        recording->BeginTrackingResourceState(
          *item->buffer, graphics::ResourceStates::kCommon, true);
      }
    }
    static const auto pipeline = MakePipeline();
    recording->SetPipelineState(pipeline);
    const auto depth_column = glm::column(view.projection, 2);
    const auto translation_column = glm::column(view.projection, 3);
    auto pass = LightGridPassConstants {
      .lighting_bindings_srv = header,
      .ranges_uav = resources.ranges.uav,
      .indices_uav = resources.indices.uav,
      .status_uav = resources.status.uav,
      .counts_uav = resources.counts.uav,
      .offsets_uav = resources.scan.at(0).uav,
      .work_count = view.bindings.cluster_count,
      .scan_destination_uav = resources.scan.at(1).uav,
      .view_matrix = view.view_matrix,
      .inverse_projection = view.inverse_projection,
      .depth_projection = { depth_column.z, translation_column.z,
        depth_column.w, translation_column.w, },
    };
    const auto dispatch
      = [&](std::uint32_t phase, std::uint32_t groups) -> bool {
      pass.subpass = phase;
      for (auto* item : buffers) {
        if (item->buffer) {
          recording->RequireResourceState(
            *item->buffer, graphics::ResourceStates::kUnorderedAccess);
        }
      }
      recording->FlushBarriers();
      auto allocation = impl_->constants.Allocate(1U);
      if (!allocation || !allocation->TryWriteObject(pass)) {
        return false;
      }
      recording->SetComputeRoot32BitConstant(
        static_cast<std::uint32_t>(
          bindless::generated::d3d12::RootParam::kRootConstants),
        allocation->srv.get(), 1U);
      recording->Dispatch(groups, 1U, 1U);
      return true;
    };
    const auto groups = (pass.work_count + 63U) / 64U;
    if (!dispatch(0U, 1U) || !dispatch(1U, groups)) {
      recording.Discard();
      return false;
    }
    for (std::uint32_t stride = 1U; stride < pass.work_count;) {
      pass.scan_stride = stride;
      if (!dispatch(2U, groups)) {
        recording.Discard();
        return false;
      }
      std::swap(pass.offsets_uav, pass.scan_destination_uav);
      if (stride > pass.work_count / 2U) {
        break;
      }
      stride *= 2U;
    }
    if (!dispatch(3U, groups) || !dispatch(4U, 1U)) {
      recording.Discard();
      return false;
    }
    if (state.feedback_pool && !resources.feedback) {
      auto feedback = state.feedback_pool->Reserve();
      if (feedback
        && feedback->EnqueueCopy(*recording, *resources.status.buffer,
          { 0U, sizeof(LightGridBuildStatus) })) {
        pending_feedback = std::move(*feedback);
      }
    }
    for (auto* item :
      { &resources.ranges, &resources.indices, &resources.status }) {
      if (item->buffer) {
        recording->RequireResourceStateFinal(
          *item->buffer, graphics::ResourceStates::kShaderResource);
      }
    }
  }
  const auto submitted = recording.Submit();
  if (submitted && pending_feedback && pending_feedback.Commit()) {
    resources.feedback = std::move(pending_feedback);
    resources.feedback_sequence = impl_->sequence;
  }
  return submitted;
}

} // namespace oxygen::vortex::lighting::internal
