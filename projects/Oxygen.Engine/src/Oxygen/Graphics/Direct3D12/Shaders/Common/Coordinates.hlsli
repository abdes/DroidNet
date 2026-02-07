//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

//! Common Coordinate System Utilities
//!
//! Provides coordinate transformations, UV conversions, and planet-centered
//! coordinate system helpers used across the rendering pipeline.

#ifndef OXYGEN_SHADERS_COMMON_COORDINATES_HLSLI
#define OXYGEN_SHADERS_COMMON_COORDINATES_HLSLI

#include "Math.hlsli"

// ============================================================================
// Planet-Centered Coordinate Transforms
// ============================================================================

//! Computes planet-centered position from altitude.
//!
//! Places the position at the given altitude on the +Z axis (zenith).
//! This is the standard reference position for atmosphere calculations.
//!
//! @param planet_radius Planet radius in meters.
//! @param altitude Altitude above planet surface in meters.
//! @return Position in planet-centered coordinates (0, 0, planet_radius + altitude).
float3 PlanetCenteredPositionFromAltitude(float planet_radius, float altitude)
{
    float r = planet_radius + altitude;
    return float3(0.0, 0.0, r);
}

//! Computes altitude from planet-centered position.
//!
//! @param position Position in planet-centered coordinates.
//! @param planet_radius Planet radius in meters.
//! @return Altitude above planet surface in meters.
float AltitudeFromPosition(float3 position, float planet_radius)
{
    return length(position) - planet_radius;
}

//! Computes normalized direction from planet center to position.
//!
//! Useful for computing zenith angles and horizon calculations.
//!
//! @param position Position in planet-centered coordinates.
//! @return Normalized direction from planet center.
float3 DirectionFromPosition(float3 position)
{
    return normalize(position);
}

//! Converts world-space position to planet-centered coordinates.
//!
//! @param world_pos Position in world space.
//! @param planet_center_world Planet center in world space.
//! @return Position in planet-centered coordinates.
float3 PlanetCenteredPositionFromWorld(float3 world_pos, float3 planet_center_world)
{
    return world_pos - planet_center_world;
}

//! Converts planet-centered position to world space.
//!
//! @param planet_pos Position in planet-centered coordinates.
//! @param planet_center_world Planet center in world space.
//! @return Position in world space.
float3 WorldPositionFromPlanetCentered(float3 planet_pos, float3 planet_center_world)
{
    return planet_pos + planet_center_world;
}

// ============================================================================
// View and Direction Helpers
// ============================================================================

//! Computes view direction from world-space camera and target positions.
//!
//! @param camera_pos Camera position in world space.
//! @param target_pos Target position in world space.
//! @return Normalized view direction.
float3 ViewDirectionFromWorld(float3 camera_pos, float3 target_pos)
{
    return normalize(target_pos - camera_pos);
}

//! Computes view ray from UV coordinates (simple perspective projection).
//!
//! This is a basic helper for converting screen-space UV to view direction.
//! For more complex projections, use camera-specific transforms.
//!
//! @param uv Screen-space UV coordinates [0, 1].
//! @param fov_y Vertical field of view in radians.
//! @param aspect_ratio Aspect ratio (width / height).
//! @return Normalized view direction in camera space (Z-forward).
float3 ViewRayFromUv(float2 uv, float fov_y, float aspect_ratio)
{
    // Convert UV to NDC [-1, 1]
    float2 ndc = uv * 2.0 - 1.0;
    ndc.y = -ndc.y; // Flip Y for standard screen coordinates

    // Compute view direction
    float tan_half_fov = tan(fov_y * 0.5);
    float3 dir;
    dir.x = ndc.x * aspect_ratio * tan_half_fov;
    dir.y = ndc.y * tan_half_fov;
    dir.z = 1.0;

    return normalize(dir);
}

// ============================================================================
// UV and Texture Coordinate Helpers
// ============================================================================

//! Converts texel coordinates to UV with half-texel offset.
//!
//! Ensures sampling at texel centers for correct filtering.
//!
//! @param texel Texel coordinates (integer or float).
//! @param texture_size Texture dimensions.
//! @return UV coordinates [0, 1] at texel center.
float2 TexelToUv(float2 texel, float2 texture_size)
{
    return (texel + 0.5) / texture_size;
}

//! Converts UV to texel coordinates.
//!
//! @param uv UV coordinates [0, 1].
//! @param texture_size Texture dimensions.
//! @return Texel coordinates (float).
float2 UvToTexel(float2 uv, float2 texture_size)
{
    return uv * texture_size - 0.5;
}

//! Applies half-texel offset to UV coordinates for LUT sampling.
//!
//! This is critical for correct LUT sampling to avoid bilinear filtering
//! artifacts at LUT boundaries.
//!
//! @param uv UV coordinates [0, 1].
//! @param texture_size Texture dimensions.
//! @return UV coordinates with half-texel offset applied.
float2 ApplyHalfTexelOffset(float2 uv, float2 texture_size)
{
    float2 texel = uv * texture_size;
    float2 texel_floor = floor(texel);
    float2 texel_frac = texel - texel_floor;

    // Clamp to [0.5, size - 0.5] texel range
    float2 texel_clamped = clamp(texel, float2(0.5, 0.5), texture_size - 0.5);

    return texel_clamped / texture_size;
}

// ============================================================================
// Clip, View, and World Space Helpers
// ============================================================================

//! Converts clip-space position to view space.
//!
//! @param clip_pos Position in clip space.
//! @param inv_projection Inverse projection matrix.
//! @return Position in view space.
float3 ClipToViewSpace(float4 clip_pos, float4x4 inv_projection)
{
    float4 view_pos = mul(inv_projection, clip_pos);
    return view_pos.xyz / view_pos.w;
}

//! Converts view-space position to world space.
//!
//! @param view_pos Position in view space.
//! @param inv_view Inverse view matrix.
//! @return Position in world space.
float3 ViewToWorldSpace(float3 view_pos, float4x4 inv_view)
{
    float4 world_pos = mul(inv_view, float4(view_pos, 1.0));
    return world_pos.xyz;
}

//! Converts clip-space position to world space.
//!
//! @param clip_pos Position in clip space.
//! @param inv_view_projection Inverse view-projection matrix.
//! @return Position in world space.
float3 ClipToWorldSpace(float4 clip_pos, float4x4 inv_view_projection)
{
    float4 world_pos = mul(inv_view_projection, clip_pos);
    return world_pos.xyz / world_pos.w;
}

// ============================================================================
// Spherical Coordinate Helpers
// ============================================================================

//! Converts Cartesian direction to spherical coordinates.
//!
//! @param dir Normalized direction vector.
//! @param theta [out] Polar angle (zenith) in radians [0, π].
//! @param phi [out] Azimuthal angle in radians [0, 2π].
void CartesianToSpherical(float3 dir, out float theta, out float phi)
{
    theta = acos(clamp(dir.z, -1.0, 1.0));
    phi = atan2(dir.y, dir.x);
    if (phi < 0.0)
    {
        phi += TWO_PI;
    }
}

//! Converts spherical coordinates to Cartesian direction.
//!
//! @param theta Polar angle (zenith) in radians [0, π].
//! @param phi Azimuthal angle in radians [0, 2π].
//! @return Normalized direction vector.
float3 SphericalToCartesian(float theta, float phi)
{
    float sin_theta = sin(theta);
    return float3(
        sin_theta * cos(phi),
        sin_theta * sin(phi),
        cos(theta)
    );
}

#endif // OXYGEN_SHADERS_COMMON_COORDINATES_HLSLI
