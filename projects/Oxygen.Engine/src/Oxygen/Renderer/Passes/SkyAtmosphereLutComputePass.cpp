//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <span>
#include <stdexcept>
#include <utility>

#include <fmt/format.h>
#include <glm/glm.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/Types/Geometry.h>
#include <Oxygen/Core/Bindless/Generated.RootSignature.h>
#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Constants.h>
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
#include <Oxygen/Renderer/Internal/EnvironmentStaticDataManager.h>
#include <Oxygen/Renderer/Internal/SkyAtmosphereLutManager.h>
#include <Oxygen/Renderer/Passes/SkyAtmosphereLutComputePass.h>
#include <Oxygen/Renderer/PreparedSceneFrame.h>
#include <Oxygen/Renderer/RenderContext.h>
#include <Oxygen/Renderer/Renderer.h>

using oxygen::Extent;
using oxygen::kInvalidShaderVisibleIndex;
using oxygen::ShaderVisibleIndex;
using oxygen::engine::SkyAtmosphereLutComputePass;
using oxygen::engine::SkyAtmosphereLutComputePassConfig;
using oxygen::graphics::Buffer;
using oxygen::graphics::CommandRecorder;
using oxygen::graphics::ComputePipelineDesc;
using oxygen::graphics::ResourceViewType;

namespace oxygen::engine {

namespace {

