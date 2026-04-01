//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Renderer/Passes/ConventionalShadowReceiverMaskPass.h>

#include <algorithm>
#include <cstring>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>

#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>

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
#include <Oxygen/Renderer/Passes/ConventionalShadowReceiverAnalysisPass.h>
#include <Oxygen/Renderer/Passes/ScreenHzbBuildPass.h>
#include <Oxygen/Renderer/RenderContext.h>
#include <Oxygen/Renderer/Renderer.h>
#include <Oxygen/Renderer/ShadowManager.h>
#include <Oxygen/Renderer/Types/ConventionalShadowReceiverAnalysis.h>
#include <Oxygen/Renderer/Types/ConventionalShadowReceiverMask.h>

using oxygen::kInvalidShaderVisibleIndex;
using oxygen::ShaderVisibleIndex;
using oxygen::graphics::Buffer;
using oxygen::graphics::BufferDesc;
using oxygen::graphics::BufferMemory;
using oxygen::graphics::BufferUsage;
using oxygen::graphics::CommandRecorder;
using oxygen::graphics::ComputePipelineDesc;
using oxygen::graphics::ResourceStates;
using oxygen::graphics::ResourceViewType;
using oxygen::graphics::Texture;
using oxygen::renderer::ConventionalShadowReceiverAnalysis;
using oxygen::renderer::ConventionalShadowReceiverAnalysisJob;
using oxygen::renderer::ConventionalShadowReceiverMaskSummary;

namespace oxygen::engine {

namespace {

  constexpr std::uint32_t kScreenThreadGroupSize = 8U;
  constexpr std::uint32_t kLinearThreadGroupSize = 64U;

