//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <span>
#include <stdexcept>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/Bindless/Generated.BindlessAbi.h>
#include <Oxygen/Core/Bindless/Generated.RootSignature.D3D12.h>
#include <Oxygen/Core/Types/ShaderType.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/CommandList.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/PipelineState.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Profiling/GpuEventScope.h>
#include <Oxygen/Graphics/Common/Types/DescriptorVisibility.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Graphics/Common/Types/ResourceViewType.h>
#include <Oxygen/Vortex/Environment/SceneBackground.h>
#include <Oxygen/Vortex/Internal/PerViewStructuredPublisher.h>
#include <Oxygen/Vortex/Internal/ViewportClamp.h>
#include <Oxygen/Vortex/PostProcess/Passes/ExposurePass.h>
#include <Oxygen/Vortex/RenderContext.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/Types/ExposureStateData.h>
#include <Oxygen/Vortex/Types/ExposureTargetData.h>

namespace oxygen::vortex::postprocess {

namespace {

  namespace bindless_d3d12 = oxygen::bindless::generated::d3d12;

  constexpr std::uint32_t kHistogramWordCount = 264U;
  constexpr std::uint32_t kHistogramGridLimit = 512U;
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

  template <typename Resource>
  auto RegisterResourceIfNeeded(
    Graphics& graphics, const std::shared_ptr<Resource>& resource) -> void
  {
    if (!resource) {
      return;
    }
    auto& registry = graphics.GetResourceRegistry();
    if (!registry.Contains(*resource)) {
      registry.Register(resource);
    }
  }

  auto TrackTextureFromKnownOrInitial(graphics::CommandRecorder& recorder,
    const graphics::Texture& texture) -> void
  {
    if (recorder.IsResourceTracked(texture)
      || recorder.AdoptKnownResourceState(texture)) {
      return;
    }

    const auto initial = texture.GetDescriptor().initial_state;
    CHECK_F(initial != graphics::ResourceStates::kUnknown
        && initial != graphics::ResourceStates::kUndefined,
      "ExposurePass: cannot track '{}' without a known or declared initial "
      "state",
      texture.GetName());
    recorder.BeginTrackingResourceState(texture, initial);
  }

  auto MeteringRectangle(
    const RenderContext& ctx, const graphics::TextureDesc& desc) -> Scissors
  {
    auto rectangle = Scissors {
      .left = 0,
      .top = 0,
      .right = static_cast<std::int32_t>(desc.width),
      .bottom = static_cast<std::int32_t>(desc.height),
    };
    if (ctx.current_view.resolved_view != nullptr) {
      const auto clamped
        = ::oxygen::vortex::internal::ResolveClampedViewportState(
          ctx.current_view.resolved_view->Viewport(),
          ctx.current_view.resolved_view->Scissor(), desc.width, desc.height);
      const auto& viewport = clamped.viewport;
      // Include exactly the pixel centres covered by the raster viewport.
      rectangle.left = std::max(clamped.scissors.left,
        static_cast<std::int32_t>(std::ceil(viewport.top_left_x - 0.5F)));
      rectangle.top = std::max(clamped.scissors.top,
        static_cast<std::int32_t>(std::ceil(viewport.top_left_y - 0.5F)));
      rectangle.right = std::min(clamped.scissors.right,
        static_cast<std::int32_t>(
          std::ceil(viewport.top_left_x + viewport.width - 0.5F)));
      rectangle.bottom = std::min(clamped.scissors.bottom,
        static_cast<std::int32_t>(
          std::ceil(viewport.top_left_y + viewport.height - 0.5F)));
    }
    return rectangle;
  }

  auto SourceFallbackScale(const ExposurePass::Source& source) -> float
  {
    const auto& initial = source.config.Exposure();
    const bool automatic = initial.authored.enabled
      && initial.authored.mode == engine::ExposureMode::kAuto;
    auto log_gain = initial.initial_log_gain;
    if (automatic && !source.rejection && source.transition
      && source.transition->seed_ev
      && source.transition->policy
        == ExposureTransitionPolicy::kSeedFromEv100) {
      const auto seed = scene::ResolveExposureSeedLogGain(
        initial, *source.transition->seed_ev);
      if (seed)
        log_gain = *seed;
    }
    return automatic
      ? (initial.authored.target_luminance == 0.0F ? 0.0F : std::exp2(log_gain))
      : initial.fixed_scale;
  }

  struct alignas(packing::kShaderDataFieldAlignment)
    AutoExposureHistogramConstants {
    std::uint32_t source_texture_index;
    std::uint32_t histogram_buffer_index;
    float min_log_luminance;
    float inv_log_luminance_range;
    std::uint32_t metering_left;
    std::uint32_t metering_top;
    std::uint32_t metering_width;
    std::uint32_t metering_height;
    std::uint32_t metering_mode;
    float spot_meter_radius;
    std::uint32_t mask_texture_index;
    std::uint32_t background_enabled;
    float one_over_pre_exposure;
    float black_influence;
    std::uint32_t frame_exposure_srv;
    std::uint32_t _pad1;
  };

  static_assert(sizeof(AutoExposureHistogramConstants) == 64U);

  struct alignas(packing::kShaderDataFieldAlignment)
    AutoExposureAverageConstants {
    std::uint32_t histogram_buffer_index;
    std::uint32_t exposure_buffer_index;
    float min_log_luminance;
    float log_luminance_range;
    float low_percentile;
    float high_percentile;
    float min_ev;
    float log2_transition_distance;
    float log2_speed_up;
    float log2_speed_down;
    float log2_delta_time;
    std::uint32_t targets_srv;
    std::array<std::uint32_t, 2> settings_revision;
    std::array<std::uint32_t, 2> frame_sequence;
    std::uint32_t previous_state_srv;
    float fixed_scale;
    std::uint32_t exposure_mode;
    std::uint32_t control_flags;
    std::array<std::uint32_t, 2> requested_generation;
    std::uint32_t transition_policy;
    float seed_log_gain;
    std::uint32_t status_uav;
    std::uint32_t borrowed_state_srv;
    std::array<std::uint32_t, 2> view_lifetime;
  };

  static_assert(sizeof(AutoExposureAverageConstants) == 112U);
  static_assert(
    offsetof(AutoExposureAverageConstants, previous_state_srv) == 64U);
  static_assert(
    offsetof(AutoExposureAverageConstants, requested_generation) == 80U);
  static_assert(offsetof(AutoExposureAverageConstants, seed_log_gain) == 92U);
  static_assert(offsetof(AutoExposureAverageConstants, status_uav) == 96U);
  static_assert(
    offsetof(AutoExposureAverageConstants, borrowed_state_srv) == 100U);
  static_assert(offsetof(AutoExposureAverageConstants, view_lifetime) == 104U);

  struct alignas(packing::kShaderDataFieldAlignment) ExposureFrameConstants {
    std::uint32_t output_uav;
    std::uint32_t current_state_uav;
    std::uint32_t history_srv;
    std::uint32_t candidate_srv;
    float fixed_scale;
    float initial_log_gain;
    float seed_log_gain;
    std::uint32_t mode;
    std::uint32_t flags;
    std::uint32_t controls;
    std::uint32_t current_state_srv;
    std::uint32_t status_uav;
  };
  static_assert(sizeof(ExposureFrameConstants) == 48U);
  static_assert(offsetof(ExposureFrameConstants, fixed_scale) == 16U);
  static_assert(offsetof(ExposureFrameConstants, flags) == 32U);
  static_assert(offsetof(ExposureFrameConstants, current_state_srv) == 40U);
  static_assert(offsetof(ExposureFrameConstants, status_uav) == 44U);

