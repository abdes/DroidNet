//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <span>
#include <stdexcept>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/Bindless/Generated.RootSignature.D3D12.h>
#include <Oxygen/Core/Types/ShaderType.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/PipelineState.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/DescriptorVisibility.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Graphics/Common/Types/ResourceViewType.h>
#include <Oxygen/Vortex/PostProcess/Passes/ExposurePass.h>
#include <Oxygen/Vortex/RenderContext.h>
#include <Oxygen/Vortex/Renderer.h>

namespace oxygen::vortex::postprocess {

namespace {

  namespace bindless_d3d12 = oxygen::bindless::generated::d3d12;

  constexpr float kMinLogLuminanceRange = 1.0e-4F;
  constexpr float kMinTargetLuminance = 1.0e-6F;
  constexpr float kMinSpotMeterRadius = 0.01F;
  constexpr std::uint32_t kHistogramBinCount = 256U;
  constexpr std::uint32_t kHistogramDispatchGroupSize = 16U;
  constexpr std::uint32_t kPassConstantsStride = 256U;
  constexpr std::size_t kPassConstantsSlots = 8U;
  constexpr std::uint32_t kExposureStateElementCount = 4U;
  constexpr std::uint32_t kExposureStateBufferSizeBytes
    = kExposureStateElementCount * sizeof(float);
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
    std::uint32_t _pad0;
    std::uint32_t _pad1;
  };

  static_assert(sizeof(AutoExposureHistogramConstants) == 48U);

  struct alignas(packing::kShaderDataFieldAlignment)
    AutoExposureAverageConstants {
    std::uint32_t histogram_buffer_index;
    std::uint32_t exposure_buffer_index;
    float min_log_luminance;
    float log_luminance_range;
    float low_percentile;
    float high_percentile;
    float min_ev;
    float max_ev;
    float adaptation_speed_up;
    float adaptation_speed_down;
    float delta_time;
    float target_luminance;
  };

  static_assert(sizeof(AutoExposureAverageConstants) == 48U);

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
  pass_constants_indices_.fill(kInvalidShaderVisibleIndex);
}

ExposurePass::~ExposurePass()
{
  ReleasePassConstantsBuffer();

  if (init_upload_buffer_ && init_upload_buffer_->IsMapped()) {
    init_upload_buffer_->UnMap();
  }
}

