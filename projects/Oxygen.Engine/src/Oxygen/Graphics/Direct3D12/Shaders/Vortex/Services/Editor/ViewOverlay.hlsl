//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Editor view overlay: world-space triangles and constant-width lines drawn
// over the view output. Lines are expanded to screen-space quads with one
// pixel of antialiasing. Where the scene depth is tested, pixels behind scene
// geometry are dimmed instead of hidden.

#include "Core/Bindless/Generated.BindlessAbi.hlsl"
#include "Vortex/Contracts/View/ViewConstants.hlsli"

cbuffer RootConstants : register(b2, space0)
{
    uint g_DrawIndex;
    uint g_PassConstantsIndex;
}

struct ViewOverlayConstants
{
    uint triangles_srv;
    uint lines_srv;
    uint scene_depth_srv;
    uint test_occlusion;
    float2 viewport_size;
    float occluded_alpha;
    uint pad0;
};

struct ViewOverlayVertex
{
    float3 position;
    float pad0;
    float4 color;
};

struct ViewOverlayLine
{
    float3 start;
    float width;
    float3 end;
    float pad0;
    float4 color;
};

struct ViewOverlayVSOutput
{
    float4 position : SV_POSITION;
    float4 color : COLOR0;
    // Signed distance, in pixels, from the line centre; zero for triangles.
    float edge : TEXCOORD0;
    float half_width : TEXCOORD1;
};

static const float kTriangleHalfWidth = 1.0e4f;
// Endpoints closer than this to the eye plane are moved onto it, so a line
// crossing the camera does not project through infinity.
static const float kMinClipW = 1.0e-4f;

bool LoadOverlayConstants(out ViewOverlayConstants constants)
{
    constants = (ViewOverlayConstants)0;
    if (g_PassConstantsIndex == K_INVALID_BINDLESS_INDEX) {
        return false;
    }
    StructuredBuffer<ViewOverlayConstants> buffer
        = ResourceDescriptorHeap[g_PassConstantsIndex];
    constants = buffer[0];
    return true;
}

float4 ProjectToClip(float3 world_position)
{
    return mul(projection_matrix, mul(view_matrix, float4(world_position, 1.0f)));
}

[shader("vertex")]
ViewOverlayVSOutput VortexViewOverlayTriangleVS(uint vertex_id : SV_VertexID)
{
    ViewOverlayVSOutput output = (ViewOverlayVSOutput)0;
    output.position = float4(0.0f, 0.0f, 0.0f, 1.0f);
    ViewOverlayConstants pc;
    if (!LoadOverlayConstants(pc) || pc.triangles_srv == K_INVALID_BINDLESS_INDEX) {
        return output;
    }
    StructuredBuffer<ViewOverlayVertex> vertices
        = ResourceDescriptorHeap[pc.triangles_srv];
    const ViewOverlayVertex vertex = vertices[vertex_id];
    output.position = ProjectToClip(vertex.position);
    output.color = vertex.color;
    output.edge = 0.0f;
    output.half_width = kTriangleHalfWidth;
    return output;
}

[shader("vertex")]
ViewOverlayVSOutput VortexViewOverlayLineVS(uint vertex_id : SV_VertexID)
{
    ViewOverlayVSOutput output = (ViewOverlayVSOutput)0;
    output.position = float4(0.0f, 0.0f, 0.0f, 1.0f);
    ViewOverlayConstants pc;
    if (!LoadOverlayConstants(pc) || pc.lines_srv == K_INVALID_BINDLESS_INDEX) {
        return output;
    }
    StructuredBuffer<ViewOverlayLine> lines = ResourceDescriptorHeap[pc.lines_srv];
    const ViewOverlayLine segment = lines[vertex_id / 6u];
    const uint corner = vertex_id % 6u;

    float4 a = ProjectToClip(segment.start);
    float4 b = ProjectToClip(segment.end);
    if (a.w < kMinClipW && b.w < kMinClipW) {
        return output;
    }
    if (a.w < kMinClipW) {
        a = lerp(a, b, (kMinClipW - a.w) / (b.w - a.w));
    } else if (b.w < kMinClipW) {
        b = lerp(b, a, (kMinClipW - b.w) / (a.w - b.w));
    }

    const float2 half_viewport = max(pc.viewport_size, float2(1.0f, 1.0f)) * 0.5f;
    const float2 screen_a = a.xy / a.w * half_viewport;
    const float2 screen_b = b.xy / b.w * half_viewport;
    float2 direction = screen_b - screen_a;
    const float length_px = length(direction);
    direction = length_px > 1.0e-4f ? direction / length_px : float2(1.0f, 0.0f);
    const float2 normal = float2(-direction.y, direction.x);

    // Two triangles: (a-, b-, b+) and (a-, b+, a+). The quad reaches one
    // pixel past the stroke on every side for the antialiased edge.
    const bool at_end = corner == 1u || corner == 2u || corner == 4u;
    const float side = (corner == 2u || corner == 4u || corner == 5u) ? 1.0f : -1.0f;
    const float half_width = max(segment.width, 0.5f) * 0.5f;
    const float extent = half_width + 1.0f;
    float4 position = at_end ? b : a;
    const float2 offset_px = normal * side * extent
        + direction * (at_end ? 1.0f : -1.0f);
    position.xy += offset_px / half_viewport * position.w;

    output.position = position;
    output.color = segment.color;
    output.edge = side * extent;
    output.half_width = half_width;
    return output;
}

[shader("pixel")]
float4 VortexViewOverlayPS(ViewOverlayVSOutput input) : SV_Target0
{
    ViewOverlayConstants pc;
    if (!LoadOverlayConstants(pc)) {
        discard;
    }

    float4 color = input.color;
    color.a *= saturate(input.half_width + 0.5f - abs(input.edge));

    if (pc.test_occlusion != 0u && pc.scene_depth_srv != K_INVALID_BINDLESS_INDEX) {
        Texture2D<float> scene_depth = ResourceDescriptorHeap[pc.scene_depth_srv];
        const float scene_z = scene_depth.Load(int3(int2(input.position.xy), 0));
        const float tolerance = 1.0e-6f + abs(scene_z) * 1.0e-4f;
        const bool hidden = reverse_z != 0u
            ? input.position.z < scene_z - tolerance
            : input.position.z > scene_z + tolerance;
        if (hidden) {
            color.a *= pc.occluded_alpha;
        }
    }

    if (color.a <= 0.0f) {
        discard;
    }
    return color;
}
