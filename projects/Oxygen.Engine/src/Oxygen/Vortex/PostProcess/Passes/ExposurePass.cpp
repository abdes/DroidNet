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
    std::uint32_t _pad0;
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
    std::uint32_t reserved;
    std::array<std::uint32_t, 2> view_lifetime;
  };

  static_assert(sizeof(AutoExposureAverageConstants) == 112U);
  static_assert(
    offsetof(AutoExposureAverageConstants, previous_state_srv) == 64U);
  static_assert(
    offsetof(AutoExposureAverageConstants, requested_generation) == 80U);
  static_assert(offsetof(AutoExposureAverageConstants, seed_log_gain) == 92U);
  static_assert(offsetof(AutoExposureAverageConstants, status_uav) == 96U);
  static_assert(offsetof(AutoExposureAverageConstants, view_lifetime) == 104U);

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
  prior_states_.clear();
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
}

auto ExposurePass::Execute(RenderContext& ctx, const PostProcessConfig& config,
  const Inputs& inputs) -> Result
{
  CHECK_F(config.resolved_exposure.has_value(),
    "Exposure requires resolved settings");
  const auto& resolved = *config.resolved_exposure;
  const bool automatic = !config.temporary_unit_exposure
    && resolved.authored.enabled
    && resolved.authored.mode == engine::ExposureMode::kAuto;
  auto result = Result { .requested = true,
    .used_fixed_exposure = !automatic,
    .exposure_value = config.temporary_unit_exposure ? 1.0F
      : automatic ? (resolved.authored.target_luminance == 0.0F
                        ? 0.0F
                        : std::exp2(resolved.initial_log_gain))
                  : resolved.fixed_scale };
  auto gfx = renderer_.GetGraphics();
  if (!gfx)
    return result;
  EnsurePipelines();
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
    target_frame_.reset();
  }
  OnFrameStart(ctx.frame_sequence, ctx.frame_slot);
  const auto handle = ctx.current_view.view_state_handle;
  CHECK_F(!inputs.transition || inputs.transition->target == handle,
    "Exposure transition targets a different view state");
  auto* view = config.temporary_unit_exposure
      || handle == CompositionView::kInvalidViewStateHandle
    ? nullptr
    : &exposure_states_[handle];
  const auto publish_result = [&](StateLease state, bool executed) {
    result.state = std::move(state);
    result.executed = executed;
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
  auto state = AcquireState();
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
    publish_result(previous, false);
    return result;
  }
  auto recorder = gfx->AcquireCommandRecorder(
    gfx->QueueKeyFor(graphics::QueueRole::kGraphics), "Vortex Exposure");
  if (!recorder) {
    publish_result(previous, false);
    return result;
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
  recorder->RequireResourceState(
    *state->status_buffer, graphics::ResourceStates::kUnorderedAccess);
  recorder->RequireResourceState(
    *state->buffer, graphics::ResourceStates::kUnorderedAccess);
  if (previous) {
    track_buffer(*previous->buffer);
    recorder->RequireResourceState(
      *previous->buffer, graphics::ResourceStates::kShaderResource);
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
  recorder->SetPipelineState(*average_pipeline_);
  UpdateAverageConstants(ctx, *recorder, config, *state, targets_srv,
    previous ? previous->srv_index : kInvalidShaderVisibleIndex, inputs);
  recorder->Dispatch(1U, 1U, 1U);
  recorder->RequireResourceStateFinal(
    *state->buffer, graphics::ResourceStates::kShaderResource);
  recorder->RequireResourceStateFinal(
    *state->status_buffer, graphics::ResourceStates::kCopySource);
  if (automatic)
    recorder->RequireResourceStateFinal(
      *state->histogram_buffer, graphics::ResourceStates::kCommon);
  recorder.reset();
  if (!recording || !recording->IsSubmitted()) {
    publish_result(previous, false);
    return result;
  }
  if (view) {
    view->latest = state;
    view->submitted_frame = ctx.frame_sequence;
  }
  publish_result(state, true);
  return result;
}

auto ExposurePass::RemoveViewState(CompositionView::ViewStateHandle handle)
  -> void
{
  exposure_states_.erase(handle);
  // Prior-frame readers may still need a removed owner's pinned state.
}

auto ExposurePass::EnsurePipelines() -> void
{
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
    = gfx->CreateBuffer({ .size_bytes = sizeof(ExposureCompletedStatus),
      .usage = graphics::BufferUsage::kStorage,
      .memory = graphics::BufferMemory::kDeviceLocal,
      .debug_name = "Vortex.PostProcess.Exposure.Status" });
  CHECK_NOTNULL_F(state->status_buffer.get());
  RegisterResourceIfNeeded(*gfx, state->status_buffer);
  auto status_handle
    = allocator.AllocateRaw(graphics::ResourceViewType::kRawBuffer_UAV,
      graphics::DescriptorVisibility::kShaderVisible);
  CHECK_F(status_handle.IsValid());
  state->status_uav_index = allocator.GetShaderVisibleIndex(status_handle);
  const auto status_view
    = registry.RegisterView(*state->status_buffer, std::move(status_handle),
      graphics::BufferViewDescription {
        .view_type = graphics::ResourceViewType::kRawBuffer_UAV,
        .visibility = graphics::DescriptorVisibility::kShaderVisible,
        .range = { 0U, sizeof(ExposureCompletedStatus) },
        .stride = 0U });
  CHECK_F(status_view->IsValid());
  state_pool_.push_back(state);
  return state;
}

auto ExposurePass::UpdateHistogramConstants(RenderContext& ctx,
  graphics::CommandRecorder& recorder, const Inputs& inputs,
  const PostProcessConfig& config, const StateResources& state) -> void
{
  DCHECK_NOTNULL_F(constants_publisher_.get());
  const auto desc = inputs.scene_signal ? inputs.scene_signal->GetDescriptor()
                                        : graphics::TextureDesc {};
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
  const auto constants = AutoExposureHistogramConstants {
    .source_texture_index = inputs.scene_signal_srv.get(),
    .histogram_buffer_index = state.histogram_uav_index.get(),
    .min_log_luminance = config.auto_exposure_min_log_luminance,
    .inv_log_luminance_range = 1.0F / config.auto_exposure_log_luminance_range,
    .metering_left = static_cast<std::uint32_t>(rectangle.left),
    .metering_top = static_cast<std::uint32_t>(rectangle.top),
    .metering_width
    = static_cast<std::uint32_t>(std::max(0, rectangle.right - rectangle.left)),
    .metering_height
    = static_cast<std::uint32_t>(std::max(0, rectangle.bottom - rectangle.top)),
    .metering_mode = static_cast<std::uint32_t>(config.metering_mode),
    .spot_meter_radius = config.auto_exposure_spot_meter_radius,
    .mask_texture_index = inputs.metering_mask_srv.get(),
    .background_enabled
    = environment::ResolveSceneBackground(ctx).has_value() ? 1U : 0U,
    .one_over_pre_exposure = inputs.one_over_pre_exposure,
    .black_influence = config.resolved_exposure->authored.black_influence,
    ._pad0 = 0U,
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
  graphics::CommandRecorder& recorder, const PostProcessConfig& config,
  const StateResources& state, const ShaderVisibleIndex targets_srv,
  const ShaderVisibleIndex previous_srv, const Inputs& inputs) -> void
{
  DCHECK_NOTNULL_F(average_constants_publisher_.get());
  const auto log_rate = [](const float value) {
    // -256 is outside log2 of every positive finite binary32 input.
    return std::isfinite(value) && value > 0.0F
      ? static_cast<float>(std::log2(static_cast<double>(value)))
      : -256.0F;
  };
  const auto& resolved = *config.resolved_exposure;
  const auto seed = inputs.transition
      && inputs.transition->policy == ExposureTransitionPolicy::kSeedFromEv100
      && inputs.transition->seed_ev
    ? scene::ResolveExposureSeedLogGain(resolved, *inputs.transition->seed_ev)
    : std::expected<float, scene::ExposureSettingsError> { 0.0F };
  const auto generation = inputs.transition && !config.temporary_unit_exposure
    ? inputs.transition->generation
    : 0U;
  const auto constants = AutoExposureAverageConstants {
    .histogram_buffer_index = state.histogram_uav_index.get(),
    .exposure_buffer_index = state.uav_index.get(),
    .min_log_luminance = config.auto_exposure_min_log_luminance,
    .log_luminance_range = config.auto_exposure_log_luminance_range,
    .low_percentile
    = std::clamp(config.auto_exposure_low_percentile, 0.0F, 1.0F),
    .high_percentile
    = std::clamp(config.auto_exposure_high_percentile, 0.0F, 1.0F),
    .min_ev = config.auto_exposure_min_ev,
    .log2_transition_distance
    = log_rate(config.resolved_exposure->authored.transition_distance),
    .log2_speed_up = log_rate(config.auto_exposure_speed_up),
    .log2_speed_down = log_rate(config.auto_exposure_speed_down),
    .log2_delta_time = log_rate(ctx.delta_time),
    .targets_srv = targets_srv.get(),
    .settings_revision
    = { static_cast<std::uint32_t>(config.exposure_settings_revision),
      static_cast<std::uint32_t>(config.exposure_settings_revision >> 32U) },
    .frame_sequence = { static_cast<std::uint32_t>(ctx.frame_sequence.get()),
      static_cast<std::uint32_t>(ctx.frame_sequence.get() >> 32U) },
    .previous_state_srv = previous_srv.get(),
    .fixed_scale = config.temporary_unit_exposure ? 1.0F : resolved.fixed_scale,
    .exposure_mode
    = !config.temporary_unit_exposure && resolved.authored.enabled
      ? static_cast<std::uint32_t>(resolved.authored.mode)
      : 3U,
    .control_flags = seed.has_value() ? 0U : 1U,
    .requested_generation = { static_cast<std::uint32_t>(generation),
      static_cast<std::uint32_t>(generation >> 32U) },
    .transition_policy = inputs.transition && !config.temporary_unit_exposure
      ? static_cast<std::uint32_t>(inputs.transition->policy) + 1U
      : 0U,
    .seed_log_gain = seed.value_or(0.0F),
    .status_uav = state.status_uav_index.get(),
    .reserved = 0U,
    .view_lifetime = { inputs.transition
        ? static_cast<std::uint32_t>(inputs.transition->lifetime)
        : 0U,
      inputs.transition
        ? static_cast<std::uint32_t>(inputs.transition->lifetime >> 32U)
        : 0U },
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
  exposure_states_.clear();
  prior_states_.clear();
  for (auto& states : frame_states_)
    states.clear();
  if (auto gfx = renderer_.GetGraphics()) {
    auto& registry = gfx->GetResourceRegistry();
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
}

} // namespace oxygen::vortex::postprocess
