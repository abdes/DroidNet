//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <unordered_map>
#include <utility>
#include <vector>

#include <Oxygen/Base/Hash.h>
#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Bindless/Generated.RootSignature.D3D12.h>
#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Core/Types/ShaderType.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/PipelineState.h>
#include <Oxygen/Graphics/Common/ReadbackManager.h>
#include <Oxygen/Graphics/Common/ReadbackTypes.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Graphics/Common/Shaders.h>
#include <Oxygen/Graphics/Common/Types/DescriptorVisibility.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Graphics/Common/Types/ResourceViewType.h>
#include <Oxygen/Profiling/GpuEventScope.h>
#include <Oxygen/Profiling/ProfileScope.h>
#include <Oxygen/Scene/Types/NodeHandle.h>
#include <Oxygen/Vortex/PreparedSceneFrame.h>
#include <Oxygen/Vortex/RenderContext.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/SceneRenderer/SceneTextures.h>
#include <Oxygen/Vortex/SceneRenderer/Stages/Occlusion/OcclusionConfig.h>
#include <Oxygen/Vortex/SceneRenderer/Stages/Occlusion/OcclusionModule.h>
#include <Oxygen/Vortex/SceneRenderer/Stages/Occlusion/Types/OcclusionStats.h>
#include <Oxygen/Vortex/Upload/TransientStructuredBuffer.h>

namespace oxygen::vortex {

namespace {

  namespace bindless_d3d12 = oxygen::bindless::generated::d3d12;

  constexpr std::uint32_t kThreadGroupSize = 64U;

  struct GpuOcclusionCandidate {
    std::array<float, 4> sphere_world {};
    std::uint32_t draw_index { 0U };
    std::uint32_t _pad0 { 0U };
    std::uint32_t _pad1 { 0U };
    std::uint32_t _pad2 { 0U };
  };
  static_assert(sizeof(GpuOcclusionCandidate) == 32U);

