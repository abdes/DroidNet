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
#include <glm/gtc/type_ptr.hpp>

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
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Renderer/Internal/SkyAtmosphereLutManager.h>
#include <Oxygen/Renderer/Passes/SkyAtmosphereLutComputePass.h>
#include <Oxygen/Renderer/RenderContext.h>
#include <Oxygen/Renderer/Types/EnvironmentStaticData.h>

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
  uint32_t output_uav_index { oxygen::engine::kInvalidDescriptorSlot };
  uint32_t output_width { 0 };
  uint32_t output_height { 0 };
  uint32_t _pad0 { 0 };
};
static_assert(sizeof(TransmittanceLutPassConstants) == 16,
  "TransmittanceLutPassConstants must be 16 bytes");

//! Pass constants for sky-view LUT generation.
/*!
 Layout must match `SkyViewLutPassConstants` in SkyViewLut_CS.hlsl.
*/
struct alignas(16) SkyViewLutPassConstants {
  uint32_t output_uav_index { oxygen::engine::kInvalidDescriptorSlot };
  uint32_t transmittance_srv_index { oxygen::engine::kInvalidDescriptorSlot };
  uint32_t output_width { 0 };
  uint32_t output_height { 0 };

  uint32_t transmittance_width { 0 };
  uint32_t transmittance_height { 0 };
  float camera_altitude_m { 0.0F };
  float sun_cos_zenith { 0.0F }; // Cosine of sun zenith angle (sun_dir.z)

  uint32_t atmosphere_flags {
    0
  }; // Debug/feature flags (kUseAmbientTerm, etc.)
  uint32_t _pad0 { 0 };
  uint32_t _pad1 { 0 };
  uint32_t _pad2 { 0 };
};
static_assert(sizeof(SkyViewLutPassConstants) == 48,
  "SkyViewLutPassConstants must be 48 bytes");

// Thread group size must match HLSL shaders
constexpr uint32_t kThreadGroupSizeX = 8;
constexpr uint32_t kThreadGroupSizeY = 8;

} // namespace

namespace oxygen::engine {

//=== Implementation Details ===----------------------------------------------//

struct SkyAtmosphereLutComputePass::Impl {
  observer_ptr<Graphics> gfx;
  std::shared_ptr<Config> config;
  std::string name;

  // Pass constants buffers (one for each shader)
  std::shared_ptr<Buffer> transmittance_constants_buffer;
  std::shared_ptr<Buffer> sky_view_constants_buffer;
  void* transmittance_constants_mapped { nullptr };
  void* sky_view_constants_mapped { nullptr };
  ShaderVisibleIndex transmittance_constants_cbv_index {
    kInvalidShaderVisibleIndex
  };
  ShaderVisibleIndex sky_view_constants_cbv_index {
    kInvalidShaderVisibleIndex
  };

  // Pipeline state descriptions (cached for rebuild detection)
  std::optional<ComputePipelineDesc> transmittance_pso_desc;
  std::optional<ComputePipelineDesc> sky_view_pso_desc;

  // Track if we've ever built the PSOs
  bool pso_built { false };

  Impl(observer_ptr<Graphics> gfx_in, std::shared_ptr<Config> config_in)
    : gfx(gfx_in)
    , config(std::move(config_in))
    , name(this->config->debug_name)
  {
  }

  ~Impl()
  {
    if (transmittance_constants_buffer && transmittance_constants_mapped) {
      transmittance_constants_buffer->UnMap();
      transmittance_constants_mapped = nullptr;
    }
    if (sky_view_constants_buffer && sky_view_constants_mapped) {
      sky_view_constants_buffer->UnMap();
      sky_view_constants_mapped = nullptr;
    }
  }

