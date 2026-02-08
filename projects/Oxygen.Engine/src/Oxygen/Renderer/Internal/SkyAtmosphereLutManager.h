//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <memory>
#include <optional>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Base/Types/Geometry.h>
#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Types/Atmosphere.h>
#include <Oxygen/Core/Types/TextureType.h>
#include <Oxygen/Graphics/Common/NativeObject.h>
#include <Oxygen/Renderer/Internal/ISkyAtmosphereLutProvider.h>
#include <Oxygen/Renderer/Types/SunState.h>
#include <Oxygen/Renderer/Upload/Types.h>
#include <Oxygen/Renderer/api_export.h>

namespace oxygen {
class Graphics;
} // namespace oxygen

namespace oxygen::graphics {
class Texture;
} // namespace oxygen::graphics

namespace oxygen::engine {
struct GpuSkyAtmosphereParams;
namespace upload {
  class UploadCoordinator;
  class StagingProvider;
} // namespace upload
} // namespace oxygen::engine

namespace oxygen::engine::internal {

//! LUT dimensions for sky atmosphere precomputation.

struct SkyAtmosphereLutConfig {
  //! Transmittance LUT width (cos_zenith parameterization).
  uint32_t transmittance_width { 256U };

  //! Transmittance LUT height (altitude parameterization).
  uint32_t transmittance_height { 96U };

  //! Sky-view LUT width (azimuth parameterization).
  uint32_t sky_view_width { 384U };

  //! Sky-view LUT height (zenith parameterization).
  uint32_t sky_view_height { 216U };

  //! Number of altitude slices in the sky-view LUT array (UI range: 4..32).
  uint32_t sky_view_slices { 16U };

  //! Altitude mapping mode for sky-view LUT slices (0 = linear, 1 = log).
  uint32_t sky_view_alt_mapping_mode { 1U };

  //! Multiple scattering LUT size (32x32 common).
  uint32_t multi_scat_size { 32U };

  //! Camera volume LUT width (screen-space froxel resolution).
  uint32_t camera_volume_width { 160U };

  //! Camera volume LUT height (screen-space froxel resolution).
  uint32_t camera_volume_height { 90U };

  //! Camera volume LUT depth (number of depth slices, typically 32).
  uint32_t camera_volume_depth { 32U };
};

//! Default LUT configuration for atmosphere precomputation.
inline constexpr SkyAtmosphereLutConfig kDefaultSkyAtmosphereLutConfig {};

//! Manages persistent LUT textures for sky atmosphere rendering.
/*!
 Owns the transmittance and sky-view LUT textures used for physically-based
 atmospheric scattering. Textures are created as UAV targets for compute shader
 generation and exposed via bindless SRV slots for sampling.

 ### Dirty State Tracking

 The manager tracks whether atmosphere parameters have changed since the last
 LUT generation. Call `UpdateParameters()` with current atmosphere parameters;
 if they differ from the cached values, `IsDirty()` returns true. After the
 compute pass regenerates the LUTs, call `MarkClean()`.

 ### Resource Lifecycle

 Textures are created lazily on first access and persist for the manager's
 lifetime. The manager registers textures with the resource registry and
 allocates shader-visible descriptors from the bindless heap.

 @see SkyAtmosphereLutComputePass, GpuSkyAtmosphereParams
*/
class SkyAtmosphereLutManager : public ISkyAtmosphereLutProvider {
public:
  using Config = SkyAtmosphereLutConfig;

  OXGN_RNDR_NDAPI explicit SkyAtmosphereLutManager(observer_ptr<Graphics> gfx,
    observer_ptr<upload::UploadCoordinator> uploader,
    observer_ptr<upload::StagingProvider> staging_provider,
    Config config = kDefaultSkyAtmosphereLutConfig);

  OXGN_RNDR_API ~SkyAtmosphereLutManager() override;

  OXYGEN_MAKE_NON_COPYABLE(SkyAtmosphereLutManager)
  OXYGEN_DEFAULT_MOVABLE(SkyAtmosphereLutManager)

  //=== Resource Access ===---------------------------------------------------//

  //! Returns shader-visible SRV index for the transmittance LUT, or
  //! `kInvalidShaderVisibleIndex` if resources are not yet created.
  OXGN_RNDR_NDAPI auto GetTransmittanceLutSlot() const noexcept
    -> ShaderVisibleIndex override;

  //! Returns transmittance LUT dimensions.
  [[nodiscard]] auto GetTransmittanceLutSize() const noexcept
    -> Extent<uint32_t> override
  {
    return {
      .width = config_.transmittance_width,
      .height = config_.transmittance_height,
    };
  }

