//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_D3D12_SHADERS_VORTEX_CONTRACTS_DEFINITIONS_SCENEDEFINITIONS_HLSLI
#define OXYGEN_D3D12_SHADERS_VORTEX_CONTRACTS_DEFINITIONS_SCENEDEFINITIONS_HLSLI

#include "Core/Bindless/Generated.BindlessAbi.hlsl"

// Mirror SceneTextureSetupMode::Flag from SceneTextures.h exactly.
static const uint SCENE_TEXTURE_FLAG_SCENE_DEPTH = (1u << 0u);
static const uint SCENE_TEXTURE_FLAG_PARTIAL_DEPTH = (1u << 1u);
static const uint SCENE_TEXTURE_FLAG_VELOCITY = (1u << 2u);
static const uint SCENE_TEXTURE_FLAG_GBUFFERS = (1u << 3u);
static const uint SCENE_TEXTURE_FLAG_SCENE_COLOR = (1u << 4u);
static const uint SCENE_TEXTURE_FLAG_STENCIL = (1u << 5u);
static const uint SCENE_TEXTURE_FLAG_CUSTOM_DEPTH = (1u << 6u);

static const uint SHADING_MODEL_DEFAULT_LIT = 0u;
static const uint SHADING_MODEL_UNLIT = 1u;
static const uint SHADING_MODEL_SUBSURFACE = 2u;
static const uint SHADING_MODEL_CLOTH = 3u;

static const uint INVALID_BINDLESS_INDEX = K_INVALID_BINDLESS_INDEX;

#endif // OXYGEN_D3D12_SHADERS_VORTEX_CONTRACTS_DEFINITIONS_SCENEDEFINITIONS_HLSLI