  struct GpuOcclusionPassConstants {
    ShaderVisibleIndex candidates_srv { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex result_uav { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex screen_hzb_frame_slot { kInvalidShaderVisibleIndex };
    std::uint32_t candidate_count { 0U };
  };
  static_assert(sizeof(GpuOcclusionPassConstants) == 16U);

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
          table.count
            = range.num_descriptors == std::numeric_limits<std::uint32_t>::max()
            ? std::numeric_limits<std::uint32_t>::max()
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

  auto BuildPipelineDesc() -> graphics::ComputePipelineDesc
  {
    auto root_bindings = BuildVortexRootBindings();
    return graphics::ComputePipelineDesc::Builder()
      .SetComputeShader({
        .stage = ShaderType::kCompute,
        .source_path = "Vortex/Stages/Occlusion/OcclusionTest.hlsl",
        .entry_point = "VortexOcclusionTestCS",
      })
      .SetRootBindings(std::span<const graphics::RootBindingItem>(
        root_bindings.data(), root_bindings.size()))
      .SetDebugName("Vortex.Stage5.OcclusionTest")
      .Build();
  }

  template <typename Resource>
  auto RegisterResourceIfNeeded(
    Graphics& gfx, const std::shared_ptr<Resource>& resource) -> void
  {
    if (!resource) {
      return;
    }
    auto& registry = gfx.GetResourceRegistry();
    if (!registry.Contains(*resource)) {
      registry.Register(resource);
    }
  }

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

  auto MakeStructuredViewDesc(const graphics::ResourceViewType view_type,
    const std::uint64_t size_bytes, const std::uint32_t stride)
    -> graphics::BufferViewDescription
  {
    return graphics::BufferViewDescription {
      .view_type = view_type,
      .visibility = graphics::DescriptorVisibility::kShaderVisible,
      .range = { 0U, size_bytes },
      .stride = stride,
    };
  }

  //! A draw's identity across frames. Draw indices follow each frame's sort
  //! order, so a result read back frames later is matched by its source.
  struct DrawSourceKey {
    scene::NodeHandle node;
    std::uint32_t submesh_index { 0U };

    auto operator==(const DrawSourceKey&) const -> bool = default;
  };

  struct DrawSourceKeyHash {
    auto operator()(const DrawSourceKey& key) const noexcept -> std::size_t
    {
      auto seed = std::hash<scene::NodeHandle> {}(key.node);
      HashCombine(seed, key.submesh_index);
      return seed;
    }
  };

  //! Marks a source that produced more than one draw this frame; its result
  //! cannot be attributed and the draws stay visible.
  constexpr auto kAmbiguousDraw = std::numeric_limits<std::uint32_t>::max();

  auto ToKey(const PreparedSceneFrame::DrawSource& source) noexcept
    -> DrawSourceKey
  {
    return DrawSourceKey {
      .node = source.node,
      .submesh_index = source.submesh_index,
    };
  }

  auto PreparedDrawCount(const PreparedSceneFrame& prepared_scene) noexcept
    -> std::uint32_t
  {
    return static_cast<std::uint32_t>(prepared_scene.GetDrawMetadata().size());
  }

} // namespace

struct OcclusionModule::Impl {
  std::vector<std::uint8_t> visibility_storage;
  std::vector<GpuOcclusionCandidate> candidate_storage;
  //! The draw source of each candidate, in candidate order.
  std::vector<DrawSourceKey> candidate_sources;
  std::unordered_map<DrawSourceKey, std::uint32_t, DrawSourceKeyHash>
    draw_index_by_source;
  std::shared_ptr<graphics::Buffer> result_buffer;
  ShaderVisibleIndex result_buffer_uav { kInvalidShaderVisibleIndex };
  std::uint32_t result_buffer_capacity { 0U };
  std::unique_ptr<upload::TransientStructuredBuffer> candidate_buffer;
  std::unique_ptr<upload::TransientStructuredBuffer> pass_constants_buffer;

  //! A view's submitted test: results return frames later and belong only to
  //! that view and to the draw sources that were submitted.
  struct ViewState {
    std::shared_ptr<graphics::GpuBufferReadback> readback;
    std::vector<DrawSourceKey> pending_sources;
  };
  std::unordered_map<ViewId, ViewState> view_states;
  std::optional<graphics::ComputePipelineDesc> pipeline_desc;
  std::weak_ptr<Graphics> gfx_weak;
  OcclusionFrameResults current_results
    = MakeInvalidOcclusionFrameResults(OcclusionFallbackReason::kStageDisabled);
  OcclusionStats stats {};

  OXYGEN_MAKE_NON_COPYABLE(Impl)
  OXYGEN_MAKE_NON_MOVABLE(Impl)

  explicit Impl(Renderer& renderer)
  {
    auto gfx = renderer.GetGraphics();
    if (gfx == nullptr) {
      return;
    }
    gfx_weak = gfx;
    candidate_buffer = std::make_unique<upload::TransientStructuredBuffer>(
      observer_ptr { gfx.get() }, renderer.GetStagingProvider(),
      static_cast<std::uint32_t>(sizeof(GpuOcclusionCandidate)),
      observer_ptr { &renderer.GetInlineTransfersCoordinator() },
      "Vortex.Stage5.Occlusion.Candidates");
    pass_constants_buffer = std::make_unique<upload::TransientStructuredBuffer>(
      observer_ptr { gfx.get() }, renderer.GetStagingProvider(),
      static_cast<std::uint32_t>(sizeof(GpuOcclusionPassConstants)),
      observer_ptr { &renderer.GetInlineTransfersCoordinator() },
      "Vortex.Stage5.Occlusion.PassConstants");
  }