  //! Returns shader-visible SRV index for the sky-view LUT, or
  //! `kInvalidShaderVisibleIndex` if resources are not yet created.
  OXGN_RNDR_NDAPI auto GetSkyViewLutSlot() const noexcept
    -> ShaderVisibleIndex override;

  //! Returns sky-view LUT dimensions (per-slice, not total texels).
  [[nodiscard]] auto GetSkyViewLutSize() const noexcept
    -> Extent<uint32_t> override
  {
    return {
      .width = config_.sky_view_width,
      .height = config_.sky_view_height,
    };
  }

  //! Returns the number of altitude slices in the sky-view LUT array.
  [[nodiscard]] auto GetSkyViewLutSlices() const noexcept -> uint32_t override
  {
    return config_.sky_view_slices;
  }

  //! Returns the altitude mapping mode (0 = linear, 1 = log).
  [[nodiscard]] auto GetAltMappingMode() const noexcept -> uint32_t override
  {
    return config_.sky_view_alt_mapping_mode;
  }

  //! Sets the number of altitude slices for the sky-view LUT.
  //!
  //! If the count changes, existing resources are destroyed and will be
  //! recreated on the next frame with the new array size. [P16]
  OXGN_RNDR_API auto SetSkyViewLutSlices(uint32_t slices) -> void;

  //! Sets the altitude mapping mode (0 = linear, 1 = log).
  OXGN_RNDR_API auto SetAltMappingMode(uint32_t mode) -> void;

  //! Returns shader-visible SRV index for the multiple scattering LUT.
  OXGN_RNDR_NDAPI auto GetMultiScatLutSlot() const noexcept
    -> ShaderVisibleIndex override;

  //! Returns multiple scattering LUT dimensions.
  [[nodiscard]] auto GetMultiScatLutSize() const noexcept
    -> Extent<uint32_t> override
  {
    return { config_.multi_scat_size, config_.multi_scat_size };
  }

  //! Returns shader-visible SRV index for the camera volume LUT.
  OXGN_RNDR_NDAPI auto GetCameraVolumeLutSlot() const noexcept
    -> ShaderVisibleIndex override;

  //! Returns camera volume LUT dimensions (width, height, depth).
  [[nodiscard]] auto GetCameraVolumeLutSize() const noexcept
    -> std::tuple<uint32_t, uint32_t, uint32_t> override
  {
    return { config_.camera_volume_width, config_.camera_volume_height,
      config_.camera_volume_depth };
  }

  //! Returns shader-visible SRV index for the blue noise texture.
  OXGN_RNDR_NDAPI auto GetBlueNoiseSlot() const noexcept
    -> ShaderVisibleIndex override;

  //! Returns blue noise texture dimensions (width, height, slices).
  [[nodiscard]] auto GetBlueNoiseSize() const noexcept
    -> std::tuple<uint32_t, uint32_t, uint32_t> override;

  //=== Parameter Tracking ===------------------------------------------------//

  //! Updates cached atmosphere parameters and sets dirty flag if changed.
  /*!
   Compares the provided parameters against the cached values. If any relevant
   parameter differs, the manager is marked dirty.

   @param params Current atmosphere parameters from the scene.
  */
  OXGN_RNDR_API auto UpdateParameters(const GpuSkyAtmosphereParams& params)
    -> void override;

  //! Returns true if LUTs need regeneration.
  OXGN_RNDR_NDAPI auto IsDirty() const noexcept -> bool;

  //! Clears the dirty flag after LUT regeneration.
  OXGN_RNDR_API auto MarkClean() noexcept -> void;

  //! Forces LUTs to regenerate on next frame.
  /*!
   Use when external state affecting LUT generation changes (e.g., debug flags).
  */
  OXGN_RNDR_API auto MarkDirty() noexcept -> void;

  //! Sets atmosphere debug/feature flags for LUT generation.
  /*!
   These flags affect how the sky-view LUT is computed (e.g., use ambient term
   instead of Rayleigh phase function). If the flags change, the LUTs are
   marked dirty and will regenerate.

   @param flags Bitfield of AtmosphereFlags values.
  */
  OXGN_RNDR_API auto SetAtmosphereFlags(uint32_t flags) noexcept -> void;

  //! Returns the current atmosphere flags.
  [[nodiscard]] auto GetAtmosphereFlags() const noexcept -> uint32_t
  {
    return atmosphere_flags_;
  }

