//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef ATMOSPHERE_PASS_CONSTANTS_HLSLI
#define ATMOSPHERE_PASS_CONSTANTS_HLSLI

//! Unified pass constants for all sky atmosphere LUT generation passes.
struct AtmospherePassConstants
{
    // --- 16-byte boundary ---
    uint output_uav_index;          //!< UAV index for the output texture
    uint transmittance_srv_index;   //!< SRV index for the transmittance LUT
    uint multi_scat_srv_index;      //!< SRV index for the Multiple Scattering LUT
    uint sky_irradiance_srv_index;  //!< SRV index for the Sky Irradiance LUT

    // --- 16-byte boundary ---
    uint2 output_extent;            //!< Output texture extent (width, height)
    uint2 transmittance_extent;     //!< Transmittance LUT extent (width, height)

    // --- 16-byte boundary ---
    uint2 sky_irradiance_extent;    //!< Sky irradiance LUT extent (width, height)
    uint output_depth;              //!< Output texture depth or slice count
    float atmosphere_height_m;      //!< Total atmosphere height in meters

    // --- 16-byte boundary ---
    float planet_radius_m;          //!< Planet radius in meters
    float sun_cos_zenith;           //!< Cosine of sun zenith angle
    uint alt_mapping_mode;          //!< Altitude mapping mode (0=linear, 1=log)
    uint atmosphere_flags;          //!< Misc flags for atmospheric effects

    // --- 16-byte boundary ---
    float max_distance_km;          //!< Maximum distance for aerial perspective (km)
    uint _pad0;
    uint _pad1;
    uint _pad2;

    // --- 16-byte boundary (x4) ---
    float4x4 inv_projection_matrix; //!< Inverse projection matrix

    // --- 16-byte boundary (x4) ---
    float4x4 inv_view_matrix;       //!< Inverse view matrix

    // --- 16-byte boundary (x3) ---
    // Padding to reach 256 bytes (D3D12 CBV requirement)
    uint _final_padding[12];
};

#endif // ATMOSPHERE_PASS_CONSTANTS_HLSLI
