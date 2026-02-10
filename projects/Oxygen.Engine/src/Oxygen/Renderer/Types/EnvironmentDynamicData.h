//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>

#include <glm/vec4.hpp>

#include <Oxygen/Renderer/Types/SceneConstants.h>

namespace oxygen::engine {

//! Bitfield flags for atmosphere rendering features.
/*!
 Controls which atmosphere rendering path is used and enables debug features.
 These flags must match the HLSL constants in AerialPerspective.hlsli.
*/
enum class AtmosphereFlags : uint32_t {
  kNone = 0x0,
  kUseLut = 0x1, //!< Use LUT sampling when LUTs are available.
  kOverrideSun = 0x2, //!< Use debug override sun direction/intensity.
};

//! Bitwise OR for AtmosphereFlags.
constexpr auto operator|(AtmosphereFlags lhs, AtmosphereFlags rhs) noexcept
  -> AtmosphereFlags
{
  return static_cast<AtmosphereFlags>(
    static_cast<uint32_t>(lhs) | static_cast<uint32_t>(rhs));
}

//! Bitwise AND for AtmosphereFlags.
constexpr auto operator&(AtmosphereFlags lhs, AtmosphereFlags rhs) noexcept
  -> AtmosphereFlags
{
  return static_cast<AtmosphereFlags>(
    static_cast<uint32_t>(lhs) & static_cast<uint32_t>(rhs));
}

//! Check if a flag is set.
constexpr auto HasFlag(uint32_t flags, AtmosphereFlags flag) noexcept -> bool
{
  return (flags & static_cast<uint32_t>(flag)) != 0;
}

struct ClusterGridSlot {
  ShaderVisibleIndex value;
  explicit constexpr ClusterGridSlot(
    const ShaderVisibleIndex v = kInvalidShaderVisibleIndex)
    : value(v)
  {
  }
  constexpr auto IsValid() const noexcept
  {
    return value != kInvalidShaderVisibleIndex;
  }
  constexpr auto operator<=>(const ClusterGridSlot&) const = default;
  constexpr operator uint32_t() const noexcept { return value.get(); }
};

struct ClusterIndexListSlot {
  ShaderVisibleIndex value;
  explicit constexpr ClusterIndexListSlot(
    const ShaderVisibleIndex v = kInvalidShaderVisibleIndex)
    : value(v)
  {
  }
  constexpr auto IsValid() const noexcept
  {
    return value != kInvalidShaderVisibleIndex;
  }
  constexpr auto operator<=>(const ClusterIndexListSlot&) const = default;
  constexpr operator uint32_t() const noexcept { return value.get(); }
};

//! Per-frame environment payload consumed directly by shaders.
/*!
 This structure is intended to be bound as a **root CBV** (register b3) and is
 therefore kept small and frequently updated ("hot").

 The payload contains:

 - View-exposure values computed by the renderer (potentially from authored
   post-process settings + auto-exposure).
 - Bindless SRV slots and dimensions for clustered lighting buffers.
 - Z-binning parameters for clustered light culling.

 Layout mirrors the HLSL struct `EnvironmentDynamicData`.

 ### Cluster Configuration

 For tile-based Forward+ (cluster_dim_z == 1):
 - Only X/Y dimensions are used
 - Z-binning parameters are ignored
 - Per-tile min/max depth from depth prepass provides depth bounds

 For clustered (cluster_dim_z > 1):
 - Full 3D grid with logarithmic depth slices
 - Z-binning: slice = log2(z / z_near) * z_scale + z_bias

 ### Ownership and Future Extension

 Currently owned and uploaded by **LightCullingPass**, which populates cluster
 data and uses a default value (1.0) for exposure.

 When post-process/auto-exposure is implemented, the owning pass has two
 options:
 1. Query the LightCullingPass buffer and overwrite exposure fields, or
 2. Split into separate CBVs (ClusterData on b3, ExposureData on b4).

 @warning This struct must remain 16-byte aligned for D3D12 root CBV bindings.
 @see ClusterConfig, LightCullingPass
*/
struct alignas(16) EnvironmentDynamicData {
  // Exposure
  float exposure { 1.0F };