  //! Returns true if LUTs have been generated at least once.
  /*!
   When true, the LUT textures are in SRV state (ready for sampling).
   When false, they're still in their creation state (UAV).
  */
  OXGN_RNDR_NDAPI auto HasBeenGenerated() const noexcept -> bool override;

  //! Marks that LUTs have been successfully generated.
  /*!
   Called after compute passes complete to indicate textures are now in
   SRV state, ready for sampling in rendering passes.
  */
  OXGN_RNDR_API auto MarkGenerated() noexcept -> void;

  //! Returns a monotonic generation token that increases when parameters
  //! change.
  [[nodiscard]] auto GetGeneration() const noexcept -> std::uint64_t override
  {
    return generation_;
  }

  //! Returns the current planet radius in meters.
  /*!
   Used by the compute pass for horizon-aware LUT generation.
  */
  [[nodiscard]] auto GetPlanetRadiusMeters() const noexcept -> float
  {
    return cached_params_.planet_radius_m;
  }

  //! Returns the current atmosphere height in meters.
  /*!
   Used by the compute pass to derive per-slice altitude via the mapping
   function h(t) = H * (2^t - 1).
  */
  [[nodiscard]] auto GetAtmosphereHeightMeters() const noexcept -> float
  {
    return cached_params_.atmosphere_height_m;
  }

  //! Updates cached sun state and marks dirty when elevation changes.
  /*!
   Sky-view LUT parameterization depends on sun elevation (zenith cosine).
   Azimuth does not impact LUT generation, but is preserved for consumers
   that need full sun metadata. Changing the enabled flag also triggers a
   dirty mark so LUTs regenerate when toggling the sun contribution.

   @param sun Value object containing sun direction/intensity and derived
     zenith cosine.
  */
  auto UpdateSunState(const SunState& sun) noexcept -> void;

  //! Returns the cached sun state used for LUT generation.
  [[nodiscard]] auto GetSunState() const noexcept -> const SunState&
  {
    return sun_state_;
  }

  //! Returns the transmittance LUT texture (for UAV binding in compute pass).
  /*!
   @return The transmittance LUT texture, or nullptr if not yet created.
  */
  OXGN_RNDR_NDAPI auto GetTransmittanceLutTexture() const noexcept
    -> observer_ptr<graphics::Texture>;

  //! Returns the sky-view LUT texture (for UAV binding in compute pass).
  /*!
   @return The sky-view LUT texture, or nullptr if not yet created.
  */
  OXGN_RNDR_NDAPI auto GetSkyViewLutTexture() const noexcept
    -> observer_ptr<graphics::Texture>;

  //! Returns the multiple scattering LUT texture.
  OXGN_RNDR_NDAPI auto GetMultiScatLutTexture() const noexcept
    -> observer_ptr<graphics::Texture>;

  //! Returns the camera volume LUT texture (for UAV binding in compute pass).
  /*!
   @return The camera volume LUT texture, or nullptr if not yet created.
  */
  OXGN_RNDR_NDAPI auto GetCameraVolumeLutTexture() const noexcept
    -> observer_ptr<graphics::Texture>;

  //! Returns the blue noise texture.
  OXGN_RNDR_NDAPI auto GetBlueNoiseTexture() const noexcept
    -> observer_ptr<graphics::Texture>;

  //! Returns shader-visible UAV index for the transmittance LUT.
  /*!
   Used by the compute pass to bind the LUT as a write target.
   Returns `kInvalidShaderVisibleIndex` if resources are not yet created.
  */
  OXGN_RNDR_NDAPI auto GetTransmittanceLutUavSlot() const noexcept
    -> ShaderVisibleIndex;

  //! Returns shader-visible UAV index for the sky-view LUT.
  /*!
   Used by the compute pass to bind the LUT as a write target.
   Returns `kInvalidShaderVisibleIndex` if resources are not yet created.
  */
  OXGN_RNDR_NDAPI auto GetSkyViewLutUavSlot() const noexcept
    -> ShaderVisibleIndex;

  //! Returns shader-visible UAV index for the multiple scattering LUT.
  OXGN_RNDR_NDAPI auto GetMultiScatLutUavSlot() const noexcept
    -> ShaderVisibleIndex;

  //! Returns shader-visible UAV index for the camera volume LUT.
  /*!
   Used by the compute pass to bind the LUT as a write target.
   Returns `kInvalidShaderVisibleIndex` if resources are not yet created.
  */
  OXGN_RNDR_NDAPI auto GetCameraVolumeLutUavSlot() const noexcept
    -> ShaderVisibleIndex;

