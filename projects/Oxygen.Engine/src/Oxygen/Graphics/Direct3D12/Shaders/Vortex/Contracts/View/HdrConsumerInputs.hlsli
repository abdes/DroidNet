//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef VORTEX_HDR_CONSUMER_INPUTS_HLSLI
#define VORTEX_HDR_CONSUMER_INPUTS_HLSLI

#include "Vortex/Contracts/View/FrameExposureHelpers.hlsli"
#include "Vortex/Contracts/View/HdrErrorBounds.hlsli"
#include "Vortex/Contracts/View/HdrStoreChecks.hlsli"

static const uint HDR_INPUT_TRANSLUCENCY = 0u;
static const uint HDR_INPUT_HEIGHT_FOG = 1u;
static const uint HDR_CONSUMER_SKY = 16u;
static const uint HDR_CONSUMER_OPAQUE_AP = 32u;
static const uint HDR_CONSUMER_TRANSLUCENT_AP = 64u;

static void RecordHdrSceneSource(float3 scene_rgb, uint product)
{
    const ViewFrameBindings bindings = LoadViewFrameBindings(bindless_view_frame_bindings_slot);
    CheckHdrStoreRange(float4(scene_rgb, 1.0), product,
        bindings.exposure_status_uav, 0u, 1.0);
}

static void RecordHdrConsumerUsage(uint usage, float3 sky_gain = 1.0.xxx)
{
    if (IsFp32OnlyExposure()) return;
    const ViewFrameBindings bindings = LoadViewFrameBindings(bindless_view_frame_bindings_slot);
    if (bindings.exposure_status_uav == K_INVALID_BINDLESS_INDEX) return;
    const bool valid = HdrFiniteNonnegative(sky_gain.x)
        && HdrFiniteNonnegative(sky_gain.y) && HdrFiniteNonnegative(sky_gain.z);
    const uint flags = WaveActiveBitOr(usage | (valid ? 0u : 128u));
    const uint3 magnitude = asuint(sky_gain) & 0x7fffffffu.xxx;
    const uint gain = WaveActiveMax(valid ? max(magnitude.x, max(magnitude.y, magnitude.z)) : 0u);
    if (WaveIsFirstLane()) {
        RWByteAddressBuffer status = ResourceDescriptorHeap[bindings.exposure_status_uav];
        uint unused;
        status.InterlockedOr(360u, flags, unused);
        if ((usage & HDR_CONSUMER_SKY) != 0u)
            status.InterlockedMax(364u, gain, unused);
    }
}

// Reduce once per active wave. Helper lanes do not contribute UAV writes.
// The owning pass transitions and retains this frame's existing status buffer.
static void RecordHdrConsumerInput(float3 scene_rgb, uint input_kind)
{
    const ViewFrameBindings bindings = LoadViewFrameBindings(bindless_view_frame_bindings_slot);
    if (bindings.exposure_status_uav == K_INVALID_BINDLESS_INDEX) return;
    if (IsFp32OnlyExposure()) {
        const uint failure = WaveActiveBitOr(
            ClassifyHdrStoreRange(float4(scene_rgb, 1.0), 1.0, 0u));
        if (WaveIsFirstLane()) {
            RecordHdrRangeFailure(failure,
                input_kind == HDR_INPUT_TRANSLUCENCY ? 4u : 8u, bindings.exposure_status_uav);
        }
        return;
    }
    const bool valid = HdrFiniteNonnegative(scene_rgb.x)
        && HdrFiniteNonnegative(scene_rgb.y) && HdrFiniteNonnegative(scene_rgb.z);
    const uint observed = 1u << input_kind;
    const uint flags = WaveActiveBitOr(observed | (valid ? 0u : observed << 2u));
    const uint3 magnitude = asuint(scene_rgb) & 0x7fffffffu.xxx;
    const uint peak = WaveActiveMax(valid ? max(magnitude.x, max(magnitude.y, magnitude.z)) : 0u);
    const uint range_failure = WaveActiveBitOr(
        ClassifyHdrStoreRange(float4(scene_rgb, 1.0), 1.0, 0u));
    if (WaveIsFirstLane()) {
        RWByteAddressBuffer status = ResourceDescriptorHeap[bindings.exposure_status_uav];
        uint unused;
        status.InterlockedMax(352u + input_kind * 4u, peak, unused);
        status.InterlockedOr(360u, flags, unused);
        RecordHdrRangeFailure(range_failure,
            input_kind == HDR_INPUT_TRANSLUCENCY ? 4u : 8u, bindings.exposure_status_uav);
    }
}

#endif
