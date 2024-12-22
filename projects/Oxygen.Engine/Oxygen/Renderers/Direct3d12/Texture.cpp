//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Oxygen/Renderers/Direct3d12/Texture.h"

using namespace oxygen::renderer::d3d12;

void Texture::Initialize(const TextureInitInfo& info)
{
  const auto device = GetMainDevice();
  DCHECK_NOTNULL_F(device);

  const D3D12_CLEAR_VALUE* const clear_value
  {
    (info.desc &&
      (info.desc->Flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET ||
        info.desc->Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL))
      ? info.clear_value
      : nullptr
  };

  if (info.resource)
  {
    DCHECK_EQ_F(nullptr, info.heap);
    resource_.reset(info.resource);
  }
  else {
    CHECK_NOTNULL_F(info.desc);

    if (info.heap)
    {
      ID3D12Resource* resource{ nullptr };
      CheckResult(device->CreatePlacedResource(
        info.heap,
        info.alloc_info.Offset,
        info.desc,
        info.initial_state,
        clear_value,
        IID_PPV_ARGS(&resource)));
      resource_.reset(resource);
    }
    else
    {
      ID3D12Resource* resource{ nullptr };
      CheckResult(device->CreateCommittedResource(
        &kHeapProperties.default_heap_props,
        D3D12_HEAP_FLAG_NONE,
        info.desc,
        D3D12_RESOURCE_STATE_COMMON,
        clear_value,
        IID_PPV_ARGS(&resource)));
      resource_.reset(resource);
    }
  }

  srv_ = detail::GetRenderer().SrvHeap().Allocate();
  device->CreateShaderResourceView(resource_.get(), info.srv_dec, srv_.cpu);

}

void Texture::Release()
{
  detail::GetRenderer().SrvHeap().Free(srv_);
  resource_.reset();
}

void RenderTexture::Initialize(const TextureInitInfo& info)
{
  texture_.Initialize(info);


  const auto resource = texture_.GetResource();

  DCHECK_NOTNULL_F(info.desc);
  const auto mip_levels = info.desc->MipLevels;
  DCHECK_LE_F(mip_levels, Texture::max_mips);
  mip_count_ = mip_levels;

  const auto device = GetMainDevice();
  DCHECK_NOTNULL_F(device);

  D3D12_RENDER_TARGET_VIEW_DESC rtv_desc
  {
    .Format = info.desc->Format,
    .ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D,
    .Texture2D = {.MipSlice = 0, .PlaneSlice = 0 }
  };

  for (uint32_t i = 0; i < mip_levels; ++i)
  {
    rtv_[i] = detail::GetRenderer().RtvHeap().Allocate();
    device->CreateRenderTargetView(resource, &rtv_desc, rtv_[i].cpu);
    rtv_desc.Texture2D.MipSlice++;
  }
}

void RenderTexture::Release()
{
  for (uint32_t i = 0; i < mip_count_; ++i)
  {
    detail::GetRenderer().RtvHeap().Free(rtv_[i]);
  }
  texture_.Release();
  mip_count_ = 0;
}

void DepthBuffer::Initialize(TextureInitInfo info)
{
  DCHECK_NOTNULL_F(info.desc);
  DCHECK_NOTNULL_F(texture_.GetResource());
  DCHECK_NOTNULL_F(GetMainDevice());

  // We will use the depth buffer to read from it in the shader and to write to
  // it. Therefore, we will create it with TYPELESS format, and create two views
  // for it.

  const DXGI_FORMAT dsv_format = info.desc->Format;

  D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc{};
  if (info.desc->Format == DXGI_FORMAT_D32_FLOAT)
  {
    info.desc->Format = DXGI_FORMAT_R32_TYPELESS;
    srv_desc.Format = DXGI_FORMAT_R32_FLOAT;
  }
  srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
  srv_desc.Texture2D.MipLevels = 1;
  srv_desc.Texture2D.MostDetailedMip = 0;
  srv_desc.Texture2D.PlaneSlice = 0;
  srv_desc.Texture2D.ResourceMinLODClamp = 0.0f;


  DCHECK_F(!info.srv_dec && !info.resource);
  info.srv_dec = &srv_desc;

  texture_.Initialize(info);

  const D3D12_DEPTH_STENCIL_VIEW_DESC dsv_desc
  {
      .Format = dsv_format,
      .ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D,
      .Flags = D3D12_DSV_FLAG_NONE,
      .Texture2D = {.MipSlice = 0 }
  };

  dsv_ = detail::GetRenderer().DsvHeap().Allocate();
  GetMainDevice()->CreateDepthStencilView(texture_.GetResource(), &dsv_desc, dsv_.cpu);
}

void DepthBuffer::Release()
{
  detail::GetRenderer().DsvHeap().Free(dsv_);
  texture_.Release();
}
