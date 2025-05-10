//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Graphics/Direct3D12/Detail/Converters.h>

auto oxygen::graphics::d3d12::detail::ConvertResourceStates(ResourceStates states)
    -> D3D12_RESOURCE_STATES
{
    if (states == ResourceStates::kUnknown) {
        // kUnknown (0) implies no specific state, which is D3D12_RESOURCE_STATE_COMMON (0).
        return D3D12_RESOURCE_STATE_COMMON;
    }

    // If only kCommon is specified, return D3D12_RESOURCE_STATE_COMMON.
    // D3D12_RESOURCE_STATE_COMMON is 0. If other specific states are present,
    // they will define the actual D3D12 state.
    if (states == ResourceStates::kCommon) {
        return D3D12_RESOURCE_STATE_COMMON;
    }

    D3D12_RESOURCE_STATES d3d_states = D3D12_RESOURCE_STATE_COMMON; // Initialize to 0

    // Helper lambda to check a flag and append D3D12 state(s)
    // Captures 'states' by value and 'd3d_states' by reference.
    auto append_if_set =
        [&d3d_states, states](ResourceStates flag_to_check, D3D12_RESOURCE_STATES d3d12_flags_to_add) {
            if ((states & flag_to_check) == flag_to_check) {
                d3d_states |= d3d12_flags_to_add;
            }
        };

    auto is_set = [states](ResourceStates flag) {
        return (states & flag) == flag;
    };

    // kUndefined (OXYGEN_FLAG(0)) is not explicitly mapped to a unique D3D12 state bit here.
    // If it's the only state (other than kUnknown/kCommon handled above),
    // the result will be D3D12_RESOURCE_STATE_COMMON, which is appropriate.

    append_if_set(ResourceStates::kVertexBuffer, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
    append_if_set(ResourceStates::kConstantBuffer, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
    append_if_set(ResourceStates::kIndexBuffer, D3D12_RESOURCE_STATE_INDEX_BUFFER);
    append_if_set(ResourceStates::kRenderTarget, D3D12_RESOURCE_STATE_RENDER_TARGET);
    append_if_set(ResourceStates::kUnorderedAccess, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    append_if_set(ResourceStates::kDepthWrite, D3D12_RESOURCE_STATE_DEPTH_WRITE);
    append_if_set(ResourceStates::kDepthRead, D3D12_RESOURCE_STATE_DEPTH_READ);
    append_if_set(ResourceStates::kStreamOut, D3D12_RESOURCE_STATE_STREAM_OUT);
    append_if_set(ResourceStates::kIndirectArgument, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
    append_if_set(ResourceStates::kCopyDest, D3D12_RESOURCE_STATE_COPY_DEST);
    append_if_set(ResourceStates::kCopySource, D3D12_RESOURCE_STATE_COPY_SOURCE);
    append_if_set(ResourceStates::kResolveDest, D3D12_RESOURCE_STATE_RESOLVE_DEST);
    append_if_set(ResourceStates::kResolveSource, D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
    append_if_set(ResourceStates::kPresent, D3D12_RESOURCE_STATE_PRESENT);
    append_if_set(ResourceStates::kBuildAccelStructureRead, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    append_if_set(ResourceStates::kShadingRate, D3D12_RESOURCE_STATE_SHADING_RATE_SOURCE);

    // Special cases for combined input flags or specific D3D12 combinations:

    // kShaderResource and kInputAttachment are both read by shaders.
    if (is_set(ResourceStates::kShaderResource) || is_set(ResourceStates::kInputAttachment)) {
        d3d_states |= D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        d3d_states |= D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    }

    // kBuildAccelStructureWrite is the output AS itself.
    // kRayTracing is using the AS in shaders.
    if (is_set(ResourceStates::kBuildAccelStructureWrite) || is_set(ResourceStates::kRayTracing)) {
        d3d_states |= D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE;
    }

    // If states contained kCommon along with other flags, D3D12_RESOURCE_STATE_COMMON (0)
    // being ORed in doesn't change the specific states.
    // If d3d_states is still 0 (D3D12_RESOURCE_STATE_COMMON) after all checks,
    // it means no specific mappable states were found (e.g., only kUndefined was set),
    // so D3D12_RESOURCE_STATE_COMMON is the correct default.
    return d3d_states;
}
