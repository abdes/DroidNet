//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Renderer/Passes/AutoExposurePass.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <span>
#include <stdexcept>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/Bindless/Generated.RootSignature.h>
#include <Oxygen/Core/Constants.h>
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
#include <Oxygen/Renderer/RenderContext.h>

namespace oxygen::engine {

namespace {
  constexpr float kMinLogLuminanceRange = 1.0e-4F;
  constexpr float kMinTargetLuminance = 1.0e-6F;
  constexpr float kMinSpotMeterRadius = 0.01F;
  constexpr uint32_t kHistogramBinCount = 256U;
  constexpr uint32_t kHistogramDispatchGroupSize = 16U;
  constexpr uint32_t kExposureStateElementCount = 4U;
  constexpr uint32_t kExposureStateBufferSizeBytes
    = kExposureStateElementCount * sizeof(float);
  constexpr float kDefaultExposureLuminance = 0.18F;
  constexpr float kEv100Scale = 100.0F;
  constexpr float kEv100CalibrationConstant = 12.5F;

  struct alignas(packing::kShaderDataFieldAlignment)
    AutoExposureHistogramConstants {
    uint32_t source_texture_index;
    uint32_t histogram_buffer_index;
    float min_log_luminance;
    float inv_log_luminance_range;
    uint32_t screen_width;
    uint32_t screen_height;
    uint32_t metering_mode;
    uint32_t _pad;
    float spot_meter_radius;
    float _pad1;
    float _pad2;
    float _pad3;
  };

  static_assert(sizeof(AutoExposureHistogramConstants) == 48); // NOLINT

  // Must match HLSL `AutoExposureAverageConstants` in
  // Shaders/Compositing/AutoExposure_Average_CS.hlsl.
  struct alignas(packing::kShaderDataFieldAlignment)
    AutoExposureAverageConstants {
    uint32_t histogram_buffer_index;
    uint32_t exposure_buffer_index;
    float min_log_luminance;
    float log_luminance_range;
    float low_percentile;
    float high_percentile;
    float adaptation_speed_up;
    float adaptation_speed_down;
    float delta_time;
    float target_luminance;
  };

