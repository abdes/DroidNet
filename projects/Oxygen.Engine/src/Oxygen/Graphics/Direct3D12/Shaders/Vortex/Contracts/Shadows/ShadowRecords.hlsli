//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_VORTEX_SHADOW_RECORDS_HLSLI
#define OXYGEN_VORTEX_SHADOW_RECORDS_HLSLI

#include "Vortex/Contracts/Lighting/LightingIndices.hlsli"

static const uint SHADOW_PROJECTION_NONE = 0u;
static const uint SHADOW_PROJECTION_CASCADED_2D = 1u;
static const uint SHADOW_PROJECTION_LOCAL_CUBE = 2u;
static const uint SHADOW_PROJECTION_LOCAL_PROJECTED_2D = 3u;
static const uint SHADOW_COVERAGE_NO_REQUEST = 0u;
static const uint SHADOW_COVERAGE_NO_INFLUENCE = 1u;
static const uint SHADOW_COVERAGE_COMPLETE = 2u;
static const uint SHADOW_COVERAGE_OUTSIDE_AUTHORED = 3u;

// Matches Vortex/Shadows/Types/LightShadowReference.h (16 bytes).
struct LightShadowReference {
    uint projection_kind;
    uint record_index;
    uint selection_index;
    uint coverage_state;
};

// Matches Vortex/Shadows/Types/DirectionalShadowRecord.h (16 bytes).
struct DirectionalShadowRecord {
    uint selection_index;
    uint first_cascade;
    uint cascade_count;
    uint reserved;
};

#endif
