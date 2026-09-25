//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_VORTEX_IBL_PRODUCT_METADATA_HLSLI
#define OXYGEN_VORTEX_IBL_PRODUCT_METADATA_HLSLI

#include "Core/Bindless/Generated.BindlessAbi.hlsl"
#include "Vortex/Contracts/View/HdrIntervalMath.hlsli"

static const uint kIblProductFinite = 1u;
static const uint kIblProductComplete = 2u;
static const uint kIblHalfCertificateComplete = 1u;

// Mirrors environment::IblProductMetadata; structured-buffer stride is 32.
struct IblProductMetadata
{
    float source_radiance_scale;
    float average_brightness;
    uint processing_flags;
    uint product_revision;
    float maximum_half_gain;
    uint precision_flags;
    uint processed_half_srv;
    uint specular_half_srv;
};

static bool IsIblProductReady(IblProductMetadata metadata, uint revision)
{
    return revision != 0u && metadata.product_revision == revision
        && metadata.processing_flags == (kIblProductFinite | kIblProductComplete)
        && isfinite(metadata.source_radiance_scale)
        && metadata.source_radiance_scale >= 1.0
        && isfinite(metadata.average_brightness)
        && metadata.average_brightness >= 0.0;
}

// Gain is an outward-rounded bound on authored/material amplification, in
// scene units. The certificate already includes source_radiance_scale.
static uint SelectIblCubeSrv(IblProductMetadata metadata, uint revision,
    uint canonical_srv, uint half_srv, float3 gain)
{
    if (!IsIblProductReady(metadata, revision)
        || metadata.precision_flags != kIblHalfCertificateComplete
        || !BX_IN_TEXTURES(half_srv)
        || !HdrFiniteNonnegative(metadata.maximum_half_gain)
        || !HdrFiniteNonnegative(gain.r) || !HdrFiniteNonnegative(gain.g)
        || !HdrFiniteNonnegative(gain.b)) return canonical_srv;
    // Compare nonnegative float encodings so FTZ cannot admit a nonzero gain
    // against a zero limit. Treat either signed zero as mathematical zero.
    uint maximum = max(asuint(gain.r) & 0x7fffffffu,
        max(asuint(gain.g) & 0x7fffffffu, asuint(gain.b) & 0x7fffffffu));
    return maximum <= (asuint(metadata.maximum_half_gain) & 0x7fffffffu)
        ? half_srv : canonical_srv;
}

#endif
