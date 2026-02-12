//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstring>
#include <stdexcept>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/Bindless/Generated.RootSignature.h>
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
#include <Oxygen/Renderer/Internal/EnvironmentStaticDataManager.h>
#include <Oxygen/Renderer/Internal/IblManager.h>
#include <Oxygen/Renderer/Passes/IblComputePass.h>
#include <Oxygen/Renderer/RenderContext.h>
#include <Oxygen/Renderer/Renderer.h>

// Implementation of IblPassTagFactory. Provides access to IblPassTag capability
// tokens, only from the IblComputePass. When building tests, allow tests to
// override by defining OXYGEN_ENGINE_TESTING.
#if !defined(OXYGEN_ENGINE_TESTING)
namespace oxygen::engine::internal {
auto IblPassTagFactory::Get() noexcept -> IblPassTag { return IblPassTag {}; }
} // namespace oxygen::engine::internal
#endif

namespace {

constexpr uint32_t kThreadGroupSize = 8;

} // namespace

namespace oxygen::engine {

IblComputePass::IblComputePass(std::string name)
  : RenderPass(std::move(name))
{
}

IblComputePass::~IblComputePass()
{
  if (pass_constants_buffer_) {
    // Ensure the buffer is unmapped before destruction to avoid backend
    // validation errors.
    // TODO: Consider wrapping mapped buffers in an RAII helper (e.g.
    // ScopedBufferType).
    if (pass_constants_mapped_) {
      pass_constants_buffer_->UnMap();
      pass_constants_mapped_ = nullptr;
    }
  }
}

auto IblComputePass::RequestRegenerationOnce() noexcept -> void
{
  regeneration_requested_.store(true, std::memory_order_release);
}

auto IblComputePass::DoExecute(graphics::CommandRecorder& recorder) -> co::Co<>
{
  LOG_SCOPE_FUNCTION(2);

  const auto env_manager
    = Context().GetRenderer().GetEnvironmentStaticDataManager();
  const auto view_id = Context().current_view.view_id;
  if (!env_manager) {
    if (!logged_missing_env_manager_) {
      LOG_F(WARNING,
        "IblComputePass: EnvironmentStaticDataManager unavailable; skipping");
      logged_missing_env_manager_ = true;
    }
    co_return;
  }
  logged_missing_env_manager_ = false;

  auto ibl_manager = Context().GetRenderer().GetIblManager();
  if (!ibl_manager) {
    if (!logged_missing_ibl_manager_) {
      LOG_F(WARNING, "IblComputePass: IblManager unavailable; skipping");
      logged_missing_ibl_manager_ = true;
    }
    co_return;
  }
  logged_missing_ibl_manager_ = false;

  const auto source_slot = ResolveSourceCubemapSlot();
  if (source_slot == kInvalidShaderVisibleIndex) {
    if (!logged_missing_source_slot_) {
      const auto sky_light_slot = env_manager->GetSkyLightCubemapSlot(view_id);
      const auto sky_sphere_slot = env_manager->GetSkySphereCubemapSlot(view_id);
      const auto env_static_srv
        = env_manager->GetSrvIndex(Context().current_view.view_id);
      LOG_F(WARNING,
        "IblComputePass: No environment cubemap source slot (frame_slot={} "
        "frame_seq={} "
        "SkyLight={} SkySphere={} EnvStaticSRV={} ExplicitSourceValid={} "
        "ExplicitSource={}); IBL will be black",
        Context().frame_slot.get(), Context().frame_sequence.get(),
        sky_light_slot.get(), sky_sphere_slot.get(), env_static_srv.get(),
        explicit_source_slot_.IsValid(), explicit_source_slot_.get());
      logged_missing_source_slot_ = true;
    }
    co_return;
  }
  logged_missing_source_slot_ = false;

  if (!ibl_manager->EnsureResourcesCreatedForView(view_id)) {
    LOG_F(WARNING, "IblComputePass: Failed to ensure IBL resources");
    co_return;
  }

  const bool regeneration_requested
    = regeneration_requested_.load(std::memory_order_acquire);

  const auto current_outputs
    = ibl_manager->QueryOutputsFor(view_id, source_slot);
  if (current_outputs.irradiance.IsValid()
    && current_outputs.prefilter.IsValid() && !regeneration_requested) {
    co_return;
  }

  LOG_F(INFO,
    "IblComputePass: Regenerating IBL (frame_slot={} frame_seq={} env_srv={} "
    "source={})",
    Context().frame_slot.get(), Context().frame_sequence.get(),
    env_manager->GetSrvIndex(view_id).get(), source_slot.get());

  LOG_F(2, "IblComputePass: targets (irr_srv={}, pref_srv={})",
    current_outputs.irradiance.get(), current_outputs.prefilter.get());

  EnsurePassConstantsBuffer();

  EnsurePipelineStateDescs();

  if (!irradiance_pso_desc_ || !prefilter_pso_desc_) {
    LOG_F(WARNING, "IblComputePass: missing PSO desc(s); skipping");
    co_return;
  }
  if (!pass_constants_buffer_ || !pass_constants_mapped_
    || pass_constants_srv_index_ == kInvalidShaderVisibleIndex) {
    LOG_F(WARNING, "IblComputePass: missing pass constants; skipping");
    co_return;
  }

  DCHECK_NOTNULL_F(Context().scene_constants);

  // Intensity is applied at shading time via EnvironmentStaticData (e.g.
  // `env_data.sky_light.radiance_scale`). Keep the filtered IBL maps in the
  // same scale as the source cubemap to avoid requiring regeneration when
  // artists tweak intensity.
  constexpr float source_intensity = 1.0F;

  DispatchIrradiance(
    recorder, *ibl_manager, view_id, source_slot, source_intensity);
  DispatchPrefilter(
    recorder, *ibl_manager, view_id, source_slot, source_intensity);

  recorder.FlushBarriers();
  std::uint64_t source_content_version = 0ULL;
  const auto view_sky_light_slot = env_manager->GetSkyLightCubemapSlot(view_id);
  if (env_manager->IsSkyLightCapturedSceneSource(view_id)
    && source_slot == view_sky_light_slot) {
    source_content_version = env_manager->GetSkyCaptureGeneration(view_id);
    if (source_content_version == 0ULL) {
      LOG_F(ERROR,
        "IblComputePass: captured-scene IBL regeneration has zero source content "
        "version (view={} source={})",
        view_id.get(), source_slot.get());
    }
  }

  auto tag = oxygen::engine::internal::IblPassTagFactory::Get();
  ibl_manager->MarkGenerated(
    tag, view_id, source_slot, source_content_version);

  if (regeneration_requested) {
    regeneration_requested_.store(false, std::memory_order_release);
  }

  const auto final_outputs = ibl_manager->QueryOutputsFor(view_id, source_slot);
  LOG_F(INFO,
    "IblComputePass: IBL generated (source={}, irr_srv={}, pref_srv={})",
    source_slot.get(), final_outputs.irradiance.get(),
    final_outputs.prefilter.get());

  co_return;
}

auto IblComputePass::EnsurePassConstantsBuffer() -> void
{
  if (pass_constants_buffer_) {
    return;
  }

  auto& gfx = Context().GetGraphics();
  auto& registry = gfx.GetResourceRegistry();
  auto& allocator = gfx.GetDescriptorAllocator();

  constexpr uint32_t kStrideBytes
    = static_cast<uint32_t>(sizeof(IblFilteringPassConstants));
  static_assert(kStrideBytes % 16U == 0U, "Stride must be 16-byte aligned");

  const graphics::BufferDesc desc {
    .size_bytes = static_cast<uint64_t>(kMaxDispatches) * kStrideBytes,
    .usage = graphics::BufferUsage::kNone,
    .memory = graphics::BufferMemory::kUpload,
    .debug_name = std::string(GetName()) + "_IblPassConstants",
  };

  pass_constants_buffer_ = gfx.CreateBuffer(desc);
  if (!pass_constants_buffer_) {
    throw std::runtime_error("IblComputePass: failed to create constants");
  }
  pass_constants_buffer_->SetName(desc.debug_name);

  pass_constants_mapped_ = pass_constants_buffer_->Map(0, desc.size_bytes);
  if (!pass_constants_mapped_) {
    throw std::runtime_error("IblComputePass: failed to map constants");
  }

  graphics::BufferViewDescription srv_view_desc;
  srv_view_desc.view_type = graphics::ResourceViewType::kStructuredBuffer_SRV;
  srv_view_desc.visibility = graphics::DescriptorVisibility::kShaderVisible;
  srv_view_desc.range = { 0u, desc.size_bytes };
  srv_view_desc.stride = kStrideBytes;

  auto srv_handle
    = allocator.Allocate(graphics::ResourceViewType::kStructuredBuffer_SRV,
      graphics::DescriptorVisibility::kShaderVisible);
  if (!srv_handle.IsValid()) {
    throw std::runtime_error("IblComputePass: failed to allocate SRV");
  }
  pass_constants_srv_index_ = allocator.GetShaderVisibleIndex(srv_handle);

  registry.Register(pass_constants_buffer_);
  registry.RegisterView(
    *pass_constants_buffer_, std::move(srv_handle), srv_view_desc);
}

auto IblComputePass::EnsurePipelineStateDescs() -> void
{
  if (irradiance_pso_desc_ && prefilter_pso_desc_) {
    return;
  }

  auto root_bindings = RenderPass::BuildRootBindings();
  const auto bindings = std::span<const graphics::RootBindingItem>(
    root_bindings.data(), root_bindings.size());

  graphics::ShaderRequest irradiance_shader {
    .stage = oxygen::ShaderType::kCompute,
    .source_path = "Lighting/IblFiltering.hlsl",
    .entry_point = "CS_IrradianceConvolution",
  };

  irradiance_pso_desc_ = graphics::ComputePipelineDesc::Builder()
                           .SetComputeShader(std::move(irradiance_shader))
                           .SetRootBindings(bindings)
                           .SetDebugName("IBL_Irradiance_PSO")
                           .Build();

  graphics::ShaderRequest prefilter_shader {
    .stage = oxygen::ShaderType::kCompute,
    .source_path = "Lighting/IblFiltering.hlsl",
    .entry_point = "CS_SpecularPrefilter",
  };

  prefilter_pso_desc_ = graphics::ComputePipelineDesc::Builder()
                          .SetComputeShader(std::move(prefilter_shader))
                          .SetRootBindings(bindings)
                          .SetDebugName("IBL_Prefilter_PSO")
                          .Build();
}

auto IblComputePass::ResolveSourceCubemapSlot() const noexcept
  -> ShaderVisibleIndex
{
  if (explicit_source_slot_.IsValid()) {
    return explicit_source_slot_;
  }

  const auto env_manager
    = Context().GetRenderer().GetEnvironmentStaticDataManager();
  if (!env_manager) {
    return kInvalidShaderVisibleIndex;
  }

  const auto sky_light_slot
    = env_manager->GetSkyLightCubemapSlot(Context().current_view.view_id);
  if (sky_light_slot.IsValid()) {
    return sky_light_slot;
  }

  const auto sky_sphere_slot
    = env_manager->GetSkySphereCubemapSlot(Context().current_view.view_id);
  if (sky_sphere_slot.IsValid()) {
    return sky_sphere_slot;
  }

  return kInvalidShaderVisibleIndex;
}

auto IblComputePass::DispatchIrradiance(graphics::CommandRecorder& recorder,
  internal::IblManager& ibl, const ViewId view_id,
  const ShaderVisibleIndex source_slot, const float source_intensity) -> void
{
  auto tag = oxygen::engine::internal::IblPassTagFactory::Get();

  auto target = ibl.GetIrradianceMap(tag, view_id);
  if (!target) {
    LOG_F(WARNING, "IblComputePass: irradiance target texture missing");
    return;
  }

  const auto uav_slot = ibl.GetIrradianceMapUavSlot(tag, view_id);
  if (uav_slot == kInvalidShaderVisibleIndex) {
    LOG_F(WARNING, "IblComputePass: irradiance UAV slot missing");
    return;
  }

  // Resource state tracking is per CommandRecorder. Ensure the irradiance
  // texture is being tracked before requesting transitions.
  {
    const auto initial_state = irradiance_in_shader_resource_state_
      ? graphics::ResourceStates::kShaderResource
      : graphics::ResourceStates::kUnorderedAccess;

    if (!recorder.IsResourceTracked(*target)) {
      recorder.BeginTrackingResourceState(*target, initial_state, false);
    }
    recorder.EnableAutoMemoryBarriers(*target);
  }

  recorder.RequireResourceState(
    *target, graphics::ResourceStates::kUnorderedAccess);
  recorder.FlushBarriers();

  IblFilteringPassConstants constants {
    .source_cubemap_slot = source_slot,
    .target_uav_slot = uav_slot,
    .roughness = 0.0F,
    .face_size = ibl.GetConfig().irradiance_size,
    .source_intensity = source_intensity,
  };
  const uint32_t constants_index = 0U;
  auto* constants_dst = static_cast<std::byte*>(pass_constants_mapped_)
    + (static_cast<std::size_t>(constants_index) * sizeof(constants));
  std::memcpy(constants_dst, &constants, sizeof(constants));

  LOG_F(2,
    "IblComputePass: irradiance dispatch src={}, uav={}, face_size={}, "
    "groups={}",
    constants.source_cubemap_slot, constants.target_uav_slot,
    constants.face_size,
    (constants.face_size + kThreadGroupSize - 1) / kThreadGroupSize);

  recorder.SetPipelineState(*irradiance_pso_desc_);
  recorder.SetComputeRootConstantBufferView(
    static_cast<uint32_t>(binding::RootParam::kSceneConstants),
    Context().scene_constants->GetGPUVirtualAddress());
  recorder.SetComputeRoot32BitConstant(
    static_cast<uint32_t>(binding::RootParam::kRootConstants), constants_index,
    0);
  recorder.SetComputeRoot32BitConstant(
    static_cast<uint32_t>(binding::RootParam::kRootConstants),
    pass_constants_srv_index_.get(), 1);

  const uint32_t groups
    = (constants.face_size + kThreadGroupSize - 1) / kThreadGroupSize;
  recorder.Dispatch(groups, groups, 6);

  recorder.RequireResourceState(
    *target, graphics::ResourceStates::kShaderResource);

  irradiance_in_shader_resource_state_ = true;
}

auto IblComputePass::DispatchPrefilter(graphics::CommandRecorder& recorder,
  internal::IblManager& ibl, const ViewId view_id,
  const ShaderVisibleIndex source_slot, const float source_intensity) -> void
{
  auto tag = oxygen::engine::internal::IblPassTagFactory::Get();

  auto target = ibl.GetPrefilterMap(tag, view_id);
  if (!target) {
    LOG_F(WARNING, "IblComputePass: prefilter target texture missing");
    return;
  }

  // Resource state tracking is per CommandRecorder. Ensure the prefilter
  // texture is being tracked before requesting transitions.
  {
    const auto initial_state = prefilter_in_shader_resource_state_
      ? graphics::ResourceStates::kShaderResource
      : graphics::ResourceStates::kUnorderedAccess;

    if (!recorder.IsResourceTracked(*target)) {
      recorder.BeginTrackingResourceState(*target, initial_state, false);
    }
    recorder.EnableAutoMemoryBarriers(*target);
  }

  recorder.RequireResourceState(
    *target, graphics::ResourceStates::kUnorderedAccess);
  recorder.FlushBarriers();

  recorder.SetPipelineState(*prefilter_pso_desc_);
  recorder.SetComputeRootConstantBufferView(
    static_cast<uint32_t>(binding::RootParam::kSceneConstants),
    Context().scene_constants->GetGPUVirtualAddress());
  recorder.SetComputeRoot32BitConstant(
    static_cast<uint32_t>(binding::RootParam::kRootConstants), 0U, 0);
  recorder.SetComputeRoot32BitConstant(
    static_cast<uint32_t>(binding::RootParam::kRootConstants),
    pass_constants_srv_index_.get(), 1);

  const uint32_t mips = target->GetDescriptor().mip_levels;
  const uint32_t base_size = target->GetDescriptor().width;

  LOG_F(2, "IblComputePass: prefilter dispatch src={}, mips={}, base_size={}",
    source_slot.get(), mips, base_size);

  const uint32_t constants_base = 1U;
  // Compute how many mips we can fit into the remaining slots after the
  // irradiance pass (index 0).
  const uint32_t max_mips = (kMaxDispatches > constants_base)
    ? (kMaxDispatches - constants_base)
    : 0U;
  const uint32_t safe_mips = (std::min)(mips, max_mips);

  for (uint32_t mip = 0; mip < safe_mips; ++mip) {
    const uint32_t mip_size = (std::max)(1U, base_size >> mip);
    const float roughness = (mips > 1U)
      ? static_cast<float>(mip) / static_cast<float>(mips - 1U)
      : 0.0F;

    const auto uav_slot = ibl.GetPrefilterMapUavSlot(tag, view_id, mip);
    if (uav_slot == kInvalidShaderVisibleIndex) {
      LOG_F(
        WARNING, "IblComputePass: prefilter UAV slot missing for mip {}", mip);
      continue;
    }

    IblFilteringPassConstants constants {
      .source_cubemap_slot = source_slot,
      .target_uav_slot = uav_slot,
      .roughness = roughness,
      .face_size = mip_size,
      .source_intensity = source_intensity,
    };
    const uint32_t constants_index = constants_base + mip;
    auto* constants_dst = static_cast<std::byte*>(pass_constants_mapped_)
      + (static_cast<std::size_t>(constants_index) * sizeof(constants));
    std::memcpy(constants_dst, &constants, sizeof(constants));

    recorder.SetComputeRoot32BitConstant(
      static_cast<uint32_t>(binding::RootParam::kRootConstants),
      constants_index, 0);

    const uint32_t groups
      = (mip_size + kThreadGroupSize - 1) / kThreadGroupSize;
    recorder.Dispatch(groups, groups, 6);
  }

  if (safe_mips != mips) {
    LOG_F(WARNING,
      "IblComputePass: prefilter mip dispatch clamped (mips={}, dispatched={})",
      mips, safe_mips);
  }

  recorder.RequireResourceState(
    *target, graphics::ResourceStates::kShaderResource);

  prefilter_in_shader_resource_state_ = true;
}

} // namespace oxygen::engine
