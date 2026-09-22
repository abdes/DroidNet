//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_VORTEX_LIGHTING_INDICES_HLSLI
#define OXYGEN_VORTEX_LIGHTING_INDICES_HLSLI

// Matches the absent-array-element value in Vortex/Types/LightingIndices.h.
// This value is unrelated to a descriptor allocation or shader-visible slot.
static const uint LIGHTING_INVALID_ARRAY_INDEX = 0xffffffffu;

// A range with this offset enumerates the complete local light record array.
static const uint COMPLETE_LIGHT_LIST_OFFSET = LIGHTING_INVALID_ARRAY_INDEX;

#endif
