//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <ranges>
#include <stdexcept>
#include <tuple>
#include <unordered_map>

#include <d3d12.h>

#include <Oxygen/Graphics/Common/ObjectRelease.h>
#include <Oxygen/Graphics/Direct3D12/Detail/Converters.h>
#include <Oxygen/Graphics/Direct3D12/Detail/FormatUtils.h>
#include <Oxygen/Graphics/Direct3D12/Detail/PipelineStateCache.h>
#include <Oxygen/Graphics/Direct3D12/Graphics.h>

namespace oxygen::graphics::d3d12::detail {

// Helper to load shader bytecode from shader manager
namespace {
    auto LoadShaderBytecode(const Graphics* gfx, const ShaderStageDesc& desc) -> D3D12_SHADER_BYTECODE
    {
        const auto shader = gfx->GetShader(desc.shader);
        if (!shader) {
            throw std::runtime_error("Shader not found: " + desc.shader);
        }
        return { shader->Data(), shader->Size() };
    } // Helper to setup framebuffer formats
    void SetupFramebufferFormats(const FramebufferLayoutDesc& fb_layout, D3D12_GRAPHICS_PIPELINE_STATE_DESC& pso_desc)
    {
        // Set render target formats
        pso_desc.NumRenderTargets = static_cast<UINT>(fb_layout.color_target_formats.size());
        for (UINT i = 0; i < pso_desc.NumRenderTargets; ++i) {
            const auto& format_mapping = GetDxgiFormatMapping(fb_layout.color_target_formats[i]);
            pso_desc.RTVFormats[i] = format_mapping.rtv_format;
        }

        // Set depth-stencil format if present
        if (fb_layout.depth_stencil_format) {
            const auto& format_mapping = GetDxgiFormatMapping(*fb_layout.depth_stencil_format);
            pso_desc.DSVFormat = format_mapping.rtv_format;
        }

        // Set sample description
        pso_desc.SampleDesc.Count = fb_layout.sample_count;
        pso_desc.SampleDesc.Quality = 0;
    }
} // namespace

PipelineStateCache::~PipelineStateCache()
{
    // Release all pipeline states and root signatures.

    for (auto& entry_tuple : graphics_pipelines_ | std::views::values) {
        auto& [pipeline_state, root_signature] = std::get<1>(entry_tuple);
        ObjectRelease(pipeline_state);
        ObjectRelease(root_signature);
    }
    graphics_pipelines_.clear();

    for (auto& entry_tuple : compute_pipelines_ | std::views::values) {
        auto& [pipeline_state, root_signature] = std::get<1>(entry_tuple);
        ObjectRelease(pipeline_state);
        ObjectRelease(root_signature);
    }
    compute_pipelines_.clear();
}

//! Get or create a graphics pipeline state object and root signature.
auto PipelineStateCache::GetOrCreateGraphicsPipeline(
    dx::IRootSignature* root_signature, GraphicsPipelineDesc desc, size_t hash) -> Entry
{
    auto it = graphics_pipelines_.find(hash);
    if (it != graphics_pipelines_.end()) {
        const auto& [cached_desc, entry] = it->second;
        return entry;
    }

    // Create new pipeline state and root signature
    dx::IPipelineState* pso = nullptr;
    dx::IRootSignature* root_sig = root_signature;

    // 2. Translate GraphicsPipelineDesc to D3D12_GRAPHICS_PIPELINE_STATE_DESC
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc = {};
    pso_desc.pRootSignature = root_sig;

    // Set shader stages
    if (desc.VertexShader()) {
        const auto& shader_desc = desc.VertexShader().value();
        pso_desc.VS = LoadShaderBytecode(gfx_, shader_desc);
    }

    if (desc.PixelShader()) {
        const auto& shader_desc = desc.PixelShader().value();
        pso_desc.PS = LoadShaderBytecode(gfx_, shader_desc);
    }

    if (desc.GeometryShader()) {
        const auto& shader_desc = desc.GeometryShader().value();
        pso_desc.GS = LoadShaderBytecode(gfx_, shader_desc);
    }

    if (desc.HullShader()) {
        const auto& shader_desc = desc.HullShader().value();
        pso_desc.HS = LoadShaderBytecode(gfx_, shader_desc);
    }

    if (desc.DomainShader()) {
        const auto& shader_desc = desc.DomainShader().value();
        pso_desc.DS = LoadShaderBytecode(gfx_, shader_desc);
    } // Set up blend state
    D3D12_BLEND_DESC blend_desc = {};
    TranslateBlendState(desc.BlendState(), blend_desc);
    pso_desc.BlendState = blend_desc;
    pso_desc.SampleMask = UINT_MAX; // Sample all pixels

    // Set up rasterizer state
    D3D12_RASTERIZER_DESC raster_desc = {};
    TranslateRasterizerState(desc.RasterizerState(), raster_desc);
    pso_desc.RasterizerState = raster_desc;

    // Set up depth-stencil state
    D3D12_DEPTH_STENCIL_DESC depth_stencil_desc = {};
    TranslateDepthStencilState(desc.DepthStencilState(), depth_stencil_desc);
    pso_desc.DepthStencilState = depth_stencil_desc;
    // Set primitive topology type
    pso_desc.PrimitiveTopologyType = ConvertPrimitiveType(desc.PrimitiveTopology());

    // Set framebuffer layout
    const auto& fb_layout = desc.FramebufferLayout();
    SetupFramebufferFormats(fb_layout, pso_desc);

    // No input layout for bindless rendering (use structured/raw buffers instead)
    pso_desc.InputLayout = {
        .pInputElementDescs = nullptr,
        .NumElements = 0,
    };
    // Create the pipeline state object
    auto* device = gfx_->GetCurrentDevice();
    HRESULT hr = device->CreateGraphicsPipelineState(&pso_desc, IID_PPV_ARGS(&pso));
    if (FAILED(hr)) {
        throw std::runtime_error("Failed to create graphics pipeline state");
    }

    Entry entry {
        .pipeline_state = pso,
        .root_signature = root_sig,
    };
    graphics_pipelines_.emplace(hash, std::make_tuple(std::move(desc), entry));
    return entry;
}

//! Get or create a compute pipeline state object and root signature.
auto PipelineStateCache::GetOrCreateComputePipeline(dx::IRootSignature* root_signature, ComputePipelineDesc desc, size_t hash) -> Entry
{
    auto it = compute_pipelines_.find(hash);
    if (it != compute_pipelines_.end()) {
        const auto& [cached_desc, entry] = it->second;
        return entry;
    }

    dx::IPipelineState* pso = nullptr;
    dx::IRootSignature* root_sig = root_signature;

    // 2. Create the compute pipeline state object
    D3D12_COMPUTE_PIPELINE_STATE_DESC pso_desc = {};
    pso_desc.pRootSignature = root_sig;

    // Set compute shader
    const auto& compute_shader_desc = desc.ComputeShader();
    pso_desc.CS = LoadShaderBytecode(gfx_, compute_shader_desc);
    // Create the pipeline state object
    auto* device = gfx_->GetCurrentDevice();
    HRESULT hr = device->CreateComputePipelineState(&pso_desc, IID_PPV_ARGS(&pso));
    if (FAILED(hr)) {
        throw std::runtime_error("Failed to create compute pipeline state");
    }

    Entry entry {
        .pipeline_state = pso,
        .root_signature = root_sig,
    };
    compute_pipelines_.emplace(hash, std::make_tuple(std::move(desc), entry));
    return entry;
}

//! Get the cached graphics pipeline description for a given hash.
auto PipelineStateCache::GetGraphicsPipelineDesc(const size_t hash) const -> const GraphicsPipelineDesc&
{
    return std::get<0>(graphics_pipelines_.at(hash));
}

//! Get the cached compute pipeline description for a given hash.
auto PipelineStateCache::GetComputePipelineDesc(const size_t hash) const -> const ComputePipelineDesc&
{
    return std::get<0>(compute_pipelines_.at(hash));
}

} // namespace oxygen::graphics::d3d12::detail