  ~Impl()
  {
    if (auto gfx = gfx_weak.lock();
      gfx != nullptr && result_buffer != nullptr) {
      auto& registry = gfx->GetResourceRegistry();
      if (registry.Contains(*result_buffer)) {
        registry.UnRegisterResource(*result_buffer);
      }
      gfx->RegisterDeferredRelease(std::move(result_buffer));
    }
  }

  auto PublishInvalid(RenderContext& ctx, OcclusionFallbackReason reason)
    -> void
  {
    visibility_storage.clear();
    current_results = MakeInvalidOcclusionFrameResults(reason);
    stats = OcclusionStats { .fallback_reason = reason };
    ctx.current_view.occlusion_results
      = observer_ptr<const OcclusionFrameResults> { &current_results };
  }

  auto PublishCurrent(RenderContext& ctx, std::uint32_t draw_count,
    OcclusionFallbackReason reason, const bool previous_results_valid,
    const bool hzb_available) -> void
  {
    current_results = OcclusionFrameResults {
      .visible_by_draw = std::span<const std::uint8_t> { visibility_storage },
      .draw_count = draw_count,
      .valid = true,
      .fallback_reason = reason,
    };
    const auto visible_count = static_cast<std::uint32_t>(
      std::ranges::count(visibility_storage, std::uint8_t { 1U }));
    stats = OcclusionStats {
      .draw_count = draw_count,
      .visible_count = visible_count,
      .occluded_count = draw_count - visible_count,
      .fallback_reason = reason,
      .current_furthest_hzb_available = hzb_available,
      .previous_results_valid = previous_results_valid,
      .results_valid = true,
    };
    ctx.current_view.occlusion_results
      = observer_ptr<const OcclusionFrameResults> { &current_results };
  }

  auto PublishAllVisible(RenderContext& ctx, std::uint32_t draw_count,
    OcclusionFallbackReason reason) -> void
  {
    visibility_storage.assign(draw_count, 1U);
    PublishCurrent(ctx, draw_count, reason, false,
      ctx.current_view.screen_hzb_available
        && ctx.current_view.screen_hzb_furthest_texture.get() != nullptr);
    stats.candidate_count = draw_count;
  }

  auto EnsureResultBuffer(Graphics& gfx, const std::uint32_t capacity) -> bool
  {
    if (capacity == 0U) {
      return false;
    }
    if (result_buffer != nullptr && result_buffer_capacity >= capacity
      && result_buffer_uav.IsValid()) {
      return true;
    }

    auto& registry = gfx.GetResourceRegistry();
    if (result_buffer && registry.Contains(*result_buffer)) {
      registry.UnRegisterResource(*result_buffer);
      gfx.RegisterDeferredRelease(std::move(result_buffer));
    }
    result_buffer_uav = kInvalidShaderVisibleIndex;
    result_buffer_capacity = 0U;

    const auto size_bytes
      = static_cast<std::uint64_t>(capacity) * sizeof(std::uint32_t);
    result_buffer = gfx.CreateBuffer(graphics::BufferDesc {
      .size_bytes = size_bytes,
      .usage = graphics::BufferUsage::kStorage,
      .memory = graphics::BufferMemory::kDeviceLocal,
      .debug_name = "Vortex.Stage5.Occlusion.Results",
    });
    if (!result_buffer) {
      return false;
    }
    RegisterResourceIfNeeded(gfx, result_buffer);

    auto& allocator = gfx.GetDescriptorAllocator();
    auto uav_handle
      = allocator.AllocateRaw(graphics::ResourceViewType::kStructuredBuffer_UAV,
        graphics::DescriptorVisibility::kShaderVisible);
    if (!uav_handle.IsValid()) {
      return false;
    }
    result_buffer_uav = allocator.GetShaderVisibleIndex(uav_handle);
    const auto view = registry.RegisterView(*result_buffer,
      std::move(uav_handle),
      MakeStructuredViewDesc(graphics::ResourceViewType::kStructuredBuffer_UAV,
        size_bytes, sizeof(std::uint32_t)));
    if (!view->IsValid()) {
      result_buffer_uav = kInvalidShaderVisibleIndex;
      return false;
    }

    result_buffer_capacity = capacity;
    return true;
  }

