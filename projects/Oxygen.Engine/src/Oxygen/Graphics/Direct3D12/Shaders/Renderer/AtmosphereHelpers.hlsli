//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_D3D12_SHADERS_RENDERER_ATMOSPHEREHELPERS_HLSLI
#define OXYGEN_D3D12_SHADERS_RENDERER_ATMOSPHEREHELPERS_HLSLI

//===----------------------------------------------------------------------===//
// NOTE: This file previously contained a fake per-mesh fog implementation.
//
// That implementation has been REMOVED because:
// 1. It was mutually exclusive with Aerial Perspective (never ran when AP enabled)
// 2. It only affected mesh surfaces, not the sky (no volumetric haze)
// 3. Aerial Perspective already provides physically-based distance fog
//
// For distance-based atmospheric effects, use:
// - SkyAtmosphere with Aerial Perspective enabled (AerialPerspective.hlsli)
//
// A real volumetric fog system may be implemented in the future as a
// dedicated post-process or volumetric rendering pass.
//===----------------------------------------------------------------------===//

#endif // OXYGEN_D3D12_SHADERS_RENDERER_ATMOSPHEREHELPERS_HLSLI