  //! Ensures textures and descriptors are created.
  /*!
   Called by the compute pass before first execution. Idempotent.

   @return True if resources are ready for use.
  */
  OXGN_RNDR_API auto EnsureResourcesCreated() -> bool;

private:
  struct CachedParams {
    float planet_radius_m { 0.0F };
    float atmosphere_height_m { 0.0F };
    float rayleigh_scale_height_m { 0.0F };
    float mie_scale_height_m { 0.0F };
    float mie_g { 0.0F };

    float multi_scattering_factor { 0.0F };
    // RGB values are compared component-wise.
    float rayleigh_r { 0.0F };
    float rayleigh_g { 0.0F };
    float rayleigh_b { 0.0F };
    float mie_r { 0.0F };
    float mie_g_val { 0.0F };
    float mie_b { 0.0F };
    float absorption_r { 0.0F };
    float absorption_g { 0.0F };
    float absorption_b { 0.0F };
    float ground_albedo_r { 0.0F };
    float ground_albedo_g { 0.0F };
    float ground_albedo_b { 0.0F };

    // New absorption parameters
    atmos::DensityProfile absorption_density;

    // Slice config is tracked so changes trigger re-creation [P15].
    uint32_t sky_view_slices { 0 };
    uint32_t sky_view_alt_mapping_mode { 0 };
    // Sun disk parameters are tracked to verify propagation.
    uint32_t sun_disk_enabled { 0 };
    float sun_disk_angular_radius_radians { 0.0F };

    // Other parameters that affect rendering state
    float aerial_perspective_distance_scale { 1.0F };
    uint32_t enabled { 0 };

    [[nodiscard]] auto operator==(const CachedParams& other) const noexcept
      -> bool
      = default;
  };

  struct LutResources {
    std::shared_ptr<graphics::Texture> texture;
    graphics::NativeView srv_view;
    graphics::NativeView uav_view;
    ShaderVisibleIndex srv_index { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex uav_index { kInvalidShaderVisibleIndex };
  };

  observer_ptr<Graphics> gfx_;
  observer_ptr<upload::UploadCoordinator> uploader_;
  observer_ptr<upload::StagingProvider> staging_;
  Config config_;

  CachedParams cached_params_ {};
  SunState sun_state_;
  uint32_t atmosphere_flags_ { 0 }; //!< Debug/feature flags for LUT generation
  mutable std::uint64_t generation_ { 1 };
  bool dirty_ { true };
  bool resources_created_ { false };
  bool luts_generated_ { false }; //!< True after first successful compute

  mutable std::optional<upload::UploadTicket> blue_noise_upload_ticket_;
  mutable bool blue_noise_ready_ { false };

  LutResources transmittance_lut_ {};
  LutResources sky_view_lut_ {};
  LutResources multi_scat_lut_ {};
  LutResources camera_volume_lut_ {};
  LutResources blue_noise_lut_ {};

  //! Creates transmittance LUT texture (2D, RGBA16F).
  auto CreateTransmittanceLutTexture(Extent<uint32_t> extent)
    -> std::shared_ptr<graphics::Texture>;

  //! Creates sky-view LUT texture (2D array, RGBA16F).
  auto CreateSkyViewLutTexture(Extent<uint32_t> extent, uint32_t num_slices)
    -> std::shared_ptr<graphics::Texture>;

  //! Creates multi-scattering LUT texture (2D, RGBA16F).
  auto CreateMultiScatLutTexture(uint32_t size)
    -> std::shared_ptr<graphics::Texture>;

  //! Creates camera volume LUT texture (3D, RGBA16F).
  auto CreateCameraVolumeLutTexture(Extent<uint32_t> extent, uint32_t depth)
    -> std::shared_ptr<graphics::Texture>;

  //! Common implementation for creating LUT textures.
  auto CreateLutTexture(Extent<uint32_t> extent, uint32_t depth_or_array_size,
    bool is_rgba, const char* debug_name, TextureType texture_type)
    -> std::shared_ptr<graphics::Texture>;

  //! Creates blue noise texture (3D, R8_UNORM).
  auto CreateBlueNoiseTexture() -> std::shared_ptr<graphics::Texture>;

  //! Submits Blue Noise data for upload to GPU via the uploader coordinator.
  auto UploadBlueNoiseData() -> void;

  //! Creates SRV/UAV views for a LUT, optionally as array views [P2, P11].
  auto CreateLutViews(LutResources& lut, uint32_t array_size, bool is_rgba)
    -> bool;

  auto CleanupResources() -> void;

  [[nodiscard]] static auto ExtractCachedParams(
    const GpuSkyAtmosphereParams& params) -> CachedParams;
};

} // namespace oxygen::engine::internal
