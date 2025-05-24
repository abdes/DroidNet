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
#include <wrl/client.h> // For Microsoft::WRL::ComPtr

#include <Oxygen/Graphics/Common/ObjectRelease.h>
#include <Oxygen/Graphics/Direct3D12/Detail/Converters.h>
#include <Oxygen/Graphics/Direct3D12/Detail/FormatUtils.h>
#include <Oxygen/Graphics/Direct3D12/Detail/PipelineStateCache.h>
#include <Oxygen/Graphics/Direct3D12/Graphics.h>

namespace oxygen::graphics::d3d12::detail {

// Helper to load shader bytecode from shader manager
namespace {
    auto LoadShaderBytecode(d3d12::Graphics* gfx, const ShaderStageDesc& desc) -> D3D12_SHADER_BYTECODE
    {
        auto shader = gfx->GetShader(desc.shader);
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

#include <ranges> // Add this include for std::ranges::values

PipelineStateCache::~PipelineStateCache()
{
    // Release all pipeline states and root signatures.

    for (auto& [_, entry_tuple] : graphics_pipelines_) {
        auto& entry = std::get<1>(entry_tuple);
        ObjectRelease(entry.pipeline_state);
        ObjectRelease(entry.root_signature);
    }
    graphics_pipelines_.clear();

    for (auto& [_, entry_tuple] : compute_pipelines_) {
        auto& entry = std::get<1>(entry_tuple);
        ObjectRelease(entry.pipeline_state);
        ObjectRelease(entry.root_signature);
    }
    compute_pipelines_.clear();
}

//! Create a bindless root signature for graphics or compute pipelines
// Invariant: The root signature contains a single descriptor table with two ranges:
//   - Range 0: 1 CBV at register b0 (heap index 0)
//   - Range 1: Unbounded SRVs at register t0, space0 (heap indices 1+)
// The descriptor table is always at root parameter 0.
// The flag D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED is set for true bindless access.
// This layout must match the expectations of both the engine and the shaders (see FullScreenTriangle.hlsl).
auto PipelineStateCache::CreateBindlessRootSignature(bool is_graphics) -> dx::IRootSignature*
{
    std::vector<D3D12_ROOT_PARAMETER> root_params(1); // Single root parameter for the main descriptor table
    std::vector<D3D12_DESCRIPTOR_RANGE> ranges(2);

    // Range 0: CBV for register b0 (heap index 0)
    ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
    ranges[0].NumDescriptors = 1;
    ranges[0].BaseShaderRegister = 0; // b0
    ranges[0].RegisterSpace = 0;
    ranges[0].OffsetInDescriptorsFromTableStart = 0; // This CBV is at the start of the table

    // Range 1: SRVs for register t0 onwards (heap indices 1+)
    ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    ranges[1].NumDescriptors = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND; // Unbounded, for bindless
    ranges[1].BaseShaderRegister = 0; // t0
    ranges[1].RegisterSpace = 0; // Assuming SRVs are in space0
    ranges[1].OffsetInDescriptorsFromTableStart = 1; // SRVs start after the 1 CBV descriptor

    root_params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    root_params[0].DescriptorTable.NumDescriptorRanges = static_cast<UINT>(ranges.size());
    root_params[0].DescriptorTable.pDescriptorRanges = ranges.data();
    root_params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL; // Visible to all stages that might need it

    D3D12_ROOT_SIGNATURE_DESC root_sig_desc = {};
    root_sig_desc.NumParameters = static_cast<UINT>(root_params.size());
    root_sig_desc.pParameters = root_params.data();
    root_sig_desc.NumStaticSamplers = 0; // Add static samplers if needed
    root_sig_desc.pStaticSamplers = nullptr;
    root_sig_desc.Flags = is_graphics ? D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT : D3D12_ROOT_SIGNATURE_FLAG_NONE;
    // D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED enables true bindless access for all shaders.
    root_sig_desc.Flags |= D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED;

    Microsoft::WRL::ComPtr<ID3DBlob> sig_blob, err_blob;
    HRESULT hr = D3D12SerializeRootSignature(&root_sig_desc, D3D_ROOT_SIGNATURE_VERSION_1_0, &sig_blob, &err_blob);
    if (FAILED(hr)) {
        std::string error_msg = "Failed to serialize root signature: ";
        if (err_blob) {
            error_msg += static_cast<const char*>(err_blob->GetBufferPointer());
        }
        throw std::runtime_error(error_msg);
    }

    dx::IRootSignature* root_sig = nullptr;
    auto* device = gfx_->GetCurrentDevice();
    hr = device->CreateRootSignature(
        0, sig_blob->GetBufferPointer(), sig_blob->GetBufferSize(), IID_PPV_ARGS(&root_sig));
    if (FAILED(hr)) {
        throw std::runtime_error("Failed to create root signature");
    }

    return root_sig;
}

//! Get or create a graphics pipeline state object and root signature.
auto PipelineStateCache::GetOrCreateGraphicsPipeline(GraphicsPipelineDesc desc, size_t hash) -> Entry
{
    auto it = graphics_pipelines_.find(hash);
    if (it != graphics_pipelines_.end()) {
        const auto& [cached_desc, entry] = it->second;
        return entry;
    }

    // Create new pipeline state and root signature
    dx::IPipelineState* pso = nullptr;
    dx::IRootSignature* root_sig = CreateBindlessRootSignature(true); // Create graphics root signature

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
    pso_desc.InputLayout = { nullptr, 0 };
    // Create the pipeline state object
    auto* device = gfx_->GetCurrentDevice();
    HRESULT hr = device->CreateGraphicsPipelineState(&pso_desc, IID_PPV_ARGS(&pso));
    if (FAILED(hr)) {
        throw std::runtime_error("Failed to create graphics pipeline state");
    }

    Entry entry { pso, root_sig };
    graphics_pipelines_.emplace(hash, std::make_tuple(std::move(desc), entry));
    return entry;
}

//! Get or create a compute pipeline state object and root signature.
auto PipelineStateCache::GetOrCreateComputePipeline(ComputePipelineDesc desc, size_t hash) -> Entry
{
    auto it = compute_pipelines_.find(hash);
    if (it != compute_pipelines_.end()) {
        const auto& [cached_desc, entry] = it->second;
        return entry;
    }

    dx::IPipelineState* pso = nullptr;
    dx::IRootSignature* root_sig = CreateBindlessRootSignature(false); // Create compute root signature

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

    Entry entry { pso, root_sig };
    compute_pipelines_.emplace(hash, std::make_tuple(std::move(desc), entry));
    return entry;
}

//! Get the cached graphics pipeline description for a given hash.
auto PipelineStateCache::GetGraphicsPipelineDesc(size_t hash) const -> const GraphicsPipelineDesc&
{
    return std::get<0>(graphics_pipelines_.at(hash));
}

//! Get the cached compute pipeline description for a given hash.
auto PipelineStateCache::GetComputePipelineDesc(size_t hash) const -> const ComputePipelineDesc&
{
    return std::get<0>(compute_pipelines_.at(hash));
}

} // namespace oxygen::graphics::d3d12::detail
