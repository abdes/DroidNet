//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

//! Common Geometric Utilities
//!
//! Provides ray-sphere intersections, horizon calculations, and distance utilities
//! used across the rendering pipeline.

#ifndef OXYGEN_SHADERS_COMMON_GEOMETRY_HLSLI
#define OXYGEN_SHADERS_COMMON_GEOMETRY_HLSLI

#include "Math.hlsli"

// ============================================================================
// Ray-Sphere Intersection
// ============================================================================

//! Computes ray-sphere intersection distance (nearest intersection).
//!
//! Returns the distance to the nearest intersection point along the ray.
//! For rays originating inside the sphere, returns the exit distance.
//! For rays originating outside, returns the entry distance.
//!
//! @param origin Ray origin (relative to sphere center).
//! @param dir Ray direction (normalized).
//! @param radius Sphere radius.
//! @return Distance to intersection, or -1 if no intersection.
float RaySphereIntersectNearest(float3 origin, float3 dir, float radius)
{
    float b = dot(origin, dir);
    float c = dot(origin, origin) - radius * radius;
    float discriminant = b * b - c;

    if (discriminant < 0.0)
    {
        return -1.0;
    }

    float sqrt_disc = SafeSqrt(discriminant);
    float t0 = -b - sqrt_disc;
    float t1 = -b + sqrt_disc;

    // Return the positive intersection (exit point for inside, entry for outside)
    if (t0 > 0.0)
    {
        return t0;
    }
    if (t1 > 0.0)
    {
        return t1;
    }
    return -1.0;
}

//! Computes both ray-sphere intersection distances.
//!
//! @param origin Ray origin (relative to sphere center).
//! @param dir Ray direction (normalized).
//! @param radius Sphere radius.
//! @param t0 [out] Near intersection distance.
//! @param t1 [out] Far intersection distance.
//! @return True if intersection exists, false otherwise.
bool RaySphereIntersectBoth(float3 origin, float3 dir, float radius, out float t0, out float t1)
{
    float b = dot(origin, dir);
    float c = dot(origin, origin) - radius * radius;
    float discriminant = b * b - c;

    if (discriminant < 0.0)
    {
        t0 = -1.0;
        t1 = -1.0;
        return false;
    }

    float sqrt_disc = SafeSqrt(discriminant);
    t0 = -b - sqrt_disc;
    t1 = -b + sqrt_disc;
    return true;
}

// ============================================================================
// Horizon and Distance Utilities
// ============================================================================

//! Computes the cosine of the horizon angle at a given altitude.
//!
//! At altitude h above a planet of radius R, the horizon is where a ray
//! from the observer is tangent to the planet surface:
//! cos(horizon_zenith) = -sqrt(1 - (R/(R+h))²)
//!
//! @param planet_radius Planet radius in meters.
//! @param altitude Altitude above planet surface in meters.
//! @return Cosine of horizon zenith angle (negative value).
float HorizonCosineFromAltitude(float planet_radius, float altitude)
{
    float r = planet_radius + altitude;
    float rho = planet_radius / r;
    return -SafeSqrt(1.0 - rho * rho);
}

//! Computes the horizon distance at a given altitude.
//!
//! This is the distance to the horizon along the planet surface.
//!
//! @param planet_radius Planet radius in meters.
//! @param altitude Altitude above planet surface in meters.
//! @return Distance to horizon in meters.
float HorizonDistanceFromAltitude(float planet_radius, float altitude)
{
    return SafeSqrt(2.0 * planet_radius * altitude + altitude * altitude);
}

//! Selects the shorter of ground or atmosphere top intersection.
//!
//! Helper for determining ray length when raymarching through atmosphere.
//!
//! @param ground_dist Distance to ground intersection (-1 if no intersection).
//! @param top_dist Distance to atmosphere top intersection.
//! @return Shorter valid distance.
float SelectRayLengthGroundOrTop(float ground_dist, float top_dist)
{
    if (ground_dist > 0.0 && ground_dist < top_dist)
    {
        return ground_dist;
    }
    return top_dist;
}

//! Computes raymarch limits for atmosphere integration.
//!
//! @param origin Ray origin (planet-centered).
//! @param dir Ray direction (normalized).
//! @param planet_radius Planet radius in meters.
//! @param atmosphere_radius Atmosphere outer radius in meters.
//! @param ray_length [out] Distance to raymarch.
//! @param hits_ground [out] True if ray hits ground.
//! @return True if ray intersects atmosphere, false otherwise.
bool ComputeRaymarchLimits(
    float3 origin,
    float3 dir,
    float planet_radius,
    float atmosphere_radius,
    out float ray_length,
    out bool hits_ground)
{
    // Compute atmosphere exit distance
    float atmo_dist = RaySphereIntersectNearest(origin, dir, atmosphere_radius);
    if (atmo_dist < 0.0)
    {
        ray_length = 0.0;
        hits_ground = false;
        return false;
    }

    // Check ground intersection
    float ground_dist = RaySphereIntersectNearest(origin, dir, planet_radius);
    if (ground_dist > 0.0 && ground_dist < atmo_dist)
    {
        ray_length = ground_dist;
        hits_ground = true;
    }
    else
    {
        ray_length = atmo_dist;
        hits_ground = false;
    }

    return true;
}

// ============================================================================
// Distance and Angle Utilities
// ============================================================================

//! Computes the distance between two points.
//!
//! @param a First point.
//! @param b Second point.
//! @return Distance between points.
float Distance(float3 a, float3 b)
{
    return length(b - a);
}

//! Computes the squared distance between two points (faster than Distance).
//!
//! @param a First point.
//! @param b Second point.
//! @return Squared distance between points.
float DistanceSquared(float3 a, float3 b)
{
    float3 diff = b - a;
    return dot(diff, diff);
}

#endif // OXYGEN_SHADERS_COMMON_GEOMETRY_HLSLI
