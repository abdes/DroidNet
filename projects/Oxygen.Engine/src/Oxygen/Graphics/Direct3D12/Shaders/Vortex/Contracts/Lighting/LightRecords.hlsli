//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_VORTEX_LIGHT_RECORDS_HLSLI
#define OXYGEN_VORTEX_LIGHT_RECORDS_HLSLI

// Pure light-record loaders: they read the lighting publication passed in and
// never the bound view.

#include "Core/Bindless/Generated.BindlessAbi.hlsl"
#include "Vortex/Contracts/Lighting/LightingFrameBindings.hlsli"
#include "Vortex/Contracts/Shadows/ShadowRecords.hlsli"

static bool TryLoadDirectionalLight(LightingFrameBindings lighting, uint index,
    out DirectionalLightForwardData light)
{
    light = (DirectionalLightForwardData)0;
    if (!IsLightingPublicationReady(lighting) || index >= lighting.directional_count
        || !BX_IN_GLOBAL_SRV(lighting.directional_records_srv)) return false;
    StructuredBuffer<DirectionalLightForwardData> records = ResourceDescriptorHeap[lighting.directional_records_srv];
    uint count, stride;
    records.GetDimensions(count, stride);
    if (index >= count) return false;
    light = records[index];
    return light.selection_index == index;
}

static bool TryLoadLocalLight(LightingFrameBindings lighting, uint index,
    out ForwardLocalLightRecord light)
{
    light = (ForwardLocalLightRecord)0;
    if (!IsLightingPublicationReady(lighting) || index >= lighting.local_count
        || !BX_IN_GLOBAL_SRV(lighting.local_records_srv)) return false;
    StructuredBuffer<ForwardLocalLightRecord> records = ResourceDescriptorHeap[lighting.local_records_srv];
    uint count, stride;
    records.GetDimensions(count, stride);
    if (index >= count) return false;
    light = records[index];
    return light.selection_index == index;
}

static LightShadowReference LoadLightShadowReference(uint descriptor, uint index)
{
    LightShadowReference reference = (LightShadowReference)0;
    reference.record_index = K_INVALID_BINDLESS_INDEX;
    reference.selection_index = K_INVALID_BINDLESS_INDEX;
    if (!BX_IN_GLOBAL_SRV(descriptor)) return reference;
    StructuredBuffer<LightShadowReference> references = ResourceDescriptorHeap[descriptor];
    uint count, stride;
    references.GetDimensions(count, stride);
    if (index < count) reference = references[index];
    return reference;
}

#endif
