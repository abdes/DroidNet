//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Renderer/Passes/VirtualShadowScreenDebugPass.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <stdexcept>

#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>

#include <Oxygen/Core/Bindless/Generated.RootSignature.h>
#include <Oxygen/Core/Constants.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/ShaderType.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/DescriptorVisibility.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Graphics/Common/Types/ResourceViewType.h>
#include <Oxygen/Renderer/Passes/DepthPrePass.h>
#include <Oxygen/Renderer/RenderContext.h>
#include <Oxygen/Renderer/Renderer.h>
#include <Oxygen/Renderer/ShadowManager.h>

namespace oxygen::engine {

namespace {

  struct alignas(packing::kShaderDataFieldAlignment)
    VirtualShadowScreenDebugPassConstants {
    bindless::ShaderVisibleIndex depth_texture_index { kInvalidShaderVisibleIndex };
    bindless::ShaderVisibleIndex stats_uav_index { kInvalidShaderVisibleIndex };
    glm::uvec2 screen_dimensions { 0U, 0U };
    glm::mat4 inv_view_projection_matrix { 1.0F };
  };
  static_assert(sizeof(VirtualShadowScreenDebugPassConstants)
      % packing::kShaderDataFieldAlignment
    == 0U);
  constexpr std::uint32_t kScreenDebugPassConstantsStride
    = ((sizeof(VirtualShadowScreenDebugPassConstants)
           + oxygen::packing::kConstantBufferAlignment - 1U)
        / oxygen::packing::kConstantBufferAlignment)
    * oxygen::packing::kConstantBufferAlignment;

} // namespace

VirtualShadowScreenDebugPass::VirtualShadowScreenDebugPass(
  const observer_ptr<Graphics> gfx, std::shared_ptr<Config> config)
  : ComputeRenderPass(config ? config->debug_name : "VirtualShadowScreenDebugPass")
  , gfx_(gfx)
  , config_(std::move(config))
{
}

VirtualShadowScreenDebugPass::~VirtualShadowScreenDebugPass()
{
  if (stats_clear_upload_buffer_ && stats_clear_upload_mapped_ptr_ != nullptr) {
    stats_clear_upload_buffer_->UnMap();
    stats_clear_upload_mapped_ptr_ = nullptr;
  }
  if (pass_constants_buffer_ && pass_constants_mapped_ptr_ != nullptr) {
    pass_constants_buffer_->UnMap();
    pass_constants_mapped_ptr_ = nullptr;
  }
  for (auto& readback : stats_readbacks_) {
    if (readback.buffer && readback.mapped_words != nullptr) {
      readback.buffer->UnMap();
      readback.mapped_words = nullptr;
    }
  }
}

auto VirtualShadowScreenDebugPass::DoPrepareResources(
  graphics::CommandRecorder& recorder) -> co::Co<>
{
  active_dispatch_ = false;
  active_width_ = 0U;
  active_height_ = 0U;
  if (Context().frame_slot != frame::kInvalidSlot) {
    ProcessCompletedStats(Context().frame_slot);
  }

  const auto* prepared_frame = Context().current_view.prepared_frame.get();
  if (prepared_frame == nullptr || !prepared_frame->IsValid()
    || prepared_frame->draw_metadata_bytes.empty()
    || prepared_frame->partitions.empty()) {
    co_return;
  }

  const auto* depth_pass = Context().GetPass<DepthPrePass>();
  const auto shadow_manager = Context().GetRenderer().GetShadowManager();
  if (depth_pass == nullptr || shadow_manager == nullptr
    || Context().current_view.resolved_view == nullptr) {
    co_return;
  }

  const auto* metadata = shadow_manager->TryGetVirtualDirectionalMetadata(
    Context().current_view.view_id);
  if (metadata == nullptr || metadata->clip_level_count == 0U
    || metadata->pages_per_axis == 0U) {
    co_return;
  }

  const auto& depth_texture = depth_pass->GetDepthTexture();
  EnsurePassConstantsBuffer();
  EnsureStatsBuffer();
  EnsureStatsClearUploadBuffer();
  EnsureStatsReadbackBuffer(Context().frame_slot);

  const auto depth_texture_srv = EnsureDepthTextureSrv(depth_texture);
  if (!depth_texture_srv.IsValid() || !stats_uav_index_.IsValid()) {
    co_return;
  }

  const VirtualShadowScreenDebugPassConstants pass_constants {
    .depth_texture_index = depth_texture_srv,
    .stats_uav_index = stats_uav_index_,
    .screen_dimensions = glm::uvec2(depth_texture.GetDescriptor().width,
      depth_texture.GetDescriptor().height),
    .inv_view_projection_matrix
    = Context().current_view.resolved_view->InverseViewProjection(),
  };
  std::memcpy(
    pass_constants_mapped_ptr_, &pass_constants, sizeof(pass_constants));
  SetPassConstantsIndex(pass_constants_index_);

  if (!recorder.IsResourceTracked(depth_texture)) {
    recorder.BeginTrackingResourceState(
      depth_texture, graphics::ResourceStates::kCommon, true);
  }
  if (!recorder.IsResourceTracked(*stats_buffer_)) {
    recorder.BeginTrackingResourceState(
      *stats_buffer_, graphics::ResourceStates::kCommon, true);
  }
  if (!recorder.IsResourceTracked(*stats_clear_upload_buffer_)) {
    recorder.BeginTrackingResourceState(*stats_clear_upload_buffer_,
      graphics::ResourceStates::kCopySource, false);
  }

  recorder.RequireResourceState(
    depth_texture, graphics::ResourceStates::kShaderResource);
  recorder.RequireResourceState(
    *stats_buffer_, graphics::ResourceStates::kCopyDest);
  recorder.FlushBarriers();
  recorder.CopyBuffer(*stats_buffer_, 0U, *stats_clear_upload_buffer_, 0U,
    kStatsWordCount * sizeof(std::uint32_t));
  recorder.RequireResourceState(
    *stats_buffer_, graphics::ResourceStates::kUnorderedAccess);
  recorder.FlushBarriers();

  active_dispatch_ = true;
  active_width_ = depth_texture.GetDescriptor().width;
  active_height_ = depth_texture.GetDescriptor().height;

  co_return;
}

auto VirtualShadowScreenDebugPass::DoExecute(
  graphics::CommandRecorder& recorder) -> co::Co<>
{
  if (!active_dispatch_) {
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

    readback.pending = true;
    readback.source_frame = Context().frame_sequence;
    readback.view_id = Context().current_view.view_id;

    recorder.RequireResourceState(
      *stats_buffer_, graphics::ResourceStates::kUnorderedAccess);
    recorder.FlushBarriers();
  }

  co_return;
}

auto VirtualShadowScreenDebugPass::ValidateConfig() -> void
{
  if (!gfx_) {
    throw std::runtime_error("VirtualShadowScreenDebugPass: graphics is null");
  }
  if (!config_) {
    throw std::runtime_error("VirtualShadowScreenDebugPass: config is null");
  }
}

auto VirtualShadowScreenDebugPass::CreatePipelineStateDesc()
  -> graphics::ComputePipelineDesc
{
  auto generated_bindings = BuildRootBindings();

  graphics::ShaderRequest shader_request {
    .stage = oxygen::ShaderType::kCompute,
    .source_path = "Renderer/VirtualShadowScreenDebug.hlsl",
    .entry_point = "CS",
  };

  return graphics::ComputePipelineDesc::Builder()
    .SetComputeShader(std::move(shader_request))
    .SetRootBindings(std::span<const graphics::RootBindingItem>(
      generated_bindings.data(), generated_bindings.size()))
    .SetDebugName("VirtualShadowScreenDebug_PSO")
    .Build();
}

auto VirtualShadowScreenDebugPass::NeedRebuildPipelineState() const -> bool
{
  return !LastBuiltPsoDesc().has_value();
}

auto VirtualShadowScreenDebugPass::EnsurePassConstantsBuffer() -> void
{
  if (pass_constants_buffer_ && pass_constants_mapped_ptr_ != nullptr
    && pass_constants_index_.IsValid()) {
    return;
  }

  auto& allocator = gfx_->GetDescriptorAllocator();
  auto& registry = gfx_->GetResourceRegistry();
  const graphics::BufferDesc desc {
    .size_bytes = kScreenDebugPassConstantsStride,
    .usage = graphics::BufferUsage::kConstant,
    .memory = graphics::BufferMemory::kUpload,
    .debug_name = "VirtualShadowScreenDebugPass.Constants",
  };
  pass_constants_buffer_ = gfx_->CreateBuffer(desc);
  if (!pass_constants_buffer_) {
    throw std::runtime_error(
      "VirtualShadowScreenDebugPass: failed to create constants buffer");
  }
  registry.Register(pass_constants_buffer_);
  pass_constants_mapped_ptr_ = pass_constants_buffer_->Map(0U, desc.size_bytes);
  if (pass_constants_mapped_ptr_ == nullptr) {
    throw std::runtime_error(
      "VirtualShadowScreenDebugPass: failed to map constants buffer");
  }

  auto cbv_handle
    = allocator.Allocate(graphics::ResourceViewType::kConstantBuffer,
      graphics::DescriptorVisibility::kShaderVisible);
  if (!cbv_handle.IsValid()) {
    throw std::runtime_error(
      "VirtualShadowScreenDebugPass: failed to allocate constants CBV");
  }
  pass_constants_index_ = allocator.GetShaderVisibleIndex(cbv_handle);
  graphics::BufferViewDescription cbv_desc;
  cbv_desc.view_type = graphics::ResourceViewType::kConstantBuffer;
  cbv_desc.visibility = graphics::DescriptorVisibility::kShaderVisible;
  cbv_desc.range = { 0U, sizeof(VirtualShadowScreenDebugPassConstants) };
  pass_constants_cbv_ = registry.RegisterView(
    *pass_constants_buffer_, std::move(cbv_handle), cbv_desc);
}

auto VirtualShadowScreenDebugPass::EnsureStatsBuffer() -> void
{
  if (stats_buffer_ && stats_uav_index_.IsValid()) {
    return;
  }

  auto& allocator = gfx_->GetDescriptorAllocator();
  auto& registry = gfx_->GetResourceRegistry();
  const graphics::BufferDesc desc {
    .size_bytes = kStatsWordCount * sizeof(std::uint32_t),
    .usage = graphics::BufferUsage::kStorage,
    .memory = graphics::BufferMemory::kDeviceLocal,
    .debug_name = "VirtualShadowScreenDebugPass.Stats",
  };
  stats_buffer_ = gfx_->CreateBuffer(desc);
  if (!stats_buffer_) {
    throw std::runtime_error(
      "VirtualShadowScreenDebugPass: failed to create stats buffer");
  }
  registry.Register(stats_buffer_);

  auto uav_handle
    = allocator.Allocate(graphics::ResourceViewType::kStructuredBuffer_UAV,
    graphics::DescriptorVisibility::kShaderVisible);
  if (!uav_handle.IsValid()) {
    throw std::runtime_error(
      "VirtualShadowScreenDebugPass: failed to allocate stats UAV");
  }
  stats_uav_index_ = allocator.GetShaderVisibleIndex(uav_handle);
  graphics::BufferViewDescription uav_desc;
  uav_desc.view_type = graphics::ResourceViewType::kStructuredBuffer_UAV;
  uav_desc.visibility = graphics::DescriptorVisibility::kShaderVisible;
  uav_desc.range = { 0U,
    static_cast<std::uint32_t>(desc.size_bytes) };
  uav_desc.stride = sizeof(std::uint32_t);
  stats_uav_view_ = registry.RegisterView(
    *stats_buffer_, std::move(uav_handle), uav_desc);
}

auto VirtualShadowScreenDebugPass::EnsureStatsClearUploadBuffer() -> void
{
  if (stats_clear_upload_buffer_ && stats_clear_upload_mapped_ptr_ != nullptr) {
    return;
  }

  const graphics::BufferDesc desc {
    .size_bytes = kStatsWordCount * sizeof(std::uint32_t),
    .usage = graphics::BufferUsage::kNone,
    .memory = graphics::BufferMemory::kUpload,
    .debug_name = "VirtualShadowScreenDebugPass.StatsClearUpload",
  };
  stats_clear_upload_buffer_ = gfx_->CreateBuffer(desc);
  if (!stats_clear_upload_buffer_) {
    throw std::runtime_error(
      "VirtualShadowScreenDebugPass: failed to create stats clear upload buffer");
  }
  stats_clear_upload_mapped_ptr_
    = stats_clear_upload_buffer_->Map(0U, desc.size_bytes);
  if (stats_clear_upload_mapped_ptr_ == nullptr) {
    throw std::runtime_error(
      "VirtualShadowScreenDebugPass: failed to map stats clear upload buffer");
  }
  std::memset(stats_clear_upload_mapped_ptr_, 0, desc.size_bytes);
}

auto VirtualShadowScreenDebugPass::EnsureStatsReadbackBuffer(
  const frame::Slot slot) -> void
{
  auto& readback = stats_readbacks_[slot.get()];
  if (readback.buffer && readback.mapped_words != nullptr) {
    return;
  }

  const graphics::BufferDesc desc {
    .size_bytes = kStatsWordCount * sizeof(std::uint32_t),
    .usage = graphics::BufferUsage::kNone,
    .memory = graphics::BufferMemory::kReadBack,
    .debug_name = "VirtualShadowScreenDebugPass.StatsReadback",
  };
  readback.buffer = gfx_->CreateBuffer(desc);
  if (!readback.buffer) {
    throw std::runtime_error(
      "VirtualShadowScreenDebugPass: failed to create stats readback buffer");
  }
  readback.mapped_words = static_cast<std::uint32_t*>(
    readback.buffer->Map(0U, desc.size_bytes));
  if (readback.mapped_words == nullptr) {
    throw std::runtime_error(
      "VirtualShadowScreenDebugPass: failed to map stats readback buffer");
  }
  std::memset(readback.mapped_words, 0, desc.size_bytes);
}

auto VirtualShadowScreenDebugPass::EnsureDepthTextureSrv(
  const graphics::Texture& depth_tex) -> bindless::ShaderVisibleIndex
{
  auto& allocator = gfx_->GetDescriptorAllocator();
  auto& registry = gfx_->GetResourceRegistry();

  const auto depth_format = depth_tex.GetDescriptor().format;
  Format srv_format = Format::kR32Float;
  switch (depth_format) {
  case Format::kDepth32:
  case Format::kDepth32Stencil8:
  case Format::kDepth24Stencil8:
    srv_format = Format::kR32Float;
    break;
  case Format::kDepth16:
    srv_format = Format::kR16UNorm;
    break;
  default:
    srv_format = depth_format;
    break;
  }

  graphics::TextureViewDescription srv_desc {
    .view_type = graphics::ResourceViewType::kTexture_SRV,
    .visibility = graphics::DescriptorVisibility::kShaderVisible,
    .format = srv_format,
    .dimension = depth_tex.GetDescriptor().texture_type,
    .sub_resources = graphics::TextureSubResourceSet::EntireTexture(),
    .is_read_only_dsv = false,
  };

  if (const auto existing_index
    = registry.FindShaderVisibleIndex(depth_tex, srv_desc);
    existing_index.has_value()) {
    depth_texture_srv_ = *existing_index;
    depth_texture_owner_ = &depth_tex;
    owns_depth_texture_srv_ = false;
    return depth_texture_srv_;
  }

  if (depth_texture_srv_.IsValid() && depth_texture_owner_ == &depth_tex
    && owns_depth_texture_srv_ && registry.Contains(depth_tex, srv_desc)) {
    return depth_texture_srv_;
  }

  auto register_new_srv = [&]() -> bindless::ShaderVisibleIndex {
    auto srv_handle
      = allocator.Allocate(graphics::ResourceViewType::kTexture_SRV,
        graphics::DescriptorVisibility::kShaderVisible);
    if (!srv_handle.IsValid()) {
      return kInvalidShaderVisibleIndex;
    }
    depth_texture_srv_ = allocator.GetShaderVisibleIndex(srv_handle);
    auto native_view
      = registry.RegisterView(const_cast<graphics::Texture&>(depth_tex),
        std::move(srv_handle), srv_desc);
    if (!native_view->IsValid()) {
      depth_texture_srv_ = kInvalidShaderVisibleIndex;
      owns_depth_texture_srv_ = false;
      return kInvalidShaderVisibleIndex;
    }
    depth_texture_owner_ = &depth_tex;
    owns_depth_texture_srv_ = true;
    return depth_texture_srv_;
  };

  if (!depth_texture_srv_.IsValid() || !owns_depth_texture_srv_) {
    return register_new_srv();
  }

  const auto updated
    = registry.UpdateView(const_cast<graphics::Texture&>(depth_tex),
      bindless::HeapIndex { depth_texture_srv_.get() }, srv_desc);
  if (!updated) {
    depth_texture_srv_ = kInvalidShaderVisibleIndex;
    depth_texture_owner_ = nullptr;
    owns_depth_texture_srv_ = false;
    return register_new_srv();
  }
  depth_texture_owner_ = &depth_tex;
  owns_depth_texture_srv_ = true;
  return depth_texture_srv_;
}

auto VirtualShadowScreenDebugPass::ProcessCompletedStats(
  const frame::Slot slot) -> void
{
  auto& readback = stats_readbacks_[slot.get()];
  if (!readback.pending || readback.mapped_words == nullptr) {
    return;
  }
  readback.pending = false;

  const auto geometry_pixels = readback.mapped_words[0];
  const auto clip_selected_pixels = readback.mapped_words[1];
  const auto no_page_pixels = readback.mapped_words[2];
  const auto current_page_pixels = readback.mapped_words[3];
  const auto fallback_page_pixels = readback.mapped_words[4];
  const auto invalid_binding_pixels = readback.mapped_words[5];
  const auto clip_select_fail_pixels = readback.mapped_words[6];
  std::string requested_histogram;
  std::string resolved_histogram;
  for (std::uint32_t clip_index = 0U; clip_index < kClipHistogramCount;
       ++clip_index) {
    const auto requested_count = readback.mapped_words[7U + clip_index];
    const auto resolved_count
      = readback.mapped_words[7U + kClipHistogramCount + clip_index];
    if (requested_count > 0U) {
      if (!requested_histogram.empty()) {
        requested_histogram += ",";
      }
      requested_histogram += std::to_string(clip_index);
      requested_histogram += ":";
      requested_histogram += std::to_string(requested_count);
    }
    if (resolved_count > 0U) {
      if (!resolved_histogram.empty()) {
        resolved_histogram += ",";
      }
      resolved_histogram += std::to_string(clip_index);
      resolved_histogram += ":";
      resolved_histogram += std::to_string(resolved_count);
    }
  }
  LOG_F(INFO,
    "VirtualShadowScreenDebugPass: frame={} view={} screen_stats "
    "(geometry={} clip_selected={} no_page={} current={} fallback={} "
    "invalid_bindings={} clip_select_fail={} requested_hist=[{}] "
    "resolved_hist=[{}])",
    readback.source_frame.get(), readback.view_id.get(), geometry_pixels,
    clip_selected_pixels, no_page_pixels, current_page_pixels,
    fallback_page_pixels, invalid_binding_pixels, clip_select_fail_pixels,
    requested_histogram, resolved_histogram);

  if (geometry_pixels > 0U && (current_page_pixels + fallback_page_pixels) == 0U) {
    LOG_F(WARNING,
      "VirtualShadowScreenDebugPass: frame={} view={} no live VSM pages "
      "resolved for any geometry pixel",
      readback.source_frame.get(), readback.view_id.get());
  }
}

} // namespace oxygen::engine
