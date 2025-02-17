//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Graphics/Direct3D12/Detail/dx12_utils.h>

#include <wrl/client.h>

#include <Oxygen/Base/Windows/ComError.h>
#include <Oxygen/Graphics/Direct3D12/Forward.h>
#include <Oxygen/Graphics/Direct3D12/Graphics.h>

using Microsoft::WRL::ComPtr;
using oxygen::graphics::d3d12::detail::GetMainDevice;
using oxygen::windows::ThrowOnFailed;

auto oxygen::graphics::d3d12::create_root_signature(const D3d12RootSignatureDesc& desc) -> ID3D12RootSignature*
{
    D3D12_VERSIONED_ROOT_SIGNATURE_DESC versioned_desc {};
    versioned_desc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
    versioned_desc.Desc_1_1 = desc;

    ComPtr<ID3DBlob> signature_blob;
    ComPtr<ID3DBlob> error_blob;
    ThrowOnFailed(D3D12SerializeVersionedRootSignature(
        &versioned_desc,
        &signature_blob,
        &error_blob));

    ID3D12RootSignature* root_signature { nullptr };
    ThrowOnFailed(GetMainDevice()->CreateRootSignature(
        0,
        signature_blob->GetBufferPointer(),
        signature_blob->GetBufferSize(),
        IID_PPV_ARGS(&root_signature)));

    return root_signature;
}

auto oxygen::graphics::d3d12::create_pipeline_state(const D3D12_PIPELINE_STATE_STREAM_DESC& desc) -> ID3D12PipelineState*
{
    CHECK_NOTNULL_F(desc.pPipelineStateSubobjectStream);
    CHECK_GT_F(0, desc.SizeInBytes);

    ID3D12PipelineState* pso { nullptr };
    ThrowOnFailed(GetMainDevice()->CreatePipelineState(&desc, IID_PPV_ARGS(&pso)));
    CHECK_NOTNULL_F(pso);

    return pso;
}

auto oxygen::graphics::d3d12::create_pipeline_state(void* stream, const uint64_t stream_size) -> ID3D12PipelineState*
{
    CHECK_NOTNULL_F(stream);
    CHECK_GT_F(0, stream_size);

    D3D12_PIPELINE_STATE_STREAM_DESC desc {};
    desc.SizeInBytes = stream_size;
    desc.pPipelineStateSubobjectStream = stream;

    return create_pipeline_state(desc);
}
