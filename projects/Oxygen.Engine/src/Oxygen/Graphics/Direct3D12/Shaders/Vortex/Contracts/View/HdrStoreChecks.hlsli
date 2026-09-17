//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef VORTEX_HDR_STORE_CHECKS_HLSLI
#define VORTEX_HDR_STORE_CHECKS_HLSLI

#include "Core/Bindless/Generated.BindlessAbi.hlsl"

// Call with the FP32 value before a radiance texture store. This checks finite
// values and FP16 headroom only; it is not an image-error certificate.
void CheckHdrStoreRange(float4 value, uint product, uint status_uav, uint fp16_store)
{
    if (status_uav == K_INVALID_BINDLESS_INDEX) return;
    const uint failure = !all(isfinite(value)) ? 1u
        : fp16_store != 0u && any(abs(value.rgb) > 16376.0) ? 2u : 0u;
    if (failure == 0u) return;
    RWByteAddressBuffer status = ResourceDescriptorHeap[status_uav];
    uint unused;
    // Failed qualification (2) plus producer-origin failure (16). The latter
    // distinguishes this verdict from later candidate-evaluation failures.
    status.InterlockedOr(48u, 18u, unused);
    status.InterlockedCompareExchange(52u, 0u, product, unused);
    status.InterlockedOr(56u, failure, unused);
}

#endif