  struct alignas(packing::kShaderDataFieldAlignment)
    ConventionalShadowReceiverMaskPassConstants {
    ShaderVisibleIndex depth_texture_index { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex job_buffer_index { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex analysis_buffer_index { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex raw_mask_uav_index { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex raw_mask_srv_index { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex base_mask_uav_index { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex base_mask_srv_index { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex hierarchy_mask_uav_index { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex hierarchy_mask_srv_index { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex count_buffer_uav_index { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex count_buffer_srv_index { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex summary_buffer_uav_index { kInvalidShaderVisibleIndex };
    glm::uvec2 screen_dimensions { 0U, 0U };
    std::uint32_t job_count { 0U };
    std::uint32_t base_tile_resolution { 0U };
    std::uint32_t hierarchy_tile_resolution { 0U };
    std::uint32_t base_tiles_per_job { 0U };
    std::uint32_t hierarchy_tiles_per_job { 0U };
    std::uint32_t hierarchy_reduction { 0U };
    glm::mat4 inverse_view_projection { 1.0F };
    glm::mat4 view_matrix { 1.0F };
  };

  static_assert(offsetof(ConventionalShadowReceiverMaskPassConstants,
                  inverse_view_projection)
    == 80U);
  static_assert(
    offsetof(ConventionalShadowReceiverMaskPassConstants, view_matrix) == 144U);
  static_assert(sizeof(ConventionalShadowReceiverMaskPassConstants) == 208U);
  static_assert(sizeof(ConventionalShadowReceiverMaskPassConstants)
      % packing::kShaderDataFieldAlignment
    == 0U);

  [[nodiscard]] auto DivideRoundUp(
    const std::uint32_t value, const std::uint32_t divisor) -> std::uint32_t
  {
    return divisor == 0U ? 0U : (value + divisor - 1U) / divisor;
  }

  [[nodiscard]] auto MakeStructuredViewDesc(const ResourceViewType view_type,
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

  template <typename Resource>
  auto UnregisterResourceIfPresent(
    Graphics& gfx, const std::shared_ptr<Resource>& resource) -> void
  {
    if (!resource) {
      return;
    }
    auto& registry = gfx.GetResourceRegistry();
    if (registry.Contains(*resource)) {
      registry.UnRegisterResource(*resource);
    }
  }

  auto BindComputeStage(CommandRecorder& recorder,
    const graphics::ComputePipelineDesc& pso_desc,
    const ShaderVisibleIndex pass_constants_index, const RenderContext& context)
    -> void
  {
    DCHECK_NOTNULL_F(context.view_constants);
    recorder.SetPipelineState(pso_desc);
    recorder.SetComputeRootConstantBufferView(
      static_cast<std::uint32_t>(binding::RootParam::kViewConstants),
      context.view_constants->GetGPUVirtualAddress());
    recorder.SetComputeRoot32BitConstant(
      static_cast<std::uint32_t>(binding::RootParam::kRootConstants), 0U, 0U);
    recorder.SetComputeRoot32BitConstant(
      static_cast<std::uint32_t>(binding::RootParam::kRootConstants),
      pass_constants_index.get(), 1U);
  }

} // namespace

struct ConventionalShadowReceiverMaskPass::Impl {
  struct ViewState {
    std::shared_ptr<Buffer> job_buffer {};
    std::shared_ptr<Buffer> job_upload_buffer {};
    void* job_upload_mapped_ptr { nullptr };
    ShaderVisibleIndex job_srv_index { kInvalidShaderVisibleIndex };

    std::shared_ptr<Buffer> raw_mask_buffer {};
    ShaderVisibleIndex raw_mask_uav_index { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex raw_mask_srv_index { kInvalidShaderVisibleIndex };

    std::shared_ptr<Buffer> base_mask_buffer {};
    ShaderVisibleIndex base_mask_uav_index { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex base_mask_srv_index { kInvalidShaderVisibleIndex };

    std::shared_ptr<Buffer> hierarchy_mask_buffer {};
    ShaderVisibleIndex hierarchy_mask_uav_index { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex hierarchy_mask_srv_index { kInvalidShaderVisibleIndex };

    std::shared_ptr<Buffer> count_buffer {};
    ShaderVisibleIndex count_buffer_uav_index { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex count_buffer_srv_index { kInvalidShaderVisibleIndex };

    std::shared_ptr<Buffer> summary_buffer {};
    ShaderVisibleIndex summary_uav_index { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex summary_srv_index { kInvalidShaderVisibleIndex };

    std::uint32_t capacity { 0U };
    std::uint32_t job_count { 0U };
    std::uint32_t base_tile_resolution { 0U };
    std::uint32_t hierarchy_tile_resolution { 0U };
    std::uint32_t hierarchy_reduction { 0U };
    std::uint32_t base_tiles_per_job { 0U };
    std::uint32_t hierarchy_tiles_per_job { 0U };
    bool has_current_output { false };
  };

  observer_ptr<Graphics> gfx;
  std::shared_ptr<Config> config;
  std::unordered_map<ViewId, ViewState> view_states {};

  std::shared_ptr<Buffer> pass_constants_buffer {};
  void* pass_constants_mapped_ptr { nullptr };
  ShaderVisibleIndex pass_constants_index { kInvalidShaderVisibleIndex };

  ViewState* active_view_state { nullptr };
  ViewId active_view_id {};
  std::shared_ptr<const Buffer> active_analysis_buffer {};
  const Texture* active_depth_texture { nullptr };
  std::uint32_t active_job_count { 0U };
  glm::uvec2 active_screen_dimensions { 0U, 0U };
  std::uint32_t active_base_tile_resolution { 0U };
  std::uint32_t active_hierarchy_tile_resolution { 0U };
  std::uint32_t active_base_tiles_per_job { 0U };
  std::uint32_t active_hierarchy_tiles_per_job { 0U };
  std::uint32_t active_hierarchy_reduction { 0U };
  bool resources_prepared { false };
  bool pipelines_ready { false };

  std::optional<graphics::ComputePipelineDesc> clear_pso {};
  std::optional<graphics::ComputePipelineDesc> analyze_pso {};
  std::optional<graphics::ComputePipelineDesc> dilate_pso {};
  std::optional<graphics::ComputePipelineDesc> hierarchy_pso {};
  std::optional<graphics::ComputePipelineDesc> finalize_pso {};

  Impl(observer_ptr<Graphics> gfx_, std::shared_ptr<Config> config_)
    : gfx(gfx_)
    , config(std::move(config_))
  {
  }

  ~Impl()
  {
    if (pass_constants_buffer && pass_constants_mapped_ptr != nullptr) {
      pass_constants_buffer->UnMap();
      pass_constants_mapped_ptr = nullptr;
    }

    if (gfx == nullptr) {
      return;
    }

    UnregisterResourceIfPresent(*gfx, pass_constants_buffer);
    for (auto& [view_id, state] : view_states) {
      static_cast<void>(view_id);
      ReleaseViewResources(state);
    }
  }

  auto ReleaseViewResources(ViewState& state) -> void;
  auto EnsurePassConstantsBuffer() -> void;
  auto EnsureViewResources(const ViewId view_id,
    std::uint32_t required_capacity,
    std::uint32_t required_base_tile_resolution,
    std::uint32_t required_hierarchy_tile_resolution,
    std::uint32_t required_hierarchy_reduction) -> ViewState&;
};

auto ConventionalShadowReceiverMaskPass::Impl::ReleaseViewResources(
  ViewState& state) -> void
{
  if (state.job_upload_buffer && state.job_upload_mapped_ptr != nullptr) {
    state.job_upload_buffer->UnMap();
    state.job_upload_mapped_ptr = nullptr;
  }

  UnregisterResourceIfPresent(*gfx, state.summary_buffer);
  UnregisterResourceIfPresent(*gfx, state.count_buffer);
  UnregisterResourceIfPresent(*gfx, state.hierarchy_mask_buffer);
  UnregisterResourceIfPresent(*gfx, state.base_mask_buffer);
  UnregisterResourceIfPresent(*gfx, state.raw_mask_buffer);
  UnregisterResourceIfPresent(*gfx, state.job_upload_buffer);
  UnregisterResourceIfPresent(*gfx, state.job_buffer);

  state.job_buffer.reset();
  state.job_upload_buffer.reset();
  state.raw_mask_buffer.reset();
  state.base_mask_buffer.reset();
  state.hierarchy_mask_buffer.reset();
  state.count_buffer.reset();
  state.summary_buffer.reset();
  state.job_srv_index = kInvalidShaderVisibleIndex;
  state.raw_mask_uav_index = kInvalidShaderVisibleIndex;
  state.raw_mask_srv_index = kInvalidShaderVisibleIndex;
  state.base_mask_uav_index = kInvalidShaderVisibleIndex;
  state.base_mask_srv_index = kInvalidShaderVisibleIndex;
  state.hierarchy_mask_uav_index = kInvalidShaderVisibleIndex;
  state.hierarchy_mask_srv_index = kInvalidShaderVisibleIndex;
  state.count_buffer_uav_index = kInvalidShaderVisibleIndex;
  state.count_buffer_srv_index = kInvalidShaderVisibleIndex;
  state.summary_uav_index = kInvalidShaderVisibleIndex;
  state.summary_srv_index = kInvalidShaderVisibleIndex;
  state.capacity = 0U;
  state.job_count = 0U;
  state.base_tile_resolution = 0U;
  state.hierarchy_tile_resolution = 0U;
  state.hierarchy_reduction = 0U;
  state.base_tiles_per_job = 0U;
  state.hierarchy_tiles_per_job = 0U;
  state.has_current_output = false;
}

auto ConventionalShadowReceiverMaskPass::Impl::EnsurePassConstantsBuffer()
  -> void
{
  if (pass_constants_buffer) {
    return;
  }

  auto& registry = gfx->GetResourceRegistry();
  auto& allocator = gfx->GetDescriptorAllocator();

  const BufferDesc desc {
    .size_bytes = packing::kConstantBufferAlignment,
    .usage = BufferUsage::kConstant,
    .memory = BufferMemory::kUpload,
    .debug_name = config->debug_name + "_PassConstants",
  };
  pass_constants_buffer = gfx->CreateBuffer(desc);
  CHECK_NOTNULL_F(pass_constants_buffer.get(),
    "Failed to create conventional receiver-mask constants buffer");
  pass_constants_buffer->SetName(desc.debug_name);
  RegisterResourceIfNeeded(*gfx, pass_constants_buffer);

  pass_constants_mapped_ptr = pass_constants_buffer->Map(0U, desc.size_bytes);
  CHECK_NOTNULL_F(pass_constants_mapped_ptr,
    "Failed to map conventional receiver-mask constants buffer");

  auto cbv_handle = allocator.Allocate(ResourceViewType::kConstantBuffer,
    graphics::DescriptorVisibility::kShaderVisible);
  CHECK_F(cbv_handle.IsValid(),
    "Failed to allocate conventional receiver-mask constants CBV");
  pass_constants_index = allocator.GetShaderVisibleIndex(cbv_handle);
  registry.RegisterView(*pass_constants_buffer, std::move(cbv_handle),
    graphics::BufferViewDescription {
      .view_type = ResourceViewType::kConstantBuffer,
      .visibility = graphics::DescriptorVisibility::kShaderVisible,
      .range = { 0U, desc.size_bytes },
    });
}

auto ConventionalShadowReceiverMaskPass::Impl::EnsureViewResources(
  const ViewId view_id, const std::uint32_t required_capacity,
  const std::uint32_t required_base_tile_resolution,
  const std::uint32_t required_hierarchy_tile_resolution,
  const std::uint32_t required_hierarchy_reduction) -> ViewState&
{
  auto& state = view_states[view_id];
  if (required_capacity == 0U) {
    state.job_count = 0U;
    state.has_current_output = false;
    return state;
  }

  const auto required_base_tiles_per_job
    = required_base_tile_resolution * required_base_tile_resolution;
  const auto required_hierarchy_tiles_per_job
    = required_hierarchy_tile_resolution * required_hierarchy_tile_resolution;

  if (state.capacity >= required_capacity
    && state.base_tile_resolution == required_base_tile_resolution
    && state.hierarchy_tile_resolution == required_hierarchy_tile_resolution
    && state.hierarchy_reduction == required_hierarchy_reduction
    && state.job_buffer && state.job_upload_buffer && state.raw_mask_buffer
    && state.base_mask_buffer && state.hierarchy_mask_buffer
    && state.count_buffer && state.summary_buffer) {
    return state;
  }

  ReleaseViewResources(state);

  const auto create_buffer
    = [&](const std::string& name_suffix, const std::uint64_t size_bytes,
        const BufferUsage usage, const BufferMemory memory) {
        auto buffer = gfx->CreateBuffer(BufferDesc {
          .size_bytes = size_bytes,
          .usage = usage,
          .memory = memory,
          .debug_name = config->debug_name + "." + std::to_string(view_id.get())
            + "." + name_suffix,
        });
        CHECK_NOTNULL_F(buffer.get(),
          "Failed to create conventional receiver-mask buffer {}", name_suffix);
        buffer->SetName(buffer->GetDescriptor().debug_name);
        RegisterResourceIfNeeded(*gfx, buffer);
        return buffer;
      };

  const auto job_buffer_size = static_cast<std::uint64_t>(required_capacity)
    * sizeof(ConventionalShadowReceiverAnalysisJob);
  state.job_buffer = create_buffer(
    "Jobs", job_buffer_size, BufferUsage::kStorage, BufferMemory::kDeviceLocal);
  state.job_upload_buffer = create_buffer(
    "JobsUpload", job_buffer_size, BufferUsage::kNone, BufferMemory::kUpload);
  state.job_upload_mapped_ptr
    = state.job_upload_buffer->Map(0U, job_buffer_size);
  CHECK_NOTNULL_F(state.job_upload_mapped_ptr,
    "Failed to map conventional receiver-mask job upload buffer");

  const auto base_mask_buffer_size
    = static_cast<std::uint64_t>(
        required_capacity * required_base_tiles_per_job)
    * sizeof(std::uint32_t);
  state.raw_mask_buffer = create_buffer("RawMask", base_mask_buffer_size,
    BufferUsage::kStorage, BufferMemory::kDeviceLocal);
  state.base_mask_buffer = create_buffer("BaseMask", base_mask_buffer_size,
    BufferUsage::kStorage, BufferMemory::kDeviceLocal);

  const auto hierarchy_mask_buffer_size
    = static_cast<std::uint64_t>(
        required_capacity * required_hierarchy_tiles_per_job)
    * sizeof(std::uint32_t);
  state.hierarchy_mask_buffer
    = create_buffer("HierarchyMask", hierarchy_mask_buffer_size,
      BufferUsage::kStorage, BufferMemory::kDeviceLocal);

  const auto count_buffer_size = static_cast<std::uint64_t>(required_capacity)
    * 2U * sizeof(std::uint32_t);
  state.count_buffer = create_buffer("Counts", count_buffer_size,
    BufferUsage::kStorage, BufferMemory::kDeviceLocal);

  const auto summary_buffer_size = static_cast<std::uint64_t>(required_capacity)
    * sizeof(ConventionalShadowReceiverMaskSummary);
  state.summary_buffer = create_buffer("Summary", summary_buffer_size,
    BufferUsage::kStorage, BufferMemory::kDeviceLocal);

  auto& allocator = gfx->GetDescriptorAllocator();
  auto& registry = gfx->GetResourceRegistry();

  auto job_srv = allocator.Allocate(ResourceViewType::kStructuredBuffer_SRV,
    graphics::DescriptorVisibility::kShaderVisible);
  CHECK_F(
    job_srv.IsValid(), "Failed to allocate conventional receiver-mask job SRV");
  state.job_srv_index = allocator.GetShaderVisibleIndex(job_srv);
  registry.RegisterView(*state.job_buffer, std::move(job_srv),
    MakeStructuredViewDesc(ResourceViewType::kStructuredBuffer_SRV,
      job_buffer_size, sizeof(ConventionalShadowReceiverAnalysisJob)));

  auto raw_mask_uav
    = allocator.Allocate(ResourceViewType::kStructuredBuffer_UAV,
      graphics::DescriptorVisibility::kShaderVisible);
  CHECK_F(raw_mask_uav.IsValid(),
    "Failed to allocate conventional receiver-mask raw UAV");
  state.raw_mask_uav_index = allocator.GetShaderVisibleIndex(raw_mask_uav);
  registry.RegisterView(*state.raw_mask_buffer, std::move(raw_mask_uav),
    MakeStructuredViewDesc(ResourceViewType::kStructuredBuffer_UAV,
      base_mask_buffer_size, sizeof(std::uint32_t)));

  auto raw_mask_srv
    = allocator.Allocate(ResourceViewType::kStructuredBuffer_SRV,
      graphics::DescriptorVisibility::kShaderVisible);
  CHECK_F(raw_mask_srv.IsValid(),
    "Failed to allocate conventional receiver-mask raw SRV");
  state.raw_mask_srv_index = allocator.GetShaderVisibleIndex(raw_mask_srv);
  registry.RegisterView(*state.raw_mask_buffer, std::move(raw_mask_srv),
    MakeStructuredViewDesc(ResourceViewType::kStructuredBuffer_SRV,
      base_mask_buffer_size, sizeof(std::uint32_t)));

  auto base_mask_uav
    = allocator.Allocate(ResourceViewType::kStructuredBuffer_UAV,
      graphics::DescriptorVisibility::kShaderVisible);
  CHECK_F(base_mask_uav.IsValid(),
    "Failed to allocate conventional receiver-mask base UAV");
  state.base_mask_uav_index = allocator.GetShaderVisibleIndex(base_mask_uav);
  registry.RegisterView(*state.base_mask_buffer, std::move(base_mask_uav),
    MakeStructuredViewDesc(ResourceViewType::kStructuredBuffer_UAV,
      base_mask_buffer_size, sizeof(std::uint32_t)));

  auto base_mask_srv
    = allocator.Allocate(ResourceViewType::kStructuredBuffer_SRV,
      graphics::DescriptorVisibility::kShaderVisible);
  CHECK_F(base_mask_srv.IsValid(),
    "Failed to allocate conventional receiver-mask base SRV");
  state.base_mask_srv_index = allocator.GetShaderVisibleIndex(base_mask_srv);
  registry.RegisterView(*state.base_mask_buffer, std::move(base_mask_srv),
    MakeStructuredViewDesc(ResourceViewType::kStructuredBuffer_SRV,
      base_mask_buffer_size, sizeof(std::uint32_t)));

  auto hierarchy_mask_uav
    = allocator.Allocate(ResourceViewType::kStructuredBuffer_UAV,
      graphics::DescriptorVisibility::kShaderVisible);
  CHECK_F(hierarchy_mask_uav.IsValid(),
    "Failed to allocate conventional receiver-mask hierarchy UAV");
  state.hierarchy_mask_uav_index
    = allocator.GetShaderVisibleIndex(hierarchy_mask_uav);
  registry.RegisterView(*state.hierarchy_mask_buffer,
    std::move(hierarchy_mask_uav),
    MakeStructuredViewDesc(ResourceViewType::kStructuredBuffer_UAV,
      hierarchy_mask_buffer_size, sizeof(std::uint32_t)));

  auto hierarchy_mask_srv
    = allocator.Allocate(ResourceViewType::kStructuredBuffer_SRV,
      graphics::DescriptorVisibility::kShaderVisible);
  CHECK_F(hierarchy_mask_srv.IsValid(),
    "Failed to allocate conventional receiver-mask hierarchy SRV");
  state.hierarchy_mask_srv_index
    = allocator.GetShaderVisibleIndex(hierarchy_mask_srv);
  registry.RegisterView(*state.hierarchy_mask_buffer,
    std::move(hierarchy_mask_srv),
    MakeStructuredViewDesc(ResourceViewType::kStructuredBuffer_SRV,
      hierarchy_mask_buffer_size, sizeof(std::uint32_t)));

  auto count_buffer_uav
    = allocator.Allocate(ResourceViewType::kStructuredBuffer_UAV,
      graphics::DescriptorVisibility::kShaderVisible);
  CHECK_F(count_buffer_uav.IsValid(),
    "Failed to allocate conventional receiver-mask count UAV");
  state.count_buffer_uav_index
    = allocator.GetShaderVisibleIndex(count_buffer_uav);
  registry.RegisterView(*state.count_buffer, std::move(count_buffer_uav),
    MakeStructuredViewDesc(ResourceViewType::kStructuredBuffer_UAV,
      count_buffer_size, sizeof(std::uint32_t)));

  auto count_buffer_srv
    = allocator.Allocate(ResourceViewType::kStructuredBuffer_SRV,
      graphics::DescriptorVisibility::kShaderVisible);
  CHECK_F(count_buffer_srv.IsValid(),
    "Failed to allocate conventional receiver-mask count SRV");
  state.count_buffer_srv_index
    = allocator.GetShaderVisibleIndex(count_buffer_srv);
  registry.RegisterView(*state.count_buffer, std::move(count_buffer_srv),
    MakeStructuredViewDesc(ResourceViewType::kStructuredBuffer_SRV,
      count_buffer_size, sizeof(std::uint32_t)));

  auto summary_uav = allocator.Allocate(ResourceViewType::kStructuredBuffer_UAV,
    graphics::DescriptorVisibility::kShaderVisible);
  CHECK_F(summary_uav.IsValid(),
    "Failed to allocate conventional receiver-mask summary UAV");
  state.summary_uav_index = allocator.GetShaderVisibleIndex(summary_uav);
  registry.RegisterView(*state.summary_buffer, std::move(summary_uav),
    MakeStructuredViewDesc(ResourceViewType::kStructuredBuffer_UAV,
      summary_buffer_size, sizeof(ConventionalShadowReceiverMaskSummary)));

  auto summary_srv = allocator.Allocate(ResourceViewType::kStructuredBuffer_SRV,
    graphics::DescriptorVisibility::kShaderVisible);
  CHECK_F(summary_srv.IsValid(),
    "Failed to allocate conventional receiver-mask summary SRV");
  state.summary_srv_index = allocator.GetShaderVisibleIndex(summary_srv);
  registry.RegisterView(*state.summary_buffer, std::move(summary_srv),
    MakeStructuredViewDesc(ResourceViewType::kStructuredBuffer_SRV,
      summary_buffer_size, sizeof(ConventionalShadowReceiverMaskSummary)));

  state.capacity = required_capacity;
  state.job_count = 0U;
  state.base_tile_resolution = required_base_tile_resolution;
  state.hierarchy_tile_resolution = required_hierarchy_tile_resolution;
  state.hierarchy_reduction = required_hierarchy_reduction;
  state.base_tiles_per_job = required_base_tiles_per_job;
  state.hierarchy_tiles_per_job = required_hierarchy_tiles_per_job;
  state.has_current_output = false;
  return state;
}

ConventionalShadowReceiverMaskPass::ConventionalShadowReceiverMaskPass(
  const observer_ptr<Graphics> gfx, std::shared_ptr<Config> config)
  : ComputeRenderPass(
      config ? config->debug_name : "ConventionalShadowReceiverMaskPass")
  , impl_(std::make_unique<Impl>(gfx, std::move(config)))
{
}

ConventionalShadowReceiverMaskPass::~ConventionalShadowReceiverMaskPass()
  = default;

auto ConventionalShadowReceiverMaskPass::GetCurrentOutput(
  const ViewId view_id) const -> Output
{
  if (const auto it = impl_->view_states.find(view_id);
    it != impl_->view_states.end() && it->second.has_current_output) {
    return Output {
      .summary_buffer = it->second.summary_buffer,
      .summary_srv_index = it->second.summary_srv_index,
      .base_mask_buffer = it->second.base_mask_buffer,
      .base_mask_srv_index = it->second.base_mask_srv_index,
      .hierarchy_mask_buffer = it->second.hierarchy_mask_buffer,
      .hierarchy_mask_srv_index = it->second.hierarchy_mask_srv_index,
      .job_count = it->second.job_count,
      .base_tile_resolution = it->second.base_tile_resolution,
      .hierarchy_tile_resolution = it->second.hierarchy_tile_resolution,
      .hierarchy_reduction = it->second.hierarchy_reduction,
      .available = true,
    };
  }
  return {};
}

auto ConventionalShadowReceiverMaskPass::ValidateConfig() -> void
{
  if (!impl_->config) {
    throw std::runtime_error(
      "ConventionalShadowReceiverMaskPass requires configuration");
  }
  if (impl_->config->tile_size_texels == 0U) {
    throw std::runtime_error(
      "ConventionalShadowReceiverMaskPass requires tile_size_texels > 0");
  }
  if (impl_->config->hierarchy_reduction == 0U) {
    throw std::runtime_error(
      "ConventionalShadowReceiverMaskPass requires hierarchy_reduction > 0");
  }
}

auto ConventionalShadowReceiverMaskPass::DoPrepareResources(
  CommandRecorder& recorder) -> co::Co<>
{
  impl_->resources_prepared = false;
  impl_->active_view_state = nullptr;
  impl_->active_analysis_buffer.reset();
  impl_->active_depth_texture = nullptr;
  impl_->active_job_count = 0U;
  impl_->active_screen_dimensions = { 0U, 0U };
  impl_->active_base_tile_resolution = 0U;
  impl_->active_hierarchy_tile_resolution = 0U;
  impl_->active_base_tiles_per_job = 0U;
  impl_->active_hierarchy_tiles_per_job = 0U;
  impl_->active_hierarchy_reduction = 0U;

  const auto* resolved_view = Context().current_view.resolved_view.get();
  if (resolved_view == nullptr) {
    DLOG_F(2,
      "Conventional receiver-mask pass skipped because resolved view is "
      "unavailable");
    co_return;
  }

  const auto* screen_hzb_pass = Context().GetPass<ScreenHzbBuildPass>();
  if (screen_hzb_pass == nullptr) {
    DLOG_F(2,
      "Conventional receiver-mask pass skipped because ScreenHzbBuildPass "
      "is unavailable for view {}",
      Context().current_view.view_id.get());
    co_return;
  }

  const auto hzb_output
    = screen_hzb_pass->GetCurrentOutput(Context().current_view.view_id);
  if (!hzb_output.available || hzb_output.closest_texture == nullptr
    || !hzb_output.closest_srv_index.IsValid()) {
    DLOG_F(2,
      "Conventional receiver-mask pass skipped because current HZB output is "
      "unavailable for view {}",
      Context().current_view.view_id.get());
    co_return;
  }

  const auto* receiver_analysis_pass
    = Context().GetPass<ConventionalShadowReceiverAnalysisPass>();
  if (receiver_analysis_pass == nullptr) {
    DLOG_F(2,
      "Conventional receiver-mask pass skipped because "
      "ConventionalShadowReceiverAnalysisPass is unavailable for view {}",
      Context().current_view.view_id.get());
    co_return;
  }

  const auto analysis_output
    = receiver_analysis_pass->GetCurrentOutput(Context().current_view.view_id);
  if (!analysis_output.available || analysis_output.analysis_buffer == nullptr
    || !analysis_output.analysis_srv_index.IsValid()
    || analysis_output.job_count == 0U) {
    DLOG_F(2,
      "Conventional receiver-mask pass skipped because receiver-analysis "
      "output is unavailable for view {}",
      Context().current_view.view_id.get());
    co_return;
  }

  const auto shadow_manager = Context().GetRenderer().GetShadowManager();
  if (shadow_manager == nullptr) {
    DLOG_F(2,
      "Conventional receiver-mask pass skipped because ShadowManager is "
      "unavailable for view {}",
      Context().current_view.view_id.get());
    co_return;
  }

  const auto* plan = shadow_manager->TryGetReceiverAnalysisPlan(
    Context().current_view.view_id);
  if (plan == nullptr || plan->jobs.empty()) {
    DLOG_F(2,
      "Conventional receiver-mask pass skipped because no conventional "
      "receiver-analysis jobs were published for view {}",
      Context().current_view.view_id.get());
    co_return;
  }

  const auto& shadow_depth_texture
    = shadow_manager->GetConventionalShadowDepthTexture();
  if (shadow_depth_texture == nullptr) {
    DLOG_F(2,
      "Conventional receiver-mask pass skipped because conventional shadow "
      "depth texture is unavailable for view {}",
      Context().current_view.view_id.get());
    co_return;
  }

  const auto shadow_map_resolution
    = shadow_depth_texture->GetDescriptor().width;
  if (shadow_map_resolution == 0U) {
    DLOG_F(2,
      "Conventional receiver-mask pass skipped because conventional shadow "
      "depth texture width is zero for view {}",
      Context().current_view.view_id.get());
    co_return;
  }

  const auto job_count = static_cast<std::uint32_t>(plan->jobs.size());
  CHECK_F(job_count == analysis_output.job_count,
    "Conventional receiver-mask pass job-count mismatch: plan={} analysis={}",
    job_count, analysis_output.job_count);

  const auto base_tile_resolution
    = DivideRoundUp(shadow_map_resolution, impl_->config->tile_size_texels);
  const auto hierarchy_tile_resolution
    = DivideRoundUp(base_tile_resolution, impl_->config->hierarchy_reduction);
  CHECK_F(base_tile_resolution > 0U && hierarchy_tile_resolution > 0U,
    "Conventional receiver-mask pass requires non-zero tile resolution");

  impl_->EnsurePassConstantsBuffer();
  auto& view_state = impl_->EnsureViewResources(Context().current_view.view_id,
    job_count, base_tile_resolution, hierarchy_tile_resolution,
    impl_->config->hierarchy_reduction);
  std::memcpy(view_state.job_upload_mapped_ptr, plan->jobs.data(),
    plan->jobs.size_bytes());
  view_state.job_count = job_count;
  view_state.has_current_output = false;

  const auto constants = ConventionalShadowReceiverMaskPassConstants {
    .depth_texture_index = hzb_output.closest_srv_index,
    .job_buffer_index = view_state.job_srv_index,
    .analysis_buffer_index = analysis_output.analysis_srv_index,
    .raw_mask_uav_index = view_state.raw_mask_uav_index,
    .raw_mask_srv_index = view_state.raw_mask_srv_index,
    .base_mask_uav_index = view_state.base_mask_uav_index,
    .base_mask_srv_index = view_state.base_mask_srv_index,
    .hierarchy_mask_uav_index = view_state.hierarchy_mask_uav_index,
    .hierarchy_mask_srv_index = view_state.hierarchy_mask_srv_index,
    .count_buffer_uav_index = view_state.count_buffer_uav_index,
    .count_buffer_srv_index = view_state.count_buffer_srv_index,
    .summary_buffer_uav_index = view_state.summary_uav_index,
    .screen_dimensions = { hzb_output.width, hzb_output.height },
    .job_count = job_count,
    .base_tile_resolution = base_tile_resolution,
    .hierarchy_tile_resolution = hierarchy_tile_resolution,
    .base_tiles_per_job = view_state.base_tiles_per_job,
    .hierarchy_tiles_per_job = view_state.hierarchy_tiles_per_job,
    .hierarchy_reduction = impl_->config->hierarchy_reduction,
    .inverse_view_projection = resolved_view->InverseViewProjection(),
    .view_matrix = resolved_view->ViewMatrix(),
  };
  std::memcpy(impl_->pass_constants_mapped_ptr, &constants, sizeof(constants));
  SetPassConstantsIndex(impl_->pass_constants_index);

  if (!recorder.IsResourceTracked(*view_state.job_buffer)) {
    recorder.BeginTrackingResourceState(
      *view_state.job_buffer, ResourceStates::kCommon, true);
  }
  if (!recorder.IsResourceTracked(*view_state.job_upload_buffer)) {
    recorder.BeginTrackingResourceState(
      *view_state.job_upload_buffer, ResourceStates::kGenericRead, true);
  }
  if (!recorder.IsResourceTracked(*view_state.raw_mask_buffer)) {
    recorder.BeginTrackingResourceState(
      *view_state.raw_mask_buffer, ResourceStates::kCommon, true);
  }
  if (!recorder.IsResourceTracked(*view_state.base_mask_buffer)) {
    recorder.BeginTrackingResourceState(
      *view_state.base_mask_buffer, ResourceStates::kCommon, true);
  }
  if (!recorder.IsResourceTracked(*view_state.hierarchy_mask_buffer)) {
    recorder.BeginTrackingResourceState(
      *view_state.hierarchy_mask_buffer, ResourceStates::kCommon, true);
  }
  if (!recorder.IsResourceTracked(*view_state.count_buffer)) {
    recorder.BeginTrackingResourceState(
      *view_state.count_buffer, ResourceStates::kCommon, true);
  }
  if (!recorder.IsResourceTracked(*view_state.summary_buffer)) {
    recorder.BeginTrackingResourceState(
      *view_state.summary_buffer, ResourceStates::kCommon, true);
  }
  if (!recorder.IsResourceTracked(*hzb_output.closest_texture)) {
    recorder.BeginTrackingResourceState(
      *std::const_pointer_cast<Texture>(hzb_output.closest_texture),
      ResourceStates::kShaderResource, true);
  }
  if (!recorder.IsResourceTracked(*analysis_output.analysis_buffer)) {
    recorder.BeginTrackingResourceState(
      *std::const_pointer_cast<Buffer>(analysis_output.analysis_buffer),
      ResourceStates::kShaderResource, true);
  }

  impl_->active_view_state = &view_state;
  impl_->active_view_id = Context().current_view.view_id;
  impl_->active_analysis_buffer = analysis_output.analysis_buffer;
  impl_->active_depth_texture = hzb_output.closest_texture.get();
  impl_->active_job_count = job_count;
  impl_->active_screen_dimensions = { hzb_output.width, hzb_output.height };
  impl_->active_base_tile_resolution = base_tile_resolution;
  impl_->active_hierarchy_tile_resolution = hierarchy_tile_resolution;
  impl_->active_base_tiles_per_job = view_state.base_tiles_per_job;
  impl_->active_hierarchy_tiles_per_job = view_state.hierarchy_tiles_per_job;
  impl_->active_hierarchy_reduction = impl_->config->hierarchy_reduction;
  impl_->resources_prepared = true;

  DLOG_F(2,
    "Prepared conventional receiver-mask pass view={} jobs={} hzb={}x{} "
    "base_tiles={} hierarchy_tiles={} reduction={}",
    impl_->active_view_id.get(), impl_->active_job_count, hzb_output.width,
    hzb_output.height, base_tile_resolution, hierarchy_tile_resolution,
    impl_->active_hierarchy_reduction);

  co_return;
}

auto ConventionalShadowReceiverMaskPass::DoExecute(CommandRecorder& recorder)
  -> co::Co<>
{
  if (!impl_->resources_prepared || impl_->active_view_state == nullptr
    || impl_->active_analysis_buffer == nullptr
    || impl_->active_depth_texture == nullptr || impl_->active_job_count == 0U
    || !impl_->clear_pso || !impl_->analyze_pso || !impl_->dilate_pso
    || !impl_->hierarchy_pso || !impl_->finalize_pso) {
    DLOG_F(2, "Conventional receiver-mask pass skipped execute");
    co_return;
  }

  auto& view_state = *impl_->active_view_state;
  const auto dispatch_x
    = DivideRoundUp(impl_->active_screen_dimensions.x, kScreenThreadGroupSize);
  const auto dispatch_y
    = DivideRoundUp(impl_->active_screen_dimensions.y, kScreenThreadGroupSize);
  const auto total_raw_entries
    = impl_->active_job_count * impl_->active_base_tiles_per_job;
  const auto total_base_entries
    = impl_->active_job_count * impl_->active_base_tiles_per_job;
  const auto total_hierarchy_entries
    = impl_->active_job_count * impl_->active_hierarchy_tiles_per_job;
  const auto total_count_entries = impl_->active_job_count * 2U;
  const auto clear_dispatch
    = DivideRoundUp(std::max({ total_raw_entries, total_base_entries,
                      total_hierarchy_entries, total_count_entries }),
      kLinearThreadGroupSize);
  const auto dilate_dispatch
    = DivideRoundUp(total_base_entries, kLinearThreadGroupSize);
  const auto hierarchy_dispatch
    = DivideRoundUp(total_hierarchy_entries, kLinearThreadGroupSize);
  const auto job_dispatch
    = DivideRoundUp(impl_->active_job_count, kLinearThreadGroupSize);

  recorder.RequireResourceState(
    *view_state.job_upload_buffer, ResourceStates::kCopySource);
  recorder.RequireResourceState(
    *view_state.job_buffer, ResourceStates::kCopyDest);
  recorder.FlushBarriers();
  recorder.CopyBuffer(*view_state.job_buffer, 0U, *view_state.job_upload_buffer,
    0U,
    static_cast<std::uint64_t>(impl_->active_job_count)
      * sizeof(ConventionalShadowReceiverAnalysisJob));

  recorder.RequireResourceState(
    *view_state.raw_mask_buffer, ResourceStates::kUnorderedAccess);
  recorder.RequireResourceState(
    *view_state.base_mask_buffer, ResourceStates::kUnorderedAccess);
  recorder.RequireResourceState(
    *view_state.hierarchy_mask_buffer, ResourceStates::kUnorderedAccess);
  recorder.RequireResourceState(
    *view_state.count_buffer, ResourceStates::kUnorderedAccess);
  recorder.FlushBarriers();
  BindComputeStage(
    recorder, *impl_->clear_pso, GetPassConstantsIndex(), Context());
  recorder.Dispatch(clear_dispatch, 1U, 1U);

  recorder.RequireResourceState(
    *const_cast<Texture*>(impl_->active_depth_texture),
    ResourceStates::kShaderResource);
  recorder.RequireResourceState(
    *view_state.job_buffer, ResourceStates::kGenericRead);
  recorder.RequireResourceState(
    *view_state.raw_mask_buffer, ResourceStates::kUnorderedAccess);
  recorder.FlushBarriers();
  BindComputeStage(
    recorder, *impl_->analyze_pso, GetPassConstantsIndex(), Context());
  recorder.Dispatch(dispatch_x, dispatch_y, 1U);

  recorder.RequireResourceState(
    *view_state.job_buffer, ResourceStates::kGenericRead);
  recorder.RequireResourceState(
    *std::const_pointer_cast<Buffer>(impl_->active_analysis_buffer),
    ResourceStates::kShaderResource);
  recorder.RequireResourceState(
    *view_state.raw_mask_buffer, ResourceStates::kGenericRead);
  recorder.RequireResourceState(
    *view_state.base_mask_buffer, ResourceStates::kUnorderedAccess);
  recorder.RequireResourceState(
    *view_state.count_buffer, ResourceStates::kUnorderedAccess);
  recorder.FlushBarriers();
  BindComputeStage(
    recorder, *impl_->dilate_pso, GetPassConstantsIndex(), Context());
  recorder.Dispatch(dilate_dispatch, 1U, 1U);

  recorder.RequireResourceState(
    *view_state.base_mask_buffer, ResourceStates::kGenericRead);
  recorder.RequireResourceState(
    *view_state.hierarchy_mask_buffer, ResourceStates::kUnorderedAccess);
  recorder.RequireResourceState(
    *view_state.count_buffer, ResourceStates::kUnorderedAccess);
  recorder.FlushBarriers();
  BindComputeStage(
    recorder, *impl_->hierarchy_pso, GetPassConstantsIndex(), Context());
  recorder.Dispatch(hierarchy_dispatch, 1U, 1U);

  recorder.RequireResourceState(
    *view_state.job_buffer, ResourceStates::kGenericRead);
  recorder.RequireResourceState(
    *std::const_pointer_cast<Buffer>(impl_->active_analysis_buffer),
    ResourceStates::kShaderResource);
  recorder.RequireResourceState(
    *view_state.count_buffer, ResourceStates::kGenericRead);
  recorder.RequireResourceState(
    *view_state.summary_buffer, ResourceStates::kUnorderedAccess);
  recorder.FlushBarriers();
  BindComputeStage(
    recorder, *impl_->finalize_pso, GetPassConstantsIndex(), Context());
  recorder.Dispatch(job_dispatch, 1U, 1U);

  recorder.RequireResourceStateFinal(
    *view_state.raw_mask_buffer, ResourceStates::kCommon);
  recorder.RequireResourceStateFinal(
    *view_state.base_mask_buffer, ResourceStates::kShaderResource);
  recorder.RequireResourceStateFinal(
    *view_state.hierarchy_mask_buffer, ResourceStates::kShaderResource);
  recorder.RequireResourceStateFinal(
    *view_state.count_buffer, ResourceStates::kCommon);
  recorder.RequireResourceStateFinal(
    *view_state.summary_buffer, ResourceStates::kShaderResource);
  view_state.has_current_output = true;

  DLOG_F(2,
    "Executed conventional receiver-mask pass view={} jobs={} dispatch={}x{} "
    "base_entries={} hierarchy_entries={} dilate_dispatch={}",
    impl_->active_view_id.get(), impl_->active_job_count, dispatch_x,
    dispatch_y, total_base_entries, total_hierarchy_entries, dilate_dispatch);

  co_return;
}

auto ConventionalShadowReceiverMaskPass::CreatePipelineStateDesc()
  -> ComputePipelineDesc
{
  const auto generated_bindings = BuildRootBindings();
  const auto build_pso = [&](const char* entry_point, const char* debug_name) {
    return ComputePipelineDesc::Builder()
      .SetComputeShader({
        .stage = oxygen::ShaderType::kCompute,
        .source_path = "Renderer/ConventionalShadowReceiverMask.hlsl",
        .entry_point = entry_point,
      })
      .SetRootBindings(std::span<const graphics::RootBindingItem>(
        generated_bindings.data(), generated_bindings.size()))
      .SetDebugName(debug_name)
      .Build();
  };

  impl_->clear_pso
    = build_pso("CS_ClearMasks", "ConventionalShadowReceiverMaskClear_PSO");
  impl_->analyze_pso
    = build_pso("CS_Analyze", "ConventionalShadowReceiverMaskAnalyze_PSO");
  impl_->dilate_pso
    = build_pso("CS_DilateMasks", "ConventionalShadowReceiverMaskDilate_PSO");
  impl_->hierarchy_pso = build_pso(
    "CS_BuildHierarchy", "ConventionalShadowReceiverMaskHierarchy_PSO");
  impl_->finalize_pso
    = build_pso("CS_Finalize", "ConventionalShadowReceiverMaskFinalize_PSO");
  impl_->pipelines_ready = true;
  return *impl_->clear_pso;
}

auto ConventionalShadowReceiverMaskPass::NeedRebuildPipelineState() const
  -> bool
{
  return !impl_->pipelines_ready;
}

} // namespace oxygen::engine
