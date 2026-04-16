//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_RENDERER_VERTEX_HLSLI
#define OXYGEN_RENDERER_VERTEX_HLSLI

// Define vertex structure to match the CPU-side Vertex struct.
// TODO(vortex/skinned-morph): extend this shader ABI in lockstep with
// Data/Vertex.h once real engine skinned/morph streams are live. Stage 3/9
// closure is scoped to the current rigid-vertex engine feature set; future
// joint-weight / morph fetch belongs here, not in detached side metadata.
struct Vertex {
    float3 position;
    float3 normal;
    float2 texcoord;
    float3 tangent;
    float3 bitangent;
    float4 color;
};

#endif // OXYGEN_RENDERER_VERTEX_HLSLI