  //! Ensures the pass constants buffers are created.
  auto EnsurePassConstantsBuffers() -> void
  {
    if (transmittance_constants_buffer && sky_view_constants_buffer) {
      return;
    }

    auto& registry = gfx->GetResourceRegistry();
    auto& allocator = gfx->GetDescriptorAllocator();

    // Create transmittance constants buffer
    {
      const graphics::BufferDesc desc {
        .size_bytes = 256u, // 256-byte alignment for CBV
        .usage = graphics::BufferUsage::kConstant,
        .memory = graphics::BufferMemory::kUpload,
        .debug_name = name + "_TransmittanceConstants",
      };

      transmittance_constants_buffer = gfx->CreateBuffer(desc);
      if (!transmittance_constants_buffer) {
        throw std::runtime_error(
          "SkyAtmosphereLutComputePass: Failed to create transmittance "
          "constants buffer");
      }
      transmittance_constants_buffer->SetName(desc.debug_name);

      transmittance_constants_mapped
        = transmittance_constants_buffer->Map(0, desc.size_bytes);
      if (!transmittance_constants_mapped) {
        throw std::runtime_error(
          "SkyAtmosphereLutComputePass: Failed to map transmittance constants "
          "buffer");
      }

      graphics::BufferViewDescription cbv_view_desc;
      cbv_view_desc.view_type = ResourceViewType::kConstantBuffer;
      cbv_view_desc.visibility = graphics::DescriptorVisibility::kShaderVisible;
      cbv_view_desc.range = { 0u, desc.size_bytes };

      auto cbv_handle = allocator.Allocate(ResourceViewType::kConstantBuffer,
        graphics::DescriptorVisibility::kShaderVisible);
      if (!cbv_handle.IsValid()) {
        throw std::runtime_error(
          "SkyAtmosphereLutComputePass: Failed to allocate transmittance CBV "
          "descriptor");
      }
      transmittance_constants_cbv_index
        = allocator.GetShaderVisibleIndex(cbv_handle);

      registry.Register(transmittance_constants_buffer);
      registry.RegisterView(
        *transmittance_constants_buffer, std::move(cbv_handle), cbv_view_desc);
    }

    // Create sky-view constants buffer
    {
      const graphics::BufferDesc desc {
        .size_bytes = 256u,
        .usage = graphics::BufferUsage::kConstant,
        .memory = graphics::BufferMemory::kUpload,
        .debug_name = name + "_SkyViewConstants",
      };

      sky_view_constants_buffer = gfx->CreateBuffer(desc);
      if (!sky_view_constants_buffer) {
        throw std::runtime_error(
          "SkyAtmosphereLutComputePass: Failed to create sky-view constants "
          "buffer");
      }
      sky_view_constants_buffer->SetName(desc.debug_name);

      sky_view_constants_mapped
        = sky_view_constants_buffer->Map(0, desc.size_bytes);
      if (!sky_view_constants_mapped) {
        throw std::runtime_error(
          "SkyAtmosphereLutComputePass: Failed to map sky-view constants "
          "buffer");
      }

      graphics::BufferViewDescription cbv_view_desc;
      cbv_view_desc.view_type = ResourceViewType::kConstantBuffer;
      cbv_view_desc.visibility = graphics::DescriptorVisibility::kShaderVisible;
      cbv_view_desc.range = { 0u, desc.size_bytes };

      auto cbv_handle = allocator.Allocate(ResourceViewType::kConstantBuffer,
        graphics::DescriptorVisibility::kShaderVisible);
      if (!cbv_handle.IsValid()) {
        throw std::runtime_error(
          "SkyAtmosphereLutComputePass: Failed to allocate sky-view CBV "
          "descriptor");
      }
      sky_view_constants_cbv_index
        = allocator.GetShaderVisibleIndex(cbv_handle);

      registry.Register(sky_view_constants_buffer);
      registry.RegisterView(
        *sky_view_constants_buffer, std::move(cbv_handle), cbv_view_desc);
    }

    LOG_F(1, "SkyAtmosphereLutComputePass: Created pass constants buffers");
  }

