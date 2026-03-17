//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <stdexcept>

#include <Oxygen/Base/Logging.h>
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
#include <Oxygen/Renderer/Internal/ShadowBackendCommon.h>
#include <Oxygen/Renderer/Passes/DepthPrePass.h>
#include <Oxygen/Renderer/Passes/VirtualShadowCoarseMarkPass.h>
#include <Oxygen/Renderer/Passes/VirtualShadowRequestPass.h>
#include <Oxygen/Renderer/RenderContext.h>
#include <Oxygen/Renderer/Renderer.h>
#include <Oxygen/Renderer/ShadowManager.h>

namespace oxygen::engine {

using namespace oxygen::renderer::internal::shadow_detail;

namespace {

  struct alignas(packing::kShaderDataFieldAlignment)
    VirtualShadowCoarseMarkPassConstants {
    ShaderVisibleIndex depth_texture_index { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex virtual_directional_shadow_metadata_index {
      kInvalidShaderVisibleIndex
    };
    ShaderVisibleIndex request_words_uav_index { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex page_mark_flags_uav_index { kInvalidShaderVisibleIndex };
    std::uint32_t request_word_count { 0U };
    std::uint32_t coarse_backbone_begin { 0U };
    std::uint32_t coarse_clip_mask { 0U };
    std::uint32_t _pad0 { 0U };
    glm::uvec2 pixel_stride { 1U, 1U };
    std::uint32_t _pad_stride0 { 0U };
    std::uint32_t _pad_stride1 { 0U };

    glm::uvec2 screen_dimensions { 0U, 0U };
    std::uint32_t _pad1 { 0U };
    std::uint32_t _pad2 { 0U };

    glm::mat4 inv_view_projection_matrix { 1.0F };
  };
  static_assert(sizeof(VirtualShadowCoarseMarkPassConstants)
      % packing::kShaderDataFieldAlignment
    == 0U);
  static_assert(
    offsetof(VirtualShadowCoarseMarkPassConstants, depth_texture_index) == 0U);
  static_assert(
    offsetof(VirtualShadowCoarseMarkPassConstants, request_word_count) == 16U);
  static_assert(
    offsetof(VirtualShadowCoarseMarkPassConstants, pixel_stride) == 32U);
  static_assert(
    offsetof(VirtualShadowCoarseMarkPassConstants, screen_dimensions) == 48U);
  static_assert(
    offsetof(VirtualShadowCoarseMarkPassConstants, inv_view_projection_matrix)
    == 64U);

} // namespace

VirtualShadowCoarseMarkPass::VirtualShadowCoarseMarkPass(
  const observer_ptr<Graphics> gfx, std::shared_ptr<Config> config)
  : ComputeRenderPass(
      config ? config->debug_name : "VirtualShadowCoarseMarkPass")
  , gfx_(gfx)
  , config_(std::move(config))
{
}

VirtualShadowCoarseMarkPass::~VirtualShadowCoarseMarkPass()
{
  if (pass_constants_buffer_ && pass_constants_mapped_ptr_ != nullptr) {
    pass_constants_buffer_->UnMap();
    pass_constants_mapped_ptr_ = nullptr;
  }
}

auto VirtualShadowCoarseMarkPass::DoPrepareResources(
  graphics::CommandRecorder& recorder) -> co::Co<>
{
  active_dispatch_ = false;
  active_request_word_count_ = 0U;
  active_pages_per_axis_ = 0U;
  active_clip_level_count_ = 0U;
  active_coarse_backbone_begin_ = 0U;
  active_pixel_stride_ = std::max(1U, config_->pixel_stride);
  active_clip_grid_origin_x_.fill(0);
  active_clip_grid_origin_y_.fill(0);
  active_view_id_ = {};

  if (Context().frame_slot == frame::kInvalidSlot) {
    co_return;
  }

  const auto* prepared_frame = Context().current_view.prepared_frame.get();
  if (prepared_frame == nullptr || !prepared_frame->IsValid()
    || prepared_frame->draw_metadata_bytes.empty()
    || prepared_frame->partitions.empty()) {
    co_return;
  }

  const auto* depth_pass = Context().GetPass<DepthPrePass>();
  const auto* request_pass = Context().GetPass<VirtualShadowRequestPass>();
  const auto shadow_manager = Context().GetRenderer().GetShadowManager();
  if (depth_pass == nullptr || request_pass == nullptr
    || shadow_manager == nullptr || !request_pass->HasActiveDispatch()
    || !request_pass->GetRequestWordsBuffer()
    || !request_pass->GetRequestWordsUav().IsValid()
    || !request_pass->GetPageMarkFlagsBuffer()
    || !request_pass->GetPageMarkFlagsUav().IsValid()
    || Context().current_view.resolved_view == nullptr) {
    co_return;
  }

  const auto* metadata = shadow_manager->TryGetVirtualDirectionalMetadata(
    Context().current_view.view_id);
  const auto* publication
    = shadow_manager->TryGetFramePublication(Context().current_view.view_id);
  if (metadata == nullptr || metadata->clip_level_count == 0U
    || metadata->pages_per_axis == 0U) {
    co_return;
  }
  if (publication == nullptr
    || !publication->virtual_directional_shadow_metadata_srv.IsValid()) {
    LOG_F(ERROR,
      "VirtualShadowCoarseMarkPass: missing current virtual directional "
      "metadata publication for view {}",
      Context().current_view.view_id.get());
    co_return;
  }

  const auto& depth_texture = depth_pass->GetDepthTexture();
  EnsurePassConstantsBuffer();

  const auto depth_texture_srv = EnsureDepthTextureSrv(depth_texture);
  if (depth_texture_srv == kInvalidShaderVisibleIndex) {
    LOG_F(ERROR,
      "VirtualShadowCoarseMarkPass: failed to prepare depth SRV for view {}",
      Context().current_view.view_id.get());
    co_return;
  }

  const auto coarse_backbone_begin
    = ResolveDirectionalCoarseBackboneBegin(metadata->clip_level_count);
  const auto coarse_clip_mask
    = BuildDirectionalCoarseClipMask(metadata->clip_level_count);
  const VirtualShadowCoarseMarkPassConstants pass_constants {
    .depth_texture_index = depth_texture_srv,
    .virtual_directional_shadow_metadata_index
    = publication->virtual_directional_shadow_metadata_srv,
    .request_words_uav_index = request_pass->GetRequestWordsUav(),
    .page_mark_flags_uav_index = request_pass->GetPageMarkFlagsUav(),
    .request_word_count = request_pass->GetActiveRequestWordCount(),
    .coarse_backbone_begin = coarse_backbone_begin,
    .coarse_clip_mask = coarse_clip_mask,
    .pixel_stride = glm::uvec2(active_pixel_stride_, active_pixel_stride_),
    .screen_dimensions = glm::uvec2(depth_texture.GetDescriptor().width,
      depth_texture.GetDescriptor().height),
    .inv_view_projection_matrix
    = Context().current_view.resolved_view->InverseViewProjection(),
  };
  std::memcpy(
    pass_constants_mapped_ptr_, &pass_constants, sizeof(pass_constants));
  SetPassConstantsIndex(pass_constants_index_);

  if (!recorder.IsResourceTracked(*request_pass->GetRequestWordsBuffer())) {
    recorder.BeginTrackingResourceState(*request_pass->GetRequestWordsBuffer(),
      graphics::ResourceStates::kCommon, true);
  }
  if (!recorder.IsResourceTracked(*request_pass->GetPageMarkFlagsBuffer())) {
    recorder.BeginTrackingResourceState(*request_pass->GetPageMarkFlagsBuffer(),
      graphics::ResourceStates::kCommon, true);
  }

  recorder.RequireResourceState(
    depth_texture, graphics::ResourceStates::kShaderResource);
  recorder.RequireResourceState(*request_pass->GetRequestWordsBuffer(),
    graphics::ResourceStates::kUnorderedAccess);
  recorder.RequireResourceState(*request_pass->GetPageMarkFlagsBuffer(),
    graphics::ResourceStates::kUnorderedAccess);
  recorder.FlushBarriers();

  active_dispatch_ = true;
  active_view_id_ = Context().current_view.view_id;
  active_request_word_count_ = request_pass->GetActiveRequestWordCount();
  active_pages_per_axis_ = metadata->pages_per_axis;
  active_clip_level_count_ = metadata->clip_level_count;
  active_coarse_backbone_begin_ = coarse_backbone_begin;
  const auto active_clip_count
    = std::min(metadata->clip_level_count, kMaxSupportedClipLevels);
  for (std::uint32_t clip_index = 0U; clip_index < active_clip_count;
    ++clip_index) {
    active_clip_grid_origin_x_[clip_index]
      = ResolveDirectionalVirtualClipGridOriginX(*metadata, clip_index);
    active_clip_grid_origin_y_[clip_index]
      = ResolveDirectionalVirtualClipGridOriginY(*metadata, clip_index);
  }

  co_return;
}

auto VirtualShadowCoarseMarkPass::DoExecute(graphics::CommandRecorder& recorder)
  -> co::Co<>
{
  if (!active_dispatch_) {
    co_return;
  }

  const auto* depth_pass = Context().GetPass<DepthPrePass>();
  const auto* request_pass = Context().GetPass<VirtualShadowRequestPass>();
  if (depth_pass == nullptr || request_pass == nullptr
    || !request_pass->GetRequestWordsBuffer()) {
    co_return;
  }

  const auto& depth_texture = depth_pass->GetDepthTexture();
  const auto width = std::max(1U, depth_texture.GetDescriptor().width);
  const auto height = std::max(1U, depth_texture.GetDescriptor().height);
  const auto pixel_stride = std::max(1U, active_pixel_stride_);
  const auto group_count_x
    = (width + pixel_stride * kDispatchGroupSize - 1U)
    / (pixel_stride * kDispatchGroupSize);
  const auto group_count_y
    = (height + pixel_stride * kDispatchGroupSize - 1U)
    / (pixel_stride * kDispatchGroupSize);
  recorder.Dispatch(group_count_x, group_count_y, 1U);

  co_return;
}

auto VirtualShadowCoarseMarkPass::ValidateConfig() -> void
{
  if (!gfx_) {
    throw std::runtime_error("VirtualShadowCoarseMarkPass: graphics is null");
  }
  if (!config_) {
    throw std::runtime_error("VirtualShadowCoarseMarkPass: config is null");
  }
}

auto VirtualShadowCoarseMarkPass::CreatePipelineStateDesc()
  -> graphics::ComputePipelineDesc
{
  auto generated_bindings = BuildRootBindings();

  graphics::ShaderRequest shader_request {
    .stage = oxygen::ShaderType::kCompute,
    .source_path = "Lighting/VirtualShadowCoarseMark.hlsl",
    .entry_point = "CS",
  };

  return graphics::ComputePipelineDesc::Builder()
    .SetComputeShader(std::move(shader_request))
    .SetRootBindings(std::span<const graphics::RootBindingItem>(
      generated_bindings.data(), generated_bindings.size()))
    .SetDebugName("VirtualShadowCoarseMark_PSO")
    .Build();
}

auto VirtualShadowCoarseMarkPass::NeedRebuildPipelineState() const -> bool
{
  return !LastBuiltPsoDesc().has_value();
}

auto VirtualShadowCoarseMarkPass::EnsurePassConstantsBuffer() -> void
{
  if (pass_constants_buffer_ && pass_constants_index_.IsValid()
    && pass_constants_mapped_ptr_ != nullptr) {
    return;
  }

  auto& allocator = gfx_->GetDescriptorAllocator();
  auto& registry = gfx_->GetResourceRegistry();

  const graphics::BufferDesc desc {
    .size_bytes = 256U,
    .usage = graphics::BufferUsage::kConstant,
    .memory = graphics::BufferMemory::kUpload,
    .debug_name = "VirtualShadowCoarseMarkPass.Constants",
  };
  pass_constants_buffer_ = gfx_->CreateBuffer(desc);
  if (!pass_constants_buffer_) {
    throw std::runtime_error(
      "VirtualShadowCoarseMarkPass: failed to create pass constants buffer");
  }
  registry.Register(pass_constants_buffer_);

  pass_constants_mapped_ptr_ = pass_constants_buffer_->Map(0U, desc.size_bytes);
  if (pass_constants_mapped_ptr_ == nullptr) {
    throw std::runtime_error(
      "VirtualShadowCoarseMarkPass: failed to map pass constants buffer");
  }

  graphics::BufferViewDescription cbv_desc;
  cbv_desc.view_type = graphics::ResourceViewType::kConstantBuffer;
  cbv_desc.visibility = graphics::DescriptorVisibility::kShaderVisible;
  cbv_desc.range = { 0U, desc.size_bytes };

  auto cbv_handle
    = allocator.Allocate(graphics::ResourceViewType::kConstantBuffer,
      graphics::DescriptorVisibility::kShaderVisible);
  if (!cbv_handle.IsValid()) {
    throw std::runtime_error(
      "VirtualShadowCoarseMarkPass: failed to allocate constants CBV");
  }
  pass_constants_index_ = allocator.GetShaderVisibleIndex(cbv_handle);
  pass_constants_cbv_ = registry.RegisterView(
    *pass_constants_buffer_, std::move(cbv_handle), cbv_desc);
}

auto VirtualShadowCoarseMarkPass::EnsureDepthTextureSrv(
  const graphics::Texture& depth_tex) -> ShaderVisibleIndex
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

  if (depth_texture_owner_ == &depth_tex && depth_texture_srv_.IsValid()) {
    return depth_texture_srv_;
  }

  const auto register_new_srv = [&] {
    auto srv_handle
      = allocator.Allocate(graphics::ResourceViewType::kTexture_SRV,
        graphics::DescriptorVisibility::kShaderVisible);
    if (!srv_handle.IsValid()) {
      return kInvalidShaderVisibleIndex;
    }
    depth_texture_srv_ = allocator.GetShaderVisibleIndex(srv_handle);
    registry.RegisterView(depth_tex, std::move(srv_handle), srv_desc);
    return depth_texture_srv_;
  };

  if (depth_texture_srv_.IsValid() && owns_depth_texture_srv_) {
    return register_new_srv();
  }
  depth_texture_owner_ = &depth_tex;
  owns_depth_texture_srv_ = true;
  return register_new_srv();
}

} // namespace oxygen::engine
