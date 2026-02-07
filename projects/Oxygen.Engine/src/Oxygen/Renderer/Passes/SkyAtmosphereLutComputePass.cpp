//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <utility>

#include <fmt/format.h>
#include <glm/glm.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/Bindless/Generated.RootSignature.h>
#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Types/ShaderType.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/PipelineState.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Graphics/Common/Types/ResourceViewType.h>
#include <Oxygen/Renderer/Internal/EnvironmentDynamicDataManager.h>
#include <Oxygen/Renderer/Internal/SkyAtmosphereLutManager.h>
#include <Oxygen/Renderer/Passes/SkyAtmosphereLutComputePass.h>
#include <Oxygen/Renderer/RenderContext.h>

using oxygen::kInvalidShaderVisibleIndex;
using oxygen::ShaderVisibleIndex;
using oxygen::engine::SkyAtmosphereLutComputePass;
using oxygen::engine::SkyAtmosphereLutComputePassConfig;
using oxygen::graphics::Buffer;
using oxygen::graphics::CommandRecorder;
using oxygen::graphics::ComputePipelineDesc;
using oxygen::graphics::ResourceViewType;

namespace {

//! Pass constants for transmittance LUT generation.
/*!
 Layout must match `TransmittanceLutPassConstants` in TransmittanceLut_CS.hlsl.
*/
struct alignas(16) TransmittanceLutPassConstants {
  ShaderVisibleIndex output_uav_index { kInvalidShaderVisibleIndex };
  uint32_t output_width { 0 };
  uint32_t output_height { 0 };
  uint32_t _pad0 { 0 };
};
static_assert(sizeof(TransmittanceLutPassConstants) == 16,
  "TransmittanceLutPassConstants must be 16 bytes");

//! Pass constants for sky-view LUT generation.
/*!
 Layout must match `SkyViewLutPassConstants` in SkyViewLut_CS.hlsl.
 Camera altitude is no longer passed — each slice computes its own
 altitude from slice_index, slice_count, atmosphere_height_m, and
 alt_mapping_mode inside the shader [P6].
*/
struct alignas(16) SkyViewLutPassConstants {
  ShaderVisibleIndex output_uav_index { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex transmittance_srv_index { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex multi_scat_srv_index { kInvalidShaderVisibleIndex };
  uint32_t output_width { 0 };

  uint32_t output_height { 0 };
  uint32_t transmittance_width { 0 };
  uint32_t transmittance_height { 0 };
  uint32_t slice_count { 0 };

  float sun_cos_zenith { 0.0F };
  uint32_t atmosphere_flags { 0 };
  uint32_t alt_mapping_mode { 0 };
  float atmosphere_height_m { 0.0F };

  float planet_radius_m { 0.0F };
  uint32_t _pad0 { 0 };
  uint32_t _pad1 { 0 };
  uint32_t _pad2 { 0 };
};
static_assert(sizeof(SkyViewLutPassConstants) == 64,
  "SkyViewLutPassConstants must be 64 bytes");

//! Pass constants for Multiple Scattering LUT generation.
struct alignas(16) MultiScatLutPassConstants {
  ShaderVisibleIndex output_uav_index { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex transmittance_srv_index { kInvalidShaderVisibleIndex };
  uint32_t output_width { 0 };
  uint32_t output_height { 0 };

  uint32_t transmittance_width { 0 };
  uint32_t transmittance_height { 0 };
  float atmosphere_height_m { 0.0F };
  float planet_radius_m { 0.0F };
};
static_assert(sizeof(MultiScatLutPassConstants) == 32,
  "MultiScatLutPassConstants must be 32 bytes");

//! Pass constants for camera volume LUT generation.
/*!
 Layout must match `CameraVolumeLutPassConstants` in CameraVolumeLut_CS.hlsl.
*/
struct alignas(16) CameraVolumeLutPassConstants {
  ShaderVisibleIndex output_uav_index { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex transmittance_srv_index { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex multi_scat_srv_index { kInvalidShaderVisibleIndex };
  uint32_t output_width { 0 };

  uint32_t output_height { 0 };
  uint32_t output_depth { 0 };
  uint32_t transmittance_width { 0 };
  uint32_t transmittance_height { 0 };

  float max_distance_km { 0.0F };
  float sun_cos_zenith { 0.0F };
  uint32_t atmosphere_flags { 0 };
  uint32_t _pad0 { 0 };

  glm::mat4 inv_projection_matrix { 1.0F };
  glm::mat4 inv_view_matrix { 1.0F };
};
static_assert(sizeof(CameraVolumeLutPassConstants) == 176,
  "CameraVolumeLutPassConstants must be 176 bytes");

// Thread group size must match HLSL shaders
constexpr uint32_t kThreadGroupSizeX = 8;
constexpr uint32_t kThreadGroupSizeY = 8;

// Constant buffer alignment for D3D12/Vulkan
constexpr uint32_t kConstantBufferAlignment = 256u;

} // namespace

namespace oxygen::engine {

//=== Implementation Details ===----------------------------------------------//

struct SkyAtmosphereLutComputePass::Impl {
  observer_ptr<Graphics> gfx;
  std::shared_ptr<Config> config;
  std::string name;

  // Pass constants buffers (one for each shader)
  std::shared_ptr<Buffer> transmittance_constants_buffer;
  std::shared_ptr<Buffer> multi_scat_constants_buffer;
  std::shared_ptr<Buffer> sky_view_constants_buffer;
  std::shared_ptr<Buffer> camera_volume_constants_buffer;

  void* transmittance_constants_mapped { nullptr };
  void* multi_scat_constants_mapped { nullptr };
  void* sky_view_constants_mapped { nullptr };
  void* camera_volume_constants_mapped { nullptr };

  ShaderVisibleIndex transmittance_constants_cbv_index {
    kInvalidShaderVisibleIndex
  };
  ShaderVisibleIndex multi_scat_constants_cbv_index {
    kInvalidShaderVisibleIndex
  };
  ShaderVisibleIndex sky_view_constants_cbv_index {
    kInvalidShaderVisibleIndex
  };
  ShaderVisibleIndex camera_volume_constants_cbv_index {
    kInvalidShaderVisibleIndex
  };

  // Pipeline state descriptions (cached for rebuild detection)
  std::optional<ComputePipelineDesc> transmittance_pso_desc;
  std::optional<ComputePipelineDesc> multi_scat_pso_desc;
  std::optional<ComputePipelineDesc> sky_view_pso_desc;
  std::optional<ComputePipelineDesc> camera_volume_pso_desc;

  // Track if we've ever built the PSOs
  bool pso_built { false };

  Impl(observer_ptr<Graphics> gfx_in, std::shared_ptr<Config> config_in)
    : gfx(gfx_in)
    , config(std::move(config_in))
    , name(this->config != nullptr ? this->config->debug_name
                                   : "SkyAtmosphereLutComputePass")
  {
  }

  ~Impl()
  {
    if (transmittance_constants_buffer && transmittance_constants_mapped) {
      transmittance_constants_buffer->UnMap();
      transmittance_constants_mapped = nullptr;
    }
    if (multi_scat_constants_buffer && multi_scat_constants_mapped) {
      multi_scat_constants_buffer->UnMap();
      multi_scat_constants_mapped = nullptr;
    }
    if (sky_view_constants_buffer && sky_view_constants_mapped) {
      sky_view_constants_buffer->UnMap();
      sky_view_constants_mapped = nullptr;
    }
    if (camera_volume_constants_buffer && camera_volume_constants_mapped) {
      camera_volume_constants_buffer->UnMap();
      camera_volume_constants_mapped = nullptr;
    }
  }

  //! Ensures the pass constants buffers are created.
  auto EnsurePassConstantsBuffers() -> void
  {
    if (transmittance_constants_buffer && multi_scat_constants_buffer
      && sky_view_constants_buffer) {
      return;
    }

    auto& registry = gfx->GetResourceRegistry();
    auto& allocator = gfx->GetDescriptorAllocator();

    auto create_cbv = [&](const std::string& buffer_name,
                        std::shared_ptr<Buffer>& buffer, void*& mapped,
                        ShaderVisibleIndex& cbv_index) {
      const graphics::BufferDesc desc {
        .size_bytes = kConstantBufferAlignment,
        .usage = graphics::BufferUsage::kConstant,
        .memory = graphics::BufferMemory::kUpload,
        .debug_name = name + "_" + buffer_name,
      };

      buffer = gfx->CreateBuffer(desc);
      if (!buffer) {
        throw std::runtime_error(
          "SkyAtmosphereLutComputePass: Failed to create " + buffer_name
          + " buffer");
      }
      buffer->SetName(desc.debug_name);

      mapped = buffer->Map(0, desc.size_bytes);
      if (!mapped) {
        throw std::runtime_error("SkyAtmosphereLutComputePass: Failed to map "
          + buffer_name + " buffer");
      }

      graphics::BufferViewDescription cbv_view_desc;
      cbv_view_desc.view_type = ResourceViewType::kConstantBuffer;
      cbv_view_desc.visibility = graphics::DescriptorVisibility::kShaderVisible;
      cbv_view_desc.range = { 0u, desc.size_bytes };

      auto cbv_handle = allocator.Allocate(ResourceViewType::kConstantBuffer,
        graphics::DescriptorVisibility::kShaderVisible);
      if (!cbv_handle.IsValid()) {
        throw std::runtime_error("SkyAtmosphereLutComputePass: Failed to "
                                 "allocate CBV descriptor for "
          + buffer_name);
      }
      cbv_index = allocator.GetShaderVisibleIndex(cbv_handle);

      registry.Register(buffer);
      registry.RegisterView(*buffer, std::move(cbv_handle), cbv_view_desc);
    };

    create_cbv("TransmittanceConstants", transmittance_constants_buffer,
      transmittance_constants_mapped, transmittance_constants_cbv_index);
    create_cbv("MultiScatConstants", multi_scat_constants_buffer,
      multi_scat_constants_mapped, multi_scat_constants_cbv_index);
    create_cbv("SkyViewConstants", sky_view_constants_buffer,
      sky_view_constants_mapped, sky_view_constants_cbv_index);
    create_cbv("CameraVolumeConstants", camera_volume_constants_buffer,
      camera_volume_constants_mapped, camera_volume_constants_cbv_index);

    LOG_F(1, "SkyAtmosphereLutComputePass: Created pass constants buffers");
  }

  //! Build pipeline state descriptions for all shaders.
  auto BuildPipelineStateDescs() -> void
  {
    auto root_bindings = RenderPass::BuildRootBindings();
    const auto bindings = std::span<const graphics::RootBindingItem>(
      root_bindings.data(), root_bindings.size());

    // Transmittance LUT compute shader
    graphics::ShaderRequest transmittance_shader {
      .stage = oxygen::ShaderType::kCompute,
      .source_path = "Atmosphere/TransmittanceLut_CS.hlsl",
      .entry_point = "CS",
    };

    transmittance_pso_desc
      = ComputePipelineDesc::Builder()
          .SetComputeShader(std::move(transmittance_shader))
          .SetRootBindings(bindings)
          .SetDebugName("SkyAtmo_TransmittanceLUT_PSO")
          .Build();

    // MultiScat LUT compute shader
    graphics::ShaderRequest multi_scat_shader {
      .stage = oxygen::ShaderType::kCompute,
      .source_path = "Atmosphere/MultiScatLut_CS.hlsl",
      .entry_point = "CS",
    };

    multi_scat_pso_desc = ComputePipelineDesc::Builder()
                            .SetComputeShader(std::move(multi_scat_shader))
                            .SetRootBindings(bindings)
                            .SetDebugName("SkyAtmo_MultiScatLUT_PSO")
                            .Build();

    // Sky-view LUT compute shader
    graphics::ShaderRequest sky_view_shader {
      .stage = oxygen::ShaderType::kCompute,
      .source_path = "Atmosphere/SkyViewLut_CS.hlsl",
      .entry_point = "CS",
    };

    sky_view_pso_desc = ComputePipelineDesc::Builder()
                          .SetComputeShader(std::move(sky_view_shader))
                          .SetRootBindings(bindings)
                          .SetDebugName("SkyAtmo_SkyViewLUT_PSO")
                          .Build();

    // Camera Volume LUT compute shader
    graphics::ShaderRequest camera_volume_shader {
      .stage = oxygen::ShaderType::kCompute,
      .source_path = "Atmosphere/CameraVolumeLut_CS.hlsl",
      .entry_point = "CS",
    };

    camera_volume_pso_desc
      = ComputePipelineDesc::Builder()
          .SetComputeShader(std::move(camera_volume_shader))
          .SetRootBindings(bindings)
          .SetDebugName("SkyAtmo_CameraVolumeLUT_PSO")
          .Build();

    pso_built = true;

    LOG_F(INFO, "SkyAtmosphereLutComputePass: Built compute pipeline states");
  }
};

//=== Constructor/Destructor ===----------------------------------------------//

SkyAtmosphereLutComputePass::SkyAtmosphereLutComputePass(
  observer_ptr<Graphics> gfx, std::shared_ptr<Config> config)
  : ComputeRenderPass(
      config ? config->debug_name : "SkyAtmosphereLutComputePass")
  , impl_(std::make_unique<Impl>(gfx, std::move(config)))
{
}

SkyAtmosphereLutComputePass::~SkyAtmosphereLutComputePass() = default;

//=== DoPrepareResources ===--------------------------------------------------//

auto SkyAtmosphereLutComputePass::DoPrepareResources(CommandRecorder& recorder)
  -> co::Co<>
{
  auto* manager = impl_->config->lut_manager.get();

  // Skip if LUTs are up-to-date
  if (!manager->IsDirty()) {
    static bool logged_skip = false;
    if (!logged_skip) {
      LOG_F(WARNING,
        "SkyAtmosphereLutComputePass: LUTs not dirty; skipping sky-view "
        "raymarch dispatch (GPU debug lines will not be emitted).");
      logged_skip = true;
    }
    co_return;
  }

  // Ensure LUT textures exist
  if (!manager->EnsureResourcesCreated()) {
    LOG_F(ERROR, "SkyAtmosphereLutComputePass: Failed to create LUT resources");
    co_return;
  }

  // Ensure pass constants buffers exist
  impl_->EnsurePassConstantsBuffers();

  // Ensure PSOs are built
  if (!impl_->pso_built) {
    impl_->BuildPipelineStateDescs();
  }

  // Get textures for barrier setup
  auto* transmittance_tex = manager->GetTransmittanceLutTexture().get();
  auto* sky_view_tex = manager->GetSkyViewLutTexture().get();
  auto* multi_scat_tex = manager->GetMultiScatLutTexture().get();
  auto* camera_volume_tex = manager->GetCameraVolumeLutTexture().get();

  if (!transmittance_tex || !sky_view_tex || !multi_scat_tex
    || !camera_volume_tex) {
    LOG_F(ERROR, "SkyAtmosphereLutComputePass: LUT textures not available");
    co_return;
  }

  // Transition LUTs to UAV state for compute shader write.
  const auto initial_state = manager->HasBeenGenerated()
    ? graphics::ResourceStates::kShaderResource
    : graphics::ResourceStates::kUnorderedAccess;

  recorder.BeginTrackingResourceState(*transmittance_tex, initial_state, false);
  recorder.BeginTrackingResourceState(*sky_view_tex, initial_state, false);
  recorder.BeginTrackingResourceState(*multi_scat_tex, initial_state, false);
  recorder.BeginTrackingResourceState(*camera_volume_tex, initial_state, false);

  // Enable automatic UAV memory barriers for proper UAV-to-UAV sync
  recorder.EnableAutoMemoryBarriers(*transmittance_tex);
  recorder.EnableAutoMemoryBarriers(*sky_view_tex);
  recorder.EnableAutoMemoryBarriers(*multi_scat_tex);
  recorder.EnableAutoMemoryBarriers(*camera_volume_tex);

  recorder.RequireResourceState(
    *transmittance_tex, graphics::ResourceStates::kUnorderedAccess);
  recorder.RequireResourceState(
    *sky_view_tex, graphics::ResourceStates::kUnorderedAccess);
  recorder.RequireResourceState(
    *multi_scat_tex, graphics::ResourceStates::kUnorderedAccess);
  recorder.RequireResourceState(
    *camera_volume_tex, graphics::ResourceStates::kUnorderedAccess);

  recorder.FlushBarriers();

  co_return;
}

//=== DoExecute ===-----------------------------------------------------------//

/*!
 Executes LUT generation shaders in order:
 1. Transmittance LUT - optical depth integration
 2. MultiScat LUT - integral over directions (requires transmittance)
 3. Sky-view LUT - raymarch (requires transmittance and multi-scat)
*/
auto SkyAtmosphereLutComputePass::DoExecute(CommandRecorder& recorder)
  -> co::Co<>
{
  auto* manager = impl_->config->lut_manager.get();

  // Skip if LUTs are up-to-date
  if (!manager->IsDirty()) {
    co_return;
  }

  // Verify resources are ready
  if (!impl_->transmittance_pso_desc.has_value()
    || !impl_->multi_scat_pso_desc.has_value()
    || !impl_->sky_view_pso_desc.has_value()
    || !impl_->camera_volume_pso_desc.has_value()) {
    LOG_F(WARNING, "SkyAtmosphereLutComputePass: PSOs not built, skipping");
    co_return;
  }

  const auto transmittance_uav = manager->GetTransmittanceLutUavSlot();
  const auto multi_scat_uav = manager->GetMultiScatLutUavSlot();
  const auto sky_view_uav = manager->GetSkyViewLutUavSlot();
  const auto camera_volume_uav = manager->GetCameraVolumeLutUavSlot();

  const auto transmittance_srv = manager->GetTransmittanceLutSlot();
  const auto multi_scat_srv = manager->GetMultiScatLutSlot();

  if (transmittance_uav == kInvalidShaderVisibleIndex
    || multi_scat_uav == kInvalidShaderVisibleIndex
    || sky_view_uav == kInvalidShaderVisibleIndex
    || camera_volume_uav == kInvalidShaderVisibleIndex
    || transmittance_srv == kInvalidShaderVisibleIndex
    || multi_scat_srv == kInvalidShaderVisibleIndex) {
    LOG_F(WARNING,
      "SkyAtmosphereLutComputePass: UAV/SRV indices not valid, skipping");
    co_return;
  }

  const auto [transmittance_width, transmittance_height]
    = manager->GetTransmittanceLutSize();
  const auto [multi_scat_width, multi_scat_height]
    = manager->GetMultiScatLutSize();
  const auto [sky_view_width, sky_view_height] = manager->GetSkyViewLutSize();
  const auto [camera_volume_width, camera_volume_height, camera_volume_depth]
    = manager->GetCameraVolumeLutSize();

  const float planet_radius_m = manager->GetPlanetRadiusMeters();
  const float atmosphere_height_m = manager->GetAtmosphereHeightMeters();
  const uint32_t sky_view_slices = manager->GetSkyViewLutSlices();
  const uint32_t alt_mapping_mode = manager->GetAltMappingMode();

  DCHECK_NOTNULL_F(Context().scene_constants);
  const auto scene_const_addr
    = Context().scene_constants->GetGPUVirtualAddress();
  std::optional<uint64_t> env_dynamic_addr;
  if (const auto env_manager = Context().env_dynamic_manager) {
    const auto view_id = Context().current_view.view_id;
    env_manager->UpdateIfNeeded(view_id);
    const auto addr = env_manager->GetGpuVirtualAddress(view_id);
    if (addr != 0) {
      env_dynamic_addr = addr;
    }
  }

  //=== Dispatch 1: Transmittance LUT ===-------------------------------------//
  {
    TransmittanceLutPassConstants constants {
      .output_uav_index = transmittance_uav,
      .output_width = transmittance_width,
      .output_height = transmittance_height,
    };
    std::memcpy(
      impl_->transmittance_constants_mapped, &constants, sizeof(constants));

    recorder.SetPipelineState(*impl_->transmittance_pso_desc);
    recorder.SetComputeRootConstantBufferView(
      static_cast<uint32_t>(binding::RootParam::kSceneConstants),
      scene_const_addr);
    if (env_dynamic_addr.has_value()) {
      recorder.SetComputeRootConstantBufferView(
        static_cast<uint32_t>(binding::RootParam::kEnvironmentDynamicData),
        *env_dynamic_addr);
    }

    recorder.SetComputeRoot32BitConstant(
      static_cast<uint32_t>(binding::RootParam::kRootConstants), 0U, 0);
    recorder.SetComputeRoot32BitConstant(
      static_cast<uint32_t>(binding::RootParam::kRootConstants),
      impl_->transmittance_constants_cbv_index.get(), 1);

    recorder.Dispatch(
      (transmittance_width + kThreadGroupSizeX - 1) / kThreadGroupSizeX,
      (transmittance_height + kThreadGroupSizeY - 1) / kThreadGroupSizeY, 1);
  }

  // Barrier: UAV -> SRV for transmittance
  recorder.RequireResourceState(*manager->GetTransmittanceLutTexture(),
    graphics::ResourceStates::kShaderResource);
  recorder.FlushBarriers();

  //=== Dispatch 2: MultiScat LUT ===-----------------------------------------//
  {
    MultiScatLutPassConstants constants {
      .output_uav_index = multi_scat_uav,
      .transmittance_srv_index = transmittance_srv,
      .output_width = multi_scat_width,
      .output_height = multi_scat_height,
      .transmittance_width = transmittance_width,
      .transmittance_height = transmittance_height,
      .atmosphere_height_m = 80000.0F, // TODO: Pull from atmo params
      .planet_radius_m = planet_radius_m,
    };
    std::memcpy(
      impl_->multi_scat_constants_mapped, &constants, sizeof(constants));

    recorder.SetPipelineState(*impl_->multi_scat_pso_desc);
    recorder.SetComputeRootConstantBufferView(
      static_cast<uint32_t>(binding::RootParam::kSceneConstants),
      scene_const_addr);
    if (env_dynamic_addr.has_value()) {
      recorder.SetComputeRootConstantBufferView(
        static_cast<uint32_t>(binding::RootParam::kEnvironmentDynamicData),
        *env_dynamic_addr);
    }

    recorder.SetComputeRoot32BitConstant(
      static_cast<uint32_t>(binding::RootParam::kRootConstants), 0U, 0);
    recorder.SetComputeRoot32BitConstant(
      static_cast<uint32_t>(binding::RootParam::kRootConstants),
      impl_->multi_scat_constants_cbv_index.get(), 1);

    recorder.Dispatch(
      (multi_scat_width + kThreadGroupSizeX - 1) / kThreadGroupSizeX,
      (multi_scat_height + kThreadGroupSizeY - 1) / kThreadGroupSizeY, 1);
  }

  // Barrier: UAV -> SRV for MultiScat
  recorder.RequireResourceState(*manager->GetMultiScatLutTexture(),
    graphics::ResourceStates::kShaderResource);
  recorder.FlushBarriers();

  //=== Dispatch 3: Sky-View LUT ===------------------------------------------//
  {
    SkyViewLutPassConstants constants {
      .output_uav_index = sky_view_uav,
      .transmittance_srv_index = transmittance_srv,
      .multi_scat_srv_index = multi_scat_srv,
      .output_width = sky_view_width,
      .output_height = sky_view_height,
      .transmittance_width = transmittance_width,
      .transmittance_height = transmittance_height,
      .slice_count = sky_view_slices,
      .sun_cos_zenith = manager->GetSunState().cos_zenith,
      .atmosphere_flags = manager->GetAtmosphereFlags(),
      .alt_mapping_mode = alt_mapping_mode,
      .atmosphere_height_m = atmosphere_height_m,
      .planet_radius_m = planet_radius_m,
    };
    std::memcpy(
      impl_->sky_view_constants_mapped, &constants, sizeof(constants));

    recorder.SetPipelineState(*impl_->sky_view_pso_desc);
    recorder.SetComputeRootConstantBufferView(
      static_cast<uint32_t>(binding::RootParam::kSceneConstants),
      scene_const_addr);
    if (env_dynamic_addr.has_value()) {
      recorder.SetComputeRootConstantBufferView(
        static_cast<uint32_t>(binding::RootParam::kEnvironmentDynamicData),
        *env_dynamic_addr);
    }

    recorder.SetComputeRoot32BitConstant(
      static_cast<uint32_t>(binding::RootParam::kRootConstants), 0U, 0);
    recorder.SetComputeRoot32BitConstant(
      static_cast<uint32_t>(binding::RootParam::kRootConstants),
      impl_->sky_view_constants_cbv_index.get(), 1);

    static bool logged_sky_view_dispatch = false;
    if (!logged_sky_view_dispatch) {
      LOG_F(WARNING,
        "SkyAtmosphereLutComputePass: sky-view dispatch (uav={}, trans_srv={}, "
        "multi_scat_srv={}, slices={})",
        sky_view_uav.get(), transmittance_srv.get(), multi_scat_srv.get(),
        sky_view_slices);
      logged_sky_view_dispatch = true;
    }

    // Dispatch Z = slices (not ceil(slices/8)) because numthreads.z == 1,
    // so dispatch_thread_id.z == slice index directly [P3].
    recorder.Dispatch(
      (sky_view_width + kThreadGroupSizeX - 1) / kThreadGroupSizeX,
      (sky_view_height + kThreadGroupSizeY - 1) / kThreadGroupSizeY,
      sky_view_slices);
  }

  // Final transition for Sky-View LUT
  recorder.RequireResourceState(*manager->GetSkyViewLutTexture(),
    graphics::ResourceStates::kShaderResource);
  recorder.FlushBarriers();

  //=== Dispatch 4: Camera Volume LUT ===-------------------------------------//
  {
    CameraVolumeLutPassConstants constants {
      .output_uav_index = camera_volume_uav,
      .transmittance_srv_index = transmittance_srv,
      .multi_scat_srv_index = multi_scat_srv,
      .output_width = camera_volume_width,
      .output_height = camera_volume_height,
      .output_depth = camera_volume_depth,
      .transmittance_width = transmittance_width,
      .transmittance_height = transmittance_height,
      .max_distance_km = 128.0F, // Matches common engine defaults
      .sun_cos_zenith = manager->GetSunState().cos_zenith,
      .atmosphere_flags = manager->GetAtmosphereFlags(),
      .inv_projection_matrix
      = Context().current_view.resolved_view->InverseProjection(),
      .inv_view_matrix = Context().current_view.resolved_view->InverseView(),
    };
    std::memcpy(
      impl_->camera_volume_constants_mapped, &constants, sizeof(constants));

    recorder.SetPipelineState(*impl_->camera_volume_pso_desc);
    recorder.SetComputeRootConstantBufferView(
      static_cast<uint32_t>(binding::RootParam::kSceneConstants),
      scene_const_addr);
    if (env_dynamic_addr.has_value()) {
      recorder.SetComputeRootConstantBufferView(
        static_cast<uint32_t>(binding::RootParam::kEnvironmentDynamicData),
        *env_dynamic_addr);
    }

    recorder.SetComputeRoot32BitConstant(
      static_cast<uint32_t>(binding::RootParam::kRootConstants), 0U, 0);
    recorder.SetComputeRoot32BitConstant(
      static_cast<uint32_t>(binding::RootParam::kRootConstants),
      impl_->camera_volume_constants_cbv_index.get(), 1);

    recorder.Dispatch(
      (camera_volume_width + kThreadGroupSizeX - 1) / kThreadGroupSizeX,
      (camera_volume_height + kThreadGroupSizeY - 1) / kThreadGroupSizeY,
      camera_volume_depth);
  }

  // Final transition for Camera Volume LUT
  recorder.RequireResourceState(*manager->GetCameraVolumeLutTexture(),
    graphics::ResourceStates::kShaderResource);
  recorder.FlushBarriers();

  manager->MarkClean();
  manager->MarkGenerated();

  LOG_F(INFO, "SkyAtmosphereLutComputePass: LUTs regenerated");

  co_return;
}

//=== ComputeRenderPass Virtual Methods ===-----------------------------------//

auto SkyAtmosphereLutComputePass::ValidateConfig() -> void
{
  if (!impl_->config->lut_manager) {
    throw std::runtime_error(
      "SkyAtmosphereLutComputePass: lut_manager is required");
  }
}

auto SkyAtmosphereLutComputePass::CreatePipelineStateDesc()
  -> graphics::ComputePipelineDesc
{
  // This is required by the base class interface, but we manage our own PSOs
  // Return the transmittance PSO as the "primary" one
  if (!impl_->pso_built) {
    impl_->BuildPipelineStateDescs();
  }

  DCHECK_F(impl_->transmittance_pso_desc.has_value(),
    "Transmittance PSO should be built");
  return *impl_->transmittance_pso_desc;
}

auto SkyAtmosphereLutComputePass::NeedRebuildPipelineState() const -> bool
{
  // Rebuild if never built
  return !impl_->pso_built;
}

} // namespace oxygen::engine