  //! Build pipeline state descriptions for both shaders.
  auto BuildPipelineStateDescs() -> void
  {
    auto root_bindings = RenderPass::BuildRootBindings();

    // Transmittance LUT compute shader
    graphics::ShaderRequest transmittance_shader {
      .stage = oxygen::ShaderType::kCompute,
      .source_path = "Passes/Atmosphere/TransmittanceLut_CS.hlsl",
      .entry_point = "CS",
      .defines = {},
    };

    transmittance_pso_desc
      = ComputePipelineDesc::Builder()
          .SetComputeShader(std::move(transmittance_shader))
          .SetRootBindings(std::span<const graphics::RootBindingItem>(
            root_bindings.data(), root_bindings.size()))
          .SetDebugName("SkyAtmo_TransmittanceLUT_PSO")
          .Build();

    // Sky-view LUT compute shader
    graphics::ShaderRequest sky_view_shader {
      .stage = oxygen::ShaderType::kCompute,
      .source_path = "Passes/Atmosphere/SkyViewLut_CS.hlsl",
      .entry_point = "CS",
      .defines = {},
    };

    sky_view_pso_desc
      = ComputePipelineDesc::Builder()
          .SetComputeShader(std::move(sky_view_shader))
          .SetRootBindings(std::span<const graphics::RootBindingItem>(
            root_bindings.data(), root_bindings.size()))
          .SetDebugName("SkyAtmo_SkyViewLUT_PSO")
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

  if (!transmittance_tex || !sky_view_tex) {
    LOG_F(ERROR, "SkyAtmosphereLutComputePass: LUT textures not available");
    co_return;
  }

  // Transition LUTs to UAV state for compute shader write.
  // Use keep_initial_state=false because we will explicitly transition to SRV
  // after compute dispatch. The initial state depends on whether the LUTs have
  // been generated before: kUnorderedAccess (creation state) or kShaderResource
  // (post-generation state after previous frame sampling).
  const auto initial_state = manager->HasBeenGenerated()
    ? graphics::ResourceStates::kShaderResource
    : graphics::ResourceStates::kUnorderedAccess;

  recorder.BeginTrackingResourceState(*transmittance_tex, initial_state, false);
  recorder.BeginTrackingResourceState(*sky_view_tex, initial_state, false);

  // Enable automatic UAV memory barriers for proper UAV-to-UAV sync
  recorder.EnableAutoMemoryBarriers(*transmittance_tex);
  recorder.EnableAutoMemoryBarriers(*sky_view_tex);

  recorder.RequireResourceState(
    *transmittance_tex, graphics::ResourceStates::kUnorderedAccess);
  recorder.RequireResourceState(
    *sky_view_tex, graphics::ResourceStates::kUnorderedAccess);

  recorder.FlushBarriers();

  co_return;
}

//=== DoExecute ===-----------------------------------------------------------//

/*!
 Executes both LUT generation shaders:
 1. Transmittance LUT - optical depth integration
 2. Sky-view LUT - single-scattering raymarch (requires transmittance)

 Uses UAV barrier between dispatches to ensure transmittance is complete
 before sky-view shader reads it.
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
    || !impl_->sky_view_pso_desc.has_value()) {
    LOG_F(WARNING, "SkyAtmosphereLutComputePass: PSOs not built, skipping");
    co_return;
  }

  const auto transmittance_uav = manager->GetTransmittanceLutUavSlot();
  const auto sky_view_uav = manager->GetSkyViewLutUavSlot();
  const auto transmittance_srv = manager->GetTransmittanceLutSlot();

  if (transmittance_uav == kInvalidShaderVisibleIndex
    || sky_view_uav == kInvalidShaderVisibleIndex
    || transmittance_srv == kInvalidShaderVisibleIndex) {
    LOG_F(WARNING,
      "SkyAtmosphereLutComputePass: UAV/SRV indices not valid, skipping");
    co_return;
  }

  const auto [transmittance_width, transmittance_height]
    = manager->GetTransmittanceLutSize();
  const auto [sky_view_width, sky_view_height] = manager->GetSkyViewLutSize();

  // Get planet radius for proper altitude computation.
  const float planet_radius_m = manager->GetPlanetRadiusMeters();

  // Compute camera altitude for sky-view LUT.
  // Planet center is at (0, 0, -planet_radius) in Z-up convention,
  // so a camera at Z=0 is on the surface.
  float camera_altitude_m = 1.0F; // Default to 1m above ground
  if (const auto& view = Context().current_view.resolved_view) {
    const auto camera_pos = view->CameraPosition();
    // Planet center is at (0, 0, -planet_radius_m) in world space.
    // Distance from camera to planet center, minus radius = altitude.
    const glm::vec3 planet_center { 0.0F, 0.0F, -planet_radius_m };
    const float distance_to_center = glm::length(camera_pos - planet_center);
    camera_altitude_m = std::max(1.0F, distance_to_center - planet_radius_m);
  }

  //=== Dispatch 1: Transmittance LUT ===-------------------------------------//
  {
    // Update transmittance pass constants
    TransmittanceLutPassConstants constants {
      .output_uav_index = transmittance_uav.get(),
      .output_width = transmittance_width,
      .output_height = transmittance_height,
      ._pad0 = 0,
    };
    std::memcpy(
      impl_->transmittance_constants_mapped, &constants, sizeof(constants));

    // Set transmittance compute PSO
    recorder.SetPipelineState(*impl_->transmittance_pso_desc);

    // Bind scene constants
    DCHECK_NOTNULL_F(Context().scene_constants);
    recorder.SetComputeRootConstantBufferView(
      static_cast<uint32_t>(binding::RootParam::kSceneConstants),
      Context().scene_constants->GetGPUVirtualAddress());

    // Bind root constants (g_DrawIndex=0, g_PassConstantsIndex)
    recorder.SetComputeRoot32BitConstant(
      static_cast<uint32_t>(binding::RootParam::kRootConstants), 0U, 0);
    recorder.SetComputeRoot32BitConstant(
      static_cast<uint32_t>(binding::RootParam::kRootConstants),
      impl_->transmittance_constants_cbv_index.get(), 1);

    // Calculate dispatch dimensions
    const uint32_t dispatch_x
      = (transmittance_width + kThreadGroupSizeX - 1) / kThreadGroupSizeX;
    const uint32_t dispatch_y
      = (transmittance_height + kThreadGroupSizeY - 1) / kThreadGroupSizeY;

    recorder.Dispatch(dispatch_x, dispatch_y, 1);

    LOG_F(2, "SkyAtmosphereLutComputePass: Dispatched transmittance ({}x{})",
      dispatch_x, dispatch_y);
  }

  //=== Barrier: Transmittance UAV -> SRV for sky-view sampling ===----------//
  // Transition transmittance LUT to SRV state for sky-view shader to sample.
  // The auto memory barriers ensure proper UAV completion before this
  // transition.
  recorder.RequireResourceState(*manager->GetTransmittanceLutTexture(),
    graphics::ResourceStates::kShaderResource);
  recorder.FlushBarriers();

  //=== Dispatch 2: Sky-View LUT ===------------------------------------------//
  {
    // Update sky-view pass constants
    SkyViewLutPassConstants constants {
      .output_uav_index = sky_view_uav.get(),
      .transmittance_srv_index = transmittance_srv.get(),
      .output_width = sky_view_width,
      .output_height = sky_view_height,
      .transmittance_width = transmittance_width,
      .transmittance_height = transmittance_height,
      .camera_altitude_m = camera_altitude_m,
      .sun_cos_zenith = manager->GetSunState().cos_zenith,
      .atmosphere_flags = manager->GetAtmosphereFlags(),
      ._pad0 = 0,
      ._pad1 = 0,
      ._pad2 = 0,
    };
    std::memcpy(
      impl_->sky_view_constants_mapped, &constants, sizeof(constants));

    // Set sky-view compute PSO
    recorder.SetPipelineState(*impl_->sky_view_pso_desc);

    // Re-bind scene constants (PSO change may invalidate bindings)
    recorder.SetComputeRootConstantBufferView(
      static_cast<uint32_t>(binding::RootParam::kSceneConstants),
      Context().scene_constants->GetGPUVirtualAddress());

    // Bind root constants with sky-view pass constants index
    recorder.SetComputeRoot32BitConstant(
      static_cast<uint32_t>(binding::RootParam::kRootConstants), 0U, 0);
    recorder.SetComputeRoot32BitConstant(
      static_cast<uint32_t>(binding::RootParam::kRootConstants),
      impl_->sky_view_constants_cbv_index.get(), 1);

    // Calculate dispatch dimensions
    const uint32_t dispatch_x
      = (sky_view_width + kThreadGroupSizeX - 1) / kThreadGroupSizeX;
    const uint32_t dispatch_y
      = (sky_view_height + kThreadGroupSizeY - 1) / kThreadGroupSizeY;

    recorder.Dispatch(dispatch_x, dispatch_y, 1);

    LOG_F(2, "SkyAtmosphereLutComputePass: Dispatched sky-view ({}x{})",
      dispatch_x, dispatch_y);
  }

  //=== Final Barrier: Transition LUTs to SRV for rendering passes
  //===---------//
  // Sky-view was written to UAV, transition to SRV.
  // Transmittance is already in SRV state from the intermediate barrier.
  recorder.RequireResourceState(*manager->GetSkyViewLutTexture(),
    graphics::ResourceStates::kShaderResource);
  recorder.FlushBarriers();

  // Mark LUTs as up-to-date and record that generation has completed.
  // MarkGenerated() tracks that textures are now in SRV state, which is
  // important for proper initial state tracking on subsequent regenerations.
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