  //! Unified pass constants for all sky atmosphere LUT generation passes.
  /*!
   Layout must match `AtmospherePassConstants` in AtmospherePassConstants.hlsli.
  */
  struct alignas(oxygen::packing::kShaderDataFieldAlignment)
    AtmospherePassConstants {
    using LutExtent = Extent<uint32_t>;
    static_assert(sizeof(LutExtent) == sizeof(uint32_t) * 2);

    // --- 16-byte boundary ---
    ShaderVisibleIndex output_uav_index { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex transmittance_srv_index { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex multi_scat_srv_index { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex sky_irradiance_srv_index { kInvalidShaderVisibleIndex };

    // --- 16-byte boundary ---
    LutExtent output_extent { 0, 0 };
    LutExtent transmittance_extent { 0, 0 };

    // --- 16-byte boundary ---
    LutExtent sky_irradiance_extent { 0, 0 };
    uint32_t output_depth { 0 }; // also used as slice_count
    float atmosphere_height_m { 0.0F };

    // --- 16-byte boundary ---
    float planet_radius_m { 0.0F };
    float sun_cos_zenith { 0.0F };
    uint32_t alt_mapping_mode { 0 };
    uint32_t atmosphere_flags { 0 };

    // --- 16-byte boundary ---
    float max_distance_km { 0.0F };
    uint32_t _pad0 { 0 };
    uint32_t _pad1 { 0 };
    uint32_t _pad2 { 0 };

    // --- 16-byte boundary (x4) ---
    glm::mat4 inv_projection_matrix { 1.0F };

    // --- 16-byte boundary (x4) ---
    glm::mat4 inv_view_matrix { 1.0F };

    // --- 16-byte boundary (x3) ---
    // Padding to reach kShaderDataSizeAlignment (256 bytes)
    static constexpr uint32_t kFinalPaddingSize = 12;
    std::array<uint32_t, kFinalPaddingSize> _final_padding {};
  };
  static_assert(
    sizeof(AtmospherePassConstants) == packing::kShaderDataSizeAlignment);

  // Number of passes (LUTs) to generate
  constexpr uint32_t kNumAtmospherePasses = 5;

  namespace cbv {
    // Constants buffer sub-allocation indices (slots)
    // Used to index into our descriptor array and calculate offsets.
    constexpr uint32_t kSlotTransmittance = 0;
    constexpr uint32_t kSlotMultiScat = 1;
    constexpr uint32_t kSlotSkyIrradiance = 2;
    constexpr uint32_t kSlotSkyView = 3;
    constexpr uint32_t kSlotCameraVolume = 4;

    // Pre-calculated byte offsets in the constants buffer (constexpr evaluated)
    constexpr size_t kOffsetTransmittance
      = static_cast<size_t>(kSlotTransmittance)
      * packing::kConstantBufferAlignment;
    constexpr size_t kOffsetMultiScat
      = static_cast<size_t>(kSlotMultiScat) * packing::kConstantBufferAlignment;
    constexpr size_t kOffsetSkyIrradiance
      = static_cast<size_t>(kSlotSkyIrradiance)
      * packing::kConstantBufferAlignment;
    constexpr size_t kOffsetSkyView
      = static_cast<size_t>(kSlotSkyView) * packing::kConstantBufferAlignment;
    constexpr size_t kOffsetCameraVolume
      = static_cast<size_t>(kSlotCameraVolume)
      * packing::kConstantBufferAlignment;
  } // namespace cbv

  // Thread group size must match HLSL shaders
  constexpr uint32_t kThreadGroupSizeX = 8;
  constexpr uint32_t kThreadGroupSizeY = 8;

  auto RunSkyAtmosphereComputeSanityChecks(const RenderContext& ctx,
    const internal::SkyAtmosphereLutManager& manager, const ViewId view_id,
    const bool pso_ready, const bool constants_ready) -> bool
  {
    bool ok = true;

    if (!pso_ready) {
      LOG_F(WARNING,
        "SkyAtmosphereLutComputePass: sanity check failed (view={}) missing PSO(s)",
        view_id.get());
      ok = false;
    }
    if (!constants_ready) {
      LOG_F(WARNING,
        "SkyAtmosphereLutComputePass: sanity check failed (view={}) constants buffer/CBV not ready",
        view_id.get());
      ok = false;
    }

    if (!ctx.current_view.resolved_view) {
      LOG_F(WARNING,
        "SkyAtmosphereLutComputePass: sanity check failed (view={}) missing resolved view",
        view_id.get());
      ok = false;
    }
    if (!ctx.scene_constants) {
      LOG_F(WARNING,
        "SkyAtmosphereLutComputePass: sanity check failed (view={}) missing scene constants",
        view_id.get());
      ok = false;
    }
    if (!ctx.env_dynamic_manager) {
      LOG_F(WARNING,
        "SkyAtmosphereLutComputePass: sanity check failed (view={}) missing env dynamic manager",
        view_id.get());
      ok = false;
    }
    if (!ctx.GetRenderer().GetEnvironmentStaticDataManager()) {
      LOG_F(WARNING,
        "SkyAtmosphereLutComputePass: sanity check failed (view={}) missing env static manager",
        view_id.get());
      ok = false;
    }

    const auto transmittance_tex = manager.GetTransmittanceLutTexture();
    const auto sky_view_tex = manager.GetSkyViewLutTexture();
    const auto multi_scat_tex = manager.GetMultiScatLutTexture();
    const auto sky_irradiance_tex = manager.GetSkyIrradianceLutTexture();
    const auto camera_volume_tex = manager.GetCameraVolumeLutTexture();
    if (!transmittance_tex || !sky_view_tex || !multi_scat_tex
      || !sky_irradiance_tex || !camera_volume_tex) {
      LOG_F(WARNING,
        "SkyAtmosphereLutComputePass: sanity check failed (view={}) missing one or more LUT textures",
        view_id.get());
      ok = false;
    }

    const auto transmittance_uav = manager.GetTransmittanceLutUavSlot();
    const auto multi_scat_uav = manager.GetMultiScatLutUavSlot();
    const auto sky_irradiance_uav = manager.GetSkyIrradianceLutUavSlot();
    const auto sky_view_uav = manager.GetSkyViewLutUavSlot();
    const auto camera_volume_uav = manager.GetCameraVolumeLutUavSlot();
    const auto transmittance_srv = manager.GetTransmittanceLutBackSlot();
    const auto multi_scat_srv = manager.GetMultiScatLutBackSlot();
    const auto sky_irradiance_srv = manager.GetSkyIrradianceLutBackSlot();
    if (!transmittance_uav.IsValid() || !multi_scat_uav.IsValid()
      || !sky_irradiance_uav.IsValid() || !sky_view_uav.IsValid()
      || !camera_volume_uav.IsValid() || !transmittance_srv.IsValid()
      || !multi_scat_srv.IsValid() || !sky_irradiance_srv.IsValid()) {
      LOG_F(WARNING,
        "SkyAtmosphereLutComputePass: sanity check failed (view={}) invalid LUT UAV/SRV slots",
        view_id.get());
      ok = false;
    }

    const auto [trans_w, trans_h] = manager.GetTransmittanceLutSize();
    const auto [ms_w, ms_h] = manager.GetMultiScatLutSize();
    const auto [irr_w, irr_h] = manager.GetSkyIrradianceLutSize();
    const auto [sky_w, sky_h] = manager.GetSkyViewLutSize();
    const auto [cv_w, cv_h, cv_d] = manager.GetCameraVolumeLutSize();
    if (trans_w == 0 || trans_h == 0 || ms_w == 0 || ms_h == 0 || irr_w == 0
      || irr_h == 0 || sky_w == 0 || sky_h == 0 || cv_w == 0 || cv_h == 0
      || cv_d == 0) {
      LOG_F(WARNING,
        "SkyAtmosphereLutComputePass: sanity check failed (view={}) zero-sized LUT extent(s)",
        view_id.get());
      ok = false;
    }

    const auto planet_radius_m = manager.GetPlanetRadiusMeters();
    const auto atmosphere_height_m = manager.GetAtmosphereHeightMeters();
    const auto sun_cos_zenith = manager.GetSunState().cos_zenith;
    const auto sky_view_slices = manager.GetSkyViewLutSlices();
    const auto alt_mapping_mode = manager.GetAltMappingMode();
    if (!std::isfinite(planet_radius_m) || planet_radius_m <= 0.0F) {
      LOG_F(WARNING,
        "SkyAtmosphereLutComputePass: sanity check failed (view={}) invalid planet radius {}",
        view_id.get(), planet_radius_m);
      ok = false;
    }
    if (!std::isfinite(atmosphere_height_m) || atmosphere_height_m <= 0.0F) {
      LOG_F(WARNING,
        "SkyAtmosphereLutComputePass: sanity check failed (view={}) invalid atmosphere height {}",
        view_id.get(), atmosphere_height_m);
      ok = false;
    }
    if (!std::isfinite(sun_cos_zenith) || sun_cos_zenith < -1.0F
      || sun_cos_zenith > 1.0F) {
      LOG_F(WARNING,
        "SkyAtmosphereLutComputePass: sanity check failed (view={}) invalid sun cos zenith {}",
        view_id.get(), sun_cos_zenith);
      ok = false;
    }
    if (sky_view_slices == 0) {
      LOG_F(WARNING,
        "SkyAtmosphereLutComputePass: sanity check failed (view={}) sky view slices is zero",
        view_id.get());
      ok = false;
    }
    if (alt_mapping_mode > 1U) {
      LOG_F(WARNING,
        "SkyAtmosphereLutComputePass: sanity check failed (view={}) invalid alt mapping mode {}",
        view_id.get(), alt_mapping_mode);
      ok = false;
    }

    return ok;
  }

} // namespace

//=== Implementation Details ===----------------------------------------------//

struct SkyAtmosphereLutComputePass::Impl {
  observer_ptr<Graphics> gfx;
  std::shared_ptr<Config> config;
  std::string name;

  // Pass constants buffer (unified for all shaders, 5 slots)
  std::shared_ptr<Buffer> constants_cbv;
  std::span<std::byte> mapped_constants;

  // CBV indices for each pass (pointing to different offsets in the same
  // buffer)
  std::array<ShaderVisibleIndex, kNumAtmospherePasses> cbv_indices {
    kInvalidShaderVisibleIndex,
    kInvalidShaderVisibleIndex,
    kInvalidShaderVisibleIndex,
    kInvalidShaderVisibleIndex,
    kInvalidShaderVisibleIndex,
  };

  // Pipeline state descriptions (cached for rebuild detection)
  std::optional<ComputePipelineDesc> transmittance_pso_desc;
  std::optional<ComputePipelineDesc> multi_scat_pso_desc;
  std::optional<ComputePipelineDesc> sky_irradiance_pso_desc;
  std::optional<ComputePipelineDesc> sky_view_pso_desc;
  std::optional<ComputePipelineDesc> camera_volume_pso_desc;

  // Track if we've ever built the PSOs
  bool pso_built { false };

  explicit Impl(
    observer_ptr<Graphics> gfx_in, std::shared_ptr<Config> config_in)
    : gfx(gfx_in)
    , config(std::move(config_in))
    , name(this->config ? this->config->debug_name : "AtmosphereComputePass")
  {
  }

  ~Impl()
  {
    if (constants_cbv) {
      try {
        constants_cbv->UnMap();
      } catch (...) {
        LOG_F(ERROR, "Failed to unmap constants buffer");
      }
      mapped_constants = {};
    }
  }

  OXYGEN_MAKE_NON_COPYABLE(Impl)
  OXYGEN_MAKE_NON_MOVABLE(Impl)

  //! Ensures the pass constants buffer is created.
  auto EnsurePassConstantsBuffers() -> void
  {
    if (constants_cbv != nullptr) {
      return;
    }

    auto& registry = gfx->GetResourceRegistry();
    auto& allocator = gfx->GetDescriptorAllocator();

    const graphics::BufferDesc desc {
      .size_bytes = static_cast<uint64_t>(packing::kConstantBufferAlignment)
        * kNumAtmospherePasses,
      .usage = graphics::BufferUsage::kConstant,
      .memory = graphics::BufferMemory::kUpload,
      .debug_name = name + "_AtmosphereConstants",
    };

    constants_cbv = gfx->CreateBuffer(desc);
    if (!constants_cbv) {
      throw std::runtime_error(
        "SkyAtmosphereLutComputePass: Failed to create constants buffer");
    }
    constants_cbv->SetName(desc.debug_name);

    auto* mapped = constants_cbv->Map(0, desc.size_bytes);
    if (mapped == nullptr) {
      throw std::runtime_error(
        "SkyAtmosphereLutComputePass: Failed to map constants buffer");
    }
    mapped_constants
      = std::span(static_cast<std::byte*>(mapped), desc.size_bytes);

    registry.Register(constants_cbv);

    for (auto i = 0U; i < kNumAtmospherePasses; ++i) {
      graphics::BufferViewDescription cbv_view_desc;
      cbv_view_desc.view_type = ResourceViewType::kConstantBuffer;
      cbv_view_desc.visibility = graphics::DescriptorVisibility::kShaderVisible;
      cbv_view_desc.range
        = { static_cast<uint64_t>(i) * packing::kConstantBufferAlignment,
            packing::kConstantBufferAlignment };

      auto cbv_handle = allocator.Allocate(ResourceViewType::kConstantBuffer,
        graphics::DescriptorVisibility::kShaderVisible);
      if (!cbv_handle.IsValid()) {
        throw std::runtime_error(
          "Failed to allocate CBV descriptor for atmosphere compute passes");
      }
      cbv_indices.at(i) = allocator.GetShaderVisibleIndex(cbv_handle);

      registry.RegisterView(
        *constants_cbv, std::move(cbv_handle), cbv_view_desc);
    }

    LOG_F(INFO,
      "Created unified constants buffer for atmosphere {} compute passes",
      kNumAtmospherePasses);
  }

  //! Build pipeline state descriptions for all shaders.
  auto BuildPipelineStateDescs() -> void
  {
    auto root_bindings = RenderPass::BuildRootBindings();
    const auto bindings = std::span<const graphics::RootBindingItem>(
      root_bindings.data(), root_bindings.size());

    auto create_pso = [&](const char* shader_path, const char* debug_name) {
      graphics::ShaderRequest shader {
        .stage = oxygen::ShaderType::kCompute,
        .source_path = shader_path,
        .entry_point = "CS",
      };
      return ComputePipelineDesc::Builder()
        .SetComputeShader(std::move(shader))
        .SetRootBindings(bindings)
        .SetDebugName(debug_name)
        .Build();
    };

    transmittance_pso_desc = create_pso(
      "Atmosphere/TransmittanceLut_CS.hlsl", "SkyAtmo_TransmittanceLUT_PSO");
    multi_scat_pso_desc = create_pso(
      "Atmosphere/MultiScatLut_CS.hlsl", "SkyAtmo_MultiScatLUT_PSO");
    sky_irradiance_pso_desc = create_pso(
      "Atmosphere/SkyIrradianceLut_CS.hlsl", "SkyAtmo_SkyIrradianceLUT_PSO");
    sky_view_pso_desc
      = create_pso("Atmosphere/SkyViewLut_CS.hlsl", "SkyAtmo_SkyViewLUT_PSO");
    camera_volume_pso_desc = create_pso(
      "Atmosphere/CameraVolumeLut_CS.hlsl", "SkyAtmo_CameraVolumeLUT_PSO");

    pso_built = true;

    LOG_F(INFO, "Built {} compute PSOs for atmosphere compute passes",
      kNumAtmospherePasses);
  }
};

//=== SkyAtmosphereLutComputePass ===-----------------------------------------//

SkyAtmosphereLutComputePass::SkyAtmosphereLutComputePass(
  observer_ptr<Graphics> gfx, std::shared_ptr<Config> config)
  : ComputeRenderPass(
      config ? config->debug_name : "SkyAtmosphereLutComputePass")
  , impl_(std::make_unique<Impl>(gfx, std::move(config)))
{
}

SkyAtmosphereLutComputePass::~SkyAtmosphereLutComputePass() = default;

auto SkyAtmosphereLutComputePass::DoPrepareResources(CommandRecorder& recorder)
  -> co::Co<>
{
  auto* manager = Context().current_view.atmo_lut_manager.get();
  if (manager == nullptr) {
    co_return;
  }

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

  // Get back-buffer textures for compute shader write
  const auto transmittance_tex = manager->GetTransmittanceLutTexture();
  const auto sky_view_tex = manager->GetSkyViewLutTexture();
  const auto multi_scat_tex = manager->GetMultiScatLutTexture();
  const auto sky_irradiance_tex = manager->GetSkyIrradianceLutTexture();
  const auto camera_volume_tex = manager->GetCameraVolumeLutTexture();

  if (!transmittance_tex || !sky_view_tex || !multi_scat_tex
    || !sky_irradiance_tex || !camera_volume_tex) {
    LOG_F(ERROR, "SkyAtmosphereLutComputePass: LUT textures not available");
    co_return;
  }

  using enum graphics::ResourceStates;

  // Determine initial state for the back buffer textures:
  // - swap_count < 2: back buffer was never used, starts in UAV state
  //   (first generation writes to buffer 1, second to buffer 0)
  // - swap_count >= 2: back buffer was the front buffer in a previous frame,
  //   so it's in SRV state and needs transition to UAV for compute write
  //
  // This is more precise than HasBeenGenerated() because we need BOTH buffers
  // to have been written before we can assume the back buffer is in SRV state.
  const auto initial_state
    = manager->GetSwapCount() >= 2 ? kShaderResource : kUnorderedAccess;

  // Prepare textures for compute write
  for (const auto& tex : {
         transmittance_tex,
         sky_view_tex,
         multi_scat_tex,
         sky_irradiance_tex,
         camera_volume_tex,
       }) {
    recorder.BeginTrackingResourceState(*tex, initial_state, false);
    recorder.EnableAutoMemoryBarriers(*tex);
    recorder.RequireResourceState(*tex, kUnorderedAccess);
  }

  recorder.FlushBarriers();

  co_return;
}

//=== DoExecute ===-----------------------------------------------------------//

/*!
 Executes LUT generation shaders in order:
 1. Transmittance LUT - optical depth integration
 2. MultiScat LUT - integral over directions (requires transmittance)
 3. Sky irradiance LUT - hemispherical irradiance (requires transmittance and
   multi-scat)
 4. Sky-view LUT - raymarch (requires transmittance, multi-scat, and sky
   irradiance)
*/
auto SkyAtmosphereLutComputePass::DoExecute(CommandRecorder& recorder)
  -> co::Co<>
{
  auto manager = Context().current_view.atmo_lut_manager;
  if (!manager) {
    co_return;
  }

  // Skip if LUTs are up-to-date
  if (!manager->IsDirty()) {
    co_return;
  }

  const auto view_id = Context().current_view.view_id;
  const auto generation = manager->GetGeneration();

  const bool pso_ready = impl_->transmittance_pso_desc.has_value()
    && impl_->multi_scat_pso_desc.has_value()
    && impl_->sky_irradiance_pso_desc.has_value()
    && impl_->sky_view_pso_desc.has_value()
    && impl_->camera_volume_pso_desc.has_value();
  const bool constants_ready
    = impl_->mapped_constants.size()
      >= (packing::kConstantBufferAlignment * kNumAtmospherePasses)
    && std::ranges::all_of(
      impl_->cbv_indices, [](const auto idx) { return idx.IsValid(); });

  if (!RunSkyAtmosphereComputeSanityChecks(
        Context(), *manager, view_id, pso_ready, constants_ready)) {
    LOG_F(WARNING,
      "SkyAtmosphereLutComputePass: skipping LUT generation due to failed sanity checks (view={}, gen={})",
      view_id.get(), generation);
    co_return;
  }
  DCHECK_F(pso_ready && constants_ready);

  const auto transmittance_uav = manager->GetTransmittanceLutUavSlot();
  const auto multi_scat_uav = manager->GetMultiScatLutUavSlot();
  const auto sky_irradiance_uav = manager->GetSkyIrradianceLutUavSlot();
  const auto sky_view_uav = manager->GetSkyViewLutUavSlot();
  const auto camera_volume_uav = manager->GetCameraVolumeLutUavSlot();

  const auto transmittance_srv = manager->GetTransmittanceLutBackSlot();
  const auto multi_scat_srv = manager->GetMultiScatLutBackSlot();
  const auto sky_irradiance_srv = manager->GetSkyIrradianceLutBackSlot();

  DCHECK_F(transmittance_uav.IsValid() && multi_scat_uav.IsValid()
    && sky_irradiance_uav.IsValid() && sky_view_uav.IsValid()
    && camera_volume_uav.IsValid() && transmittance_srv.IsValid()
    && multi_scat_srv.IsValid() && sky_irradiance_srv.IsValid());

  const auto transmittance_extent = manager->GetTransmittanceLutSize();
  const auto multi_scat_extent = manager->GetMultiScatLutSize();
  const auto sky_irradiance_extent = manager->GetSkyIrradianceLutSize();
  const auto sky_view_extent = manager->GetSkyViewLutSize();
  const auto [camera_volume_width, camera_volume_height, camera_volume_depth]
    = manager->GetCameraVolumeLutSize();
  const auto camera_volume_extent = AtmospherePassConstants::LutExtent {
    .width = camera_volume_width,
    .height = camera_volume_height,
  };

  const float planet_radius_m = manager->GetPlanetRadiusMeters();
  const float atmosphere_height_m = manager->GetAtmosphereHeightMeters();
  const uint32_t sky_view_slices = manager->GetSkyViewLutSlices();
  const uint32_t alt_mapping_mode = manager->GetAltMappingMode();

  const auto env_static_manager
    = Context().GetRenderer().GetEnvironmentStaticDataManager();
  const auto env_static_srv
    = env_static_manager ? env_static_manager->GetSrvIndex(view_id).get() : 0U;

  DLOG_SCOPE_F(INFO, "Atmosphere LUT generation");
  DLOG_F(1, "view : {}", view_id.get());
  DLOG_F(1, "frame_slot : {}", Context().frame_slot.get());
  DLOG_F(1, "frame_seq : {}", Context().frame_sequence.get());
  DLOG_F(1, "env_srv : {}", env_static_srv);
  DLOG_F(1, "gen : {}", generation);

  DCHECK_NOTNULL_F(Context().scene_constants);
  DCHECK_NOTNULL_F(Context().env_dynamic_manager);

  const auto scene_const_addr
    = Context().scene_constants->GetGPUVirtualAddress();
  const auto env_manager = Context().env_dynamic_manager;
  env_manager->UpdateIfNeeded(view_id);
  const auto env_dynamic_addr = env_manager->GetGpuVirtualAddress(view_id);
  DCHECK_NE_F(env_dynamic_addr, 0);
  DLOG_F(1, "scene_const_addr : 0x{:x}", scene_const_addr);
  DLOG_F(1, "env_dynamic_addr : 0x{:x}", env_dynamic_addr);

  // Helper for safe constant updates using spans
  auto update_constants
    = [&](size_t offset, const AtmospherePassConstants& data) {
        auto target = impl_->mapped_constants.subspan(
          offset, sizeof(AtmospherePassConstants));
        auto source = std::as_bytes(std::span(&data, 1));
        std::ranges::copy(source, target.begin());
      };

  // Common constants for all atmosphere passes
  AtmospherePassConstants constants {};
  constants.atmosphere_height_m = atmosphere_height_m;
  constants.planet_radius_m = planet_radius_m;
  constants.sun_cos_zenith = manager->GetSunState().cos_zenith;
  constants.alt_mapping_mode = alt_mapping_mode;

  using b = binding::RootParam;

  auto dispatch_pass
    = [&](const graphics::ComputePipelineDesc& pso, uint32_t slot,
        Extent<uint32_t> extent, uint32_t depth = 1) {
        const size_t offset
          = static_cast<size_t>(slot) * packing::kConstantBufferAlignment;
        update_constants(offset, constants);

        recorder.SetPipelineState(pso);
        recorder.SetComputeRootConstantBufferView(
          static_cast<uint32_t>(b::kSceneConstants), scene_const_addr);
        recorder.SetComputeRootConstantBufferView(
          static_cast<uint32_t>(b::kEnvironmentDynamicData), env_dynamic_addr);

        recorder.SetComputeRoot32BitConstant(
          static_cast<uint32_t>(b::kRootConstants), 0U, 0);
        recorder.SetComputeRoot32BitConstant(
          static_cast<uint32_t>(b::kRootConstants),
          impl_->cbv_indices.at(slot).get(), 1);

        recorder.Dispatch(
          (extent.width + kThreadGroupSizeX - 1) / kThreadGroupSizeX,
          (extent.height + kThreadGroupSizeY - 1) / kThreadGroupSizeY, depth);
      };

  auto transition_to_srv = [&](const graphics::Texture& tex) {
    recorder.RequireResourceState(
      tex, graphics::ResourceStates::kShaderResource);
    recorder.FlushBarriers();
  };

  //=== Dispatch 1: Transmittance LUT ===-------------------------------------//
  {
    LOG_SCOPE_F(INFO, "Transmittance LUT");
    constants.output_uav_index = transmittance_uav;
    constants.output_extent = transmittance_extent;

    dispatch_pass(*impl_->transmittance_pso_desc, cbv::kSlotTransmittance,
      constants.output_extent);
    transition_to_srv(*manager->GetTransmittanceLutTexture());
  }

  //=== Dispatch 2: MultiScat LUT ===-----------------------------------------//
  {
    LOG_SCOPE_F(INFO, "MultiScat LUT");
    constants.output_uav_index = multi_scat_uav;
    constants.output_extent = multi_scat_extent;
    constants.transmittance_srv_index = transmittance_srv;
    constants.transmittance_extent = transmittance_extent;

    dispatch_pass(*impl_->multi_scat_pso_desc, cbv::kSlotMultiScat,
      constants.output_extent);
    transition_to_srv(*manager->GetMultiScatLutTexture());
  }

  //=== Dispatch 3: Sky Irradiance LUT ===------------------------------------//
  {
    LOG_SCOPE_F(INFO, "Sky Irradiance LUT");
    constants.output_uav_index = sky_irradiance_uav;
    constants.output_extent = sky_irradiance_extent;
    constants.multi_scat_srv_index = multi_scat_srv;

    dispatch_pass(*impl_->sky_irradiance_pso_desc, cbv::kSlotSkyIrradiance,
      constants.output_extent);
    transition_to_srv(*manager->GetSkyIrradianceLutTexture());
  }

  //=== Dispatch 4: Sky-View LUT ===------------------------------------------//
  {
    LOG_SCOPE_F(INFO, "Sky-View LUT");
    constants.output_uav_index = sky_view_uav;
    constants.output_extent = sky_view_extent;
    constants.sky_irradiance_srv_index = sky_irradiance_srv;
    constants.sky_irradiance_extent = sky_irradiance_extent;
    constants.output_depth = sky_view_slices;

    dispatch_pass(*impl_->sky_view_pso_desc, cbv::kSlotSkyView,
      constants.output_extent, sky_view_slices);
    transition_to_srv(*manager->GetSkyViewLutTexture());
  }

  //=== Dispatch 5: Camera Volume LUT ===-------------------------------------//
  {
    LOG_SCOPE_F(INFO, "Camera Volume LUT");
    constexpr float kDefaultMaxDistanceKm = 128.0F;
    constants.output_uav_index = camera_volume_uav;
    constants.output_extent = camera_volume_extent;
    constants.output_depth = camera_volume_depth;
    constants.max_distance_km = kDefaultMaxDistanceKm;
    constants.inv_projection_matrix
      = Context().current_view.resolved_view->InverseProjection();
    constants.inv_view_matrix
      = Context().current_view.resolved_view->InverseView();

    dispatch_pass(*impl_->camera_volume_pso_desc, cbv::kSlotCameraVolume,
      constants.output_extent, camera_volume_depth);
    transition_to_srv(*manager->GetCameraVolumeLutTexture());
  }

  // Atomically swap front/back buffers - shaders will now sample freshly
  // computed LUTs while next frame's compute writes to previous front buffer
  manager->SwapBuffers();

  LOG_F(INFO, "SkyAtmoLUT: regen complete (view={}, gen={}, front={}, swap={})",
    view_id.get(), generation, manager->GetFrontBufferIndex(),
    manager->GetSwapCount());

  co_return;
}

//=== ComputeRenderPass Virtual Methods ===-----------------------------------//

auto SkyAtmosphereLutComputePass::ValidateConfig() -> void { }

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
