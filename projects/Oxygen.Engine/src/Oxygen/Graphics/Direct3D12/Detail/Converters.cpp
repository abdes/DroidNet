//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <stdexcept>

#include <Oxygen/Graphics/Common/Constants.h>
#include <Oxygen/Graphics/Direct3D12/Detail/Converters.h>

namespace oxygen::graphics::d3d12::detail {

auto ConvertResourceStates(ResourceStates states) -> D3D12_RESOURCE_STATES
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
        [&d3d_states, states](const ResourceStates flag_to_check, const D3D12_RESOURCE_STATES d3d12_flags_to_add) {
            if ((states & flag_to_check) == flag_to_check) {
                d3d_states |= d3d12_flags_to_add;
            }
        };

    auto is_set = [states](const ResourceStates flag) {
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
    // so D3D12_RESOURCE_STATE_COMMON is the correct default.    return d3d_states;

    return d3d_states;
}

auto ConvertFillMode(const FillMode value) -> D3D12_FILL_MODE
{
    // ReSharper disable once CppDefaultCaseNotHandledInSwitchStatement
    // ReSharper disable once CppIncompleteSwitchStatement
    switch (value) { // NOLINT(clang-diagnostic-switch)
    case FillMode::kSolid:
        return D3D12_FILL_MODE_SOLID;
    case FillMode::kWireFrame:
        return D3D12_FILL_MODE_WIREFRAME;
    }

    throw std::runtime_error("Invalid fill mode");
}

auto ConvertCullMode(const CullMode value) -> D3D12_CULL_MODE
{
    // ReSharper disable once CppDefaultCaseNotHandledInSwitchStatement
    // ReSharper disable once CppIncompleteSwitchStatement
    switch (value) { // NOLINT(clang-diagnostic-switch)
    case CullMode::kNone:
        return D3D12_CULL_MODE_NONE;
    case CullMode::kFront:
        return D3D12_CULL_MODE_FRONT;
    case CullMode::kBack:
        return D3D12_CULL_MODE_BACK;
    case CullMode::kFrontAndBack:
        throw std::runtime_error("D3D12 doesn't support front and back face culling");
    }

    throw std::runtime_error("Invalid cull mode");
}

auto ConvertCompareOp(const CompareOp value) -> D3D12_COMPARISON_FUNC
{
    // ReSharper disable once CppDefaultCaseNotHandledInSwitchStatement
    // ReSharper disable once CppIncompleteSwitchStatement
    switch (value) { // NOLINT(clang-diagnostic-switch)
    case CompareOp::kNever:
        return D3D12_COMPARISON_FUNC_NEVER;
    case CompareOp::kLess:
        return D3D12_COMPARISON_FUNC_LESS;
    case CompareOp::kEqual:
        return D3D12_COMPARISON_FUNC_EQUAL;
    case CompareOp::kLessOrEqual:
        return D3D12_COMPARISON_FUNC_LESS_EQUAL;
    case CompareOp::kGreater:
        return D3D12_COMPARISON_FUNC_GREATER;
    case CompareOp::kNotEqual:
        return D3D12_COMPARISON_FUNC_NOT_EQUAL;
    case CompareOp::kGreaterOrEqual:
        return D3D12_COMPARISON_FUNC_GREATER_EQUAL;
    case CompareOp::kAlways:
        return D3D12_COMPARISON_FUNC_ALWAYS;
    }

    throw std::runtime_error("Invalid comparison op");
}

auto ConvertBlendFactor(const BlendFactor value) -> D3D12_BLEND
{
    // ReSharper disable once CppDefaultCaseNotHandledInSwitchStatement
    // ReSharper disable once CppIncompleteSwitchStatement
    switch (value) { // NOLINT(clang-diagnostic-switch)
    case BlendFactor::kZero:
        return D3D12_BLEND_ZERO;
    case BlendFactor::kOne:
        return D3D12_BLEND_ONE;
    case BlendFactor::kSrcColor:
        return D3D12_BLEND_SRC_COLOR;
    case BlendFactor::kInvSrcColor:
        return D3D12_BLEND_INV_SRC_COLOR;
    case BlendFactor::kSrcAlpha:
        return D3D12_BLEND_SRC_ALPHA;
    case BlendFactor::kInvSrcAlpha:
        return D3D12_BLEND_INV_SRC_ALPHA;
    case BlendFactor::kDestColor:
        return D3D12_BLEND_DEST_COLOR;
    case BlendFactor::kInvDestColor:
        return D3D12_BLEND_INV_DEST_COLOR;
    case BlendFactor::kDestAlpha:
        return D3D12_BLEND_DEST_ALPHA;
    case BlendFactor::kInvDestAlpha:
        return D3D12_BLEND_INV_DEST_ALPHA;
    case BlendFactor::kConstantColor:
        return D3D12_BLEND_BLEND_FACTOR;
    case BlendFactor::kInvConstantColor:
        return D3D12_BLEND_INV_BLEND_FACTOR;
    case BlendFactor::kSrc1Color:
        return D3D12_BLEND_SRC1_COLOR;
    case BlendFactor::kInvSrc1Color:
        return D3D12_BLEND_INV_SRC1_COLOR;
    case BlendFactor::kSrc1Alpha:
        return D3D12_BLEND_SRC1_ALPHA;
    case BlendFactor::kInvSrc1Alpha:
        return D3D12_BLEND_INV_SRC1_ALPHA;
    }

    throw std::runtime_error("Invalid blend factor");
}

auto ConvertBlendOp(const BlendOp value) -> D3D12_BLEND_OP
{
    // ReSharper disable once CppDefaultCaseNotHandledInSwitchStatement
    // ReSharper disable once CppIncompleteSwitchStatement
    switch (value) { // NOLINT(clang-diagnostic-switch)
    case BlendOp::kAdd:
        return D3D12_BLEND_OP_ADD;
    case BlendOp::kSubtract:
        return D3D12_BLEND_OP_SUBTRACT;
    case BlendOp::kRevSubtract:
        return D3D12_BLEND_OP_REV_SUBTRACT;
    case BlendOp::kMin:
        return D3D12_BLEND_OP_MIN;
    case BlendOp::kMax:
        return D3D12_BLEND_OP_MAX;
    }

    throw std::runtime_error("Invalid blend op");
}

auto ConvertColorWriteMask(const ColorWriteMask flags) -> UINT8
{
    UINT8 result = 0;
    if ((flags & ColorWriteMask::kR) == ColorWriteMask::kR) {
        result |= D3D12_COLOR_WRITE_ENABLE_RED;
    }
    if ((flags & ColorWriteMask::kG) == ColorWriteMask::kG) {
        result |= D3D12_COLOR_WRITE_ENABLE_GREEN;
    }
    if ((flags & ColorWriteMask::kB) == ColorWriteMask::kB) {
        result |= D3D12_COLOR_WRITE_ENABLE_BLUE;
    }
    if ((flags & ColorWriteMask::kA) == ColorWriteMask::kA) {
        result |= D3D12_COLOR_WRITE_ENABLE_ALPHA;
    }
    return result;
}

auto ConvertPrimitiveType(const PrimitiveType value) -> D3D12_PRIMITIVE_TOPOLOGY_TYPE
{
    // ReSharper disable once CppDefaultCaseNotHandledInSwitchStatement
    // ReSharper disable once CppIncompleteSwitchStatement
    switch (value) { // NOLINT(clang-diagnostic-switch)
    case PrimitiveType::kPointList:
        return D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
    case PrimitiveType::kLineList:
    case PrimitiveType::kLineStrip:
    case PrimitiveType::kLineStripWithRestartEnable:
    case PrimitiveType::kLineListWithAdjacency:
    case PrimitiveType::kLineStripWithAdjacency:
        return D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
    case PrimitiveType::kTriangleList:
    case PrimitiveType::kTriangleStrip:
    case PrimitiveType::kTriangleStripWithRestartEnable:
    case PrimitiveType::kTriangleListWithAdjacency:
    case PrimitiveType::kTriangleStripWithAdjacency:
        return D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    case PrimitiveType::kPatchList:
        return D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH;
    }

    throw std::runtime_error("Unsupported primitive topology type");
}

auto ConvertClearFlags(const ClearFlags flags) -> D3D12_CLEAR_FLAGS
{
    D3D12_CLEAR_FLAGS d3d12_flags = {};
    if ((flags & ClearFlags::kDepth) == ClearFlags::kDepth) {
        d3d12_flags |= D3D12_CLEAR_FLAG_DEPTH;
    }
    if ((flags & ClearFlags::kStencil) == ClearFlags::kStencil) {
        d3d12_flags |= D3D12_CLEAR_FLAG_STENCIL;
    }

    // Note: D3D12 does not have a specific clear flag for color.
    // Note: any other unknown flags are ignored.

    return d3d12_flags;
}

void TranslateRasterizerState(const RasterizerStateDesc& desc, D3D12_RASTERIZER_DESC& d3d_desc)
{
    d3d_desc.FillMode = ConvertFillMode(desc.fill_mode);
    d3d_desc.CullMode = ConvertCullMode(desc.cull_mode);
    d3d_desc.FrontCounterClockwise = desc.front_counter_clockwise;
    d3d_desc.DepthBias = static_cast<INT>(desc.depth_bias);
    d3d_desc.DepthBiasClamp = desc.depth_bias_clamp;
    d3d_desc.SlopeScaledDepthBias = desc.slope_scaled_depth_bias;
    d3d_desc.DepthClipEnable = desc.depth_clip_enable;
    d3d_desc.MultisampleEnable = desc.multisample_enable;
    d3d_desc.AntialiasedLineEnable = desc.antialiased_line_enable;
    d3d_desc.ForcedSampleCount = 0;
    d3d_desc.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;
}

void TranslateDepthStencilState(const DepthStencilStateDesc& desc, D3D12_DEPTH_STENCIL_DESC& d3d_desc)
{
    d3d_desc.DepthEnable = desc.depth_test_enable;
    d3d_desc.DepthWriteMask = desc.depth_write_enable ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
    d3d_desc.DepthFunc = ConvertCompareOp(desc.depth_func);
    d3d_desc.StencilEnable = desc.stencil_enable;
    d3d_desc.StencilReadMask = desc.stencil_read_mask;
    d3d_desc.StencilWriteMask = desc.stencil_write_mask;

    // The translation sets the default stencil operations for FrontFace and
    // BackFace to the typical D3D12 defaults most commonly used in 3D game
    // engines.
    d3d_desc.FrontFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
    d3d_desc.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
    d3d_desc.FrontFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;
    d3d_desc.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;

    d3d_desc.BackFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
    d3d_desc.BackFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
    d3d_desc.BackFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;
    d3d_desc.BackFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;
}

void TranslateBlendState(const std::vector<BlendTargetDesc>& blend_targets, D3D12_BLEND_DESC& d3d_desc)
{
    d3d_desc.AlphaToCoverageEnable = FALSE;
    d3d_desc.IndependentBlendEnable = TRUE;

    // Initialize all render targets with default values
    for (auto& blend_desc : d3d_desc.RenderTarget) {
        blend_desc.BlendEnable = FALSE;
        blend_desc.LogicOpEnable = FALSE;
        blend_desc.SrcBlend = D3D12_BLEND_ONE;
        blend_desc.DestBlend = D3D12_BLEND_ZERO;
        blend_desc.BlendOp = D3D12_BLEND_OP_ADD;
        blend_desc.SrcBlendAlpha = D3D12_BLEND_ONE;
        blend_desc.DestBlendAlpha = D3D12_BLEND_ZERO;
        blend_desc.BlendOpAlpha = D3D12_BLEND_OP_ADD;
        blend_desc.LogicOp = D3D12_LOGIC_OP_NOOP;
        blend_desc.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    }

    // Update with provided blend targets
    for (size_t i = 0; i < blend_targets.size() && i < kMaxRenderTargets; ++i) {
        const auto& target = blend_targets[i];
        auto& blend_desc = d3d_desc.RenderTarget[i];

        blend_desc.BlendEnable = target.blend_enable;
        blend_desc.SrcBlend = ConvertBlendFactor(target.src_blend);
        blend_desc.DestBlend = ConvertBlendFactor(target.dest_blend);
        blend_desc.BlendOp = ConvertBlendOp(target.blend_op);
        blend_desc.SrcBlendAlpha = ConvertBlendFactor(target.src_blend_alpha);
        blend_desc.DestBlendAlpha = ConvertBlendFactor(target.dest_blend_alpha);
        blend_desc.BlendOpAlpha = ConvertBlendOp(target.blend_op_alpha);
        blend_desc.RenderTargetWriteMask = target.write_mask
            ? ConvertColorWriteMask(*target.write_mask)
            : D3D12_COLOR_WRITE_ENABLE_ALL;
    }
}

} // namespace oxygen::graphics::d3d12::detail