auto ExposurePass::Execute(RenderContext& ctx, const PostProcessConfig& config,
  const Inputs& inputs) -> Result
{
  auto result = Result {
    .requested = config.enable_auto_exposure && inputs.scene_signal != nullptr
      && inputs.scene_signal_srv != kInvalidShaderVisibleIndex,
    .used_fixed_exposure = true,
    .exposure_value = std::max(config.fixed_exposure, 1.0e-4F),
  };
  if (!result.requested) {
    return result;
  }

  auto gfx = renderer_.GetGraphics();
  if (gfx == nullptr || inputs.scene_signal == nullptr) {
    return result;
  }

  EnsurePipelines();
  EnsureHistogramBuffer();
  EnsurePassConstantsBuffer();

  const auto queue_key = gfx->QueueKeyFor(graphics::QueueRole::kGraphics);
  auto recorder
    = gfx->AcquireCommandRecorder(queue_key, "Vortex Auto Exposure");
  if (!recorder) {
    return result;
  }
  TrackTextureFromKnownOrInitial(*recorder, *inputs.scene_signal);
  recorder->RequireResourceState(
    *inputs.scene_signal, graphics::ResourceStates::kShaderResource);

  auto& state = EnsureExposureStateForView(
    ctx, *recorder, ctx.current_view.view_id, config);
  DCHECK_NOTNULL_F(state.buffer.get());
  if (!recorder->IsResourceTracked(*histogram_buffer_)) {
    recorder->BeginTrackingResourceState(
      *histogram_buffer_, graphics::ResourceStates::kCommon, false);
  }

  recorder->RequireResourceState(
    *histogram_buffer_, graphics::ResourceStates::kUnorderedAccess);
  recorder->RequireResourceState(
    *state.buffer, graphics::ResourceStates::kUnorderedAccess);
  recorder->FlushBarriers();

  recorder->SetPipelineState(*clear_pipeline_);
  UpdateHistogramConstants(*recorder, inputs, config);
  recorder->Dispatch(1U, 1U, 1U);

  recorder->RequireResourceState(
    *histogram_buffer_, graphics::ResourceStates::kUnorderedAccess);
  recorder->FlushBarriers();

  recorder->SetPipelineState(*histogram_pipeline_);
  UpdateHistogramConstants(*recorder, inputs, config);
  const auto& tex_desc = inputs.scene_signal->GetDescriptor();
  recorder->Dispatch((tex_desc.width + (kHistogramDispatchGroupSize - 1U))
      / kHistogramDispatchGroupSize,
    (tex_desc.height + (kHistogramDispatchGroupSize - 1U))
      / kHistogramDispatchGroupSize,
    1U);

  recorder->RequireResourceState(
    *histogram_buffer_, graphics::ResourceStates::kUnorderedAccess);
  recorder->RequireResourceState(
    *state.buffer, graphics::ResourceStates::kUnorderedAccess);
  recorder->FlushBarriers();

  recorder->SetPipelineState(*average_pipeline_);
  UpdateAverageConstants(ctx, *recorder, config, state);
  recorder->Dispatch(1U, 1U, 1U);

  recorder->RequireResourceStateFinal(
    *histogram_buffer_, graphics::ResourceStates::kCommon);
  recorder->RequireResourceStateFinal(
    *state.buffer, graphics::ResourceStates::kShaderResource);

  result.executed = true;
  result.used_fixed_exposure = false;
  result.exposure_buffer = state.buffer.get();
  result.exposure_buffer_srv = state.srv_index;
  result.exposure_buffer_uav = state.uav_index;
  return result;
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

auto ExposurePass::EnsureHistogramBuffer() -> void
{
  if (histogram_buffer_ != nullptr) {
    return;
  }

  auto gfx = renderer_.GetGraphics();
  CHECK_NOTNULL_F(gfx.get());
  histogram_buffer_ = gfx->CreateBuffer({
    .size_bytes = kHistogramBinCount * sizeof(std::uint32_t),
    .usage = graphics::BufferUsage::kStorage,
    .memory = graphics::BufferMemory::kDeviceLocal,
    .debug_name = "Vortex.PostProcess.Exposure.Histogram",
  });
  CHECK_NOTNULL_F(histogram_buffer_.get());
  histogram_buffer_->SetName("Vortex.PostProcess.Exposure.Histogram");

  RegisterResourceIfNeeded(*gfx, histogram_buffer_);
  auto& allocator = gfx->GetDescriptorAllocator();
  auto& registry = gfx->GetResourceRegistry();
  auto handle
    = allocator.AllocateRaw(graphics::ResourceViewType::kRawBuffer_UAV,
      graphics::DescriptorVisibility::kShaderVisible);
  CHECK_F(handle.IsValid(),
    "ExposurePass: failed to allocate histogram UAV descriptor");
  histogram_uav_index_ = allocator.GetShaderVisibleIndex(handle);
  const auto view_desc = graphics::BufferViewDescription {
    .view_type = graphics::ResourceViewType::kRawBuffer_UAV,
    .visibility = graphics::DescriptorVisibility::kShaderVisible,
    .range = { 0U, kHistogramBinCount * sizeof(std::uint32_t) },
    .stride = 0U,
  };
  const auto view
    = registry.RegisterView(*histogram_buffer_, std::move(handle), view_desc);
  CHECK_F(
    view->IsValid(), "ExposurePass: failed to register histogram UAV view");
}

auto ExposurePass::EnsurePassConstantsBuffer() -> void
{
  if (pass_constants_buffer_ != nullptr
    && pass_constants_indices_[0].IsValid()) {
    return;
  }

  auto gfx = renderer_.GetGraphics();
  CHECK_NOTNULL_F(gfx.get());
  pass_constants_buffer_ = gfx->CreateBuffer({
    .size_bytes = kPassConstantsStride * kPassConstantsSlots,
    .usage = graphics::BufferUsage::kConstant,
    .memory = graphics::BufferMemory::kUpload,
    .debug_name = "Vortex.PostProcess.Exposure.PassConstants",
  });
  CHECK_NOTNULL_F(pass_constants_buffer_.get());
  pass_constants_buffer_->SetName("Vortex.PostProcess.Exposure.PassConstants");
  pass_constants_mapped_ptr_ = pass_constants_buffer_->Map(
    0, kPassConstantsStride * kPassConstantsSlots);
  CHECK_NOTNULL_F(pass_constants_mapped_ptr_,
    "ExposurePass: failed to map pass constants buffer");

  auto& registry = gfx->GetResourceRegistry();
  auto& allocator = gfx->GetDescriptorAllocator();
  RegisterResourceIfNeeded(*gfx, pass_constants_buffer_);
  pass_constants_indices_.fill(kInvalidShaderVisibleIndex);
  for (std::size_t slot = 0; slot < kPassConstantsSlots; ++slot) {
    auto handle
      = allocator.AllocateRaw(graphics::ResourceViewType::kConstantBuffer,
        graphics::DescriptorVisibility::kShaderVisible);
    CHECK_F(handle.IsValid(),
      "ExposurePass: failed to allocate pass constants descriptor");
    pass_constants_indices_[slot] = allocator.GetShaderVisibleIndex(handle);

    const auto offset = static_cast<std::uint32_t>(slot * kPassConstantsStride);
    const auto view_desc = graphics::BufferViewDescription {
      .view_type = graphics::ResourceViewType::kConstantBuffer,
      .visibility = graphics::DescriptorVisibility::kShaderVisible,
      .range = { offset, kPassConstantsStride },
    };
    const auto view = registry.RegisterView(
      *pass_constants_buffer_, std::move(handle), view_desc);
    CHECK_F(
      view->IsValid(), "ExposurePass: failed to register pass constants view");
  }
}

auto ExposurePass::EnsureExposureInitUploadBuffer(
  graphics::CommandRecorder& recorder, const PostProcessConfig& config) -> void
{
  if (init_upload_buffer_ != nullptr) {
    return;
  }

  auto gfx = renderer_.GetGraphics();
  CHECK_NOTNULL_F(gfx.get());
  init_upload_buffer_ = gfx->CreateBuffer({
    .size_bytes = kExposureStateBufferSizeBytes,
    .usage = graphics::BufferUsage::kNone,
    .memory = graphics::BufferMemory::kUpload,
    .debug_name = "Vortex.PostProcess.Exposure.InitUpload",
  });
  CHECK_NOTNULL_F(init_upload_buffer_.get());
  exposure_init_upload_mapped_ptr_
    = init_upload_buffer_->Map(0, kExposureStateBufferSizeBytes);
  CHECK_NOTNULL_F(exposure_init_upload_mapped_ptr_,
    "ExposurePass: failed to map init upload buffer");

  const auto base_luminance
    = std::max(config.auto_exposure_target_luminance, 0.0001F);
  const auto ev100
    = engine::AverageLuminanceToEv100(std::max(base_luminance, 1.0e-4F));
  const std::array<float, kExposureStateElementCount> init_values {
    base_luminance,
    1.0F,
    ev100,
    0.0F,
  };
  std::memcpy(
    exposure_init_upload_mapped_ptr_, init_values.data(), sizeof(init_values));

  if (!recorder.IsResourceTracked(*init_upload_buffer_)) {
    recorder.BeginTrackingResourceState(
      *init_upload_buffer_, graphics::ResourceStates::kCopySource, false);
  }
}

auto ExposurePass::EnsureExposureStateForView(RenderContext& ctx,
  graphics::CommandRecorder& recorder, ViewId view_id,
  const PostProcessConfig& config) -> PerViewExposureState&
{
  auto gfx = renderer_.GetGraphics();
  CHECK_NOTNULL_F(gfx.get());
  auto& allocator = gfx->GetDescriptorAllocator();
  auto& registry = gfx->GetResourceRegistry();
  auto& state = exposure_states_[view_id];
  state.last_seen_sequence = ctx.frame_sequence;

  if (state.buffer == nullptr) {
    state.buffer = gfx->CreateBuffer({
      .size_bytes = kExposureStateBufferSizeBytes,
      .usage = graphics::BufferUsage::kStorage,
      .memory = graphics::BufferMemory::kDeviceLocal,
      .debug_name = "Vortex.PostProcess.Exposure.State",
    });
    CHECK_NOTNULL_F(state.buffer.get());
    state.buffer->SetName("Vortex.PostProcess.Exposure.State");

    if (!recorder.IsResourceTracked(*state.buffer)) {
      recorder.BeginTrackingResourceState(
        *state.buffer, graphics::ResourceStates::kCommon, false);
    }

    EnsureExposureInitUploadBuffer(recorder, config);
    recorder.RequireResourceState(
      *init_upload_buffer_, graphics::ResourceStates::kCopySource);
    recorder.RequireResourceState(
      *state.buffer, graphics::ResourceStates::kCopyDest);
    recorder.FlushBarriers();
    recorder.CopyBuffer(*state.buffer, 0U, *init_upload_buffer_, 0U,
      kExposureStateBufferSizeBytes);
    recorder.RequireResourceState(
      *state.buffer, graphics::ResourceStates::kUnorderedAccess);
    recorder.FlushBarriers();
  } else if (!recorder.IsResourceTracked(*state.buffer)) {
    recorder.BeginTrackingResourceState(
      *state.buffer, graphics::ResourceStates::kShaderResource, false);
  }

  RegisterResourceIfNeeded(*gfx, state.buffer);

  if (!state.uav_index.IsValid()) {
    auto handle
      = allocator.AllocateRaw(graphics::ResourceViewType::kRawBuffer_UAV,
        graphics::DescriptorVisibility::kShaderVisible);
    CHECK_F(handle.IsValid(),
      "ExposurePass: failed to allocate exposure UAV descriptor");
    state.uav_index = allocator.GetShaderVisibleIndex(handle);
    const auto view_desc = graphics::BufferViewDescription {
      .view_type = graphics::ResourceViewType::kRawBuffer_UAV,
      .visibility = graphics::DescriptorVisibility::kShaderVisible,
      .range = { 0U, kExposureStateBufferSizeBytes },
      .stride = 0U,
    };
    const auto view
      = registry.RegisterView(*state.buffer, std::move(handle), view_desc);
    CHECK_F(
      view->IsValid(), "ExposurePass: failed to register exposure UAV view");
  }

  if (!state.srv_index.IsValid()) {
    auto handle
      = allocator.AllocateRaw(graphics::ResourceViewType::kRawBuffer_SRV,
        graphics::DescriptorVisibility::kShaderVisible);
    CHECK_F(handle.IsValid(),
      "ExposurePass: failed to allocate exposure SRV descriptor");
    state.srv_index = allocator.GetShaderVisibleIndex(handle);
    const auto view_desc = graphics::BufferViewDescription {
      .view_type = graphics::ResourceViewType::kRawBuffer_SRV,
      .visibility = graphics::DescriptorVisibility::kShaderVisible,
      .range = { 0U, kExposureStateBufferSizeBytes },
      .stride = 0U,
    };
    const auto view
      = registry.RegisterView(*state.buffer, std::move(handle), view_desc);
    CHECK_F(
      view->IsValid(), "ExposurePass: failed to register exposure SRV view");
  }

  return state;
}

auto ExposurePass::UpdateHistogramConstants(graphics::CommandRecorder& recorder,
  const Inputs& inputs, const PostProcessConfig& config) -> void
{
  DCHECK_NOTNULL_F(pass_constants_mapped_ptr_);
  DCHECK_F(inputs.scene_signal != nullptr);
  const auto& desc = inputs.scene_signal->GetDescriptor();
  const auto constants = AutoExposureHistogramConstants {
    .source_texture_index = inputs.scene_signal_srv.get(),
    .histogram_buffer_index = histogram_uav_index_.get(),
    .min_log_luminance = config.auto_exposure_min_log_luminance,
    .inv_log_luminance_range = 1.0F
      / std::max(
        config.auto_exposure_log_luminance_range, kMinLogLuminanceRange),
    .metering_left = 0U,
    .metering_top = 0U,
    .metering_width = desc.width,
    .metering_height = desc.height,
    .metering_mode = static_cast<std::uint32_t>(config.metering_mode),
    .spot_meter_radius = std::clamp(
      config.auto_exposure_spot_meter_radius, kMinSpotMeterRadius, 1.0F),
    ._pad0 = 0U,
    ._pad1 = 0U,
  };

  const auto slot = pass_constants_slot_ % kPassConstantsSlots;
  pass_constants_slot_++;
  std::memcpy(static_cast<std::byte*>(pass_constants_mapped_ptr_)
      + (slot * kPassConstantsStride),
    &constants, sizeof(constants));

  recorder.SetComputeRoot32BitConstant(
    static_cast<std::uint32_t>(bindless_d3d12::RootParam::kRootConstants), 0U,
    0U);
  recorder.SetComputeRoot32BitConstant(
    static_cast<std::uint32_t>(bindless_d3d12::RootParam::kRootConstants),
    pass_constants_indices_[slot].get(), 1U);
}

auto ExposurePass::UpdateAverageConstants(RenderContext& ctx,
  graphics::CommandRecorder& recorder, const PostProcessConfig& config,
  const PerViewExposureState& state) -> void
{
  DCHECK_NOTNULL_F(pass_constants_mapped_ptr_);
  const auto constants = AutoExposureAverageConstants {
    .histogram_buffer_index = histogram_uav_index_.get(),
    .exposure_buffer_index = state.uav_index.get(),
    .min_log_luminance = config.auto_exposure_min_log_luminance,
    .log_luminance_range
    = std::max(config.auto_exposure_log_luminance_range, kMinLogLuminanceRange),
    .low_percentile
    = std::clamp(config.auto_exposure_low_percentile, 0.0F, 1.0F),
    .high_percentile
    = std::clamp(config.auto_exposure_high_percentile, 0.0F, 1.0F),
    .min_ev = config.auto_exposure_min_ev,
    .max_ev = config.auto_exposure_max_ev,
    .adaptation_speed_up = std::max(config.auto_exposure_speed_up, 0.0F),
    .adaptation_speed_down = std::max(config.auto_exposure_speed_down, 0.0F),
    .delta_time = std::max(ctx.delta_time, 0.0F),
    .target_luminance
    = std::max(config.auto_exposure_target_luminance, kMinTargetLuminance),
  };

  const auto slot = pass_constants_slot_ % kPassConstantsSlots;
  pass_constants_slot_++;
  std::memcpy(static_cast<std::byte*>(pass_constants_mapped_ptr_)
      + (slot * kPassConstantsStride),
    &constants, sizeof(constants));

  recorder.SetComputeRoot32BitConstant(
    static_cast<std::uint32_t>(bindless_d3d12::RootParam::kRootConstants), 0U,
    0U);
  recorder.SetComputeRoot32BitConstant(
    static_cast<std::uint32_t>(bindless_d3d12::RootParam::kRootConstants),
    pass_constants_indices_[slot].get(), 1U);
}

auto ExposurePass::ReleasePassConstantsBuffer() -> void
{
  if (pass_constants_buffer_ == nullptr) {
    pass_constants_mapped_ptr_ = nullptr;
    pass_constants_indices_.fill(kInvalidShaderVisibleIndex);
    pass_constants_slot_ = 0U;
    return;
  }

  if (pass_constants_buffer_->IsMapped()) {
    pass_constants_buffer_->UnMap();
  }

  if (auto gfx = renderer_.GetGraphics(); gfx != nullptr) {
    auto& registry = gfx->GetResourceRegistry();
    if (registry.Contains(*pass_constants_buffer_)) {
      registry.UnRegisterResource(*pass_constants_buffer_);
    }
  }

  pass_constants_buffer_.reset();
  pass_constants_mapped_ptr_ = nullptr;
  pass_constants_indices_.fill(kInvalidShaderVisibleIndex);
  pass_constants_slot_ = 0U;
}

} // namespace oxygen::vortex::postprocess
