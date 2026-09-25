//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_VORTEX_IBL_PRODUCT_METADATA_HLSLI
#define OXYGEN_VORTEX_IBL_PRODUCT_METADATA_HLSLI

static const uint kIblProductFinite = 1u;
static const uint kIblProductComplete = 2u;

// Mirrors environment::IblProductMetadata; structured-buffer stride is 16.
struct IblProductMetadata
{
    float source_radiance_scale;
    float average_brightness;
    uint processing_flags;
    uint product_revision;
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

#endif
