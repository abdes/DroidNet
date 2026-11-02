//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//
//
// Shared scene environment enum catalogs.
//
// The canonical values of the scene environment enums that both the cooked
// scene format (Data) and the runtime scene (Scene) define. Each section below
// declares a catalog macro; every macro is undefined at the end of its
// section.
//
// ============================================================================
// SkyLightSource
// ============================================================================
// Where a sky light's image-based lighting comes from: the scene's
// sky/background captured by the renderer, or a specified cubemap asset.
//
// Expected consumer macro:
//   OXSCN_SKY_LIGHT_SOURCE(name, value)
//
#ifndef OXSCN_SKY_LIGHT_SOURCE
#  define OXSCN_SKY_LIGHT_SOURCE(name, value)
#endif

OXSCN_SKY_LIGHT_SOURCE(kCapturedScene, 0)
OXSCN_SKY_LIGHT_SOURCE(kSpecifiedCubemap, 1)

#undef OXSCN_SKY_LIGHT_SOURCE

// ============================================================================
// SkySphereSource
// ============================================================================
// What a sky sphere renders as the background: a specified cubemap asset, or
// a constant color.
//
// Expected consumer macro:
//   OXSCN_SKY_SPHERE_SOURCE(name, value)
//
#ifndef OXSCN_SKY_SPHERE_SOURCE
#  define OXSCN_SKY_SPHERE_SOURCE(name, value)
#endif

OXSCN_SKY_SPHERE_SOURCE(kCubemap, 0)
OXSCN_SKY_SPHERE_SOURCE(kSolidColor, 1)

#undef OXSCN_SKY_SPHERE_SOURCE