  // Cluster grid bindless slots (from LightCullingPass)
  ClusterGridSlot bindless_cluster_grid_slot {};
  ClusterIndexListSlot bindless_cluster_index_list_slot {};

  // Padding to complete the first 16-byte register.
  uint32_t _pad0 { 0u };

  // Cluster grid dimensions
  uint32_t cluster_dim_x { 0 };
  uint32_t cluster_dim_y { 0 };
  uint32_t cluster_dim_z { 0 };
  uint32_t tile_size_px { 16 };

  // Z-binning parameters for clustered lighting
  float z_near { 0.1F };
  float z_far { 1000.0F };
  float z_scale { 0.0F };
  float z_bias { 0.0F };

  // Per-view aerial perspective controls (SkyAtmosphere).
  float aerial_perspective_distance_scale { 1.0F };
  float aerial_scattering_strength { 1.0F };

  // Atmospheric debug/feature flags (bitfield)
  // bit0: use LUT sampling when available
  // bit1: use override sun values
  uint32_t atmosphere_flags { 0u };

  // 1 = sun enabled; 0 = fallback to default sun.
  uint32_t sun_enabled { 0u };

  // Designated sun light (toward the sun, not incoming radiance).
  // xyz = direction (Z-up: +Y with 30° elevation), w = illuminance.
  glm::vec4 sun_direction_ws_illuminance { 0.0F, 0.866F, 0.5F, 0.0F };

  // Sun spectral payload.
  // xyz = color_rgb (linear, not premultiplied), w = intensity.
  glm::vec4 sun_color_rgb_intensity { 1.0F, 1.0F, 1.0F, 1.0F };

  // Debug override sun for testing (internal only).
  // xyz = direction (Z-up: +Y with 30° elevation), w = illuminance.
  glm::vec4 override_sun_direction_ws_illuminance { 0.0F, 0.866F, 0.5F, 0.0F };

  // x = override_sun_enabled; remaining lanes reserved for future flags.
  glm::uvec4 override_sun_flags { 0u, 0u, 0u, 0u };

  // Override sun spectral payload (internal only).
  // xyz = color_rgb (linear, not premultiplied), w = intensity.
  glm::vec4 override_sun_color_rgb_intensity { 1.0F, 1.0F, 1.0F, 1.0F };

  // Planet/frame context for atmospheric sampling.
  // xyz = planet center (below Z=0 ground). w = padding (unused).
  // Note: planet_radius is in EnvironmentStaticData (static parameter).
  glm::vec4 planet_center_ws_pad { 0.0F, 0.0F, -6360000.0F, 0.0F };

  // xyz = planet up, w = camera altitude (m).
  glm::vec4 planet_up_ws_camera_altitude_m { 0.0F, 0.0F, 1.0F, 0.0F };

  // x = sky view LUT slice, y = planet_to_sun_cos_zenith.
  glm::vec4 sky_view_lut_slice_cos_zenith { 0.0F, 0.0F, 0.0F, 0.0F };
};
static_assert(alignof(EnvironmentDynamicData) == 16,
  "EnvironmentDynamicData must stay 16-byte aligned for root CBV");
static_assert(sizeof(EnvironmentDynamicData) % 16 == 0,
  "EnvironmentDynamicData size must be 16-byte aligned");
static_assert(sizeof(EnvironmentDynamicData) == 192,
  "EnvironmentDynamicData size must match HLSL cbuffer packing");

static_assert(offsetof(EnvironmentDynamicData, cluster_dim_x) == 16,
  "EnvironmentDynamicData layout mismatch: cluster_dim_x offset");
static_assert(
  offsetof(EnvironmentDynamicData, sun_direction_ws_illuminance) == 64,
  "EnvironmentDynamicData layout mismatch: sun block offset");

//! Simple POD to aggregate light-culling related binding/configuration data
//! passed from the culling pass into the environment dynamic data manager.
struct LightCullingData {
  ClusterGridSlot bindless_cluster_grid_slot {};
  ClusterIndexListSlot bindless_cluster_index_list_slot {};
  uint32_t cluster_dim_x { 0 };
  uint32_t cluster_dim_y { 0 };
  uint32_t cluster_dim_z { 0 };
  uint32_t tile_size_px { 16 };
};

} // namespace oxygen::engine
