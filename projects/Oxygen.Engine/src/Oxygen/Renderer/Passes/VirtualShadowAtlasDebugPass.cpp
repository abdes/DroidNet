//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Renderer/Passes/VirtualShadowAtlasDebugPass.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <stdexcept>

#include <glm/vec2.hpp>

#include <Oxygen/Core/Bindless/Generated.RootSignature.h>
#include <Oxygen/Core/Constants.h>
#include <Oxygen/Core/Types/ShaderType.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/DescriptorVisibility.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Graphics/Common/Types/ResourceViewType.h>
#include <Oxygen/Renderer/Passes/VirtualShadowPageRasterPass.h>
#include <Oxygen/Renderer/RenderContext.h>
#include <Oxygen/Renderer/Renderer.h>
#include <Oxygen/Renderer/ShadowManager.h>

namespace oxygen::engine {

namespace {

  struct alignas(packing::kShaderDataFieldAlignment)
    VirtualShadowAtlasDebugPassConstants {
    bindless::ShaderVisibleIndex source_texture_index {
      kInvalidShaderVisibleIndex
    };
    bindless::ShaderVisibleIndex output_texture_uav_index {
      kInvalidShaderVisibleIndex
    };
    glm::uvec2 atlas_dimensions { 0U, 0U };
    std::uint32_t page_size_texels { 0U };
    std::uint32_t _pad0 { 0U };
  };
  static_assert(sizeof(VirtualShadowAtlasDebugPassConstants)
      % packing::kShaderDataFieldAlignment
    == 0U);

} // namespace

VirtualShadowAtlasDebugPass::VirtualShadowAtlasDebugPass(
  const observer_ptr<Graphics> gfx, std::shared_ptr<Config> config)
  : ComputeRenderPass(config ? config->debug_name : "VirtualShadowAtlasDebugPass")
  , gfx_(gfx)
  , config_(std::move(config))
{
}

VirtualShadowAtlasDebugPass::~VirtualShadowAtlasDebugPass()
{
  if (pass_constants_buffer_ && pass_constants_mapped_ptr_ != nullptr) {
    pass_constants_buffer_->UnMap();
    pass_constants_mapped_ptr_ = nullptr;
  }
}

auto VirtualShadowAtlasDebugPass::GetOutputTexture() const noexcept
  -> const std::shared_ptr<graphics::Texture>&
{
  return output_texture_;
}

auto VirtualShadowAtlasDebugPass::DoPrepareResources(
  graphics::CommandRecorder& recorder) -> co::Co<>
{
  active_dispatch_ = false;
  active_width_ = 0U;
  active_height_ = 0U;

  const auto* shadow_raster_pass
    = Context().GetPass<VirtualShadowPageRasterPass>();
  const auto shadow_manager = Context().GetRenderer().GetShadowManager();
  if (shadow_raster_pass == nullptr || shadow_manager == nullptr) {
    co_return;
  }

  const auto& source_texture = shadow_manager->GetVirtualShadowDepthTexture();
  if (!source_texture) {
    co_return;
  }

  const auto* virtual_view = shadow_manager->TryGetVirtualViewIntrospection(
    Context().current_view.view_id);
  if (virtual_view == nullptr
    || virtual_view->directional_virtual_metadata.empty()) {
    co_return;
  }

  const auto& metadata = virtual_view->directional_virtual_metadata.front();
  if (metadata.page_size_texels == 0U) {
    co_return;
  }

  EnsurePassConstantsBuffer();
  EnsureOutputTexture(source_texture->GetDescriptor().width,
    source_texture->GetDescriptor().height);
  const auto source_texture_index = EnsureSourceTextureSrv(*source_texture);
  const auto output_texture_uav_index = EnsureOutputTextureUav();
  if (!source_texture_index.IsValid() || !output_texture_uav_index.IsValid()
    || !output_texture_) {
    co_return;
  }

  if (!recorder.IsResourceTracked(*source_texture)) {
    recorder.BeginTrackingResourceState(
      *source_texture, graphics::ResourceStates::kCommon, true);
  }
  if (!recorder.IsResourceTracked(*output_texture_)) {
    recorder.BeginTrackingResourceState(
      *output_texture_, graphics::ResourceStates::kCommon, true);
  }

  recorder.RequireResourceState(
    *source_texture, graphics::ResourceStates::kShaderResource);
  recorder.RequireResourceState(
    *output_texture_, graphics::ResourceStates::kUnorderedAccess);
  recorder.FlushBarriers();

  const VirtualShadowAtlasDebugPassConstants pass_constants {
    .source_texture_index = source_texture_index,
    .output_texture_uav_index = output_texture_uav_index,
    .atlas_dimensions = glm::uvec2(source_texture->GetDescriptor().width,
      source_texture->GetDescriptor().height),
    .page_size_texels = metadata.page_size_texels,
  };
  std::memcpy(
    pass_constants_mapped_ptr_, &pass_constants, sizeof(pass_constants));
  SetPassConstantsIndex(pass_constants_index_);

  active_dispatch_ = true;
  active_width_ = source_texture->GetDescriptor().width;
  active_height_ = source_texture->GetDescriptor().height;

  co_return;
}

auto VirtualShadowAtlasDebugPass::DoExecute(
  graphics::CommandRecorder& recorder) -> co::Co<>
{
  if (!active_dispatch_ || !output_texture_) {
    co_return;
  }

  const auto group_count_x
    = (std::max(1U, active_width_) + kDispatchGroupSize - 1U)
    / kDispatchGroupSize;
  const auto group_count_y
    = (std::max(1U, active_height_) + kDispatchGroupSize - 1U)
    / kDispatchGroupSize;
  recorder.Dispatch(group_count_x, group_count_y, 1U);

  recorder.RequireResourceState(
    *output_texture_, graphics::ResourceStates::kShaderResource);
  recorder.FlushBarriers();

  co_return;
}

auto VirtualShadowAtlasDebugPass::ValidateConfig() -> void
{
  if (!gfx_) {
    throw std::runtime_error("VirtualShadowAtlasDebugPass: graphics is null");
  }
  if (!config_) {
    throw std::runtime_error(
      "VirtualShadowAtlasDebugPass: config is null");
  }
}

auto VirtualShadowAtlasDebugPass::CreatePipelineStateDesc()
  -> graphics::ComputePipelineDesc
{
  auto generated_bindings = BuildRootBindings();

  graphics::ShaderRequest shader_request {
    .stage = oxygen::ShaderType::kCompute,
    .source_path = "Renderer/VirtualShadowAtlasDebug.hlsl",
    .entry_point = "CS",
  };

  return graphics::ComputePipelineDesc::Builder()
    .SetComputeShader(std::move(shader_request))
    .SetRootBindings(std::span<const graphics::RootBindingItem>(
      generated_bindings.data(), generated_bindings.size()))
    .SetDebugName("VirtualShadowAtlasDebug_PSO")
    .Build();
}

auto VirtualShadowAtlasDebugPass::NeedRebuildPipelineState() const -> bool
{
  return !LastBuiltPsoDesc().has_value();
}

auto VirtualShadowAtlasDebugPass::EnsurePassConstantsBuffer() -> void
{
  if (pass_constants_buffer_ && pass_constants_mapped_ptr_ != nullptr
    && pass_constants_index_.IsValid()) {
    return;
  }

  auto& allocator = gfx_->GetDescriptorAllocator();

  const graphics::BufferDesc desc {
    .size_bytes = 256U,
    .usage = graphics::BufferUsage::kConstant,
    .memory = graphics::BufferMemory::kUpload,
    .debug_name = "VirtualShadowAtlasDebugPass.Constants",
  };
  pass_constants_buffer_ = gfx_->CreateBuffer(desc);
  if (!pass_constants_buffer_) {
    throw std::runtime_error(
      "VirtualShadowAtlasDebugPass: failed to create constants buffer");
  }
  pass_constants_mapped_ptr_ = pass_constants_buffer_->Map(0U, desc.size_bytes);
  if (pass_constants_mapped_ptr_ == nullptr) {
    throw std::runtime_error(
      "VirtualShadowAtlasDebugPass: failed to map constants buffer");
  }

  auto cbv_handle = allocator.Allocate(graphics::ResourceViewType::kConstantBuffer,
    graphics::DescriptorVisibility::kShaderVisible);
  if (!cbv_handle.IsValid()) {
    throw std::runtime_error(
      "VirtualShadowAtlasDebugPass: failed to allocate constants CBV");
  }

  graphics::BufferViewDescription cbv_desc;
  cbv_desc.view_type = graphics::ResourceViewType::kConstantBuffer;
  cbv_desc.visibility = graphics::DescriptorVisibility::kShaderVisible;
  cbv_desc.range = { 0U, desc.size_bytes };

  pass_constants_index_ = allocator.GetShaderVisibleIndex(cbv_handle);
  pass_constants_cbv_view_
    = pass_constants_buffer_->GetNativeView(cbv_handle, cbv_desc);
  if (!pass_constants_cbv_view_->IsValid()) {
    throw std::runtime_error(
      "VirtualShadowAtlasDebugPass: failed to create constants CBV");
  }
  pass_constants_cbv_handle_ = std::move(cbv_handle);
}

auto VirtualShadowAtlasDebugPass::EnsureSourceTextureSrv(
  const graphics::Texture& source_texture) -> bindless::ShaderVisibleIndex
{
  if (source_texture_owner_ == &source_texture
    && source_texture_srv_index_.IsValid()) {
    return source_texture_srv_index_;
  }

  source_texture_owner_ = nullptr;
  source_texture_srv_view_ = {};
  source_texture_srv_handle_ = {};
  source_texture_srv_index_ = kInvalidShaderVisibleIndex;

  auto& allocator = gfx_->GetDescriptorAllocator();
  auto srv_handle = allocator.Allocate(graphics::ResourceViewType::kTexture_SRV,
    graphics::DescriptorVisibility::kShaderVisible);
  if (!srv_handle.IsValid()) {
    return kInvalidShaderVisibleIndex;
  }

  const auto& desc = source_texture.GetDescriptor();
  const graphics::TextureViewDescription srv_desc {
    .view_type = graphics::ResourceViewType::kTexture_SRV,
    .visibility = graphics::DescriptorVisibility::kShaderVisible,
    .format = ResolveSourceSrvFormat(desc.format),
    .dimension = desc.texture_type,
    .sub_resources = graphics::TextureSubResourceSet::EntireTexture(),
    .is_read_only_dsv = false,
  };

  const auto srv_view = source_texture.GetNativeView(srv_handle, srv_desc);
  if (!srv_view->IsValid()) {
    return kInvalidShaderVisibleIndex;
  }

  source_texture_srv_index_ = allocator.GetShaderVisibleIndex(srv_handle);
  source_texture_srv_view_ = srv_view;
  source_texture_srv_handle_ = std::move(srv_handle);
  source_texture_owner_ = &source_texture;
  return source_texture_srv_index_;
}

auto VirtualShadowAtlasDebugPass::EnsureOutputTexture(
  const std::uint32_t width, const std::uint32_t height) -> void
{
  if (output_texture_
    && output_texture_->GetDescriptor().width == width
    && output_texture_->GetDescriptor().height == height) {
    return;
  }

  output_texture_uav_view_ = {};
  output_texture_uav_handle_ = {};
  output_texture_uav_index_ = kInvalidShaderVisibleIndex;
  output_texture_.reset();

  graphics::TextureDesc desc {};
  desc.width = std::max(1U, width);
  desc.height = std::max(1U, height);
  desc.array_size = 1U;
  desc.mip_levels = 1U;
  desc.format = oxygen::Format::kRGBA8UNorm;
  desc.texture_type = oxygen::TextureType::kTexture2D;
  desc.is_shader_resource = true;
  desc.is_uav = true;
  desc.initial_state = graphics::ResourceStates::kCommon;
  desc.debug_name = "VirtualShadowAtlasDebugTexture";

  output_texture_ = gfx_->CreateTexture(desc);
  if (!output_texture_) {
    throw std::runtime_error(
      "VirtualShadowAtlasDebugPass: failed to create output texture");
  }
}

auto VirtualShadowAtlasDebugPass::EnsureOutputTextureUav()
  -> bindless::ShaderVisibleIndex
{
  if (output_texture_ == nullptr) {
    return kInvalidShaderVisibleIndex;
  }
  if (output_texture_uav_index_.IsValid()) {
    return output_texture_uav_index_;
  }

  auto& allocator = gfx_->GetDescriptorAllocator();
  auto uav_handle = allocator.Allocate(graphics::ResourceViewType::kTexture_UAV,
    graphics::DescriptorVisibility::kShaderVisible);
  if (!uav_handle.IsValid()) {
    return kInvalidShaderVisibleIndex;
  }

  const auto& desc = output_texture_->GetDescriptor();
  const graphics::TextureViewDescription uav_desc {
    .view_type = graphics::ResourceViewType::kTexture_UAV,
    .visibility = graphics::DescriptorVisibility::kShaderVisible,
    .format = desc.format,
    .dimension = desc.texture_type,
    .sub_resources = graphics::TextureSubResourceSet::EntireTexture(),
    .is_read_only_dsv = false,
  };

  const auto uav_view = output_texture_->GetNativeView(uav_handle, uav_desc);
  if (!uav_view->IsValid()) {
    return kInvalidShaderVisibleIndex;
  }

  output_texture_uav_index_ = allocator.GetShaderVisibleIndex(uav_handle);
  output_texture_uav_view_ = uav_view;
  output_texture_uav_handle_ = std::move(uav_handle);
  return output_texture_uav_index_;
}

auto VirtualShadowAtlasDebugPass::ResolveSourceSrvFormat(const Format format)
  -> Format
{
  switch (format) {
  case Format::kDepth32:
  case Format::kDepth32Stencil8:
  case Format::kDepth24Stencil8:
    return Format::kR32Float;
  case Format::kDepth16:
    return Format::kR16UNorm;
  default:
    return format;
  }
}

} // namespace oxygen::engine
