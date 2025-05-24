//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <d3d12.h>

#include <Oxygen/Graphics/Common/PipelineState.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>

namespace oxygen::graphics::d3d12::detail {

auto ConvertResourceStates(ResourceStates states) -> D3D12_RESOURCE_STATES;

auto ConvertFillMode(FillMode mode) -> D3D12_FILL_MODE;
auto ConvertCullMode(CullMode mode) -> D3D12_CULL_MODE;
auto ConvertCompareOp(CompareOp op) -> D3D12_COMPARISON_FUNC;
auto ConvertBlendFactor(BlendFactor factor) -> D3D12_BLEND;
auto ConvertBlendOp(BlendOp op) -> D3D12_BLEND_OP;
auto ConvertColorWriteMask(ColorWriteMask mask) -> UINT8;
auto ConvertPrimitiveType(PrimitiveType type) -> D3D12_PRIMITIVE_TOPOLOGY_TYPE;

void TranslateRasterizerState(const RasterizerStateDesc& desc, D3D12_RASTERIZER_DESC& d3d_desc);
void TranslateDepthStencilState(const DepthStencilStateDesc& desc, D3D12_DEPTH_STENCIL_DESC& d3d_desc);
void TranslateBlendState(const std::vector<BlendTargetDesc>& blend_targets, D3D12_BLEND_DESC& d3d_desc);

} // namespace oxygen::graphics::d3d12::detail
