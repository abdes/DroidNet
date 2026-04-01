//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Renderer/Passes/ConventionalShadowReceiverAnalysisPass.h>

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
#include <Oxygen/Renderer/Passes/ScreenHzbBuildPass.h>
#include <Oxygen/Renderer/RenderContext.h>
#include <Oxygen/Renderer/Renderer.h>
#include <Oxygen/Renderer/ShadowManager.h>
#include <Oxygen/Renderer/Types/ConventionalShadowReceiverAnalysis.h>

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

namespace oxygen::engine {

namespace {

  constexpr std::uint32_t kScreenThreadGroupSize = 8U;
  constexpr std::uint32_t kJobThreadGroupSize = 64U;

  struct alignas(packing::kShaderDataFieldAlignment)
    ConventionalShadowReceiverAnalysisPassConstants {
    ShaderVisibleIndex depth_texture_index { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex job_buffer_index { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex raw_buffer_uav_index { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex raw_buffer_srv_index { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex analysis_buffer_uav_index { kInvalidShaderVisibleIndex };
    glm::uvec2 screen_dimensions { 0U, 0U };
    std::uint32_t job_count { 0U };
    std::uint32_t _pad0 { 0U };
    // HLSL cbuffer packing aligns matrices to the next 16-byte register.
    std::uint32_t _pad_after_job_count[3] { 0U, 0U, 0U };
    glm::mat4 inverse_view_projection { 1.0F };
    glm::mat4 view_matrix { 1.0F };
  };

  static_assert(offsetof(ConventionalShadowReceiverAnalysisPassConstants,
                  inverse_view_projection)
    == 48U);
  static_assert(
    offsetof(ConventionalShadowReceiverAnalysisPassConstants, view_matrix)
    == 112U);
  static_assert(
    sizeof(ConventionalShadowReceiverAnalysisPassConstants) == 176U);
  static_assert(sizeof(ConventionalShadowReceiverAnalysisPassConstants)
      % packing::kShaderDataFieldAlignment
    == 0U);

  struct alignas(16) ConventionalShadowReceiverAnalysisRaw {
    std::uint32_t min_x_ordered { 0U };
    std::uint32_t min_y_ordered { 0U };
    std::uint32_t max_x_ordered { 0U };
    std::uint32_t max_y_ordered { 0U };
    std::uint32_t min_z_ordered { 0U };
    std::uint32_t max_z_ordered { 0U };
    std::uint32_t sample_count { 0U };
    std::uint32_t _pad0 { 0U };
  };

  static_assert(sizeof(ConventionalShadowReceiverAnalysisRaw) == 32U);

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

struct ConventionalShadowReceiverAnalysisPass::Impl {
  struct ViewState {
    std::shared_ptr<Buffer> job_buffer {};
    std::shared_ptr<Buffer> job_upload_buffer {};
    void* job_upload_mapped_ptr { nullptr };
    ShaderVisibleIndex job_srv_index { kInvalidShaderVisibleIndex };

    std::shared_ptr<Buffer> raw_buffer {};
    ShaderVisibleIndex raw_uav_index { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex raw_srv_index { kInvalidShaderVisibleIndex };

    std::shared_ptr<Buffer> analysis_buffer {};
    ShaderVisibleIndex analysis_uav_index { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex analysis_srv_index { kInvalidShaderVisibleIndex };

    std::uint32_t capacity { 0U };
    std::uint32_t job_count { 0U };
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
  const Texture* active_depth_texture { nullptr };
  std::uint32_t active_job_count { 0U };
  glm::uvec2 active_screen_dimensions { 0U, 0U };
  bool resources_prepared { false };
  bool pipelines_ready { false };

  std::optional<graphics::ComputePipelineDesc> clear_pso {};
  std::optional<graphics::ComputePipelineDesc> analyze_pso {};
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

  auto ReleaseViewResources(ViewState& state) -> void
  {
    if (state.job_upload_buffer && state.job_upload_mapped_ptr != nullptr) {
      state.job_upload_buffer->UnMap();
      state.job_upload_mapped_ptr = nullptr;
    }

    UnregisterResourceIfPresent(*gfx, state.analysis_buffer);
    UnregisterResourceIfPresent(*gfx, state.raw_buffer);
    UnregisterResourceIfPresent(*gfx, state.job_upload_buffer);
    UnregisterResourceIfPresent(*gfx, state.job_buffer);

    state.job_buffer.reset();
    state.job_upload_buffer.reset();
    state.raw_buffer.reset();
    state.analysis_buffer.reset();
    state.job_srv_index = kInvalidShaderVisibleIndex;
    state.raw_uav_index = kInvalidShaderVisibleIndex;
    state.raw_srv_index = kInvalidShaderVisibleIndex;
    state.analysis_uav_index = kInvalidShaderVisibleIndex;
    state.analysis_srv_index = kInvalidShaderVisibleIndex;
    state.capacity = 0U;
    state.job_count = 0U;
    state.has_current_output = false;
  }

  auto EnsurePassConstantsBuffer() -> void
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
      "Failed to create conventional receiver-analysis constants buffer");
    pass_constants_buffer->SetName(desc.debug_name);
    RegisterResourceIfNeeded(*gfx, pass_constants_buffer);

    pass_constants_mapped_ptr = pass_constants_buffer->Map(0U, desc.size_bytes);
    CHECK_NOTNULL_F(pass_constants_mapped_ptr,
      "Failed to map conventional receiver-analysis constants buffer");

    auto cbv_handle = allocator.Allocate(ResourceViewType::kConstantBuffer,
      graphics::DescriptorVisibility::kShaderVisible);
    CHECK_F(cbv_handle.IsValid(),
      "Failed to allocate conventional receiver-analysis constants CBV");
    pass_constants_index = allocator.GetShaderVisibleIndex(cbv_handle);
    registry.RegisterView(*pass_constants_buffer, std::move(cbv_handle),
      graphics::BufferViewDescription {
        .view_type = ResourceViewType::kConstantBuffer,
        .visibility = graphics::DescriptorVisibility::kShaderVisible,
        .range = { 0U, desc.size_bytes },
      });
  }

  auto EnsureViewResources(
    const ViewId view_id, const std::uint32_t required_capacity) -> ViewState&
  {
    auto& state = view_states[view_id];
    if (required_capacity == 0U) {
      state.job_count = 0U;
      state.has_current_output = false;
      return state;
    }

    if (state.capacity >= required_capacity && state.job_buffer
      && state.job_upload_buffer && state.raw_buffer && state.analysis_buffer) {
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
            .debug_name = config->debug_name + "."
              + std::to_string(view_id.get()) + "." + name_suffix,
          });
          CHECK_NOTNULL_F(buffer.get(),
            "Failed to create conventional receiver-analysis buffer {}",
            name_suffix);
          buffer->SetName(buffer->GetDescriptor().debug_name);
          RegisterResourceIfNeeded(*gfx, buffer);
          return buffer;
        };

    const auto job_buffer_size = static_cast<std::uint64_t>(required_capacity)
      * sizeof(ConventionalShadowReceiverAnalysisJob);
    state.job_buffer = create_buffer("Jobs", job_buffer_size,
      BufferUsage::kStorage, BufferMemory::kDeviceLocal);
    state.job_upload_buffer = create_buffer(
      "JobsUpload", job_buffer_size, BufferUsage::kNone, BufferMemory::kUpload);
    state.job_upload_mapped_ptr
      = state.job_upload_buffer->Map(0U, job_buffer_size);
    CHECK_NOTNULL_F(state.job_upload_mapped_ptr,
      "Failed to map conventional receiver-analysis job upload buffer");

    const auto raw_buffer_size = static_cast<std::uint64_t>(required_capacity)
      * sizeof(ConventionalShadowReceiverAnalysisRaw);
    state.raw_buffer = create_buffer("Raw", raw_buffer_size,
      BufferUsage::kStorage, BufferMemory::kDeviceLocal);

    const auto analysis_buffer_size
      = static_cast<std::uint64_t>(required_capacity)
      * sizeof(ConventionalShadowReceiverAnalysis);
    state.analysis_buffer = create_buffer("Analysis", analysis_buffer_size,
      BufferUsage::kStorage, BufferMemory::kDeviceLocal);

    auto& allocator = gfx->GetDescriptorAllocator();
    auto& registry = gfx->GetResourceRegistry();

    auto job_srv = allocator.Allocate(ResourceViewType::kStructuredBuffer_SRV,
      graphics::DescriptorVisibility::kShaderVisible);
    CHECK_F(job_srv.IsValid(),
      "Failed to allocate conventional receiver-analysis job SRV");
    state.job_srv_index = allocator.GetShaderVisibleIndex(job_srv);
    registry.RegisterView(*state.job_buffer, std::move(job_srv),
      MakeStructuredViewDesc(ResourceViewType::kStructuredBuffer_SRV,
        job_buffer_size, sizeof(ConventionalShadowReceiverAnalysisJob)));

    auto raw_uav = allocator.Allocate(ResourceViewType::kStructuredBuffer_UAV,
      graphics::DescriptorVisibility::kShaderVisible);
    CHECK_F(raw_uav.IsValid(),
      "Failed to allocate conventional receiver-analysis raw UAV");
    state.raw_uav_index = allocator.GetShaderVisibleIndex(raw_uav);
    registry.RegisterView(*state.raw_buffer, std::move(raw_uav),
      MakeStructuredViewDesc(ResourceViewType::kStructuredBuffer_UAV,
        raw_buffer_size, sizeof(ConventionalShadowReceiverAnalysisRaw)));

    auto raw_srv = allocator.Allocate(ResourceViewType::kStructuredBuffer_SRV,
      graphics::DescriptorVisibility::kShaderVisible);
    CHECK_F(raw_srv.IsValid(),
      "Failed to allocate conventional receiver-analysis raw SRV");
    state.raw_srv_index = allocator.GetShaderVisibleIndex(raw_srv);
    registry.RegisterView(*state.raw_buffer, std::move(raw_srv),
      MakeStructuredViewDesc(ResourceViewType::kStructuredBuffer_SRV,
        raw_buffer_size, sizeof(ConventionalShadowReceiverAnalysisRaw)));

    auto analysis_uav
      = allocator.Allocate(ResourceViewType::kStructuredBuffer_UAV,
        graphics::DescriptorVisibility::kShaderVisible);
    CHECK_F(analysis_uav.IsValid(),
      "Failed to allocate conventional receiver-analysis UAV");
    state.analysis_uav_index = allocator.GetShaderVisibleIndex(analysis_uav);
    registry.RegisterView(*state.analysis_buffer, std::move(analysis_uav),
      MakeStructuredViewDesc(ResourceViewType::kStructuredBuffer_UAV,
        analysis_buffer_size, sizeof(ConventionalShadowReceiverAnalysis)));

    auto analysis_srv
      = allocator.Allocate(ResourceViewType::kStructuredBuffer_SRV,
        graphics::DescriptorVisibility::kShaderVisible);
    CHECK_F(analysis_srv.IsValid(),
      "Failed to allocate conventional receiver-analysis SRV");
    state.analysis_srv_index = allocator.GetShaderVisibleIndex(analysis_srv);
    registry.RegisterView(*state.analysis_buffer, std::move(analysis_srv),
      MakeStructuredViewDesc(ResourceViewType::kStructuredBuffer_SRV,
        analysis_buffer_size, sizeof(ConventionalShadowReceiverAnalysis)));

    state.capacity = required_capacity;
    state.job_count = 0U;
    state.has_current_output = false;
    return state;
  }
};

ConventionalShadowReceiverAnalysisPass::ConventionalShadowReceiverAnalysisPass(
  const observer_ptr<Graphics> gfx, std::shared_ptr<Config> config)
  : ComputeRenderPass(
      config ? config->debug_name : "ConventionalShadowReceiverAnalysisPass")
  , impl_(std::make_unique<Impl>(gfx, std::move(config)))
{
}

ConventionalShadowReceiverAnalysisPass::
  ~ConventionalShadowReceiverAnalysisPass()
  = default;

auto ConventionalShadowReceiverAnalysisPass::GetCurrentOutput(
  const ViewId view_id) const -> Output
{
  if (const auto it = impl_->view_states.find(view_id);
    it != impl_->view_states.end() && it->second.has_current_output) {
    return Output {
      .analysis_buffer = it->second.analysis_buffer,
      .analysis_srv_index = it->second.analysis_srv_index,
      .job_count = it->second.job_count,
      .available = true,
    };
  }
  return {};
}

auto ConventionalShadowReceiverAnalysisPass::ValidateConfig() -> void
{
  if (!impl_->config) {
    throw std::runtime_error(
      "ConventionalShadowReceiverAnalysisPass requires configuration");
  }
}

auto ConventionalShadowReceiverAnalysisPass::DoPrepareResources(
  CommandRecorder& recorder) -> co::Co<>
{
  impl_->resources_prepared = false;
  impl_->active_view_state = nullptr;
  impl_->active_depth_texture = nullptr;
  impl_->active_job_count = 0U;
  impl_->active_screen_dimensions = { 0U, 0U };

  const auto* resolved_view = Context().current_view.resolved_view.get();
  if (resolved_view == nullptr) {
    DLOG_F(2,
      "Conventional receiver-analysis pass skipped because resolved view is "
      "unavailable");
    co_return;
  }

  const auto* screen_hzb_pass = Context().GetPass<ScreenHzbBuildPass>();
  if (screen_hzb_pass == nullptr) {
    DLOG_F(2,
      "Conventional receiver-analysis pass skipped because ScreenHzbBuildPass "
      "is unavailable for view {}",
      Context().current_view.view_id.get());
    co_return;
  }

  const auto hzb_output
    = screen_hzb_pass->GetCurrentOutput(Context().current_view.view_id);
  if (!hzb_output.available || hzb_output.closest_texture == nullptr
    || !hzb_output.closest_srv_index.IsValid()) {
    DLOG_F(2,
      "Conventional receiver-analysis pass skipped because current HZB output "
      "is unavailable for view {}",
      Context().current_view.view_id.get());
    co_return;
  }

  const auto shadow_manager = Context().GetRenderer().GetShadowManager();
  if (shadow_manager == nullptr) {
    DLOG_F(2,
      "Conventional receiver-analysis pass skipped because ShadowManager is "
      "unavailable for view {}",
      Context().current_view.view_id.get());
    co_return;
  }

  const auto* plan = shadow_manager->TryGetReceiverAnalysisPlan(
    Context().current_view.view_id);
  if (plan == nullptr || plan->jobs.empty()) {
    DLOG_F(2,
      "Conventional receiver-analysis pass skipped because no conventional "
      "receiver-analysis jobs were published for view {}",
      Context().current_view.view_id.get());
    co_return;
  }

  impl_->EnsurePassConstantsBuffer();
  auto& view_state = impl_->EnsureViewResources(Context().current_view.view_id,
    static_cast<std::uint32_t>(plan->jobs.size()));
  std::memcpy(view_state.job_upload_mapped_ptr, plan->jobs.data(),
    plan->jobs.size_bytes());
  view_state.job_count = static_cast<std::uint32_t>(plan->jobs.size());
  view_state.has_current_output = false;

  const auto constants = ConventionalShadowReceiverAnalysisPassConstants {
    .depth_texture_index = hzb_output.closest_srv_index,
    .job_buffer_index = view_state.job_srv_index,
    .raw_buffer_uav_index = view_state.raw_uav_index,
    .raw_buffer_srv_index = view_state.raw_srv_index,
    .analysis_buffer_uav_index = view_state.analysis_uav_index,
    .screen_dimensions = { hzb_output.width, hzb_output.height },
    .job_count = view_state.job_count,
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
  if (!recorder.IsResourceTracked(*view_state.raw_buffer)) {
    recorder.BeginTrackingResourceState(
      *view_state.raw_buffer, ResourceStates::kCommon, true);
  }
  if (!recorder.IsResourceTracked(*view_state.analysis_buffer)) {
    recorder.BeginTrackingResourceState(
      *view_state.analysis_buffer, ResourceStates::kCommon, true);
  }
  if (!recorder.IsResourceTracked(*hzb_output.closest_texture)) {
    recorder.BeginTrackingResourceState(
      *std::const_pointer_cast<Texture>(hzb_output.closest_texture),
      ResourceStates::kShaderResource, true);
  }

  impl_->active_view_state = &view_state;
  impl_->active_view_id = Context().current_view.view_id;
  impl_->active_depth_texture = hzb_output.closest_texture.get();
  impl_->active_job_count = view_state.job_count;
  impl_->active_screen_dimensions = { hzb_output.width, hzb_output.height };
  impl_->resources_prepared = true;

  DLOG_F(2,
    "Prepared conventional receiver-analysis pass view={} jobs={} hzb={}x{}",
    impl_->active_view_id.get(), impl_->active_job_count, hzb_output.width,
    hzb_output.height);

  co_return;
}

auto ConventionalShadowReceiverAnalysisPass::DoExecute(
  CommandRecorder& recorder) -> co::Co<>
{
  if (!impl_->resources_prepared || impl_->active_view_state == nullptr
    || impl_->active_depth_texture == nullptr || impl_->active_job_count == 0U
    || !impl_->clear_pso || !impl_->analyze_pso || !impl_->finalize_pso) {
    DLOG_F(2, "Conventional receiver-analysis pass skipped execute");
    co_return;
  }

  auto& view_state = *impl_->active_view_state;
  const auto dispatch_x
    = DivideRoundUp(impl_->active_screen_dimensions.x, kScreenThreadGroupSize);
  const auto dispatch_y
    = DivideRoundUp(impl_->active_screen_dimensions.y, kScreenThreadGroupSize);
  const auto job_dispatch
    = DivideRoundUp(impl_->active_job_count, kJobThreadGroupSize);

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
    *view_state.job_buffer, ResourceStates::kGenericRead);
  recorder.RequireResourceState(
    *view_state.raw_buffer, ResourceStates::kUnorderedAccess);
  recorder.FlushBarriers();
  BindComputeStage(
    recorder, *impl_->clear_pso, GetPassConstantsIndex(), Context());
  recorder.Dispatch(job_dispatch, 1U, 1U);

  recorder.RequireResourceState(
    *const_cast<Texture*>(impl_->active_depth_texture),
    ResourceStates::kShaderResource);
  recorder.RequireResourceState(
    *view_state.job_buffer, ResourceStates::kGenericRead);
  recorder.RequireResourceState(
    *view_state.raw_buffer, ResourceStates::kUnorderedAccess);
  recorder.FlushBarriers();
  BindComputeStage(
    recorder, *impl_->analyze_pso, GetPassConstantsIndex(), Context());
  recorder.Dispatch(dispatch_x, dispatch_y, 1U);

  recorder.RequireResourceState(
    *view_state.raw_buffer, ResourceStates::kGenericRead);
  recorder.RequireResourceState(
    *view_state.analysis_buffer, ResourceStates::kUnorderedAccess);
  recorder.FlushBarriers();
  BindComputeStage(
    recorder, *impl_->finalize_pso, GetPassConstantsIndex(), Context());
  recorder.Dispatch(job_dispatch, 1U, 1U);

  recorder.RequireResourceStateFinal(
    *view_state.analysis_buffer, ResourceStates::kShaderResource);
  view_state.has_current_output = true;

  DLOG_F(2,
    "Executed conventional receiver-analysis pass view={} jobs={} "
    "dispatch={}x{}",
    impl_->active_view_id.get(), impl_->active_job_count, dispatch_x,
    dispatch_y);

  co_return;
}

auto ConventionalShadowReceiverAnalysisPass::CreatePipelineStateDesc()
  -> ComputePipelineDesc
{
  const auto generated_bindings = BuildRootBindings();
  const auto build_pso = [&](const char* entry_point, const char* debug_name) {
    return ComputePipelineDesc::Builder()
      .SetComputeShader({
        .stage = oxygen::ShaderType::kCompute,
        .source_path = "Renderer/ConventionalShadowReceiverAnalysis.hlsl",
        .entry_point = entry_point,
      })
      .SetRootBindings(std::span<const graphics::RootBindingItem>(
        generated_bindings.data(), generated_bindings.size()))
      .SetDebugName(debug_name)
      .Build();
  };

  impl_->clear_pso
    = build_pso("CS_Clear", "ConventionalShadowReceiverAnalysisClear_PSO");
  impl_->analyze_pso
    = build_pso("CS_Analyze", "ConventionalShadowReceiverAnalysis_PSO");
  impl_->finalize_pso = build_pso(
    "CS_Finalize", "ConventionalShadowReceiverAnalysisFinalize_PSO");
  impl_->pipelines_ready = true;
  return *impl_->clear_pso;
}

auto ConventionalShadowReceiverAnalysisPass::NeedRebuildPipelineState() const
  -> bool
{
  return !impl_->pipelines_ready;
}

} // namespace oxygen::engine
