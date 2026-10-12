//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_D3D12_SHADERS_VORTEX_CONTRACTS_VIEWFRAMEBINDINGS_HLSLI
#define OXYGEN_D3D12_SHADERS_VORTEX_CONTRACTS_VIEWFRAMEBINDINGS_HLSLI

#include "Core/Bindless/Generated.BindlessAbi.hlsl"

#include "Vortex/Contracts/Scene/SceneTextureBindings.hlsli"
#include "Vortex/Contracts/View/Generated.ViewContracts.hlsli"

// The view's routing record; all slots invalid when `slot` is outside the
// global SRV domain.
static inline ViewFrameBindings LoadViewFrameBindings(uint slot)
{
    ViewFrameBindings bindings;
    TryLoadViewFrameBindings(slot, bindings);
    return bindings;
}

static inline SceneTextureBindingData LoadSceneTextureBindings(uint view_frame_bindings_slot)
{
    const ViewFrameBindings view_bindings = LoadViewFrameBindings(view_frame_bindings_slot);
    SceneTextureBindingData bindings;
    TryLoadSceneTextureBindingData(view_bindings.scene_texture_frame_slot, bindings);
    return bindings;
}

#endif // OXYGEN_D3D12_SHADERS_VORTEX_CONTRACTS_VIEWFRAMEBINDINGS_HLSLI
