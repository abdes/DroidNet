//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_D3D12_SHADERS_VORTEX_CONTRACTS_SCENETEXTUREBINDINGS_HLSLI
#define OXYGEN_D3D12_SHADERS_VORTEX_CONTRACTS_SCENETEXTUREBINDINGS_HLSLI

#include "Vortex/Contracts/Definitions/SceneDefinitions.hlsli"
#include "Vortex/Contracts/Scene/GBufferLayout.hlsli"
#include "Vortex/Contracts/Scene/Generated.SceneTextureBindings.hlsli"

static inline bool IsSceneTextureValid(SceneTextureBindingData bindings, uint flag)
{
    return (bindings.valid_flags & flag) != 0u;
}

#endif // OXYGEN_D3D12_SHADERS_VORTEX_CONTRACTS_SCENETEXTUREBINDINGS_HLSLI