  static auto EnsureReadback(Graphics& gfx, ViewState& view) -> bool
  {
    if (view.readback != nullptr) {
      return true;
    }
    auto manager = gfx.GetReadbackManager();
    if (manager == nullptr) {
      return false;
    }
    view.readback
      = manager->CreateBufferReadback("Vortex.Stage5.Occlusion.Results");
    return view.readback != nullptr;
  }

  //! Maps this frame's draw sources to their draw indices.
  auto IndexDrawSources(const PreparedSceneFrame& prepared_frame,
    const std::uint32_t draw_count) -> void
  {
    draw_index_by_source.clear();
    const auto sources = prepared_frame.draw_sources.first(
      (std::min)(prepared_frame.draw_sources.size(),
        static_cast<std::size_t>(draw_count)));
    draw_index_by_source.reserve(sources.size());
    for (const auto [draw_index, source] : std::views::enumerate(sources)) {
      const auto [entry, inserted] = draw_index_by_source.try_emplace(
        ToKey(source), static_cast<std::uint32_t>(draw_index));
      if (!inserted) {
        entry->second = kAmbiguousDraw;
      }
    }
  }

  auto TryConsumeReadback(ViewState& view,
    const PreparedSceneFrame& prepared_frame, const std::uint32_t draw_count)
    -> bool
  {
    if (view.readback == nullptr || view.pending_sources.empty()) {
      return false;
    }

    const auto ready = view.readback->IsReady();
    if (!ready.has_value()) {
      view.readback->Reset();
      view.pending_sources.clear();
      return false;
    }
    if (!*ready) {
      return false;
    }

    {
      auto mapped = view.readback->TryMap();
      if (!mapped.has_value()) {
        view.readback->Reset();
        view.pending_sources.clear();
        return false;
      }

      IndexDrawSources(prepared_frame, draw_count);
      const auto bytes = mapped->Bytes();
      const auto result_count
        = static_cast<std::uint32_t>(bytes.size() / sizeof(std::uint32_t));
      const auto consume_count = (std::min)(result_count,
        static_cast<std::uint32_t>(view.pending_sources.size()));
      for (const auto [i, source] : std::views::enumerate(
             view.pending_sources | std::views::take(consume_count))) {
        const auto found = draw_index_by_source.find(source);
        if (found == draw_index_by_source.end()
          || found->second == kAmbiguousDraw) {
          continue;
        }
        auto word = std::uint32_t { 0U };
        std::memcpy(&word,
          bytes.subspan(static_cast<std::size_t>(i) * sizeof(word)).data(),
          sizeof(word));
        visibility_storage.at(found->second) = word != 0U ? 1U : 0U;
      }
    }

    view.readback->Reset();
    view.pending_sources.clear();
    return true;
  }

  auto BuildCandidates(const PreparedSceneFrame& prepared_frame,
    const std::uint32_t draw_count, const OcclusionConfig& config)
    -> std::uint32_t
  {
    candidate_storage.clear();
    candidate_sources.clear();
    const auto capacity = (std::min)(draw_count, config.max_candidate_count);
    candidate_storage.reserve(capacity);
    candidate_sources.reserve(capacity);
    // A draw without a source cannot be matched to its result on a later
    // frame, so it is never submitted and stays visible.
    const auto drawn = std::views::zip(
      prepared_frame.draw_bounding_spheres, prepared_frame.draw_sources);
    for (const auto [draw_index, draw] :
      std::views::enumerate(drawn | std::views::take(capacity))) {
      const auto& [sphere, source] = draw;
      if (sphere.w <= config.tiny_object_radius_threshold) {
        continue;
      }
      candidate_storage.push_back(GpuOcclusionCandidate {
        .sphere_world = { sphere.x, sphere.y, sphere.z, sphere.w },
        .draw_index = static_cast<std::uint32_t>(draw_index),
      });
      candidate_sources.push_back(ToKey(source));
    }
    return static_cast<std::uint32_t>(candidate_storage.size());
  }

