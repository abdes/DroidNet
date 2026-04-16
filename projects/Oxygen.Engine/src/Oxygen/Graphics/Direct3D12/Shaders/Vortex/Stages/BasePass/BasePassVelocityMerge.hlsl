//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Renderer/ViewConstants.hlsli"

#include "Vortex/Contracts/SceneTextureBindings.hlsli"
#include "Vortex/Contracts/ViewFrameBindings.hlsli"

cbuffer RootConstants : register(b2, space0)
{
    uint g_BaseVelocitySrv;
    uint g_AuxVelocitySrv;
}

[numthreads(8, 8, 1)]
void BasePassVelocityMergeCS(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    if (bindless_view_frame_bindings_slot == INVALID_BINDLESS_INDEX) {
        return;
    }

    const SceneTextureBindingData bindings
        = LoadSceneTextureBindings(bindless_view_frame_bindings_slot);
    if (bindings.velocity_uav == INVALID_BINDLESS_INDEX
        || g_BaseVelocitySrv == INVALID_BINDLESS_INDEX
        || g_AuxVelocitySrv == INVALID_BINDLESS_INDEX) {
        return;
    }

    Texture2D<float2> base_velocity = ResourceDescriptorHeap[g_BaseVelocitySrv];
    Texture2D<float2> aux_velocity = ResourceDescriptorHeap[g_AuxVelocitySrv];
    RWTexture2D<float2> merged_velocity = ResourceDescriptorHeap[bindings.velocity_uav];

    uint width = 0u;
    uint height = 0u;
    base_velocity.GetDimensions(width, height);
    if (dispatch_thread_id.x >= width || dispatch_thread_id.y >= height) {
        return;
    }

    const uint2 pixel = dispatch_thread_id.xy;
    merged_velocity[pixel]
        = base_velocity.Load(uint3(pixel, 0u)) + aux_velocity.Load(uint3(pixel, 0u));
}
