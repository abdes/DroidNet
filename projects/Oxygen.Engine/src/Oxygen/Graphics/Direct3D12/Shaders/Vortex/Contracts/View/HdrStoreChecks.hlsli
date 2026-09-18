//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef VORTEX_HDR_STORE_CHECKS_HLSLI
#define VORTEX_HDR_STORE_CHECKS_HLSLI

#include "Core/Bindless/Generated.BindlessAbi.hlsl"
#include "Vortex/Contracts/View/HdrRadianceRange.hlsli"

void RecordHdrRangeFailure(uint failure, uint product, uint status_uav)
{
    if (status_uav == K_INVALID_BINDLESS_INDEX) return;
    if (failure == 0u) return;
    RWByteAddressBuffer status = ResourceDescriptorHeap[status_uav];
    uint unused;
    // Failed qualification (2) plus producer-origin failure (16). The latter
    // distinguishes this verdict from later candidate-evaluation failures.
    status.InterlockedOr(48u, 18u, unused);
    status.InterlockedCompareExchange(52u, 0u, product, unused);
    status.InterlockedOr(56u, failure, unused);
}

// Original FP32 store input, before sanitization/narrowing. This is a range
// verdict, not an image-error certificate.
void CheckHdrStoreRange(float4 value, uint product, uint status_uav,
    uint fp16_store, float pre_exposure)
{
    RecordHdrRangeFailure(ClassifyHdrStoreRange(value, pre_exposure, fp16_store),
        product, status_uav);
}

#endif
