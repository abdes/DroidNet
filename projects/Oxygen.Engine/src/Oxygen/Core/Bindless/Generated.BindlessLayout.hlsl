//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Generated file - do not edit.
// Source: projects/Oxygen.Engine/src/Oxygen/Core/Bindless/Spec.yaml
// Source-Version: 1.0.0
// Schema-Version: 1.0.0
// Tool: BindlessCodeGen 1.0.0
// Generated: 2025-08-17 23:51:04

#ifndef OXYGEN_BINDLESS_LAYOUT_HLSL
#define OXYGEN_BINDLESS_LAYOUT_HLSL

static const uint K_INVALID_BINDLESS_INDEX = 0xffffffff;

// Debug-friendly domain guards helpers (generated)
static inline bool BX_IsInDomain(uint idx, uint base, uint capacity)
{
	return (idx >= base) && (idx < (base + capacity));
}

static inline uint BX_TryUseGlobalIndexInDomain(uint idx, uint base, uint capacity)
{
#ifdef BINDLESS_VALIDATE
	return BX_IsInDomain(idx, base, capacity) ? idx : 0xffffffff;
#else
	return idx;
#endif
}

// Scene constants CBV (b1), heap index 0; holds bindless indices table

static const uint K_SCENE_DOMAIN_BASE = 0;
static const uint K_SCENE_CAPACITY = 1;

// Unified SRV table base
static const uint K_GLOBAL_SRV_DOMAIN_BASE = 1;
static const uint K_GLOBAL_SRV_CAPACITY = 2048;

static const uint K_MATERIALS_DOMAIN_BASE = 2049;
static const uint K_MATERIALS_CAPACITY = 3047;

static const uint K_TEXTURES_DOMAIN_BASE = 5096;
static const uint K_TEXTURES_CAPACITY = 65536;

static const uint K_SAMPLERS_DOMAIN_BASE = 0;
static const uint K_SAMPLERS_CAPACITY = 256;

// Domain guard macros (generated)
#define BX_DOMAIN_BASE(TAG)   K_##TAG##_DOMAIN_BASE
#define BX_DOMAIN_CAP(TAG)    K_##TAG##_CAPACITY
#define BX_IN(TAG, IDX)       BX_IsInDomain((IDX), BX_DOMAIN_BASE(TAG), BX_DOMAIN_CAP(TAG))
#define BX_TRY(TAG, IDX)      BX_TryUseGlobalIndexInDomain((IDX), BX_DOMAIN_BASE(TAG), BX_DOMAIN_CAP(TAG))

#define BX_DOMAIN_SCENE_BASE K_SCENE_DOMAIN_BASE
#define BX_DOMAIN_SCENE_CAPACITY K_SCENE_CAPACITY
#define BX_IN_SCENE(IDX)  BX_IsInDomain((IDX), BX_DOMAIN_SCENE_BASE, BX_DOMAIN_SCENE_CAPACITY)
#define BX_TRY_SCENE(IDX) BX_TryUseGlobalIndexInDomain((IDX), BX_DOMAIN_SCENE_BASE, BX_DOMAIN_SCENE_CAPACITY)

#define BX_DOMAIN_GLOBAL_SRV_BASE K_GLOBAL_SRV_DOMAIN_BASE
#define BX_DOMAIN_GLOBAL_SRV_CAPACITY K_GLOBAL_SRV_CAPACITY
#define BX_IN_GLOBAL_SRV(IDX)  BX_IsInDomain((IDX), BX_DOMAIN_GLOBAL_SRV_BASE, BX_DOMAIN_GLOBAL_SRV_CAPACITY)
#define BX_TRY_GLOBAL_SRV(IDX) BX_TryUseGlobalIndexInDomain((IDX), BX_DOMAIN_GLOBAL_SRV_BASE, BX_DOMAIN_GLOBAL_SRV_CAPACITY)

#define BX_DOMAIN_MATERIALS_BASE K_MATERIALS_DOMAIN_BASE
#define BX_DOMAIN_MATERIALS_CAPACITY K_MATERIALS_CAPACITY
#define BX_IN_MATERIALS(IDX)  BX_IsInDomain((IDX), BX_DOMAIN_MATERIALS_BASE, BX_DOMAIN_MATERIALS_CAPACITY)
#define BX_TRY_MATERIALS(IDX) BX_TryUseGlobalIndexInDomain((IDX), BX_DOMAIN_MATERIALS_BASE, BX_DOMAIN_MATERIALS_CAPACITY)

#define BX_DOMAIN_TEXTURES_BASE K_TEXTURES_DOMAIN_BASE
#define BX_DOMAIN_TEXTURES_CAPACITY K_TEXTURES_CAPACITY
#define BX_IN_TEXTURES(IDX)  BX_IsInDomain((IDX), BX_DOMAIN_TEXTURES_BASE, BX_DOMAIN_TEXTURES_CAPACITY)
#define BX_TRY_TEXTURES(IDX) BX_TryUseGlobalIndexInDomain((IDX), BX_DOMAIN_TEXTURES_BASE, BX_DOMAIN_TEXTURES_CAPACITY)

#define BX_DOMAIN_SAMPLERS_BASE K_SAMPLERS_DOMAIN_BASE
#define BX_DOMAIN_SAMPLERS_CAPACITY K_SAMPLERS_CAPACITY
#define BX_IN_SAMPLERS(IDX)  BX_IsInDomain((IDX), BX_DOMAIN_SAMPLERS_BASE, BX_DOMAIN_SAMPLERS_CAPACITY)
#define BX_TRY_SAMPLERS(IDX) BX_TryUseGlobalIndexInDomain((IDX), BX_DOMAIN_SAMPLERS_BASE, BX_DOMAIN_SAMPLERS_CAPACITY)


#endif // OXYGEN_BINDLESS_LAYOUT_HLSL