  static_assert(sizeof(AutoExposureAverageConstants) == 48); // NOLINT
} // namespace

AutoExposurePass::AutoExposurePass(
  observer_ptr<Graphics> gfx, std::shared_ptr<Config> config)
  : ComputeRenderPass(config ? config->debug_name : "AutoExposurePass")
  , config_(std::move(config))
  , graphics_(gfx)
  , pass_constants_indices_ { kInvalidShaderVisibleIndex,
    kInvalidShaderVisibleIndex, kInvalidShaderVisibleIndex,
    kInvalidShaderVisibleIndex }
{
  DCHECK_NOTNULL_F(graphics_);
  DCHECK_NOTNULL_F(config_);
}

AutoExposurePass::~AutoExposurePass()
{
  ReleasePassConstantsBuffer();

  if (graphics_ != nullptr) {
    auto& registry = graphics_->GetResourceRegistry();
    for (auto& [view_id, state] : exposure_states_) {
      (void)view_id;
      if (state.buffer && registry.Contains(*state.buffer)) {
        registry.UnRegisterResource(*state.buffer);
      }
      state.buffer.reset();
      state.uav_index = kInvalidShaderVisibleIndex;
      state.srv_index = kInvalidShaderVisibleIndex;
    }
  }
  exposure_states_.clear();
  active_exposure_state_ = nullptr;

  if (init_upload_buffer_ && init_upload_buffer_->IsMapped()) {
    init_upload_buffer_->UnMap();
  }
  exposure_init_upload_mapped_ptr_ = nullptr;
  init_upload_buffer_.reset();
}

auto AutoExposurePass::GetExposureOutput(oxygen::ViewId view_id) const
  -> ExposureOutput
{
  if (auto it = exposure_states_.find(view_id); it != exposure_states_.end()) {
    return ExposureOutput { .exposure_state_srv_index = it->second.srv_index };
  }
  return ExposureOutput {};
}

auto AutoExposurePass::ValidateConfig() -> void
{
  if (!config_) {
    throw std::runtime_error("AutoExposurePass requires configuration");
  }
  if (!config_->source_texture) {
    throw std::runtime_error("AutoExposurePass requires source_texture");
  }

  if (!std::isfinite(config_->min_log_luminance)) {
    LOG_F(WARNING,
      "AutoExposurePass: invalid min_log_luminance={}, resetting to default {}",
      config_->min_log_luminance, Config::kDefaultMinLogLuminance);
    config_->min_log_luminance = Config::kDefaultMinLogLuminance;
  }

  if (!std::isfinite(config_->log_luminance_range)
    || config_->log_luminance_range <= kMinLogLuminanceRange) {
    LOG_F(WARNING,
      "AutoExposurePass: invalid log_luminance_range={}, clamping to 0.0001",
      config_->log_luminance_range);
    config_->log_luminance_range = kMinLogLuminanceRange;
  }

  if (!std::isfinite(config_->low_percentile)) {
    LOG_F(WARNING,
      "AutoExposurePass: invalid low_percentile={}, resetting to default {}",
      config_->low_percentile, Config::kDefaultLowPercentile);
    config_->low_percentile = Config::kDefaultLowPercentile;
  }
  config_->low_percentile = std::clamp(config_->low_percentile, 0.0F, 1.0F);

  if (!std::isfinite(config_->high_percentile)) {
    LOG_F(WARNING,
      "AutoExposurePass: invalid high_percentile={}, resetting to default {}",
      config_->high_percentile, Config::kDefaultHighPercentile);
    config_->high_percentile = Config::kDefaultHighPercentile;
  }
  config_->high_percentile = std::clamp(config_->high_percentile, 0.0F, 1.0F);
  config_->high_percentile
    = (std::max)(config_->high_percentile, config_->low_percentile);

  if (!std::isfinite(config_->adaptation_speed_up)
    || config_->adaptation_speed_up < 0.0F) {
    LOG_F(WARNING,
      "AutoExposurePass: invalid adaptation_speed_up={}, clamping to 0",
      config_->adaptation_speed_up);
    config_->adaptation_speed_up = 0.0F;
  }

  if (!std::isfinite(config_->adaptation_speed_down)
    || config_->adaptation_speed_down < 0.0F) {
    LOG_F(WARNING,
      "AutoExposurePass: invalid adaptation_speed_down={}, clamping to 0",
      config_->adaptation_speed_down);
    config_->adaptation_speed_down = 0.0F;
  }

  if (!std::isfinite(config_->target_luminance)
    || config_->target_luminance <= kMinTargetLuminance) {
    LOG_F(WARNING,
      "AutoExposurePass: invalid target_luminance={}, clamping to 0.000001",
      config_->target_luminance);
    config_->target_luminance = kMinTargetLuminance;
  }

  if (!std::isfinite(config_->spot_meter_radius)
    || config_->spot_meter_radius <= 0.0F) {
    LOG_F(WARNING,
      "AutoExposurePass: invalid spot_meter_radius={}, resetting to default {}",
      config_->spot_meter_radius, Config::kDefaultSpotMeterRadius);
    config_->spot_meter_radius = Config::kDefaultSpotMeterRadius;
  }
  config_->spot_meter_radius
    = std::clamp(config_->spot_meter_radius, kMinSpotMeterRadius, 1.0F);
}

auto AutoExposurePass::NeedRebuildPipelineState() const -> bool
{
  if (!LastBuiltPsoDesc().has_value()) {
    return true;
  }
  return !pso_stages_.clear.has_value() || !pso_stages_.histogram.has_value()
    || !pso_stages_.average.has_value();
}

auto AutoExposurePass::CreatePipelineStateDesc()
  -> graphics::ComputePipelineDesc
{
  const std::string histogram_shader_path
    = "Compositing/AutoExposure_Histogram_CS.hlsl";
  const std::string average_shader_path
    = "Compositing/AutoExposure_Average_CS.hlsl";

  auto root_bindings = BuildRootBindings();
  const auto bindings = std::span<const graphics::RootBindingItem>(
    root_bindings.data(), root_bindings.size());

  pso_stages_.clear
    = graphics::ComputePipelineDesc::Builder()
        .SetComputeShader({ .stage = oxygen::ShaderType::kCompute,
          .source_path = histogram_shader_path,
          .entry_point = "ClearHistogram" })
        .SetRootBindings(bindings)
        .SetDebugName("AutoExposure_ClearHistogram")
        .Build();

  pso_stages_.average
    = graphics::ComputePipelineDesc::Builder()
        .SetComputeShader({ .stage = oxygen::ShaderType::kCompute,
          .source_path = average_shader_path,
          .entry_point = "CS" })
        .SetRootBindings(bindings)
        .SetDebugName("AutoExposure_Average")
        .Build();

  pso_stages_.histogram
    = graphics::ComputePipelineDesc::Builder()
        .SetComputeShader({ .stage = oxygen::ShaderType::kCompute,
          .source_path = histogram_shader_path,
          .entry_point = "CS" })
        .SetRootBindings(bindings)
        .SetDebugName("AutoExposure_Histogram")
        .Build();

  // ComputeRenderPass::OnExecute() sets the pipeline state before DoExecute.
  // Returning the clear stage keeps the first state change minimal.
  DCHECK_F(pso_stages_.clear.has_value());
  return *pso_stages_.clear;
}

auto AutoExposurePass::DoPrepareResources(graphics::CommandRecorder& recorder)
  -> co::Co<>
{
  auto& graphics = Context().GetGraphics();
  auto& allocator = graphics.GetDescriptorAllocator();
  auto& registry = graphics.GetResourceRegistry();

  EnsureHistogramBuffer();

  const auto view_id = Context().current_view.view_id;
  if (view_id == oxygen::kInvalidViewId) {
    LOG_F(WARNING,
      "AutoExposurePass: current_view.view_id is invalid; exposure output "
      "will be unavailable");
  }
  PruneStaleExposureStates(view_id);
  EnsureExposureStateForView(recorder, view_id);

  if (active_exposure_state_ == nullptr
    || !active_exposure_state_->srv_index.IsValid()
    || !active_exposure_state_->uav_index.IsValid()) {
    LOG_F(ERROR,
      "AutoExposurePass: exposure state missing/invalid after ensure "
      "(view_id={}, srv_valid={}, uav_valid={})",
      view_id.get(),
      active_exposure_state_ != nullptr
        && active_exposure_state_->srv_index.IsValid(),
      active_exposure_state_ != nullptr
        && active_exposure_state_->uav_index.IsValid());
  }

  DCHECK_NOTNULL_F(config_->source_texture);
  if (!config_->histogram_buffer) {
    throw std::runtime_error("AutoExposurePass: histogram buffer unavailable");
  }
  DCHECK_NOTNULL_F(active_exposure_state_);
  DCHECK_NOTNULL_F(active_exposure_state_->buffer);

  // Resource state tracking is per CommandRecorder. Track resources once for
  // this recorder lifetime; subsequent frames will skip via IsResourceTracked.
  if (!recorder.IsResourceTracked(*config_->source_texture)) {
    recorder.BeginTrackingResourceState(
      *config_->source_texture, graphics::ResourceStates::kCommon, true);
  }
  if (!recorder.IsResourceTracked(*config_->histogram_buffer)) {
    recorder.BeginTrackingResourceState(*config_->histogram_buffer,
      graphics::ResourceStates::kUnorderedAccess, false);
  }

  recorder.EnableAutoMemoryBarriers(*config_->histogram_buffer);
  recorder.EnableAutoMemoryBarriers(*active_exposure_state_->buffer);

  // 1. Histogram UAV
  if (!histogram_uav_index_.IsValid()
    || last_histogram_buffer_ != config_->histogram_buffer) {
    if (!registry.Contains(*config_->histogram_buffer)) {
      registry.Register(config_->histogram_buffer);
    }
    auto handle = allocator.Allocate(graphics::ResourceViewType::kRawBuffer_UAV,
      graphics::DescriptorVisibility::kShaderVisible);
    if (!handle.IsValid()) {
      throw std::runtime_error(
        "AutoExposurePass: failed to allocate histogram UAV descriptor");
    }
    histogram_uav_index_ = allocator.GetShaderVisibleIndex(handle);

    graphics::BufferViewDescription desc {
      .view_type = graphics::ResourceViewType::kRawBuffer_UAV,
      .visibility = graphics::DescriptorVisibility::kShaderVisible,
      .range = graphics::BufferRange(0, kHistogramBinCount * sizeof(uint32_t)),
      .stride = 0,
    };
    const auto view = registry.RegisterView(
      *config_->histogram_buffer, std::move(handle), desc);
    if (!view->IsValid()) {
      throw std::runtime_error(
        "AutoExposurePass: failed to register histogram UAV view");
    }
    last_histogram_buffer_ = config_->histogram_buffer;
  }

  // 2. Exposure state views are created per-view in EnsureExposureStateForView.

  // 3. Pass Constants
  // 3. Pass Constants
  if (!pass_constants_buffer_) {
    graphics::BufferDesc cb_desc {
      .size_bytes = kPassConstantsStride * kPassConstantsSlots,
      .usage = graphics::BufferUsage::kConstant,
      .memory = graphics::BufferMemory::kUpload,
      .debug_name = std::string(GetName()) + "_PassConstants",
    };
    pass_constants_buffer_ = graphics.CreateBuffer(cb_desc);
    if (!pass_constants_buffer_) {
      throw std::runtime_error(
        "AutoExposurePass: failed to create pass constants buffer");
    }
    pass_constants_buffer_->SetName(cb_desc.debug_name);

    pass_constants_mapped_ptr_
      = pass_constants_buffer_->Map(0, cb_desc.size_bytes);
    if (pass_constants_mapped_ptr_ == nullptr) {
      throw std::runtime_error(
        "AutoExposurePass: failed to map pass constants buffer");
    }

    pass_constants_indices_.fill(kInvalidShaderVisibleIndex);
    registry.Register(pass_constants_buffer_);

    for (size_t slot = 0; slot < kPassConstantsSlots; ++slot) {
      auto handle
        = allocator.Allocate(graphics::ResourceViewType::kConstantBuffer,
          graphics::DescriptorVisibility::kShaderVisible);
      if (!handle.IsValid()) {
        throw std::runtime_error(
          "AutoExposurePass: failed to allocate pass constants CBV descriptor");
      }
      pass_constants_indices_.at(slot)
        = allocator.GetShaderVisibleIndex(handle);

      const uint32_t offset
        = static_cast<uint32_t>(slot * kPassConstantsStride);
      graphics::BufferViewDescription desc {
        .view_type = graphics::ResourceViewType::kConstantBuffer,
        .visibility = graphics::DescriptorVisibility::kShaderVisible,
        .range = graphics::BufferRange(offset, kPassConstantsStride),
      };
      const auto view = registry.RegisterView(
        *pass_constants_buffer_, std::move(handle), desc);
      if (!view->IsValid()) {
        throw std::runtime_error(
          "AutoExposurePass: failed to register pass constants CBV view");
      }
    }
  }

  if (!pass_constants_indices_.at(0).IsValid()) {
    throw std::runtime_error("AutoExposurePass: invalid pass constants index");
  }
  SetPassConstantsIndex(pass_constants_indices_.at(0));

  co_return;
}

auto AutoExposurePass::PruneStaleExposureStates(
  const oxygen::ViewId current_view_id) -> void
{
  if (config_ == nullptr || config_->max_unseen_frames == 0U
    || exposure_states_.empty()) {
    return;
  }

  auto& registry = Context().GetGraphics().GetResourceRegistry();
  const auto current_sequence = Context().frame_sequence.get();
  const auto max_unseen_frames
    = static_cast<uint64_t>(config_->max_unseen_frames);

  for (auto it = exposure_states_.begin(); it != exposure_states_.end();) {
    if (it->first == current_view_id) {
      ++it;
      continue;
    }

    const auto last_seen_sequence = it->second.last_seen_sequence.get();
    const auto age = current_sequence > last_seen_sequence
      ? current_sequence - last_seen_sequence
      : 0ULL;
    if (age <= max_unseen_frames) {
      ++it;
      continue;
    }

    if (active_exposure_state_ == &it->second) {
      active_exposure_state_ = nullptr;
    }

    if (it->second.buffer && registry.Contains(*it->second.buffer)) {
      registry.UnRegisterResource(*it->second.buffer);
    }
    it->second.buffer.reset();
    it->second.uav_index = kInvalidShaderVisibleIndex;
    it->second.srv_index = kInvalidShaderVisibleIndex;

    it = exposure_states_.erase(it);
  }
}

auto AutoExposurePass::DoExecute(graphics::CommandRecorder& recorder)
  -> co::Co<>
{
  if (!config_->source_texture || !config_->histogram_buffer
    || active_exposure_state_ == nullptr || !active_exposure_state_->buffer) {
    co_return;
  }

  if (pass_constants_mapped_ptr_ == nullptr
    || !pass_constants_indices_.at(0).IsValid()) {
    co_return;
  }

  // 1. Clear Histogram
  if (!pso_stages_.clear.has_value() || !pso_stages_.histogram.has_value()
    || !pso_stages_.average.has_value()) {
    co_return;
  }

  recorder.RequireResourceState(
    *config_->source_texture, graphics::ResourceStates::kShaderResource);
  recorder.RequireResourceState(
    *config_->histogram_buffer, graphics::ResourceStates::kUnorderedAccess);
  recorder.RequireResourceState(*active_exposure_state_->buffer,
    graphics::ResourceStates::kUnorderedAccess);
  recorder.FlushBarriers();

  recorder.SetPipelineState(*pso_stages_.clear);
  UpdateHistogramConstants(recorder);
  recorder.Dispatch(1, 1, 1);

  // UAV-to-UAV sync between clear and build.
  recorder.RequireResourceState(
    *config_->histogram_buffer, graphics::ResourceStates::kUnorderedAccess);
  recorder.FlushBarriers();

  // 2. Build Histogram
  recorder.SetPipelineState(*pso_stages_.histogram);
  UpdateHistogramConstants(recorder);

  const auto& tex_desc = config_->source_texture->GetDescriptor();
  recorder.Dispatch((tex_desc.width + (kHistogramDispatchGroupSize - 1U))
      / kHistogramDispatchGroupSize,
    (tex_desc.height + (kHistogramDispatchGroupSize - 1U))
      / kHistogramDispatchGroupSize,
    1);

  // UAV-to-UAV sync between histogram build and average.
  recorder.RequireResourceState(
    *config_->histogram_buffer, graphics::ResourceStates::kUnorderedAccess);
  recorder.RequireResourceState(*active_exposure_state_->buffer,
    graphics::ResourceStates::kUnorderedAccess);
  recorder.FlushBarriers();

  // 3. Average and Adaptation
  recorder.SetPipelineState(*pso_stages_.average);
  UpdateAverageConstants(recorder);
  recorder.Dispatch(1, 1, 1);

  co_return;
}

auto AutoExposurePass::UpdateHistogramConstants(
  graphics::CommandRecorder& recorder) -> void
{
  auto& graphics = Context().GetGraphics();
  auto& registry = graphics.GetResourceRegistry();
  auto& allocator = graphics.GetDescriptorAllocator();

  if (!source_texture_srv_index_.IsValid()
    || last_source_texture_ != config_->source_texture) {
    const auto& tex_desc = config_->source_texture->GetDescriptor();
    graphics::TextureViewDescription srv_desc {
      .view_type = graphics::ResourceViewType::kTexture_SRV,
      .visibility = graphics::DescriptorVisibility::kShaderVisible,
      .format = tex_desc.format,
      .dimension = tex_desc.texture_type,
      .sub_resources = graphics::TextureSubResourceSet::EntireTexture(),
      .is_read_only_dsv = false,
    };

    // If an equivalent view is already registered, reuse its shader-visible
    // index instead of allocating and attempting to re-register the view.
    if (registry.Contains(*config_->source_texture, srv_desc)) {
      if (auto maybe_index
        = registry.FindShaderVisibleIndex(*config_->source_texture, srv_desc);
        maybe_index.has_value()) {
        source_texture_srv_index_ = *maybe_index;
        last_source_texture_ = config_->source_texture;
      } else {
        // Fallback: allocate and register (should be rare/edge-case)
        auto handle
          = allocator.Allocate(graphics::ResourceViewType::kTexture_SRV,
            graphics::DescriptorVisibility::kShaderVisible);
        if (!handle.IsValid()) {
          throw std::runtime_error("AutoExposurePass: failed to allocate "
                                   "source texture SRV descriptor");
        }
        source_texture_srv_index_ = allocator.GetShaderVisibleIndex(handle);
        const auto view = registry.RegisterView(
          *config_->source_texture, std::move(handle), srv_desc);
        if (!view->IsValid()) {
          throw std::runtime_error(
            "AutoExposurePass: failed to register source texture SRV view");
        }
        last_source_texture_ = config_->source_texture;
      }
    } else {
      auto handle = allocator.Allocate(graphics::ResourceViewType::kTexture_SRV,
        graphics::DescriptorVisibility::kShaderVisible);
      if (!handle.IsValid()) {
        throw std::runtime_error(
          "AutoExposurePass: failed to allocate source texture SRV descriptor");
      }
      source_texture_srv_index_ = allocator.GetShaderVisibleIndex(handle);
      const auto view = registry.RegisterView(
        *config_->source_texture, std::move(handle), srv_desc);
      if (!view->IsValid()) {
        throw std::runtime_error(
          "AutoExposurePass: failed to register source texture SRV view");
      }
      last_source_texture_ = config_->source_texture;
    }
  }

  const auto& tex_desc = config_->source_texture->GetDescriptor();

  AutoExposureHistogramConstants constants { .source_texture_index
    = source_texture_srv_index_.get(),
    .histogram_buffer_index = histogram_uav_index_.get(),
    .min_log_luminance = config_->min_log_luminance,
    .inv_log_luminance_range = 1.0F / config_->log_luminance_range,
    .screen_width = tex_desc.width,
    .screen_height = tex_desc.height,
    .metering_mode = static_cast<uint32_t>(config_->metering_mode),
    ._pad = 0,
    .spot_meter_radius = config_->spot_meter_radius,
    ._pad1 = 0.0F,
    ._pad2 = 0.0F,
    ._pad3 = 0.0F };

  const auto slot = pass_constants_slot_ % kPassConstantsSlots;
  pass_constants_slot_++;
  auto mapped_bytes
    = std::span { static_cast<std::byte*>(pass_constants_mapped_ptr_),
        kPassConstantsSlots * kPassConstantsStride };
  auto slot_bytes
    = mapped_bytes.subspan(slot * kPassConstantsStride, sizeof(constants));
  std::memcpy(slot_bytes.data(), &constants, sizeof(constants));

  const auto& index = pass_constants_indices_.at(slot);
  SetPassConstantsIndex(index);

  recorder.SetComputeRoot32BitConstant(
    static_cast<uint32_t>(binding::RootParam::kRootConstants), index.get(), 1);

  (void)recorder;
}

auto AutoExposurePass::UpdateAverageConstants(
  graphics::CommandRecorder& recorder) -> void
{
  CHECK_NOTNULL_F(active_exposure_state_);
  CHECK_NOTNULL_F(active_exposure_state_->buffer);
  CHECK_F(active_exposure_state_->uav_index.IsValid());

  AutoExposureAverageConstants constants {
    .histogram_buffer_index = histogram_uav_index_.get(),
    .exposure_buffer_index = active_exposure_state_->uav_index.get(),
    .min_log_luminance = config_->min_log_luminance,
    .log_luminance_range = config_->log_luminance_range,
    .low_percentile = config_->low_percentile,
    .high_percentile = config_->high_percentile,
    .adaptation_speed_up = config_->adaptation_speed_up,
    .adaptation_speed_down = config_->adaptation_speed_down,
    .delta_time = std::max(Context().delta_time, 0.0F),
    .target_luminance = config_->target_luminance,
  };

  const auto slot = pass_constants_slot_ % kPassConstantsSlots;
  pass_constants_slot_++;
  auto mapped_bytes
    = std::span { static_cast<std::byte*>(pass_constants_mapped_ptr_),
        kPassConstantsSlots * kPassConstantsStride };
  auto slot_bytes
    = mapped_bytes.subspan(slot * kPassConstantsStride, sizeof(constants));
  std::memcpy(slot_bytes.data(), &constants, sizeof(constants));

  const auto& index = pass_constants_indices_.at(slot);
  SetPassConstantsIndex(index);

  recorder.SetComputeRoot32BitConstant(
    static_cast<uint32_t>(binding::RootParam::kRootConstants), index.get(), 1);

  (void)recorder;
}

auto AutoExposurePass::EnsureExposureInitUploadBuffer(
  graphics::CommandRecorder& recorder) -> void
{
  if (init_upload_buffer_) {
    return;
  }

  auto& gfx = Context().GetGraphics();

  graphics::BufferDesc desc {};
  desc.size_bytes = kExposureStateBufferSizeBytes;
  desc.usage = graphics::BufferUsage::kNone;
  desc.memory = graphics::BufferMemory::kUpload;
  desc.debug_name = std::string(GetName()) + "_ExposureInit";

  init_upload_buffer_ = gfx.CreateBuffer(desc);
  if (!init_upload_buffer_) {
    throw std::runtime_error(
      "AutoExposurePass: failed to create exposure init upload buffer");
  }

  exposure_init_upload_mapped_ptr_
    = init_upload_buffer_->Map(0, desc.size_bytes);
  if (exposure_init_upload_mapped_ptr_ == nullptr) {
    throw std::runtime_error(
      "AutoExposurePass: failed to map exposure init upload buffer");
  }

  const float base_luminance = (std::max)(config_ ? config_->target_luminance
                                                  : kDefaultExposureLuminance,
    kMinLogLuminanceRange);
  const float ev100 = std::log2((std::max)(kMinLogLuminanceRange,
    base_luminance * kEv100Scale / kEv100CalibrationConstant));
  const std::array<float, kExposureStateElementCount> init_values {
    base_luminance,
    1.0F,
    ev100,
    0.0F,
  };
  const auto init_bytes = std::as_bytes(std::span { init_values });
  std::memcpy(
    exposure_init_upload_mapped_ptr_, init_bytes.data(), init_bytes.size());

  if (!recorder.IsResourceTracked(*init_upload_buffer_)) {
    recorder.BeginTrackingResourceState(
      *init_upload_buffer_, graphics::ResourceStates::kCopySource, true);
  }
}

auto AutoExposurePass::EnsureExposureStateForView(
  graphics::CommandRecorder& recorder, oxygen::ViewId view_id) -> void
{
  auto& gfx = Context().GetGraphics();
  auto& allocator = gfx.GetDescriptorAllocator();
  auto& registry = gfx.GetResourceRegistry();

  auto& state = exposure_states_[view_id];
  state.last_seen_sequence = Context().frame_sequence;

  if (!state.buffer) {
    graphics::BufferDesc exp_desc {};
    exp_desc.size_bytes = kExposureStateBufferSizeBytes;
    exp_desc.usage = graphics::BufferUsage::kStorage;
    exp_desc.memory = graphics::BufferMemory::kDeviceLocal;
    exp_desc.debug_name = std::string(GetName()) + "_ExposureState_"
      + std::to_string(view_id.get());

    state.buffer = gfx.CreateBuffer(exp_desc);
    if (!state.buffer) {
      throw std::runtime_error(
        "AutoExposurePass: failed to create exposure state buffer");
    }
    state.buffer->SetName(exp_desc.debug_name);

    if (!recorder.IsResourceTracked(*state.buffer)) {
      recorder.BeginTrackingResourceState(
        *state.buffer, graphics::ResourceStates::kCopyDest, false);
    }

    EnsureExposureInitUploadBuffer(recorder);

    recorder.CopyBuffer(
      *state.buffer, 0, *init_upload_buffer_, 0, kExposureStateBufferSizeBytes);
    recorder.RequireResourceState(
      *state.buffer, graphics::ResourceStates::kUnorderedAccess);
    recorder.FlushBarriers();
  } else {
    if (!recorder.IsResourceTracked(*state.buffer)) {
      recorder.BeginTrackingResourceState(
        *state.buffer, graphics::ResourceStates::kUnorderedAccess, false);
    }
  }

  if (!registry.Contains(*state.buffer)) {
    registry.Register(state.buffer);
  }

  if (!state.uav_index.IsValid()) {
    auto uav_handle
      = allocator.Allocate(graphics::ResourceViewType::kRawBuffer_UAV,
        graphics::DescriptorVisibility::kShaderVisible);
    if (!uav_handle.IsValid()) {
      throw std::runtime_error(
        "AutoExposurePass: failed to allocate exposure UAV descriptor");
    }
    state.uav_index = allocator.GetShaderVisibleIndex(uav_handle);

    graphics::BufferViewDescription uav_desc {
      .view_type = graphics::ResourceViewType::kRawBuffer_UAV,
      .visibility = graphics::DescriptorVisibility::kShaderVisible,
      .range = graphics::BufferRange(0, kExposureStateBufferSizeBytes),
      .stride = 0,
    };
    const auto view
      = registry.RegisterView(*state.buffer, std::move(uav_handle), uav_desc);
    if (!view->IsValid()) {
      throw std::runtime_error(
        "AutoExposurePass: failed to register exposure UAV view");
    }
  }

  if (!state.srv_index.IsValid()) {
    auto srv_handle
      = allocator.Allocate(graphics::ResourceViewType::kRawBuffer_SRV,
        graphics::DescriptorVisibility::kShaderVisible);
    if (!srv_handle.IsValid()) {
      throw std::runtime_error(
        "AutoExposurePass: failed to allocate exposure SRV descriptor");
    }
    state.srv_index = allocator.GetShaderVisibleIndex(srv_handle);

    graphics::BufferViewDescription srv_desc {
      .view_type = graphics::ResourceViewType::kRawBuffer_SRV,
      .visibility = graphics::DescriptorVisibility::kShaderVisible,
      .range = graphics::BufferRange(0, kExposureStateBufferSizeBytes),
      .stride = 0,
    };
    const auto view
      = registry.RegisterView(*state.buffer, std::move(srv_handle), srv_desc);
    if (!view->IsValid()) {
      throw std::runtime_error(
        "AutoExposurePass: failed to register exposure SRV view");
    }
  }

  active_exposure_state_ = &state;
}

auto AutoExposurePass::EnsureHistogramBuffer() -> void
{
  if (!config_->histogram_buffer) {
    graphics::BufferDesc desc { .size_bytes
      = kHistogramBinCount * sizeof(uint32_t),
      .usage = graphics::BufferUsage::kStorage,
      .memory = graphics::BufferMemory::kDeviceLocal,
      .debug_name = "AutoExposure_Histogram" };
    config_->histogram_buffer = Context().GetGraphics().CreateBuffer(desc);
    if (config_->histogram_buffer) {
      config_->histogram_buffer->SetName(desc.debug_name);
    }
  }
}

auto AutoExposurePass::ReleasePassConstantsBuffer() noexcept -> void
{
  if (!pass_constants_buffer_) {
    pass_constants_mapped_ptr_ = nullptr;
    pass_constants_indices_.fill(kInvalidShaderVisibleIndex);
    pass_constants_slot_ = 0U;
    return;
  }

  if (pass_constants_buffer_->IsMapped()) {
    pass_constants_buffer_->UnMap();
  }

  pass_constants_mapped_ptr_ = nullptr;
  pass_constants_buffer_.reset();
  pass_constants_indices_.fill(kInvalidShaderVisibleIndex);
  pass_constants_slot_ = 0U;
}

auto AutoExposurePass::ResetExposure(graphics::CommandRecorder& recorder,
  oxygen::ViewId view_id, float initial_avg_luminance) -> void
{
  auto it = exposure_states_.find(view_id);
  if (it == exposure_states_.end() || !it->second.buffer) {
    // If the view state doesn't exist yet, we can't reset it.
    // However, it will be created in EnsureExposureStateForView during
    // PrepareResources. We could store this pending reset request and apply it
    // then, but for now we only support resetting existing states.
    // Given the rendering loop structure, the view state should exist if we've
    // rendered at least one frame.
    return;
  }

  auto& state = it->second;

  EnsureExposureInitUploadBuffer(recorder);

  float ev = 0.0F;
  if (initial_avg_luminance > kMinLogLuminanceRange) {
    ev = std::log2(
      initial_avg_luminance * kEv100Scale / kEv100CalibrationConstant);
  }

  const std::array<float, kExposureStateElementCount> init_values {
    (std::max)(initial_avg_luminance, kMinLogLuminanceRange),
    1.0F,
    ev,
    0.0F,
  };
  const auto init_bytes = std::as_bytes(std::span { init_values });
  std::memcpy(
    exposure_init_upload_mapped_ptr_, init_bytes.data(), init_bytes.size());

  using enum oxygen::graphics::ResourceStates;

  // We always expect to start as UAV, and we always transition back to UAV at
  // the end for a consistent state.
  if (!recorder.IsResourceTracked(*state.buffer)) {
    recorder.BeginTrackingResourceState(*state.buffer, kUnorderedAccess, true);
  }

  // This reset buffer is always used as a copy source, and its initial state
  // will always be as such.
  if (!recorder.IsResourceTracked(*init_upload_buffer_)) {
    recorder.BeginTrackingResourceState(
      *init_upload_buffer_, kCopySource, false);
  }

  recorder.RequireResourceState(*state.buffer, kCopyDest);
  recorder.FlushBarriers();

  recorder.CopyBuffer(
    *state.buffer, 0, *init_upload_buffer_, 0, kExposureStateBufferSizeBytes);
}

} // namespace oxygen::engine
