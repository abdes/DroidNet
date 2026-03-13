//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Renderer/Passes/VirtualShadowAtlasDebugPass.h>

#include <algorithm>
#include <bit>
#include <cstddef>
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
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
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
    bindless::ShaderVisibleIndex tile_state_buffer_index {
      kInvalidShaderVisibleIndex
    };
    bindless::ShaderVisibleIndex output_texture_uav_index {
      kInvalidShaderVisibleIndex
    };
    bindless::ShaderVisibleIndex stats_uav_index {
      kInvalidShaderVisibleIndex
    };
    glm::uvec2 atlas_dimensions { 0U, 0U };
    std::uint32_t atlas_tiles_per_axis { 0U };
    std::uint32_t page_size_texels { 0U };
  };
  static_assert(
    offsetof(VirtualShadowAtlasDebugPassConstants, atlas_dimensions) == 16U);
  static_assert(
    offsetof(VirtualShadowAtlasDebugPassConstants, atlas_tiles_per_axis) == 24U);
  static_assert(
    offsetof(VirtualShadowAtlasDebugPassConstants, page_size_texels) == 28U);
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
  if (tile_state_buffer_ && tile_state_mapped_ptr_ != nullptr) {
    tile_state_buffer_->UnMap();
    tile_state_mapped_ptr_ = nullptr;
  }
  if (stats_clear_upload_buffer_ && stats_clear_upload_mapped_ptr_ != nullptr) {
    stats_clear_upload_buffer_->UnMap();
    stats_clear_upload_mapped_ptr_ = nullptr;
  }
  for (auto& readback : stats_readbacks_) {
    if (readback.buffer && readback.mapped_words != nullptr) {
      readback.buffer->UnMap();
      readback.mapped_words = nullptr;
    }
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
  if (Context().frame_slot != frame::kInvalidSlot) {
    ProcessCompletedStats(Context().frame_slot);
  }

  const auto* shadow_raster_pass
    = Context().GetPass<VirtualShadowPageRasterPass>();
  const auto shadow_manager = Context().GetRenderer().GetShadowManager();
  if (shadow_raster_pass == nullptr || shadow_manager == nullptr) {
    LOG_F(INFO,
      "VirtualShadowAtlasDebugPass: skipped for view {} "
      "(raster_pass={} shadow_manager={})",
      Context().current_view.view_id.get(), shadow_raster_pass != nullptr,
      shadow_manager != nullptr);
    co_return;
  }

  const auto& source_texture = shadow_manager->GetVirtualShadowDepthTexture();
  if (!source_texture) {
    LOG_F(INFO,
      "VirtualShadowAtlasDebugPass: skipped for view {} "
      "(no virtual shadow depth texture)",
      Context().current_view.view_id.get());
    co_return;
  }

  const auto* virtual_view = shadow_manager->TryGetVirtualViewIntrospection(
    Context().current_view.view_id);
  if (virtual_view == nullptr
    || virtual_view->directional_virtual_metadata.empty()) {
    LOG_F(INFO,
      "VirtualShadowAtlasDebugPass: skipped for view {} "
      "(virtual_view={} metadata_count={})",
      Context().current_view.view_id.get(), virtual_view != nullptr,
      virtual_view != nullptr
        ? virtual_view->directional_virtual_metadata.size()
        : 0U);
    co_return;
  }

  const auto& metadata = virtual_view->directional_virtual_metadata.front();
  if (metadata.page_size_texels == 0U) {
    LOG_F(INFO,
      "VirtualShadowAtlasDebugPass: skipped for view {} "
      "(page_size_texels=0)",
      Context().current_view.view_id.get());
    co_return;
  }

  EnsurePassConstantsBuffer();
  EnsureStatsBuffer();
  EnsureStatsClearUploadBuffer();
  EnsureOutputTexture(source_texture->GetDescriptor().width,
    source_texture->GetDescriptor().height);
  const auto source_texture_index = EnsureSourceTextureSrv(*source_texture);
  const auto tile_state_buffer_index
    = UploadTileStates(virtual_view->atlas_tile_debug_states);
  const auto output_texture_uav_index = EnsureOutputTextureUav();
  if (Context().frame_slot != frame::kInvalidSlot) {
    EnsureStatsReadbackBuffer(Context().frame_slot);
  }
  if (!source_texture_index.IsValid() || !tile_state_buffer_index.IsValid()
    || !output_texture_uav_index.IsValid() || !stats_uav_index_.IsValid()
    || !output_texture_) {
    LOG_F(INFO,
      "VirtualShadowAtlasDebugPass: skipped for view {} "
      "(source_srv={} tile_states={} output_uav={} stats_uav={} "
      "output_texture={} tile_state_count={})",
      Context().current_view.view_id.get(), source_texture_index.IsValid(),
      tile_state_buffer_index.IsValid(), output_texture_uav_index.IsValid(),
      stats_uav_index_.IsValid(), output_texture_ != nullptr,
      virtual_view->atlas_tile_debug_states.size());
    co_return;
  }

  if (!recorder.IsResourceTracked(*source_texture)) {
    recorder.BeginTrackingResourceState(
      *source_texture, graphics::ResourceStates::kCommon, true);
  }
  if (tile_state_buffer_ != nullptr
    && !recorder.IsResourceTracked(*tile_state_buffer_)) {
    recorder.BeginTrackingResourceState(
      *tile_state_buffer_, graphics::ResourceStates::kGenericRead, true);
  }
  if (stats_buffer_ != nullptr && !recorder.IsResourceTracked(*stats_buffer_)) {
    recorder.BeginTrackingResourceState(
      *stats_buffer_, graphics::ResourceStates::kCommon, true);
  }
  if (stats_clear_upload_buffer_ != nullptr
    && !recorder.IsResourceTracked(*stats_clear_upload_buffer_)) {
    recorder.BeginTrackingResourceState(*stats_clear_upload_buffer_,
      graphics::ResourceStates::kCopySource, true);
  }
  if (!recorder.IsResourceTracked(*output_texture_)) {
    recorder.BeginTrackingResourceState(
      *output_texture_, graphics::ResourceStates::kCommon, true);
  }

  recorder.RequireResourceState(
    *source_texture, graphics::ResourceStates::kShaderResource);
  if (tile_state_buffer_ != nullptr) {
    recorder.RequireResourceState(
      *tile_state_buffer_, graphics::ResourceStates::kShaderResource);
  }
  recorder.RequireResourceState(
    *stats_buffer_, graphics::ResourceStates::kCopyDest);
  recorder.RequireResourceState(
    *stats_clear_upload_buffer_, graphics::ResourceStates::kCopySource);
  recorder.RequireResourceState(
    *output_texture_, graphics::ResourceStates::kUnorderedAccess);
  recorder.FlushBarriers();
  recorder.CopyBuffer(*stats_buffer_, 0U, *stats_clear_upload_buffer_, 0U,
    kStatsWordCount * sizeof(std::uint32_t));
  recorder.RequireResourceState(
    *stats_buffer_, graphics::ResourceStates::kUnorderedAccess);
  recorder.FlushBarriers();

  const VirtualShadowAtlasDebugPassConstants pass_constants {
    .source_texture_index = source_texture_index,
    .tile_state_buffer_index = tile_state_buffer_index,
    .output_texture_uav_index = output_texture_uav_index,
    .stats_uav_index = stats_uav_index_,
    .atlas_dimensions = glm::uvec2(source_texture->GetDescriptor().width,
      source_texture->GetDescriptor().height),
    .atlas_tiles_per_axis = metadata.page_size_texels > 0U
      ? source_texture->GetDescriptor().width / metadata.page_size_texels
      : 0U,
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

  if (Context().frame_slot != frame::kInvalidSlot) {
    auto& readback = stats_readbacks_[Context().frame_slot.get()];
    if (stats_buffer_ != nullptr && readback.buffer != nullptr) {
      if (!recorder.IsResourceTracked(*readback.buffer)) {
        recorder.BeginTrackingResourceState(
          *readback.buffer, graphics::ResourceStates::kCopyDest, false);
      }
      recorder.RequireResourceState(
        *stats_buffer_, graphics::ResourceStates::kCopySource);
      recorder.RequireResourceState(
        *readback.buffer, graphics::ResourceStates::kCopyDest);
      recorder.FlushBarriers();
      recorder.CopyBuffer(*readback.buffer, 0U, *stats_buffer_, 0U,
        kStatsWordCount * sizeof(std::uint32_t));
      readback.source_frame = Context().frame_sequence;
      readback.view_id = Context().current_view.view_id;
      readback.atlas_width = active_width_;
      readback.atlas_height = active_height_;
      readback.pending = true;
    }
  }

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

auto VirtualShadowAtlasDebugPass::EnsureTileStateBuffer(
  const std::uint32_t tile_count) -> void
{
  if (tile_count == 0U) {
    return;
  }
  if (tile_state_buffer_ != nullptr && tile_state_mapped_ptr_ != nullptr
    && tile_state_srv_index_.IsValid() && tile_state_capacity_ >= tile_count) {
    return;
  }

  if (tile_state_buffer_ != nullptr && tile_state_mapped_ptr_ != nullptr) {
    tile_state_buffer_->UnMap();
    tile_state_mapped_ptr_ = nullptr;
  }

  tile_state_srv_view_ = {};
  tile_state_srv_handle_ = {};
  tile_state_srv_index_ = kInvalidShaderVisibleIndex;
  tile_state_buffer_.reset();

  const auto capacity = std::max(1U, tile_count);
  const graphics::BufferDesc desc {
    .size_bytes = static_cast<std::uint64_t>(capacity) * sizeof(std::uint32_t),
    .usage = graphics::BufferUsage::kNone,
    .memory = graphics::BufferMemory::kUpload,
    .debug_name = "VirtualShadowAtlasDebugPass.TileStates",
  };
  tile_state_buffer_ = gfx_->CreateBuffer(desc);
  if (!tile_state_buffer_) {
    throw std::runtime_error(
      "VirtualShadowAtlasDebugPass: failed to create tile-state buffer");
  }

  tile_state_mapped_ptr_ = tile_state_buffer_->Map(0U, desc.size_bytes);
  if (tile_state_mapped_ptr_ == nullptr) {
    tile_state_buffer_.reset();
    throw std::runtime_error(
      "VirtualShadowAtlasDebugPass: failed to map tile-state buffer");
  }

  auto& registry = gfx_->GetResourceRegistry();
  if (!registry.Contains(*tile_state_buffer_)) {
    registry.Register(tile_state_buffer_);
  }

  auto& allocator = gfx_->GetDescriptorAllocator();
  auto srv_handle = allocator.Allocate(
    graphics::ResourceViewType::kStructuredBuffer_SRV,
    graphics::DescriptorVisibility::kShaderVisible);
  if (!srv_handle.IsValid()) {
    throw std::runtime_error(
      "VirtualShadowAtlasDebugPass: failed to allocate tile-state SRV");
  }

  graphics::BufferViewDescription srv_desc {};
  srv_desc.view_type = graphics::ResourceViewType::kStructuredBuffer_SRV;
  srv_desc.visibility = graphics::DescriptorVisibility::kShaderVisible;
  srv_desc.range = graphics::BufferRange(
    0U, static_cast<std::uint64_t>(capacity) * sizeof(std::uint32_t));
  srv_desc.stride = sizeof(std::uint32_t);

  tile_state_srv_index_ = allocator.GetShaderVisibleIndex(srv_handle);
  tile_state_srv_view_
    = registry.RegisterView(*tile_state_buffer_, std::move(srv_handle), srv_desc);
  if (!tile_state_srv_view_->IsValid()) {
    throw std::runtime_error(
      "VirtualShadowAtlasDebugPass: failed to create tile-state SRV");
  }

  tile_state_capacity_ = capacity;
}

auto VirtualShadowAtlasDebugPass::UploadTileStates(
  const std::span<const std::uint32_t> tile_states)
  -> bindless::ShaderVisibleIndex
{
  if (tile_states.empty()) {
    return kInvalidShaderVisibleIndex;
  }

  EnsureTileStateBuffer(static_cast<std::uint32_t>(tile_states.size()));
  if (!tile_state_srv_index_.IsValid() || tile_state_mapped_ptr_ == nullptr) {
    return kInvalidShaderVisibleIndex;
  }

  std::memcpy(tile_state_mapped_ptr_, tile_states.data(),
    tile_states.size_bytes());
  return tile_state_srv_index_;
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

auto VirtualShadowAtlasDebugPass::EnsureStatsBuffer() -> void
{
  if (stats_buffer_ != nullptr && stats_uav_index_.IsValid()) {
    return;
  }

  auto& allocator = gfx_->GetDescriptorAllocator();
  auto& registry = gfx_->GetResourceRegistry();

  const graphics::BufferDesc desc {
    .size_bytes = kStatsWordCount * sizeof(std::uint32_t),
    .usage = graphics::BufferUsage::kStorage,
    .memory = graphics::BufferMemory::kDeviceLocal,
    .debug_name = "VirtualShadowAtlasDebugPass.Stats",
  };
  stats_buffer_ = gfx_->CreateBuffer(desc);
  if (!stats_buffer_) {
    throw std::runtime_error(
      "VirtualShadowAtlasDebugPass: failed to create stats buffer");
  }
  registry.Register(stats_buffer_);

  auto uav_handle
    = allocator.Allocate(graphics::ResourceViewType::kStructuredBuffer_UAV,
      graphics::DescriptorVisibility::kShaderVisible);
  if (!uav_handle.IsValid()) {
    throw std::runtime_error(
      "VirtualShadowAtlasDebugPass: failed to allocate stats UAV");
  }

  graphics::BufferViewDescription uav_desc {};
  uav_desc.view_type = graphics::ResourceViewType::kStructuredBuffer_UAV;
  uav_desc.visibility = graphics::DescriptorVisibility::kShaderVisible;
  uav_desc.range = { 0U, desc.size_bytes };
  uav_desc.stride = sizeof(std::uint32_t);

  stats_uav_index_ = allocator.GetShaderVisibleIndex(uav_handle);
  stats_uav_view_
    = registry.RegisterView(*stats_buffer_, std::move(uav_handle), uav_desc);
  if (!stats_uav_view_->IsValid()) {
    throw std::runtime_error(
      "VirtualShadowAtlasDebugPass: failed to create stats UAV");
  }
}

auto VirtualShadowAtlasDebugPass::EnsureStatsClearUploadBuffer() -> void
{
  if (stats_clear_upload_buffer_ != nullptr
    && stats_clear_upload_mapped_ptr_ != nullptr) {
    return;
  }

  const graphics::BufferDesc desc {
    .size_bytes = kStatsWordCount * sizeof(std::uint32_t),
    .usage = graphics::BufferUsage::kNone,
    .memory = graphics::BufferMemory::kUpload,
    .debug_name = "VirtualShadowAtlasDebugPass.StatsClearUpload",
  };
  stats_clear_upload_buffer_ = gfx_->CreateBuffer(desc);
  if (!stats_clear_upload_buffer_) {
    throw std::runtime_error(
      "VirtualShadowAtlasDebugPass: failed to create stats clear upload buffer");
  }

  stats_clear_upload_mapped_ptr_
    = stats_clear_upload_buffer_->Map(0U, desc.size_bytes);
  if (stats_clear_upload_mapped_ptr_ == nullptr) {
    throw std::runtime_error(
      "VirtualShadowAtlasDebugPass: failed to map stats clear upload buffer");
  }

  auto* clear_words
    = static_cast<std::uint32_t*>(stats_clear_upload_mapped_ptr_);
  clear_words[0] = 0U;
  clear_words[1] = 0U;
  clear_words[2] = 0xFFFFFFFFU;
  clear_words[3] = 0U;
  clear_words[4] = 0U;
}

auto VirtualShadowAtlasDebugPass::EnsureStatsReadbackBuffer(
  const frame::Slot slot) -> void
{
  auto& readback = stats_readbacks_[slot.get()];
  if (readback.buffer != nullptr && readback.mapped_words != nullptr) {
    return;
  }

  const graphics::BufferDesc desc {
    .size_bytes = kStatsWordCount * sizeof(std::uint32_t),
    .usage = graphics::BufferUsage::kNone,
    .memory = graphics::BufferMemory::kReadBack,
    .debug_name = "VirtualShadowAtlasDebugPass.StatsReadback",
  };
  readback.buffer = gfx_->CreateBuffer(desc);
  if (!readback.buffer) {
    throw std::runtime_error(
      "VirtualShadowAtlasDebugPass: failed to create stats readback buffer");
  }

  readback.mapped_words
    = static_cast<std::uint32_t*>(readback.buffer->Map(0U, desc.size_bytes));
  if (readback.mapped_words == nullptr) {
    throw std::runtime_error(
      "VirtualShadowAtlasDebugPass: failed to map stats readback buffer");
  }
  std::memset(readback.mapped_words, 0, desc.size_bytes);
}

auto VirtualShadowAtlasDebugPass::ProcessCompletedStats(const frame::Slot slot)
  -> void
{
  auto& readback = stats_readbacks_[slot.get()];
  if (!readback.pending || readback.mapped_words == nullptr) {
    return;
  }
  readback.pending = false;

  const auto written_pixels = readback.mapped_words[0];
  const auto nonclear_pixels = readback.mapped_words[1];
  const auto min_depth_bits = readback.mapped_words[2];
  const auto max_depth_bits = readback.mapped_words[3];
  const auto nonzero_tile_state_pixels = readback.mapped_words[4];
  const auto atlas_pixels = readback.atlas_width * readback.atlas_height;
  const auto min_depth = std::bit_cast<float>(
    written_pixels > 0U ? min_depth_bits : 0x3F800000U);
  const auto max_depth = std::bit_cast<float>(max_depth_bits);

  LOG_F(INFO,
    "VirtualShadowAtlasDebugPass: frame={} view={} atlas_stats "
    "(pixels={} written={} nonclear={} min_depth={:.6f} max_depth={:.6f} "
    "nonzero_tile_state_pixels={})",
    readback.source_frame.get(), readback.view_id.get(), atlas_pixels,
    written_pixels, nonclear_pixels, min_depth, max_depth,
    nonzero_tile_state_pixels);

  if (written_pixels != atlas_pixels) {
    LOG_F(ERROR,
      "VirtualShadowAtlasDebugPass: frame={} view={} atlas debug dispatch "
      "did not cover the full texture (written={} pixels={})",
      readback.source_frame.get(), readback.view_id.get(), written_pixels,
      atlas_pixels);
  }
  if (written_pixels > 0U && nonclear_pixels == 0U) {
    LOG_F(WARNING,
      "VirtualShadowAtlasDebugPass: frame={} view={} source atlas contains no "
      "non-clear depth texels even though the debug pass executed",
      readback.source_frame.get(), readback.view_id.get());
  }
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