  auto RecordCurrent(RenderContext& ctx, graphics::CommandRecorder& recorder,
    Graphics& gfx, ViewState& view, const std::uint32_t draw_count,
    const std::uint32_t candidate_count) -> bool
  {
    if (candidate_count == 0U || result_buffer == nullptr
      || !result_buffer_uav.IsValid() || candidate_buffer == nullptr
      || pass_constants_buffer == nullptr) {
      return false;
    }
    if (ctx.frame_slot == frame::kInvalidSlot) {
      return false;
    }
    if (view.readback != nullptr
      && view.readback->GetState() == graphics::ReadbackState::kPending) {
      return false;
    }
    if (view.readback != nullptr
      && view.readback->GetState() != graphics::ReadbackState::kIdle) {
      view.readback->Reset();
    }

    candidate_buffer->OnFrameStart(ctx.frame_sequence, ctx.frame_slot);
    pass_constants_buffer->OnFrameStart(ctx.frame_sequence, ctx.frame_slot);
    const auto candidates_alloc = candidate_buffer->Allocate(candidate_count);
    if (!candidates_alloc.has_value()
      || !candidates_alloc->TryWriteRange(
        std::span<const GpuOcclusionCandidate> {
          candidate_storage.data(), candidate_count })) {
      return false;
    }

    const auto pass_constants = GpuOcclusionPassConstants {
      .candidates_srv = candidates_alloc->srv,
      .result_uav = result_buffer_uav,
      .screen_hzb_frame_slot = ctx.current_view.screen_hzb_frame_slot,
      .candidate_count = candidate_count,
    };
    const auto constants_alloc = pass_constants_buffer->Allocate(1U);
    if (!constants_alloc.has_value()
      || !constants_alloc->TryWriteObject(pass_constants)) {
      return false;
    }

    if (!pipeline_desc.has_value()) {
      pipeline_desc = BuildPipelineDesc();
    }

    TrackBufferFromKnownOrInitial(recorder, *result_buffer);
    recorder.RequireResourceState(
      *result_buffer, graphics::ResourceStates::kUnorderedAccess);
    recorder.FlushBarriers();

    recorder.SetPipelineState(*pipeline_desc);
    if (ctx.view_constants != nullptr) {
      recorder.SetComputeRootConstantBufferView(
        static_cast<std::uint32_t>(bindless_d3d12::RootParam::kViewConstants),
        ctx.view_constants->GetGPUVirtualAddress());
    }
    recorder.SetComputeRoot32BitConstant(
      static_cast<std::uint32_t>(bindless_d3d12::RootParam::kRootConstants), 0U,
      0U);
    recorder.SetComputeRoot32BitConstant(
      static_cast<std::uint32_t>(bindless_d3d12::RootParam::kRootConstants),
      constants_alloc->srv.get(), 1U);

    graphics::GpuEventScope pass_scope(recorder, "Vortex.Stage5.OcclusionTest",
      profiling::ProfileGranularity::kDiagnostic,
      profiling::ProfileCategory::kPass);
    recorder.Dispatch(
      (candidate_count + (kThreadGroupSize - 1U)) / kThreadGroupSize, 1U, 1U);

    auto enqueued = false;
    if (EnsureReadback(gfx, view)) {
      const auto readback_size
        = static_cast<std::uint64_t>(candidate_count) * sizeof(std::uint32_t);
      const auto ticket = view.readback->EnqueueCopy(
        recorder, *result_buffer, graphics::BufferRange { 0U, readback_size });
      if (ticket.has_value()) {
        enqueued = true;
        view.pending_sources.assign(candidate_sources.begin(),
          std::next(candidate_sources.begin(), candidate_count));
      } else {
        LOG_F(WARNING, "occlusion_readback_enqueue_failed");
      }
    }

    recorder.RequireResourceState(
      *result_buffer, graphics::ResourceStates::kCommon);
    stats.submitted_count = candidate_count;
    stats.overflow_visible_count
      = draw_count > candidate_count ? draw_count - candidate_count : 0U;
    return enqueued;
  }
};

OcclusionModule::OcclusionModule(Renderer& renderer, OcclusionConfig config)
  : renderer_(renderer)
  , config_(config)
  , impl_(std::make_unique<Impl>(renderer))
{
}

OcclusionModule::~OcclusionModule() = default;

void OcclusionModule::SetConfig(OcclusionConfig config) noexcept
{
  config_ = config;
}

auto OcclusionModule::GetConfig() const noexcept -> const OcclusionConfig&
{
  return config_;
}

void OcclusionModule::Execute(RenderContext& ctx,
  graphics::CommandRecorder& recorder, SceneTextures& scene_textures)
{
  (void)scene_textures;

  if (!config_.enabled) {
    impl_->PublishInvalid(ctx, OcclusionFallbackReason::kStageDisabled);
    return;
  }

  const auto* prepared_frame = ctx.current_view.prepared_frame.get();
  if (prepared_frame == nullptr) {
    impl_->PublishInvalid(ctx, OcclusionFallbackReason::kNoPreparedFrame);
    return;
  }

  const auto draw_count = PreparedDrawCount(*prepared_frame);
  if (draw_count == 0U) {
    impl_->PublishAllVisible(
      ctx, draw_count, OcclusionFallbackReason::kNoDraws);
    return;
  }

  if (!ctx.current_view.screen_hzb_available
    || ctx.current_view.screen_hzb_furthest_texture.get() == nullptr) {
    impl_->PublishAllVisible(
      ctx, draw_count, OcclusionFallbackReason::kNoCurrentFurthestHzb);
    return;
  }

  auto& view = impl_->view_states[ctx.current_view.view_id];
  impl_->visibility_storage.assign(draw_count, 1U);
  const auto previous_results_valid
    = impl_->TryConsumeReadback(view, *prepared_frame, draw_count);
  const auto fallback_reason = previous_results_valid
    ? OcclusionFallbackReason::kNone
    : OcclusionFallbackReason::kNoPreviousResults;
  impl_->PublishCurrent(
    ctx, draw_count, fallback_reason, previous_results_valid, true);

  auto gfx = renderer_.GetGraphics();
  if (gfx == nullptr) {
    impl_->stats.fallback_reason
      = OcclusionFallbackReason::kReadbackUnavailable;
    return;
  }

  const auto candidate_count
    = impl_->BuildCandidates(*prepared_frame, draw_count, config_);
  impl_->stats.candidate_count = candidate_count;
  if (!impl_->EnsureResultBuffer(*gfx, config_.max_candidate_count)) {
    impl_->stats.fallback_reason
      = OcclusionFallbackReason::kReadbackUnavailable;
    return;
  }

  const auto readback_enqueued = impl_->RecordCurrent(
    ctx, recorder, *gfx, view, draw_count, candidate_count);
  if (!readback_enqueued && !previous_results_valid) {
    impl_->stats.fallback_reason
      = OcclusionFallbackReason::kReadbackUnavailable;
    impl_->current_results.fallback_reason
      = OcclusionFallbackReason::kReadbackUnavailable;
  }
}

void OcclusionModule::RemoveViewState(const ViewId view_id)
{
  impl_->view_states.erase(view_id);
}

auto OcclusionModule::GetCurrentResults() const noexcept
  -> const OcclusionFrameResults&
{
  return impl_->current_results;
}

auto OcclusionModule::GetStats() const noexcept -> const OcclusionStats&
{
  return impl_->stats;
}

} // namespace oxygen::vortex