  auto BuildExposurePipeline(std::string_view entry_point,
    std::string_view debug_name) -> graphics::ComputePipelineDesc
  {
    auto bindings = std::vector<graphics::RootBindingItem> {};
    bindings.reserve(bindless_d3d12::kRootParamTableCount);

    for (std::uint32_t index = 0; index < bindless_d3d12::kRootParamTableCount;
      ++index) {
      const auto& desc = bindless_d3d12::kRootParamTable.at(index);
      auto binding = graphics::RootBindingDesc {};
      binding.binding_slot_desc.register_index = desc.shader_register;
      binding.binding_slot_desc.register_space = desc.register_space;
      binding.visibility = graphics::ShaderStageFlags::kAll;

      switch (desc.kind) {
      case bindless_d3d12::RootParamKind::DescriptorTable: {
        auto table = graphics::DescriptorTableBinding {};
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

    return graphics::ComputePipelineDesc::Builder()
      .SetComputeShader(graphics::ShaderRequest {
        .stage = ShaderType::kCompute,
        .source_path = "Vortex/Services/PostProcess/Exposure.hlsl",
        .entry_point = std::string(entry_point),
      })
      .SetRootBindings(std::span<const graphics::RootBindingItem>(
        bindings.data(), bindings.size()))
      .SetDebugName(std::string(debug_name))
      .Build();
  }

} // namespace

ExposurePass::ExposurePass(Renderer& renderer)
  : renderer_(renderer)
{
}

ExposurePass::~ExposurePass() { ReleaseExposureResources(); }

auto ExposurePass::OnFrameStart(
  frame::SequenceNumber sequence, frame::Slot slot) -> void
{
  if (target_frame_ == sequence) {
    return;
  }
  CHECK_LT_F(slot.get(), frame_states_.size());
  frame_states_[slot.get()].clear();
  frame_bindings_[slot.get()].clear();
  resolved_frames_.clear();
  submitted_suitability_.clear();
  submitted_conversion_.clear();
  submitted_composition_input_.clear();
  submitted_opaque_ap_error_.clear();
  submitted_filter_gradients_.clear();
  prior_states_.clear();
  bootstrap_states_.clear();
  for (const auto& [handle, view] : exposure_states_) {
    if (view.latest) {
      prior_states_.emplace(handle, view.latest);
    }
  }
  target_frame_ = sequence;
  if (target_publisher_)
    target_publisher_->OnFrameStart(sequence, slot);
  if (constants_publisher_)
    constants_publisher_->OnFrameStart(sequence, slot);
  if (average_constants_publisher_)
    average_constants_publisher_->OnFrameStart(sequence, slot);
  if (frame_constants_publisher_)
    frame_constants_publisher_->OnFrameStart(sequence, slot);
  if (suitability_constants_publisher_)
    suitability_constants_publisher_->OnFrameStart(sequence, slot);
  if (conversion_constants_publisher_)
    conversion_constants_publisher_->OnFrameStart(sequence, slot);
}

auto ExposurePass::PreparePublishers(RenderContext& ctx) -> void
{
  auto gfx = renderer_.GetGraphics();
  if (!target_publisher_) {
    target_publisher_
      = std::make_unique<::oxygen::vortex::internal::PerViewStructuredPublisher<
        ExposureTargetData>>(observer_ptr { gfx.get() },
        renderer_.GetStagingProvider(),
        observer_ptr { &renderer_.GetInlineTransfersCoordinator() },
        "Vortex.PostProcess.Exposure.Targets");
    constants_publisher_
      = std::make_unique<::oxygen::vortex::internal::PerViewStructuredPublisher<
        std::array<std::uint32_t, 16U>>>(observer_ptr { gfx.get() },
        renderer_.GetStagingProvider(),
        observer_ptr { &renderer_.GetInlineTransfersCoordinator() },
        "Vortex.PostProcess.Exposure.Constants");
    average_constants_publisher_
      = std::make_unique<::oxygen::vortex::internal::PerViewStructuredPublisher<
        std::array<std::uint32_t, 28U>>>(observer_ptr { gfx.get() },
        renderer_.GetStagingProvider(),
        observer_ptr { &renderer_.GetInlineTransfersCoordinator() },
        "Vortex.PostProcess.Exposure.SolveConstants");
    frame_constants_publisher_
      = std::make_unique<::oxygen::vortex::internal::PerViewStructuredPublisher<
        std::array<std::uint32_t, 12U>>>(observer_ptr { gfx.get() },
        renderer_.GetStagingProvider(),
        observer_ptr { &renderer_.GetInlineTransfersCoordinator() },
        "Vortex.PostProcess.Exposure.FrameConstants");
    suitability_constants_publisher_
      = std::make_unique<::oxygen::vortex::internal::PerViewStructuredPublisher<
        std::array<std::uint32_t, 32U>>>(observer_ptr { gfx.get() },
        renderer_.GetStagingProvider(),
        observer_ptr { &renderer_.GetInlineTransfersCoordinator() },
        "Vortex.Exposure.Suitability.Constants");
    conversion_constants_publisher_
      = std::make_unique<::oxygen::vortex::internal::PerViewStructuredPublisher<
        std::array<std::uint32_t, 8U>>>(observer_ptr { gfx.get() },
        renderer_.GetStagingProvider(),
        observer_ptr { &renderer_.GetInlineTransfersCoordinator() },
        "Vortex.Exposure.Conversion.Constants");
    target_frame_.reset();
  }
  OnFrameStart(ctx.frame_sequence, ctx.frame_slot);
}

auto ExposurePass::AcquireFrame() -> std::shared_ptr<FrameResources>
{
  for (const auto& frame : frame_pool_) {
    if (frame.use_count() == 1) {
      frame->current_state.reset();
      frame->selected_history.reset();
      frame->precision_history.reset();
      frame->qualified_candidate.reset();
      return frame;
    }
  }
  auto gfx = renderer_.GetGraphics();
  CHECK_NOTNULL_F(gfx.get());
  auto frame = std::make_shared<FrameResources>();
  frame->buffer = gfx->CreateBuffer({ .size_bytes = sizeof(FrameExposureData),
    .usage = graphics::BufferUsage::kStorage,
    .memory = graphics::BufferMemory::kDeviceLocal,
    .debug_name = "Vortex.PostProcess.Exposure.Frame" });
  CHECK_NOTNULL_F(frame->buffer.get());
  RegisterResourceIfNeeded(*gfx, frame->buffer);
  auto& registry = gfx->GetResourceRegistry();
  auto& allocator = gfx->GetDescriptorAllocator();
  for (const auto type : { graphics::ResourceViewType::kStructuredBuffer_SRV,
         graphics::ResourceViewType::kStructuredBuffer_UAV }) {
    auto handle = type == graphics::ResourceViewType::kStructuredBuffer_SRV
      ? allocator.AllocateBindless(
          ::oxygen::bindless::generated::kGlobalSrvDomain, type)
      : allocator.AllocateRaw(
          type, graphics::DescriptorVisibility::kShaderVisible);
    CHECK_F(handle.IsValid());
    const auto index = allocator.GetShaderVisibleIndex(handle);
    const auto view = registry.RegisterView(*frame->buffer, std::move(handle),
      graphics::BufferViewDescription { .view_type = type,
        .visibility = graphics::DescriptorVisibility::kShaderVisible,
        .range = { 0U, sizeof(FrameExposureData) },
        .stride = sizeof(FrameExposureData) });
    CHECK_F(view->IsValid());
    if (type == graphics::ResourceViewType::kStructuredBuffer_SRV)
      frame->srv_index = index;
    else
      frame->uav_index = index;
  }
  const auto create_report
    = [&](std::shared_ptr<graphics::Buffer>& buffer, ShaderVisibleIndex& srv,
        ShaderVisibleIndex& uav, std::string_view name) {
        buffer = gfx->CreateBuffer({ .size_bytes = sizeof(HdrSuitabilityData),
          .usage = graphics::BufferUsage::kStorage,
          .memory = graphics::BufferMemory::kDeviceLocal,
          .debug_name = std::string(name) });
        CHECK_NOTNULL_F(buffer.get());
        RegisterResourceIfNeeded(*gfx, buffer);
        for (const auto type : { graphics::ResourceViewType::kRawBuffer_SRV,
               graphics::ResourceViewType::kRawBuffer_UAV }) {
          auto handle = allocator.AllocateRaw(
            type, graphics::DescriptorVisibility::kShaderVisible);
          CHECK_F(handle.IsValid());
          const auto index = allocator.GetShaderVisibleIndex(handle);
          const auto view = registry.RegisterView(*buffer, std::move(handle),
            graphics::BufferViewDescription { .view_type = type,
              .visibility = graphics::DescriptorVisibility::kShaderVisible,
              .range = { 0U, sizeof(HdrSuitabilityData) },
              .stride = 0U });
          CHECK_F(view->IsValid());
          if (type == graphics::ResourceViewType::kRawBuffer_SRV)
            srv = index;
          else
            uav = index;
        }
      };
  create_report(frame->suitability_buffer, frame->suitability_srv,
    frame->suitability_uav, "Vortex.Exposure.Suitability");
  create_report(frame->conversion_buffer, frame->conversion_srv,
    frame->conversion_uav, "Vortex.Exposure.Conversion");
  frame_pool_.push_back(frame);
  return frame;
}

auto ExposurePass::ResolveFrame(RenderContext& ctx,
  const ResolvedPostProcessConfig& config, const FrameInputs& inputs)
  -> FrameLease
{
  CHECK_F(!inputs.transition
    || inputs.transition->target == ctx.current_view.view_state_handle);
  auto gfx = renderer_.GetGraphics();
  if (!gfx)
    return {};
  EnsurePipelines();
  PreparePublishers(ctx);
  const auto key = std::pair { ctx.current_view.view_id,
    ctx.current_view.view_state_handle };
  if (const auto found = resolved_frames_.find(key);
    found != resolved_frames_.end())
    return found->second;
  const bool sharing
    = inputs.source && !config.Settings().temporary_unit_exposure;
  const auto owner
    = sharing ? inputs.source->handle : ctx.current_view.view_state_handle;
  const auto lifetime = sharing ? inputs.source->lifetime : inputs.lifetime;
  CHECK_F(!sharing
    || (owner != CompositionView::kInvalidViewStateHandle
      && owner != ctx.current_view.view_state_handle));
  auto frame = AcquireFrame();
  frame->current_state = AcquireState();
  frame->current_state->owner_lifetime = inputs.lifetime;
  frame->current_state->borrowed_from
    = sharing ? owner : CompositionView::kInvalidViewStateHandle;
  frame->current_state->borrowed_lifetime = sharing ? lifetime : 0U;
  if (!config.Settings().temporary_unit_exposure) {
    const auto own = prior_states_.find(ctx.current_view.view_state_handle);
    if (own != prior_states_.end()
      && own->second->owner_lifetime == inputs.lifetime
      && own->second->borrowed_from == frame->current_state->borrowed_from
      && own->second->borrowed_lifetime
        == frame->current_state->borrowed_lifetime)
      frame->precision_history = own->second;
  }
  if (!config.Settings().temporary_unit_exposure) {
    if (const auto prior = prior_states_.find(owner);
      prior != prior_states_.end() && prior->second->owner_lifetime == lifetime)
      frame->selected_history = prior->second;
  }
  bool source_fallback = false;
  if (sharing && !frame->selected_history) {
    source_fallback = true;
    if (const auto prior = bootstrap_states_.find(owner);
      prior != bootstrap_states_.end()
      && prior->second->owner_lifetime == lifetime)
      frame->selected_history = prior->second;
    else {
      frame->selected_history = RecordState(ctx, inputs.source->config,
        Inputs { .metering_available = false,
          .transition = inputs.source->transition,
          .rejection = inputs.source->rejection,
          .lifetime = lifetime },
        {}, {}, true);
      if (frame->selected_history)
        bootstrap_states_.insert_or_assign(owner, frame->selected_history);
    }
    if (!frame->selected_history)
      return {};
  }
  if (inputs.qualified_candidate) {
    CHECK_F(inputs.qualified_candidate->owner_lifetime == inputs.lifetime);
    frame->qualified_candidate = inputs.qualified_candidate;
  }
  frame_bindings_[ctx.frame_slot.get()].push_back(frame);
  auto recorder = gfx->AcquireCommandRecorder(
    gfx->QueueKeyFor(graphics::QueueRole::kGraphics), "Vortex Exposure Frame");
  if (!recorder)
    return {};
  const auto recording = recorder->GetCommandListForInspection();
  const auto track
    = [&](const graphics::Buffer& buffer, graphics::ResourceStates state) {
        if (!recorder->IsResourceTracked(buffer)
          && !recorder->AdoptKnownResourceState(buffer))
          recorder->BeginTrackingResourceState(
            buffer, graphics::ResourceStates::kCommon, false);
        recorder->RequireResourceState(buffer, state);
      };
  track(*frame->buffer, graphics::ResourceStates::kUnorderedAccess);
  track(*frame->current_state->status_buffer,
    graphics::ResourceStates::kUnorderedAccess);
  track(
    *frame->current_state->buffer, graphics::ResourceStates::kUnorderedAccess);
  if (frame->selected_history)
    track(*frame->selected_history->buffer,
      graphics::ResourceStates::kShaderResource);
  if (frame->qualified_candidate)
    track(*frame->qualified_candidate->buffer,
      graphics::ResourceStates::kShaderResource);
  const auto& resolved = config.Exposure();
  auto seed = std::optional<float> {};
  if (!sharing && !inputs.rejection && inputs.transition
    && inputs.transition->seed_ev
    && inputs.transition->policy == ExposureTransitionPolicy::kSeedFromEv100
    && resolved.authored.enabled
    && resolved.authored.mode == engine::ExposureMode::kAuto) {
    const auto resolved_seed = scene::ResolveExposureSeedLogGain(
      resolved, *inputs.transition->seed_ev);
    if (resolved_seed)
      seed = *resolved_seed;
  }
  const auto flags = (inputs.use_fp32 ? 1U : 0U) | (sharing ? 2U : 0U)
    | (config.Settings().temporary_unit_exposure ? 4U : 0U)
    | (source_fallback ? 8U : 0U);
  const auto constants = ExposureFrameConstants {
    .output_uav = frame->uav_index.get(),
    .current_state_uav = frame->current_state->uav_index.get(),
    .history_srv = frame->selected_history
      ? frame->selected_history->srv_index.get()
      : kInvalidShaderVisibleIndex.get(),
    .candidate_srv = frame->qualified_candidate
      ? frame->qualified_candidate->srv_index.get()
      : kInvalidShaderVisibleIndex.get(),
    .fixed_scale = resolved.fixed_scale,
    .initial_log_gain = resolved.initial_log_gain,
    .seed_log_gain = seed.value_or(0.0F),
    .mode = resolved.authored.enabled
      ? static_cast<std::uint32_t>(resolved.authored.mode)
      : 3U,
    .flags = flags,
    .controls = (seed.has_value() ? 1U : 0U)
      | (resolved.authored.enabled
            && resolved.authored.mode == engine::ExposureMode::kAuto
            && resolved.authored.target_luminance == 0.0F
          ? 2U
          : 0U),
    .current_state_srv = frame->current_state->srv_index.get(),
    .status_uav = frame->current_state->status_uav_index.get(),
  };
  const auto slot
    = frame_constants_publisher_->Publish(ctx.current_view.view_id,
      std::bit_cast<std::array<std::uint32_t, 12U>>(constants));
  CHECK_F(slot.IsValid());
  recorder->FlushBarriers();
  recorder->SetPipelineState(*frame_pipeline_);
  recorder->SetComputeRoot32BitConstant(
    static_cast<std::uint32_t>(bindless_d3d12::RootParam::kRootConstants), 0U,
    0U);
  recorder->SetComputeRoot32BitConstant(
    static_cast<std::uint32_t>(bindless_d3d12::RootParam::kRootConstants),
    slot.get(), 1U);
  recorder->Dispatch(1U, 1U, 1U);
  recorder->RequireResourceStateFinal(
    *frame->buffer, graphics::ResourceStates::kShaderResource);
  recorder->RequireResourceStateFinal(
    *frame->current_state->buffer, graphics::ResourceStates::kShaderResource);
  recorder.reset();
  if (!recording || !recording->IsSubmitted())
    return {};
  resolved_frames_.emplace(key, frame);
  return frame;
}

auto ExposurePass::RestoreFrameFallback(RenderContext& ctx,
  const ResolvedPostProcessConfig& config, const FrameResources& frame,
  StateLease fallback) -> bool
{
  if (!fallback)
    return true;
  auto gfx = renderer_.GetGraphics();
  if (!gfx)
    return false;
  auto recorder = gfx->AcquireCommandRecorder(
    gfx->QueueKeyFor(graphics::QueueRole::kGraphics),
    "Vortex Exposure Fallback");
  if (!recorder)
    return false;
  const auto recording = recorder->GetCommandListForInspection();
  const auto track
    = [&](const graphics::Buffer& buffer, graphics::ResourceStates state) {
        if (!recorder->IsResourceTracked(buffer)
          && !recorder->AdoptKnownResourceState(buffer))
          recorder->BeginTrackingResourceState(
            buffer, graphics::ResourceStates::kCommon, false);
        recorder->RequireResourceState(buffer, state);
      };
  track(*fallback->buffer, graphics::ResourceStates::kShaderResource);
  track(
    *frame.current_state->buffer, graphics::ResourceStates::kUnorderedAccess);
  const auto& settings = config.Exposure().authored;
  const auto constants = ExposureFrameConstants {
    .current_state_uav = frame.current_state->uav_index.get(),
    .history_srv = fallback->srv_index.get(),
    .controls = settings.enabled && settings.mode == engine::ExposureMode::kAuto
        && settings.target_luminance == 0.0F
      ? 2U
      : 0U,
  };
  const auto slot
    = frame_constants_publisher_->Publish(ctx.current_view.view_id,
      std::bit_cast<std::array<std::uint32_t, 12U>>(constants));
  CHECK_F(slot.IsValid());
  recorder->FlushBarriers();
  recorder->SetPipelineState(*fallback_pipeline_);
  recorder->SetComputeRoot32BitConstant(
    static_cast<std::uint32_t>(bindless_d3d12::RootParam::kRootConstants), 0U,
    0U);
  recorder->SetComputeRoot32BitConstant(
    static_cast<std::uint32_t>(bindless_d3d12::RootParam::kRootConstants),
    slot.get(), 1U);
  recorder->Dispatch(1U, 1U, 1U);
  recorder->RequireResourceStateFinal(
    *frame.current_state->buffer, graphics::ResourceStates::kShaderResource);
  recorder.reset();
  frame_states_[ctx.frame_slot.get()].push_back(std::move(fallback));
  return recording && recording->IsSubmitted();
}

auto ExposurePass::CapturePreEnvironmentRange(RenderContext& ctx,
  const FrameLease& frame, const graphics::Texture& source,
  const ShaderVisibleIndex source_srv) -> bool
{
  return RecordSceneRange(ctx, frame, source, source_srv, true);
}

auto ExposurePass::CheckSceneColorRange(RenderContext& ctx,
  const FrameLease& frame, const graphics::Texture& source,
  const ShaderVisibleIndex source_srv) -> bool
{
  return RecordSceneRange(ctx, frame, source, source_srv, false);
}

auto ExposurePass::RecordSceneRange(RenderContext& ctx, const FrameLease& frame,
  const graphics::Texture& source, const ShaderVisibleIndex source_srv,
  const bool capture_opaque_input) -> bool
{
  CHECK_NOTNULL_F(frame.get());
  if (capture_opaque_input) {
    submitted_composition_input_.erase(frame.get());
    submitted_opaque_ap_error_.erase(frame.get());
  }
  const auto& desc = source.GetDescriptor();
  if (!source_srv.IsValid() || desc.format != Format::kRGBA32Float
    || desc.texture_type != TextureType::kTexture2D || desc.sample_count != 1U)
    return false;
  auto gfx = renderer_.GetGraphics();
  if (!gfx)
    return false;
  PreparePublishers(ctx);
  EnsurePipelines();
  auto recorder = gfx->AcquireCommandRecorder(
    gfx->QueueKeyFor(graphics::QueueRole::kGraphics),
    capture_opaque_input ? "Vortex Exposure PreEnvironment Range"
                         : "Vortex Exposure Final Scene Range");
  if (!recorder)
    return false;
  const auto recording = recorder->GetCommandListForInspection();
  auto& status = *frame->current_state->status_buffer;
  if (!recorder->AdoptKnownResourceState(status))
    recorder->BeginTrackingResourceState(
      status, graphics::ResourceStates::kCommon, false);
  TrackTextureFromKnownOrInitial(*recorder, source);
  recorder->RequireResourceState(
    source, graphics::ResourceStates::kShaderResource);
  recorder->RequireResourceState(
    status, graphics::ResourceStates::kUnorderedAccess);
  const auto constants = std::array<std::uint32_t, 32U> {
    frame->current_state->status_uav_index.get(), source_srv.get(),
    frame->srv_index.get(), frame->current_state->srv_index.get(), desc.width,
    desc.height, 1U, 32U | (capture_opaque_input ? 0U : 2048U), 11U, 0U,
    kInvalidShaderVisibleIndex.get(), 0U, 0U, 0U, desc.width, desc.height, 0U,
    std::bit_cast<std::uint32_t>(1.0F), 0U, 0U,
    std::bit_cast<std::uint32_t>(1.0F), kInvalidShaderVisibleIndex.get(), 0U, 0U
  };
  const auto slot = suitability_constants_publisher_->Publish(
    ctx.current_view.view_id, constants);
  CHECK_F(slot.IsValid());
  for (unsigned index = capture_opaque_input ? 0U : 1U; index < 2U; ++index) {
    recorder->RequireResourceState(
      status, graphics::ResourceStates::kUnorderedAccess);
    recorder->FlushBarriers();
    recorder->SetPipelineState(*suitability_pipelines_[index]);
    recorder->SetComputeRoot32BitConstant(
      static_cast<std::uint32_t>(bindless_d3d12::RootParam::kRootConstants), 0U,
      0U);
    recorder->SetComputeRoot32BitConstant(
      static_cast<std::uint32_t>(bindless_d3d12::RootParam::kRootConstants),
      slot.get(), 1U);
    recorder->Dispatch(index == 0U ? 1U : (desc.width + 7U) / 8U,
      index == 0U ? 1U : (desc.height + 7U) / 8U, 1U);
  }
  recorder.reset();
  const bool submitted = recording && recording->IsSubmitted();
  if (submitted && capture_opaque_input)
    submitted_composition_input_.insert(frame.get());
  return submitted;
}

auto ExposurePass::PropagateOpaqueApError(RenderContext& ctx,
  const FrameLease& frame, const float scattering_strength) -> bool
{
  CHECK_NOTNULL_F(frame.get());
  submitted_opaque_ap_error_.erase(frame.get());
  if (!HasPreEnvironmentRange(frame))
    return false;
  auto gfx = renderer_.GetGraphics();
  if (!gfx)
    return false;
  PreparePublishers(ctx);
  EnsurePipelines();
  auto recorder = gfx->AcquireCommandRecorder(
    gfx->QueueKeyFor(graphics::QueueRole::kGraphics),
    "Vortex Exposure Opaque AP Error");
  if (!recorder)
    return false;
  const auto recording = recorder->GetCommandListForInspection();
  auto& status = *frame->current_state->status_buffer;
  if (!recorder->AdoptKnownResourceState(status))
    recorder->BeginTrackingResourceState(
      status, graphics::ResourceStates::kCommon, false);
  if (!recorder->AdoptKnownResourceState(*frame->buffer))
    recorder->BeginTrackingResourceState(
      *frame->buffer, graphics::ResourceStates::kCommon, false);
  recorder->RequireResourceState(
    status, graphics::ResourceStates::kUnorderedAccess);
  recorder->RequireResourceState(
    *frame->buffer, graphics::ResourceStates::kShaderResource);
  auto constants = std::array<std::uint32_t, 32U> {};
  constants[0] = frame->current_state->status_uav_index.get();
  constants[2] = frame->srv_index.get();
  constants[7] = 64U;
  constants[20] = std::bit_cast<std::uint32_t>(scattering_strength);
  const auto slot = suitability_constants_publisher_->Publish(
    ctx.current_view.view_id, constants);
  CHECK_F(slot.IsValid());
  recorder->FlushBarriers();
  recorder->SetPipelineState(*suitability_pipelines_[2]);
  recorder->SetComputeRoot32BitConstant(
    static_cast<std::uint32_t>(bindless_d3d12::RootParam::kRootConstants), 0U,
    0U);
  recorder->SetComputeRoot32BitConstant(
    static_cast<std::uint32_t>(bindless_d3d12::RootParam::kRootConstants),
    slot.get(), 1U);
  recorder->Dispatch(1U, 1U, 1U);
  recorder.reset();
  const bool submitted = recording && recording->IsSubmitted();
  if (submitted)
    submitted_opaque_ap_error_.insert(frame.get());
  return submitted;
}

auto ExposurePass::HasFilterGradients(
  const FrameLease& frame, const std::uint32_t product) const -> bool
{
  if (!frame || (product != 5U && product != 6U && product != 10U))
    return false;
  const auto found = submitted_filter_gradients_.find(frame.get());
  return found != submitted_filter_gradients_.end()
    && (found->second & (1U << (product - 1U))) != 0U;
}

auto ExposurePass::GatherFilterGradients(RenderContext& ctx,
  const FrameLease& frame, const HdrProduct& product) -> bool
{
  CHECK_NOTNULL_F(frame.get());
  if (product.id != 5U && product.id != 6U && product.id != 10U)
    return false;
  const auto bit = 1U << (product.id - 1U);
  submitted_filter_gradients_[frame.get()] &= ~bit;
  if (!product.texture || !product.srv.IsValid())
    return false;
  const auto& desc = product.texture->GetDescriptor();
  if ((desc.format != Format::kRGBA32Float
        && desc.format != Format::kRGBA16Float)
    || (desc.texture_type != TextureType::kTexture2D
      && desc.texture_type != TextureType::kTexture3D)
    || desc.sample_count != 1U || desc.width == 0U || desc.height == 0U
    || desc.depth == 0U
    || std::uint64_t(desc.width) * desc.height * desc.depth
      > std::numeric_limits<std::uint32_t>::max())
    return false;
  auto gfx = renderer_.GetGraphics();
  if (!gfx)
    return false;
  PreparePublishers(ctx);
  EnsurePipelines();
  auto recorder = gfx->AcquireCommandRecorder(
    gfx->QueueKeyFor(graphics::QueueRole::kGraphics),
    "Vortex Exposure Filter Gradients");
  if (!recorder)
    return false;
  const auto recording = recorder->GetCommandListForInspection();
  auto& status = *frame->current_state->status_buffer;
  if (!recorder->AdoptKnownResourceState(status))
    recorder->BeginTrackingResourceState(
      status, graphics::ResourceStates::kCommon, false);
  if (!recorder->AdoptKnownResourceState(*frame->buffer))
    recorder->BeginTrackingResourceState(
      *frame->buffer, graphics::ResourceStates::kCommon, false);
  TrackTextureFromKnownOrInitial(*recorder, *product.texture);
  recorder->RequireResourceState(
    *product.texture, graphics::ResourceStates::kShaderResource);
  recorder->RequireResourceState(
    *frame->buffer, graphics::ResourceStates::kShaderResource);
  auto constants = std::array<std::uint32_t, 32U> {};
  constants[0] = frame->current_state->status_uav_index.get();
  constants[1] = product.srv.get();
  constants[2] = frame->srv_index.get();
  constants[4] = desc.width;
  constants[5] = desc.height;
  constants[6] = desc.depth;
  constants[7] = 128U | (product.transmittance ? 4U : 0U)
    | (desc.texture_type == TextureType::kTexture3D ? 8U : 0U);
  constants[8] = product.id;
  const auto slot = suitability_constants_publisher_->Publish(
    ctx.current_view.view_id, constants);
  CHECK_F(slot.IsValid());
  for (unsigned pipeline = 0U; pipeline < 2U; ++pipeline) {
    recorder->RequireResourceState(
      status, graphics::ResourceStates::kUnorderedAccess);
    recorder->FlushBarriers();
    recorder->SetPipelineState(*suitability_pipelines_[pipeline]);
    recorder->SetComputeRoot32BitConstant(
      static_cast<std::uint32_t>(bindless_d3d12::RootParam::kRootConstants), 0U,
      0U);
    recorder->SetComputeRoot32BitConstant(
      static_cast<std::uint32_t>(bindless_d3d12::RootParam::kRootConstants),
      slot.get(), 1U);
    recorder->Dispatch(pipeline == 0U ? 1U : (desc.width + 7U) / 8U,
      pipeline == 0U ? 1U : (desc.height + 7U) / 8U,
      pipeline == 0U ? 1U : desc.depth);
  }
  recorder.reset();
  const bool submitted = recording && recording->IsSubmitted();
  if (submitted)
    submitted_filter_gradients_[frame.get()] |= bit;
  return submitted;
}

auto ExposurePass::EvaluateFp16Products(RenderContext& ctx,
  const FrameLease& frame, const ResolvedPostProcessConfig& config,
  const std::span<const HdrProduct> products, const Inputs& metering,
  const SuitabilityScale scale) -> bool
{
  CHECK_NOTNULL_F(frame.get());
  auto gfx = renderer_.GetGraphics();
  if (!gfx)
    return false;
  PreparePublishers(ctx);
  EnsurePipelines();
  const bool current_scale = scale == SuitabilityScale::kCurrentFrame;
  const auto& report_buffer
    = current_scale ? frame->conversion_buffer : frame->suitability_buffer;
  const auto report_uav
    = current_scale ? frame->conversion_uav : frame->suitability_uav;
  const auto report_srv
    = current_scale ? frame->conversion_srv : frame->suitability_srv;
  if (current_scale)
    submitted_conversion_.erase(frame.get());
  else
    submitted_suitability_.erase(frame.get());
  std::uint32_t expected_mask = 0U;
  std::uint64_t texels = 0U;
  for (const auto& product : products) {
    CHECK_F(product.id > 0U && product.id <= 31U);
    const auto bit = 1U << (product.id - 1U);
    CHECK_F((expected_mask & bit) == 0U);
    expected_mask |= bit;
    CHECK_F(std::isfinite(product.error_budget_share)
      && product.error_budget_share > 0.0F
      && product.error_budget_share <= 1.0F);
    CHECK_F(!product.composed_error || product.id == 11U);
    if (!product.texture || !product.srv.IsValid())
      continue;
    const auto& desc = product.texture->GetDescriptor();
    // Normal-mode intermediates carry their pre-store enclosure in the frame's
    // status record. Sampling their typed half texture is valid only for these
    // bound-producing products; SceneColor remains an FP32 accumulation.
    CHECK_F(desc.format == Format::kRGBA32Float
      || (desc.format == Format::kRGBA16Float
        && (product.id == 5U || product.id == 6U || product.id == 10U)));
    CHECK_F(desc.texture_type == TextureType::kTexture2D
      || desc.texture_type == TextureType::kTexture3D);
    CHECK_F(!product.metering || desc.texture_type == TextureType::kTexture2D);
    texels += std::uint64_t(desc.width) * desc.height * desc.depth;
  }
  CHECK_LE_F(texels, std::numeric_limits<std::uint32_t>::max());
  auto recorder = gfx->AcquireCommandRecorder(
    gfx->QueueKeyFor(graphics::QueueRole::kGraphics),
    "Vortex Exposure Suitability");
  if (!recorder)
    return false;
  const auto recording = recorder->GetCommandListForInspection();
  const auto track
    = [&](const graphics::Buffer& buffer, graphics::ResourceStates state) {
        if (!recorder->IsResourceTracked(buffer)
          && !recorder->AdoptKnownResourceState(buffer))
          recorder->BeginTrackingResourceState(
            buffer, graphics::ResourceStates::kCommon, false);
        recorder->RequireResourceState(buffer, state);
      };
  track(*frame->buffer, graphics::ResourceStates::kShaderResource);
  track(
    *frame->current_state->buffer, graphics::ResourceStates::kShaderResource);
  track(*frame->current_state->status_buffer,
    graphics::ResourceStates::kShaderResource);
  track(*report_buffer, graphics::ResourceStates::kUnorderedAccess);
  if (metering.metering_mask) {
    TrackTextureFromKnownOrInitial(*recorder, *metering.metering_mask);
    recorder->RequireResourceState(
      *metering.metering_mask, graphics::ResourceStates::kShaderResource);
  }
  if (metering.scene_composition && metering.scene_composition->opaque_depth) {
    TrackTextureFromKnownOrInitial(
      *recorder, *metering.scene_composition->opaque_depth);
    recorder->RequireResourceState(*metering.scene_composition->opaque_depth,
      graphics::ResourceStates::kShaderResource);
  }
  const auto background
    = environment::ResolveSceneBackground(ctx).value_or(Vec3 { 0.0F });
  const auto dispatch = [&](const unsigned pipeline, const HdrProduct* product,
                          const std::uint32_t additional_flags = 0U) {
    const bool candidate_bounds = (additional_flags & 512U) != 0U;
    const auto desc
      = product ? product->texture->GetDescriptor() : graphics::TextureDesc {};
    bool opaque_depth_usable = false;
    if (product && product->id == 11U && metering.scene_composition
      && metering.scene_composition->opaque_depth
      && metering.scene_composition->opaque_depth_srv.IsValid()) {
      const auto& depth = metering.scene_composition->opaque_depth->GetDescriptor();
      opaque_depth_usable = depth.texture_type == TextureType::kTexture2D
        && depth.sample_count == 1U && depth.width == desc.width
        && depth.height == desc.height;
    }
    const auto rectangle = product && product->metering
      ? MeteringRectangle(ctx, desc)
      : Scissors { .right = static_cast<std::int32_t>(desc.width),
          .bottom = static_cast<std::int32_t>(desc.height) };
    const auto flags = product ? (product->metering ? 1U : 0U)
        | (product->coverage ? 2U : 0U) | (product->transmittance ? 4U : 0U)
        | (product->composed_error ? 256U : 0U)
        | (desc.texture_type == TextureType::kTexture3D ? 8U : 0U)
                               : 0U;
    const auto constants = std::array<std::uint32_t, 32U> { candidate_bounds
        ? frame->current_state->status_uav_index.get()
        : report_uav.get(),
      product ? product->srv.get() : kInvalidShaderVisibleIndex.get(),
      frame->srv_index.get(), frame->current_state->srv_index.get(), desc.width,
      desc.height, desc.depth,
      flags | (scale == SuitabilityScale::kCurrentFrame ? 16U : 0U)
        | additional_flags,
      product ? product->id : 0U, expected_mask,
      metering.metering_mask_srv.get(),
      static_cast<std::uint32_t>(config.Exposure().authored.metering_mode),
      static_cast<std::uint32_t>(rectangle.left),
      static_cast<std::uint32_t>(rectangle.top),
      static_cast<std::uint32_t>(std::max(0, rectangle.right - rectangle.left)),
      static_cast<std::uint32_t>(std::max(0, rectangle.bottom - rectangle.top)),
      std::bit_cast<std::uint32_t>(
        config.Exposure().authored.spot_meter_radius),
      std::bit_cast<std::uint32_t>(
        product ? product->error_budget_share : 1.0F),
      std::bit_cast<std::uint32_t>(
        config.Exposure().authored.min_log_luminance),
      std::bit_cast<std::uint32_t>(config.Exposure().authored.black_influence),
      std::bit_cast<std::uint32_t>(product ? product->consumer_rgb_gain : 1.0F),
      frame->current_state->status_srv_index.get(),
      candidate_bounds ? report_srv.get() : 0U, metering.composition_products,
      std::bit_cast<std::uint32_t>(background.x),
      std::bit_cast<std::uint32_t>(background.y),
      std::bit_cast<std::uint32_t>(background.z),
      static_cast<std::uint32_t>(config.Settings().tone_mapper),
      std::bit_cast<std::uint32_t>(std::max(config.Settings().gamma, 1.0e-4F)),
      metering.scene_composition
        ? metering.scene_composition->opaque_depth_srv.get()
        : 0U,
      metering.scene_composition && metering.scene_composition->reverse_z ? 1U
                                                                          : 0U,
      opaque_depth_usable ? 1U : 0U };
    const auto slot = suitability_constants_publisher_->Publish(
      ctx.current_view.view_id, constants);
    CHECK_F(slot.IsValid());
    recorder->RequireResourceState(*report_buffer,
      candidate_bounds ? graphics::ResourceStates::kShaderResource
                       : graphics::ResourceStates::kUnorderedAccess);
    recorder->RequireResourceState(*frame->current_state->status_buffer,
      candidate_bounds ? graphics::ResourceStates::kUnorderedAccess
                       : graphics::ResourceStates::kShaderResource);
    recorder->FlushBarriers();
    recorder->SetPipelineState(*suitability_pipelines_[pipeline]);
    recorder->SetComputeRoot32BitConstant(
      static_cast<std::uint32_t>(bindless_d3d12::RootParam::kRootConstants), 0U,
      0U);
    recorder->SetComputeRoot32BitConstant(
      static_cast<std::uint32_t>(bindless_d3d12::RootParam::kRootConstants),
      slot.get(), 1U);
    recorder->Dispatch(product ? (desc.width + 7U) / 8U : 1U,
      product ? (desc.height + 7U) / 8U : 1U,
      product && desc.texture_type == TextureType::kTexture3D ? desc.depth
                                                              : 1U);
  };
  dispatch(0U, nullptr);
  if (!current_scale && metering.scene_composition)
    dispatch(0U, nullptr, 512U | 1024U);
  for (const auto& product : products) {
    if (!product.texture || !product.srv.IsValid())
      continue;
    TrackTextureFromKnownOrInitial(*recorder, *product.texture);
    recorder->RequireResourceState(
      *product.texture, graphics::ResourceStates::kShaderResource);
    dispatch(1U, &product);
  }
  dispatch(2U, nullptr);
  if (!current_scale) {
    dispatch(0U, nullptr, 512U);
    for (const auto& product : products)
      if (product.texture && product.srv.IsValid()
        && (product.id == 5U || product.id == 6U || product.id == 10U))
        dispatch(1U, &product, 512U);
  }
  if (!current_scale && metering.scene_composition) {
    // Same 128-byte publisher and selector pipeline; a separate constant view
    // describes the complete consumer chain after candidate stores exist.
    auto constants = std::array<std::uint32_t, 32U> {};
    constants[0] = frame->current_state->status_uav_index.get();
    constants[1] = report_srv.get();
    constants[2] = frame->srv_index.get();
    constants[7] = 1024U;
    constants[20] = std::bit_cast<std::uint32_t>(1.0F);
    const auto& composition = *metering.scene_composition;
    const auto operations = 16ULL
      * (std::min<std::uint64_t>(composition.translucent_triangles, 0xffffffffU)
        + composition.local_fog_instances + 8ULL);
    constants[21] = static_cast<std::uint32_t>(
      std::min<std::uint64_t>(operations, 0xffffffffU));
    constants[22] = expected_mask;
    for (const auto& product : products) {
      if (product.id == 11U && product.texture && product.srv.IsValid()) {
        const auto& desc = product.texture->GetDescriptor();
        constants[23] = desc.width * desc.height;
      }
      const auto index = product.id == 5U ? 8U
        : product.id == 6U                ? 12U
        : product.id == 10U               ? 16U
                                          : 0U;
      if (index == 0U || !product.texture || !product.srv.IsValid())
        continue;
      const auto& desc = product.texture->GetDescriptor();
      constants[index] = desc.width;
      constants[index + 1U] = desc.height;
      constants[index + 2U] = desc.depth;
      constants[index + 3U] = desc.format == Format::kRGBA16Float ? 1U : 0U;
      if (product.id == 6U)
        constants[20] = std::bit_cast<std::uint32_t>(product.consumer_rgb_gain);
    }
    const auto slot = suitability_constants_publisher_->Publish(
      ctx.current_view.view_id, constants);
    CHECK_F(slot.IsValid());
    recorder->RequireResourceState(
      *report_buffer, graphics::ResourceStates::kShaderResource);
    recorder->RequireResourceState(*frame->current_state->status_buffer,
      graphics::ResourceStates::kUnorderedAccess);
    recorder->FlushBarriers();
    recorder->SetPipelineState(*suitability_pipelines_[2U]);
    recorder->SetComputeRoot32BitConstant(
      static_cast<std::uint32_t>(bindless_d3d12::RootParam::kRootConstants), 0U,
      0U);
    recorder->SetComputeRoot32BitConstant(
      static_cast<std::uint32_t>(bindless_d3d12::RootParam::kRootConstants),
      slot.get(), 1U);
    recorder->Dispatch(1U, 1U, 1U);
  }
  for (const auto& product : products)
    if (product.texture && product.srv.IsValid())
      dispatch(3U, &product);
  recorder->RequireResourceStateFinal(
    *report_buffer, graphics::ResourceStates::kShaderResource);
  recorder.reset();
  if (!recording || !recording->IsSubmitted())
    return false;
  if (!current_scale)
    submitted_suitability_.insert(frame.get());
  return true;
}

auto ExposurePass::FinalizeFp16Suitability(RenderContext& ctx,
  const FrameLease& frame, const EligibilityInputs& inputs) -> bool
{
  CHECK_F(inputs.product_layout_revision != 0U && inputs.expected_products != 0U
    && (inputs.expected_products & 0x80000000U) == 0U);
  auto gfx = renderer_.GetGraphics();
  if (!gfx)
    return false;
  PreparePublishers(ctx);
  const auto current = resolved_frames_.find(
    { ctx.current_view.view_id, ctx.current_view.view_state_handle });
  if (!frame || current == resolved_frames_.end() || current->second != frame
    || !submitted_suitability_.contains(frame.get()))
    return false;
  EnsurePipelines();
  auto recorder = gfx->AcquireCommandRecorder(
    gfx->QueueKeyFor(graphics::QueueRole::kGraphics),
    "Vortex FP16 Eligibility");
  if (!recorder)
    return false;
  const auto recording = recorder->GetCommandListForInspection();
  const auto track
    = [&](const graphics::Buffer& buffer, graphics::ResourceStates state) {
        if (!recorder->AdoptKnownResourceState(buffer))
          recorder->BeginTrackingResourceState(
            buffer, graphics::ResourceStates::kCommon, false);
        recorder->RequireResourceState(buffer, state);
      };
  track(*frame->buffer, graphics::ResourceStates::kShaderResource);
  track(*frame->suitability_buffer, graphics::ResourceStates::kShaderResource);
  const bool converted = submitted_conversion_.contains(frame.get());
  if (converted)
    track(*frame->conversion_buffer, graphics::ResourceStates::kShaderResource);
  track(
    *frame->current_state->buffer, graphics::ResourceStates::kUnorderedAccess);
  track(*frame->current_state->status_buffer,
    graphics::ResourceStates::kUnorderedAccess);
  if (frame->precision_history)
    track(*frame->precision_history->buffer,
      graphics::ResourceStates::kShaderResource);
  const auto words = [](const std::uint64_t value) {
    return std::array<std::uint32_t, 2> { static_cast<std::uint32_t>(value),
      static_cast<std::uint32_t>(value >> 32U) };
  };
  const auto layout = words(inputs.product_layout_revision);
  const auto sequence = words(ctx.frame_sequence.get());
  const auto lifetime = words(frame->current_state->owner_lifetime);
  const std::array<std::uint32_t, 16U> constants {
    frame->current_state->uav_index.get(),
    frame->precision_history ? frame->precision_history->srv_index.get()
                             : kInvalidShaderVisibleIndex.get(),
    frame->current_state->status_uav_index.get(), frame->suitability_srv.get(),
    layout[0], layout[1], inputs.expected_products,
    (ctx.current_view.view_state_handle
          != CompositionView::kInvalidViewStateHandle
        ? 1U
        : 0U)
      | (inputs.invalidate_previous ? 2U : 0U),
    frame->srv_index.get(), sequence[0], sequence[1],
    converted ? frame->conversion_srv.get() : kInvalidShaderVisibleIndex.get(),
    lifetime[0], lifetime[1], 0U, 0U
  };
  const auto slot
    = constants_publisher_->Publish(ctx.current_view.view_id, constants);
  CHECK_F(slot.IsValid());
  recorder->FlushBarriers();
  recorder->SetPipelineState(*eligibility_pipeline_);
  recorder->SetComputeRoot32BitConstant(
    static_cast<std::uint32_t>(bindless_d3d12::RootParam::kRootConstants), 0U,
    0U);
  recorder->SetComputeRoot32BitConstant(
    static_cast<std::uint32_t>(bindless_d3d12::RootParam::kRootConstants),
    slot.get(), 1U);
  recorder->Dispatch(1U, 1U, 1U);
  recorder->RequireResourceStateFinal(
    *frame->current_state->buffer, graphics::ResourceStates::kShaderResource);
  recorder->RequireResourceStateFinal(*frame->current_state->status_buffer,
    graphics::ResourceStates::kCopySource);
  recorder.reset();
  return recording && recording->IsSubmitted();
}

auto ExposurePass::ConvertCheckedSceneColor(RenderContext& ctx,
  const FrameLease& frame, const ResolvedPostProcessConfig& config,
  const Inputs& inputs, graphics::Texture& destination,
  const ShaderVisibleIndex destination_uav) -> bool
{
  CHECK_NOTNULL_F(inputs.scene_signal);
  CHECK_F(inputs.scene_signal_srv.IsValid() && destination_uav.IsValid());
  const auto& source_desc = inputs.scene_signal->GetDescriptor();
  const auto& target_desc = destination.GetDescriptor();
  CHECK_F(source_desc.format == Format::kRGBA32Float
    && target_desc.format == Format::kRGBA16Float
    && source_desc.texture_type == TextureType::kTexture2D
    && target_desc.texture_type == TextureType::kTexture2D
    && source_desc.sample_count == 1U && target_desc.sample_count == 1U
    && source_desc.array_size == 1U && target_desc.array_size == 1U
    && source_desc.mip_levels == 1U && target_desc.mip_levels == 1U
    && source_desc.depth == 1U && target_desc.depth == 1U
    && source_desc.width == target_desc.width
    && source_desc.height == target_desc.height && target_desc.is_uav);
  const std::array products { HdrProduct { .texture = inputs.scene_signal,
    .srv = inputs.scene_signal_srv,
    .id = 11U,
    .metering = true,
    .coverage = environment::ResolveSceneBackground(ctx).has_value(),
    .composed_error = inputs.composition_products != 0U } };
  if (!EvaluateFp16Products(
        ctx, frame, config, products, inputs, SuitabilityScale::kCurrentFrame))
    return false;
  const auto gfx = renderer_.GetGraphics();
  auto recorder = gfx->AcquireCommandRecorder(
    gfx->QueueKeyFor(graphics::QueueRole::kGraphics),
    "Vortex Checked SceneColor Conversion");
  if (!recorder)
    return false;
  const auto recording = recorder->GetCommandListForInspection();
  TrackTextureFromKnownOrInitial(*recorder, *inputs.scene_signal);
  TrackTextureFromKnownOrInitial(*recorder, destination);
  CHECK_F(recorder->AdoptKnownResourceState(*frame->conversion_buffer));
  recorder->RequireResourceState(
    *inputs.scene_signal, graphics::ResourceStates::kShaderResource);
  recorder->RequireResourceState(
    *frame->conversion_buffer, graphics::ResourceStates::kShaderResource);
  recorder->RequireResourceState(
    destination, graphics::ResourceStates::kUnorderedAccess);
  const std::array<std::uint32_t, 8U> constants { inputs.scene_signal_srv.get(),
    destination_uav.get(), frame->conversion_srv.get(), source_desc.width,
    source_desc.height, 0U, 0U, 0U };
  const auto slot = conversion_constants_publisher_->Publish(
    ctx.current_view.view_id, constants);
  CHECK_F(slot.IsValid());
  recorder->FlushBarriers();
  recorder->SetPipelineState(*convert_pipeline_);
  recorder->SetComputeRoot32BitConstant(
    static_cast<std::uint32_t>(bindless_d3d12::RootParam::kRootConstants), 0U,
    0U);
  recorder->SetComputeRoot32BitConstant(
    static_cast<std::uint32_t>(bindless_d3d12::RootParam::kRootConstants),
    slot.get(), 1U);
  recorder->Dispatch(
    (source_desc.width + 7U) / 8U, (source_desc.height + 7U) / 8U, 1U);
  recorder->RequireResourceStateFinal(
    destination, graphics::ResourceStates::kShaderResource);
  recorder.reset();
  if (!recording || !recording->IsSubmitted())
    return false;
  submitted_conversion_.insert(frame.get());
  return true;
}

auto ExposurePass::Execute(RenderContext& ctx,
  const ResolvedPostProcessConfig& config, const Inputs& inputs) -> Result
{
  const auto& resolved = config.Exposure();
  const bool sharing
    = inputs.source && !config.Settings().temporary_unit_exposure;
  const bool automatic = !sharing && !config.Settings().temporary_unit_exposure
    && resolved.authored.enabled
    && resolved.authored.mode == engine::ExposureMode::kAuto;
  auto result = Result { .requested = true,
    .used_fixed_exposure = !automatic && !sharing,
    .borrowed_exposure = sharing,
    .exposure_value = config.Settings().temporary_unit_exposure ? 1.0F
      : automatic ? (resolved.authored.target_luminance == 0.0F
                        ? 0.0F
                        : std::exp2(resolved.initial_log_gain))
                  : resolved.fixed_scale };
  auto gfx = renderer_.GetGraphics();
  if (!gfx)
    return result;
  EnsurePipelines();
  PreparePublishers(ctx);
  const auto handle = ctx.current_view.view_state_handle;
  if (const auto found
    = resolved_frames_.find({ ctx.current_view.view_id, handle });
    found != resolved_frames_.end()) {
    result.frame = found->second;
    CHECK_F(result.frame->current_state->owner_lifetime == inputs.lifetime);
  }
  CHECK_F(!inputs.transition || inputs.transition->target == handle,
    "Exposure transition targets a different view state");
  auto* view = config.Settings().temporary_unit_exposure
      || handle == CompositionView::kInvalidViewStateHandle
    ? nullptr
    : &exposure_states_[handle];
  if (view && view->latest && view->latest->owner_lifetime != inputs.lifetime) {
    view->latest.reset();
    view->submitted_frame.reset();
  }
  if (view && view->selected_borrow
    && view->selected_borrow->consumer_lifetime != inputs.lifetime)
    view->selected_borrow.reset();
  const auto publish_result = [&](StateLease state, bool executed) {
    result.state = std::move(state);
    result.executed = executed;
    if (view && sharing)
      view->selected_borrow
        = PerViewExposureState::BorrowSelection { result.state, *inputs.source,
            inputs.lifetime };
    if (result.state) {
      result.exposure_buffer = result.state->buffer.get();
      result.histogram_buffer
        = automatic ? result.state->histogram_buffer.get() : nullptr;
      result.exposure_buffer_srv = result.state->srv_index;
      result.exposure_buffer_uav = result.state->uav_index;
      frame_states_[ctx.frame_slot.get()].push_back(result.state);
    }
  };
  if (view && view->submitted_frame == ctx.frame_sequence) {
    publish_result(view->latest, false);
    return result;
  }
  const auto previous = view ? view->latest : StateLease {};
  if (view
    && (sharing
      || (view->source_loss
        && view->source_loss->consumer_lifetime != inputs.lifetime)))
    view->source_loss.reset();
  StateLease borrowed;
  if (sharing) {
    CHECK_F(inputs.source->handle != handle
        && inputs.source->handle != CompositionView::kInvalidViewStateHandle,
      "A shared exposure source must be a distinct persistent root");
    if (const auto prior = prior_states_.find(inputs.source->handle);
      prior != prior_states_.end()
      && prior->second->owner_lifetime == inputs.source->lifetime) {
      borrowed = prior->second;
    } else if (const auto initial
      = bootstrap_states_.find(inputs.source->handle);
      initial != bootstrap_states_.end()
      && initial->second->owner_lifetime == inputs.source->lifetime) {
      borrowed = initial->second;
    } else {
      borrowed = RecordState(ctx, inputs.source->config,
        Inputs { .metering_available = false,
          .transition = inputs.source->transition,
          .rejection = inputs.source->rejection,
          .lifetime = inputs.source->lifetime },
        {}, {}, true);
      if (borrowed)
        bootstrap_states_.insert_or_assign(inputs.source->handle, borrowed);
    }
    if (!borrowed) {
      // Submission failure cannot substitute the consumer's own history.
      result.exposure_value = SourceFallbackScale(*inputs.source);
      if (view)
        view->selected_borrow = PerViewExposureState::BorrowSelection { {},
          *inputs.source, inputs.lifetime };
      return result;
    }
    frame_states_[ctx.frame_slot.get()].push_back(borrowed);
  }
  StateLease continuity;
  if (view && view->source_loss) {
    auto& loss = *view->source_loss;
    const auto* selected = view->selected_borrow
        && view->selected_borrow->source.handle == loss.source.handle
        && view->selected_borrow->source.lifetime == loss.source.lifetime
      ? &*view->selected_borrow
      : nullptr;
    if (selected) {
      continuity = selected->state;
      if (!continuity) {
        continuity = RecordState(ctx, selected->source.config,
          Inputs { .metering_available = false,
            .transition = selected->source.transition,
            .rejection = selected->source.rejection,
            .lifetime = selected->source.lifetime },
          {}, {}, true);
        loss.source = selected->source;
        loss.fallback = continuity;
      }
    } else if (previous && previous->borrowed_from == loss.source.handle
      && previous->borrowed_lifetime == loss.source.lifetime) {
      continuity = previous;
    } else if (loss.fallback) {
      continuity = loss.fallback;
    } else {
      continuity = RecordState(ctx, loss.source.config,
        Inputs { .metering_available = false,
          .transition = loss.source.transition,
          .rejection = loss.source.rejection,
          .lifetime = loss.source.lifetime },
        {}, {}, true);
      loss.fallback = continuity;
    }
    if (!continuity) {
      result.exposure_value = SourceFallbackScale(loss.source);
      return result;
    }
    frame_states_[ctx.frame_slot.get()].push_back(continuity);
  }
  auto solve_inputs = inputs;
  auto reserved = std::shared_ptr<StateResources> {};
  if (result.frame) {
    solve_inputs.frame_exposure = result.frame.get();
    reserved = result.frame->current_state;
  }
  auto state = RecordState(ctx, config, solve_inputs, previous,
    continuity ? continuity : borrowed, false, static_cast<bool>(continuity),
    reserved);
  if (!state) {
    result.solve_failed = true;
    const auto fallback = continuity ? continuity
      : sharing                      ? borrowed
                                     : previous;
    if (result.frame && !sharing) {
      if (RestoreFrameFallback(ctx, config, *result.frame, fallback))
        publish_result(result.frame->current_state, false);
    } else {
      publish_result(fallback, false);
    }
    return result;
  }
  if (view) {
    view->latest = state;
    view->submitted_frame = ctx.frame_sequence;
    view->source_loss.reset();
    if (!sharing)
      view->selected_borrow.reset();
  }
  publish_result(state, true);
  return result;
}

auto ExposurePass::RecordState(RenderContext& ctx,
  const ResolvedPostProcessConfig& config, const Inputs& inputs,
  StateLease previous, StateLease borrowed, const bool bootstrap,
  const bool source_loss, std::shared_ptr<StateResources> reserved)
  -> StateLease
{
  const auto& resolved = config.Exposure();
  const bool automatic = (!borrowed || source_loss) && !bootstrap
    && !config.Settings().temporary_unit_exposure && resolved.authored.enabled
    && resolved.authored.mode == engine::ExposureMode::kAuto;
  auto gfx = renderer_.GetGraphics();
  CHECK_NOTNULL_F(gfx.get());
  auto state = reserved ? std::move(reserved) : AcquireState();
  state->owner_lifetime = inputs.lifetime;
  state->borrowed_from = borrowed && !source_loss && inputs.source
    ? inputs.source->handle
    : CompositionView::kInvalidViewStateHandle;
  state->borrowed_lifetime
    = borrowed && !source_loss && inputs.source ? inputs.source->lifetime : 0U;
  // Even an unpublished/failed attempt can contain submitted work. Retain its
  // resources through this frame slot before allowing pool reuse.
  frame_states_[ctx.frame_slot.get()].push_back(state);
  if (automatic)
    EnsureHistogramBuffer(*state);
  auto targets = ExposureTargetData {};
  CHECK_LE_F(resolved.auto_log_targets.size(), targets.keys.size());
  targets.key_count
    = static_cast<std::uint32_t>(resolved.auto_log_targets.size());
  targets.flags = (resolved.authored.min_ev == resolved.authored.max_ev
                      ? ExposureTargetData::kLocked
                      : 0U)
    | (resolved.authored.target_luminance == 0.0F
        ? ExposureTargetData::kZeroTarget
        : 0U);
  targets.initial_log_gain = resolved.initial_log_gain;
  targets.dark_log_gain = resolved.dark_log_gain;
  std::ranges::copy(resolved.auto_log_targets, targets.keys.begin());
  const auto targets_srv
    = target_publisher_->Publish(ctx.current_view.view_id, targets);
  if (!targets_srv.IsValid()) {
    return {};
  }
  auto recorder = gfx->AcquireCommandRecorder(
    gfx->QueueKeyFor(graphics::QueueRole::kGraphics), "Vortex Exposure");
  if (!recorder) {
    return {};
  }
  const auto recording = recorder->GetCommandListForInspection();
  const auto track_buffer = [&](const graphics::Buffer& buffer) {
    if (!recorder->IsResourceTracked(buffer)
      && !recorder->AdoptKnownResourceState(buffer)) {
      recorder->BeginTrackingResourceState(
        buffer, graphics::ResourceStates::kCommon, false);
    }
  };
  track_buffer(*state->buffer);
  track_buffer(*state->status_buffer);
  if (inputs.frame_exposure) {
    track_buffer(*inputs.frame_exposure->buffer);
    recorder->RequireResourceState(*inputs.frame_exposure->buffer,
      graphics::ResourceStates::kShaderResource);
  }
  recorder->RequireResourceState(
    *state->status_buffer, graphics::ResourceStates::kUnorderedAccess);
  recorder->RequireResourceState(
    *state->buffer, graphics::ResourceStates::kUnorderedAccess);
  if (previous) {
    track_buffer(*previous->buffer);
    recorder->RequireResourceState(
      *previous->buffer, graphics::ResourceStates::kShaderResource);
  }
  if (borrowed) {
    track_buffer(*borrowed->buffer);
    recorder->RequireResourceState(
      *borrowed->buffer, graphics::ResourceStates::kShaderResource);
  }
  if (automatic) {
    track_buffer(*state->histogram_buffer);
    recorder->RequireResourceState(
      *state->histogram_buffer, graphics::ResourceStates::kUnorderedAccess);
    recorder->FlushBarriers();
    recorder->SetPipelineState(*clear_pipeline_);
    UpdateHistogramConstants(ctx, *recorder, inputs, config, *state);
    recorder->Dispatch(1U, 1U, 1U);
    if (inputs.metering_available && inputs.scene_signal
      && inputs.scene_signal_srv.IsValid()) {
      CHECK_F(std::isfinite(inputs.one_over_pre_exposure)
        && inputs.one_over_pre_exposure > 0.0F);
      CHECK_F((inputs.metering_mask != nullptr)
        == inputs.metering_mask_srv.IsValid());
      TrackTextureFromKnownOrInitial(*recorder, *inputs.scene_signal);
      recorder->RequireResourceState(
        *inputs.scene_signal, graphics::ResourceStates::kShaderResource);
      if (inputs.metering_mask) {
        TrackTextureFromKnownOrInitial(*recorder, *inputs.metering_mask);
        recorder->RequireResourceState(
          *inputs.metering_mask, graphics::ResourceStates::kShaderResource);
      }
      recorder->RequireResourceState(
        *state->histogram_buffer, graphics::ResourceStates::kUnorderedAccess);
      recorder->FlushBarriers();
      recorder->SetPipelineState(*histogram_pipeline_);
      UpdateHistogramConstants(ctx, *recorder, inputs, config, *state);
      const auto& desc = inputs.scene_signal->GetDescriptor();
      recorder->Dispatch(
        (std::min(desc.width, kHistogramGridLimit) + 15U) / 16U,
        (std::min(desc.height, kHistogramGridLimit) + 15U) / 16U, 1U);
    }
    recorder->RequireResourceState(
      *state->histogram_buffer, graphics::ResourceStates::kUnorderedAccess);
  }
  recorder->FlushBarriers();
  {
    graphics::GpuEventScope solve_scope(*recorder,
      "Vortex.PostProcess.Exposure.Solve",
      profiling::ProfileGranularity::kDiagnostic,
      profiling::ProfileCategory::kPass);
    recorder->SetPipelineState(*average_pipeline_);
    UpdateAverageConstants(ctx, *recorder, config, *state, targets_srv,
      previous ? previous->srv_index : kInvalidShaderVisibleIndex, inputs,
      borrowed ? borrowed->srv_index : kInvalidShaderVisibleIndex, bootstrap,
      automatic, source_loss);
    recorder->Dispatch(1U, 1U, 1U);
  }
  recorder->RequireResourceStateFinal(
    *state->buffer, graphics::ResourceStates::kShaderResource);
  recorder->RequireResourceStateFinal(
    *state->status_buffer, graphics::ResourceStates::kCopySource);
  if (automatic)
    recorder->RequireResourceStateFinal(
      *state->histogram_buffer, graphics::ResourceStates::kCommon);
  recorder.reset();
  if (!recording || !recording->IsSubmitted()) {
    return {};
  }
  return state;
}

auto ExposurePass::PreserveRemovedSource(
  std::shared_ptr<const ExposureSourceLoss> loss, const Source& source,
  CompositionView::ViewStateHandle only_consumer) -> void
{
  StateLease fallback;
  if (const auto root = exposure_states_.find(source.handle);
    root != exposure_states_.end() && root->second.latest
    && root->second.latest->owner_lifetime == source.lifetime)
    fallback = root->second.latest;
  for (const auto& consumer : loss->consumers) {
    if (only_consumer != CompositionView::kInvalidViewStateHandle
      && consumer.handle != only_consumer)
      continue;
    auto& view = exposure_states_[consumer.handle];
    if (view.source_loss && view.source_loss->event == loss)
      continue;
    view.source_loss = PerViewExposureState::PendingSourceLoss { loss, source,
      fallback, consumer.lifetime };
  }
}

auto ExposurePass::RemoveViewState(CompositionView::ViewStateHandle handle)
  -> void
{
  exposure_states_.erase(handle);
  // Prior-frame readers may still need a removed owner's pinned state.
}

auto ExposurePass::EnsurePipelines() -> void
{
  if (!eligibility_pipeline_)
    eligibility_pipeline_ = BuildExposurePipeline(
      "FinalizeFp16Suitability", "Vortex.Exposure.Fp16Eligibility");
  if (!convert_pipeline_)
    convert_pipeline_ = BuildExposurePipeline(
      "ConvertQualifiedSceneColor", "Vortex.Exposure.CheckedSceneColor");
  constexpr std::array names { "ClearSuitability", "GatherSuitabilityMaximum",
    "SelectSuitabilityCandidate", "CheckSuitabilityProduct" };
  for (std::size_t index = 0; index < names.size(); ++index)
    if (!suitability_pipelines_[index])
      suitability_pipelines_[index]
        = BuildExposurePipeline(names[index], names[index]);
  if (!fallback_pipeline_) {
    fallback_pipeline_ = BuildExposurePipeline(
      "VortexExposureFallbackCS", "Vortex.PostProcess.Exposure.Fallback");
  }
  if (!frame_pipeline_) {
    frame_pipeline_ = BuildExposurePipeline(
      "VortexExposureFrameCS", "Vortex.PostProcess.Exposure.Frame");
  }
  if (!clear_pipeline_.has_value()) {
    clear_pipeline_ = BuildExposurePipeline(
      "ClearHistogram", "Vortex.PostProcess.Exposure.Clear");
  }
  if (!histogram_pipeline_.has_value()) {
    histogram_pipeline_ = BuildExposurePipeline(
      "VortexExposureHistogramCS", "Vortex.PostProcess.Exposure.Histogram");
  }
  if (!average_pipeline_.has_value()) {
    average_pipeline_ = BuildExposurePipeline(
      "VortexExposureAverageCS", "Vortex.PostProcess.Exposure.Average");
  }
}

auto ExposurePass::EnsureHistogramBuffer(StateResources& state) -> void
{
  if (state.histogram_buffer != nullptr) {
    return;
  }

  auto gfx = renderer_.GetGraphics();
  CHECK_NOTNULL_F(gfx.get());
  state.histogram_buffer = gfx->CreateBuffer({
    .size_bytes = kHistogramWordCount * sizeof(std::uint32_t),
    .usage = graphics::BufferUsage::kStorage,
    .memory = graphics::BufferMemory::kDeviceLocal,
    .debug_name = "Vortex.PostProcess.Exposure.Histogram",
  });
  CHECK_NOTNULL_F(state.histogram_buffer.get());
  state.histogram_buffer->SetName("Vortex.PostProcess.Exposure.Histogram");

  RegisterResourceIfNeeded(*gfx, state.histogram_buffer);
  auto& allocator = gfx->GetDescriptorAllocator();
  auto& registry = gfx->GetResourceRegistry();
  auto handle
    = allocator.AllocateRaw(graphics::ResourceViewType::kRawBuffer_UAV,
      graphics::DescriptorVisibility::kShaderVisible);
  CHECK_F(handle.IsValid(),
    "ExposurePass: failed to allocate histogram UAV descriptor");
  state.histogram_uav_index = allocator.GetShaderVisibleIndex(handle);
  const auto view_desc = graphics::BufferViewDescription {
    .view_type = graphics::ResourceViewType::kRawBuffer_UAV,
    .visibility = graphics::DescriptorVisibility::kShaderVisible,
    .range = { 0U, kHistogramWordCount * sizeof(std::uint32_t) },
    .stride = 0U,
  };
  const auto view = registry.RegisterView(
    *state.histogram_buffer, std::move(handle), view_desc);
  CHECK_F(
    view->IsValid(), "ExposurePass: failed to register histogram UAV view");
}

auto ExposurePass::AcquireState() -> std::shared_ptr<StateResources>
{
  for (const auto& state : state_pool_) {
    if (state.use_count() == 1)
      return state;
  }
  auto gfx = renderer_.GetGraphics();
  CHECK_NOTNULL_F(gfx.get());
  auto state = std::make_shared<StateResources>();
  state->buffer = gfx->CreateBuffer({ .size_bytes = sizeof(ExposureStateData),
    .usage = graphics::BufferUsage::kStorage,
    .memory = graphics::BufferMemory::kDeviceLocal,
    .debug_name = "Vortex.PostProcess.Exposure.State" });
  CHECK_NOTNULL_F(state->buffer.get());
  RegisterResourceIfNeeded(*gfx, state->buffer);
  auto& registry = gfx->GetResourceRegistry();
  auto& allocator = gfx->GetDescriptorAllocator();
  for (const auto type : { graphics::ResourceViewType::kRawBuffer_SRV,
         graphics::ResourceViewType::kRawBuffer_UAV }) {
    auto handle = allocator.AllocateRaw(
      type, graphics::DescriptorVisibility::kShaderVisible);
    CHECK_F(handle.IsValid());
    const auto index = allocator.GetShaderVisibleIndex(handle);
    const auto view = registry.RegisterView(*state->buffer, std::move(handle),
      graphics::BufferViewDescription { .view_type = type,
        .visibility = graphics::DescriptorVisibility::kShaderVisible,
        .range = { 0U, sizeof(ExposureStateData) },
        .stride = 0U });
    CHECK_F(view->IsValid());
    if (type == graphics::ResourceViewType::kRawBuffer_SRV)
      state->srv_index = index;
    else
      state->uav_index = index;
  }
  state->status_buffer
    = gfx->CreateBuffer({ .size_bytes = sizeof(ExposureStatusStorage),
      .usage = graphics::BufferUsage::kStorage,
      .memory = graphics::BufferMemory::kDeviceLocal,
      .debug_name = "Vortex.PostProcess.Exposure.Status" });
  CHECK_NOTNULL_F(state->status_buffer.get());
  RegisterResourceIfNeeded(*gfx, state->status_buffer);
  for (const auto type : { graphics::ResourceViewType::kRawBuffer_SRV,
         graphics::ResourceViewType::kRawBuffer_UAV }) {
    auto handle = allocator.AllocateRaw(
      type, graphics::DescriptorVisibility::kShaderVisible);
    CHECK_F(handle.IsValid());
    const auto index = allocator.GetShaderVisibleIndex(handle);
    const auto view
      = registry.RegisterView(*state->status_buffer, std::move(handle),
        graphics::BufferViewDescription { .view_type = type,
          .visibility = graphics::DescriptorVisibility::kShaderVisible,
          .range = { 0U, sizeof(ExposureStatusStorage) },
          .stride = 0U });
    CHECK_F(view->IsValid());
    if (type == graphics::ResourceViewType::kRawBuffer_SRV)
      state->status_srv_index = index;
    else
      state->status_uav_index = index;
  }
  state_pool_.push_back(state);
  return state;
}

auto ExposurePass::UpdateHistogramConstants(RenderContext& ctx,
  graphics::CommandRecorder& recorder, const Inputs& inputs,
  const ResolvedPostProcessConfig& config, const StateResources& state) -> void
{
  DCHECK_NOTNULL_F(constants_publisher_.get());
  const auto desc = inputs.scene_signal ? inputs.scene_signal->GetDescriptor()
                                        : graphics::TextureDesc {};
  const auto rectangle = MeteringRectangle(ctx, desc);
  const auto constants = AutoExposureHistogramConstants {
    .source_texture_index = inputs.scene_signal_srv.get(),
    .histogram_buffer_index = state.histogram_uav_index.get(),
    .min_log_luminance = config.Exposure().authored.min_log_luminance,
    .inv_log_luminance_range
    = 1.0F / config.Exposure().authored.log_luminance_range,
    .metering_left = static_cast<std::uint32_t>(rectangle.left),
    .metering_top = static_cast<std::uint32_t>(rectangle.top),
    .metering_width
    = static_cast<std::uint32_t>(std::max(0, rectangle.right - rectangle.left)),
    .metering_height
    = static_cast<std::uint32_t>(std::max(0, rectangle.bottom - rectangle.top)),
    .metering_mode
    = static_cast<std::uint32_t>(config.Exposure().authored.metering_mode),
    .spot_meter_radius = config.Exposure().authored.spot_meter_radius,
    .mask_texture_index = inputs.metering_mask_srv.get(),
    .background_enabled
    = environment::ResolveSceneBackground(ctx).has_value() ? 1U : 0U,
    .one_over_pre_exposure = inputs.one_over_pre_exposure,
    .black_influence = config.Exposure().authored.black_influence,
    .frame_exposure_srv = inputs.frame_exposure
      ? inputs.frame_exposure->srv_index.get()
      : kInvalidShaderVisibleIndex.get(),
    ._pad1 = 0U,
  };

  const auto slot = constants_publisher_->Publish(ctx.current_view.view_id,
    std::bit_cast<std::array<std::uint32_t, 16U>>(constants));
  CHECK_F(slot.IsValid(), "Exposure constants publication failed");

  recorder.SetComputeRoot32BitConstant(
    static_cast<std::uint32_t>(bindless_d3d12::RootParam::kRootConstants), 0U,
    0U);
  recorder.SetComputeRoot32BitConstant(
    static_cast<std::uint32_t>(bindless_d3d12::RootParam::kRootConstants),
    slot.get(), 1U);
}

auto ExposurePass::UpdateAverageConstants(RenderContext& ctx,
  graphics::CommandRecorder& recorder, const ResolvedPostProcessConfig& config,
  const StateResources& state, const ShaderVisibleIndex targets_srv,
  const ShaderVisibleIndex previous_srv, const Inputs& inputs,
  ShaderVisibleIndex borrowed_srv, const bool bootstrap, const bool metering,
  const bool source_loss) -> void
{
  DCHECK_NOTNULL_F(average_constants_publisher_.get());
  const auto log_rate = [](const float value) {
    // -256 is outside log2 of every positive finite binary32 input.
    return std::isfinite(value) && value > 0.0F
      ? static_cast<float>(std::log2(static_cast<double>(value)))
      : -256.0F;
  };
  const auto& resolved = config.Exposure();
  const auto seed = inputs.transition
      && inputs.transition->policy == ExposureTransitionPolicy::kSeedFromEv100
      && inputs.transition->seed_ev
    ? scene::ResolveExposureSeedLogGain(resolved, *inputs.transition->seed_ev)
    : std::expected<float, scene::ExposureSettingsError> { 0.0F };
  const auto generation
    = inputs.transition && !config.Settings().temporary_unit_exposure
    ? inputs.transition->generation
    : 0U;
  std::uint32_t rejection = 0U;
  if (inputs.rejection && !config.Settings().temporary_unit_exposure) {
    switch (*inputs.rejection) {
    case ExposureTransitionError::kNotAuto:
      rejection = 1U;
      break;
    case ExposureTransitionError::kUnsupportedSeed:
      rejection = 2U;
      break;
    case ExposureTransitionError::kSharedConsumer:
      rejection = 3U;
      break;
    default:
      CHECK_F(false, "Unexpected captured exposure rejection");
    }
  }
  const auto constants = AutoExposureAverageConstants {
    .histogram_buffer_index = metering ? state.histogram_uav_index.get()
                                       : kInvalidShaderVisibleIndex.get(),
    .exposure_buffer_index = state.uav_index.get(),
    .min_log_luminance = config.Exposure().authored.min_log_luminance,
    .log_luminance_range = config.Exposure().authored.log_luminance_range,
    .low_percentile
    = std::clamp(config.Exposure().authored.low_percentile, 0.0F, 1.0F),
    .high_percentile
    = std::clamp(config.Exposure().authored.high_percentile, 0.0F, 1.0F),
    .min_ev = config.Exposure().authored.min_ev,
    .log2_transition_distance
    = log_rate(config.Exposure().authored.transition_distance),
    .log2_speed_up = log_rate(config.Exposure().authored.speed_up),
    .log2_speed_down = log_rate(config.Exposure().authored.speed_down),
    .log2_delta_time = log_rate(ctx.delta_time),
    .targets_srv = targets_srv.get(),
    .settings_revision = { static_cast<std::uint32_t>(config.Revision()),
      static_cast<std::uint32_t>(config.Revision() >> 32U) },
    .frame_sequence = { static_cast<std::uint32_t>(ctx.frame_sequence.get()),
      static_cast<std::uint32_t>(ctx.frame_sequence.get() >> 32U) },
    .previous_state_srv = previous_srv.get(),
    .fixed_scale
    = config.Settings().temporary_unit_exposure ? 1.0F : resolved.fixed_scale,
    .exposure_mode
    = !config.Settings().temporary_unit_exposure && resolved.authored.enabled
      ? static_cast<std::uint32_t>(resolved.authored.mode)
      : 3U,
    .control_flags = (seed.has_value() ? 0U : 1U) | (bootstrap ? 2U : 0U)
      | (rejection << 2U) | (source_loss ? 64U : 0U)
      | (inputs.frame_exposure
            && inputs.frame_exposure->current_state.get() == &state
          ? 128U
          : 0U),
    .requested_generation = { static_cast<std::uint32_t>(generation),
      static_cast<std::uint32_t>(generation >> 32U) },
    .transition_policy
    = inputs.transition && !config.Settings().temporary_unit_exposure
      ? static_cast<std::uint32_t>(inputs.transition->policy) + 1U
      : 0U,
    .seed_log_gain = seed.value_or(0.0F),
    .status_uav = state.status_uav_index.get(),
    .borrowed_state_srv = borrowed_srv.get(),
    .view_lifetime
    = { static_cast<std::uint32_t>(inputs.lifetime != 0U ? inputs.lifetime
            : inputs.transition ? inputs.transition->lifetime
                                : 0U),
      static_cast<std::uint32_t>(
        (inputs.lifetime != 0U  ? inputs.lifetime
            : inputs.transition ? inputs.transition->lifetime
                                : 0U)
        >> 32U) },
  };

  const auto slot
    = average_constants_publisher_->Publish(ctx.current_view.view_id,
      std::bit_cast<std::array<std::uint32_t, 28U>>(constants));
  CHECK_F(slot.IsValid(), "Exposure constants publication failed");

  recorder.SetComputeRoot32BitConstant(
    static_cast<std::uint32_t>(bindless_d3d12::RootParam::kRootConstants), 0U,
    0U);
  recorder.SetComputeRoot32BitConstant(
    static_cast<std::uint32_t>(bindless_d3d12::RootParam::kRootConstants),
    slot.get(), 1U);
}

auto ExposurePass::ReleaseExposureResources() -> void
{
  resolved_frames_.clear();
  exposure_states_.clear();
  prior_states_.clear();
  bootstrap_states_.clear();
  for (auto& states : frame_states_)
    states.clear();
  for (auto& frames : frame_bindings_)
    frames.clear();
  for (auto& frame : frame_pool_) {
    frame->current_state.reset();
    frame->selected_history.reset();
    frame->precision_history.reset();
    frame->qualified_candidate.reset();
  }
  if (auto gfx = renderer_.GetGraphics()) {
    auto& registry = gfx->GetResourceRegistry();
    for (auto& frame : frame_pool_) {
      gfx->ForgetKnownResourceState(*frame->buffer);
      if (registry.Contains(*frame->buffer))
        registry.UnRegisterResource(*frame->buffer);
      gfx->RegisterDeferredRelease(std::move(frame->buffer));
      gfx->ForgetKnownResourceState(*frame->suitability_buffer);
      if (registry.Contains(*frame->suitability_buffer))
        registry.UnRegisterResource(*frame->suitability_buffer);
      gfx->RegisterDeferredRelease(std::move(frame->suitability_buffer));
      gfx->ForgetKnownResourceState(*frame->conversion_buffer);
      if (registry.Contains(*frame->conversion_buffer))
        registry.UnRegisterResource(*frame->conversion_buffer);
      gfx->RegisterDeferredRelease(std::move(frame->conversion_buffer));
    }
    for (auto& state : state_pool_) {
      for (auto* resource :
        { &state->buffer, &state->histogram_buffer, &state->status_buffer }) {
        if (*resource) {
          gfx->ForgetKnownResourceState(**resource);
          if (registry.Contains(**resource))
            registry.UnRegisterResource(**resource);
          gfx->RegisterDeferredRelease(std::move(*resource));
        }
      }
    }
  }
  state_pool_.clear();
  frame_pool_.clear();
}

} // namespace oxygen::vortex::postprocess
