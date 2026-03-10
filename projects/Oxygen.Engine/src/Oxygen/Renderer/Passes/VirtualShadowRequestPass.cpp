//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <cstring>
#include <stdexcept>

#include <glm/integer.hpp>

#include <Oxygen/Base/Logging.h>
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
#include <Oxygen/Renderer/Passes/VirtualShadowRequestPass.h>
#include <Oxygen/Renderer/RenderContext.h>
#include <Oxygen/Renderer/Renderer.h>
#include <Oxygen/Renderer/ShadowManager.h>
#include <Oxygen/Renderer/Types/VirtualShadowRequestFeedback.h>

namespace oxygen::engine {

namespace {

  struct alignas(packing::kShaderDataFieldAlignment)
    VirtualShadowRequestPassConstants {
    ShaderVisibleIndex depth_texture_index { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex request_words_uav_index { kInvalidShaderVisibleIndex };
    std::uint32_t request_word_count { 0U };
    std::uint32_t _pad0 { 0U };

    glm::uvec2 screen_dimensions { 0U, 0U };
    std::uint32_t _pad1 { 0U };
    std::uint32_t _pad2 { 0U };

    glm::mat4 inv_view_projection_matrix { 1.0F };
  };
  static_assert(sizeof(VirtualShadowRequestPassConstants)
      % packing::kShaderDataFieldAlignment
    == 0U);

} // namespace

VirtualShadowRequestPass::VirtualShadowRequestPass(
  const observer_ptr<Graphics> gfx, std::shared_ptr<Config> config)
  : ComputeRenderPass(config->debug_name)
  , gfx_(gfx)
  , config_(std::move(config))
{
}

VirtualShadowRequestPass::~VirtualShadowRequestPass()
{
  if (clear_upload_buffer_ && clear_upload_mapped_ptr_ != nullptr) {
    clear_upload_buffer_->UnMap();
    clear_upload_mapped_ptr_ = nullptr;
  }
  if (pass_constants_buffer_ && pass_constants_mapped_ptr_ != nullptr) {
    pass_constants_buffer_->UnMap();
    pass_constants_mapped_ptr_ = nullptr;
  }
  for (auto& slot : slot_readbacks_) {
    if (slot.buffer && slot.mapped_words != nullptr) {
      slot.buffer->UnMap();
      slot.mapped_words = nullptr;
    }
    slot.buffer.reset();
  }
}

auto VirtualShadowRequestPass::DoPrepareResources(
  graphics::CommandRecorder& recorder) -> co::Co<>
{
  active_dispatch_ = false;
  active_request_word_count_ = 0U;
  active_pages_per_axis_ = 0U;
  active_clip_level_count_ = 0U;
  active_view_id_ = {};

  if (Context().frame_slot == frame::kInvalidSlot) {
    co_return;
  }

  ProcessCompletedFeedback(Context().frame_slot);

  const auto* depth_pass = Context().GetPass<DepthPrePass>();
  const auto shadow_manager = Context().GetRenderer().GetShadowManager();
  if (depth_pass == nullptr || shadow_manager == nullptr
    || Context().current_view.resolved_view == nullptr) {
    co_return;
  }

  const auto* virtual_view = shadow_manager->TryGetVirtualViewIntrospection(
    Context().current_view.view_id);
  if (virtual_view == nullptr
    || virtual_view->directional_virtual_metadata.empty()) {
    co_return;
  }

  const auto& metadata = virtual_view->directional_virtual_metadata.front();
  if (metadata.clip_level_count == 0U || metadata.pages_per_axis == 0U) {
    co_return;
  }

  const auto total_pages = metadata.clip_level_count * metadata.pages_per_axis
    * metadata.pages_per_axis;
  const auto request_word_count = (std::max(1U, total_pages) + 31U) / 32U;
  if (request_word_count > kMaxRequestWordCount) {
    LOG_F(WARNING,
      "VirtualShadowRequestPass: skipping view {} because request buffer "
      "capacity {} words is smaller than required {} words",
      Context().current_view.view_id.get(), kMaxRequestWordCount,
      request_word_count);
    co_return;
  }

  auto& depth_texture
    = const_cast<graphics::Texture&>(depth_pass->GetDepthTexture());
  EnsureRequestBuffers();
  EnsurePassConstantsBuffer();
  EnsureReadbackBuffer(Context().frame_slot);

  const auto depth_texture_srv = EnsureDepthTextureSrv(depth_texture);
  if (depth_texture_srv == kInvalidShaderVisibleIndex) {
    LOG_F(ERROR,
      "VirtualShadowRequestPass: failed to prepare depth SRV for view {}",
      Context().current_view.view_id.get());
    co_return;
  }

  const VirtualShadowRequestPassConstants pass_constants {
    .depth_texture_index = depth_texture_srv,
    .request_words_uav_index = request_words_uav_,
    .request_word_count = request_word_count,
    .screen_dimensions = glm::uvec2(depth_texture.GetDescriptor().width,
      depth_texture.GetDescriptor().height),
    .inv_view_projection_matrix
    = Context().current_view.resolved_view->InverseViewProjection(),
  };
  std::memcpy(
    pass_constants_mapped_ptr_, &pass_constants, sizeof(pass_constants));
  SetPassConstantsIndex(pass_constants_index_);

  if (!recorder.IsResourceTracked(*clear_upload_buffer_)) {
    recorder.BeginTrackingResourceState(
      *clear_upload_buffer_, graphics::ResourceStates::kCopySource, false);
  }
  if (!recorder.IsResourceTracked(*request_words_buffer_)) {
    recorder.BeginTrackingResourceState(
      *request_words_buffer_, graphics::ResourceStates::kCommon, true);
  }

  auto& readback = slot_readbacks_[Context().frame_slot.get()];
  if (!recorder.IsResourceTracked(*readback.buffer)) {
    recorder.BeginTrackingResourceState(
      *readback.buffer, graphics::ResourceStates::kCopyDest, false);
  }

  recorder.RequireResourceState(
    depth_texture, graphics::ResourceStates::kShaderResource);
  recorder.RequireResourceState(
    *request_words_buffer_, graphics::ResourceStates::kCopyDest);
  recorder.FlushBarriers();
  recorder.CopyBuffer(*request_words_buffer_, 0U, *clear_upload_buffer_, 0U,
    static_cast<std::size_t>(kMaxRequestWordCount * sizeof(std::uint32_t)));

  recorder.RequireResourceState(
    *request_words_buffer_, graphics::ResourceStates::kUnorderedAccess);
  recorder.FlushBarriers();

  active_dispatch_ = true;
  active_view_id_ = Context().current_view.view_id;
  active_request_word_count_ = request_word_count;
  active_pages_per_axis_ = metadata.pages_per_axis;
  active_clip_level_count_ = metadata.clip_level_count;

  co_return;
}

auto VirtualShadowRequestPass::DoExecute(graphics::CommandRecorder& recorder)
  -> co::Co<>
{
  if (!active_dispatch_) {
    co_return;
  }

  const auto* depth_pass = Context().GetPass<DepthPrePass>();
  if (depth_pass == nullptr) {
    co_return;
  }

  const auto& depth_texture = depth_pass->GetDepthTexture();
  const auto width = std::max(1U, depth_texture.GetDescriptor().width);
  const auto height = std::max(1U, depth_texture.GetDescriptor().height);
  const auto group_count_x
    = (width + kDispatchGroupSize - 1U) / kDispatchGroupSize;
  const auto group_count_y
    = (height + kDispatchGroupSize - 1U) / kDispatchGroupSize;
  recorder.Dispatch(group_count_x, group_count_y, 1U);

  auto& readback = slot_readbacks_[Context().frame_slot.get()];
  recorder.RequireResourceState(
    *request_words_buffer_, graphics::ResourceStates::kCopySource);
  recorder.RequireResourceState(
    *readback.buffer, graphics::ResourceStates::kCopyDest);
  recorder.FlushBarriers();
  recorder.CopyBuffer(*readback.buffer, 0U, *request_words_buffer_, 0U,
    static_cast<std::size_t>(kMaxRequestWordCount * sizeof(std::uint32_t)));

  readback.pending_feedback = true;
  readback.view_id = active_view_id_;
  readback.source_frame_sequence = Context().frame_sequence;
  readback.pages_per_axis = active_pages_per_axis_;
  readback.clip_level_count = active_clip_level_count_;
  readback.request_word_count = active_request_word_count_;

  co_return;
}

auto VirtualShadowRequestPass::ValidateConfig() -> void
{
  if (!gfx_) {
    throw std::runtime_error("VirtualShadowRequestPass: graphics is null");
  }
  if (!config_) {
    throw std::runtime_error("VirtualShadowRequestPass: config is null");
  }
}

auto VirtualShadowRequestPass::CreatePipelineStateDesc()
  -> graphics::ComputePipelineDesc
{
  auto generated_bindings = BuildRootBindings();

  graphics::ShaderRequest shader_request {
    .stage = oxygen::ShaderType::kCompute,
    .source_path = "Lighting/VirtualShadowRequest.hlsl",
    .entry_point = "CS",
  };

  return graphics::ComputePipelineDesc::Builder()
    .SetComputeShader(std::move(shader_request))
    .SetRootBindings(std::span<const graphics::RootBindingItem>(
      generated_bindings.data(), generated_bindings.size()))
    .SetDebugName("VirtualShadowRequest_PSO")
    .Build();
}

auto VirtualShadowRequestPass::NeedRebuildPipelineState() const -> bool
{
  return !LastBuiltPsoDesc().has_value();
}

auto VirtualShadowRequestPass::EnsureRequestBuffers() -> void
{
  if (request_words_buffer_ && clear_upload_buffer_
    && clear_upload_mapped_ptr_ != nullptr && request_words_uav_.IsValid()) {
    return;
  }

  auto& allocator = gfx_->GetDescriptorAllocator();
  auto& registry = gfx_->GetResourceRegistry();

  constexpr std::uint64_t kBufferSize
    = kMaxRequestWordCount * sizeof(std::uint32_t);

  if (!request_words_buffer_) {
    const graphics::BufferDesc desc {
      .size_bytes = kBufferSize,
      .usage = graphics::BufferUsage::kStorage,
      .memory = graphics::BufferMemory::kDeviceLocal,
      .debug_name = "VirtualShadowRequestPass.RequestWords",
    };
    request_words_buffer_ = gfx_->CreateBuffer(desc);
    if (!request_words_buffer_) {
      throw std::runtime_error(
        "VirtualShadowRequestPass: failed to create request buffer");
    }
    registry.Register(request_words_buffer_);

    auto uav_handle
      = allocator.Allocate(graphics::ResourceViewType::kStructuredBuffer_UAV,
        graphics::DescriptorVisibility::kShaderVisible);
    if (!uav_handle.IsValid()) {
      throw std::runtime_error(
        "VirtualShadowRequestPass: failed to allocate request UAV");
    }
    request_words_uav_ = allocator.GetShaderVisibleIndex(uav_handle);

    graphics::BufferViewDescription uav_desc;
    uav_desc.view_type = graphics::ResourceViewType::kStructuredBuffer_UAV;
    uav_desc.visibility = graphics::DescriptorVisibility::kShaderVisible;
    uav_desc.range = { 0U, kBufferSize };
    uav_desc.stride = sizeof(std::uint32_t);
    registry.RegisterView(
      *request_words_buffer_, std::move(uav_handle), uav_desc);
  }

  if (!clear_upload_buffer_) {
    const graphics::BufferDesc desc {
      .size_bytes = kBufferSize,
      .usage = graphics::BufferUsage::kNone,
      .memory = graphics::BufferMemory::kUpload,
      .debug_name = "VirtualShadowRequestPass.ClearUpload",
    };
    clear_upload_buffer_ = gfx_->CreateBuffer(desc);
    if (!clear_upload_buffer_) {
      throw std::runtime_error(
        "VirtualShadowRequestPass: failed to create clear upload buffer");
    }
    clear_upload_mapped_ptr_ = clear_upload_buffer_->Map(0U, desc.size_bytes);
    if (clear_upload_mapped_ptr_ == nullptr) {
      throw std::runtime_error(
        "VirtualShadowRequestPass: failed to map clear upload buffer");
    }
    std::memset(
      clear_upload_mapped_ptr_, 0, static_cast<std::size_t>(desc.size_bytes));
  }
}

auto VirtualShadowRequestPass::EnsurePassConstantsBuffer() -> void
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
    .debug_name = "VirtualShadowRequestPass.Constants",
  };
  pass_constants_buffer_ = gfx_->CreateBuffer(desc);
  if (!pass_constants_buffer_) {
    throw std::runtime_error(
      "VirtualShadowRequestPass: failed to create pass constants buffer");
  }
  registry.Register(pass_constants_buffer_);

  pass_constants_mapped_ptr_ = pass_constants_buffer_->Map(0U, desc.size_bytes);
  if (pass_constants_mapped_ptr_ == nullptr) {
    throw std::runtime_error(
      "VirtualShadowRequestPass: failed to map pass constants buffer");
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
      "VirtualShadowRequestPass: failed to allocate constants CBV");
  }
  pass_constants_index_ = allocator.GetShaderVisibleIndex(cbv_handle);
  pass_constants_cbv_ = registry.RegisterView(
    *pass_constants_buffer_, std::move(cbv_handle), cbv_desc);
}

auto VirtualShadowRequestPass::EnsureReadbackBuffer(const frame::Slot slot)
  -> void
{
  auto& readback = slot_readbacks_[slot.get()];
  if (readback.buffer && readback.mapped_words != nullptr) {
    return;
  }

  const graphics::BufferDesc desc {
    .size_bytes = kMaxRequestWordCount * sizeof(std::uint32_t),
    .usage = graphics::BufferUsage::kNone,
    .memory = graphics::BufferMemory::kReadBack,
    .debug_name = "VirtualShadowRequestPass.Readback",
  };
  readback.buffer = gfx_->CreateBuffer(desc);
  if (!readback.buffer) {
    throw std::runtime_error(
      "VirtualShadowRequestPass: failed to create readback buffer");
  }
  readback.mapped_words
    = static_cast<std::uint32_t*>(readback.buffer->Map(0U, desc.size_bytes));
  if (readback.mapped_words == nullptr) {
    throw std::runtime_error(
      "VirtualShadowRequestPass: failed to map readback buffer");
  }
  std::memset(
    readback.mapped_words, 0, static_cast<std::size_t>(desc.size_bytes));
}

auto VirtualShadowRequestPass::EnsureDepthTextureSrv(
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

  if (depth_texture_srv_.IsValid() && depth_texture_owner_ == &depth_tex
    && owns_depth_texture_srv_ && registry.Contains(depth_tex, srv_desc)) {
    return depth_texture_srv_;
  }

  auto register_new_srv = [&]() -> ShaderVisibleIndex {
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

auto VirtualShadowRequestPass::ProcessCompletedFeedback(const frame::Slot slot)
  -> void
{
  auto& readback = slot_readbacks_[slot.get()];
  if (!readback.pending_feedback || readback.mapped_words == nullptr) {
    return;
  }

  const auto shadow_manager = Context().GetRenderer().GetShadowManager();
  if (shadow_manager == nullptr) {
    readback.pending_feedback = false;
    return;
  }

  const auto total_pages = readback.pages_per_axis * readback.pages_per_axis
    * readback.clip_level_count;
  renderer::VirtualShadowRequestFeedback feedback {};
  feedback.source_frame_sequence = readback.source_frame_sequence;
  feedback.pages_per_axis = readback.pages_per_axis;
  feedback.clip_level_count = readback.clip_level_count;

  const auto max_words = std::min<std::uint32_t>(readback.request_word_count,
    static_cast<std::uint32_t>(kMaxRequestWordCount));
  for (std::uint32_t word_index = 0U; word_index < max_words; ++word_index) {
    auto word = readback.mapped_words[word_index];
    while (word != 0U) {
      const auto bit_index = static_cast<std::uint32_t>(std::countr_zero(word));
      const auto page_index = word_index * 32U + bit_index;
      if (page_index < total_pages) {
        feedback.requested_page_indices.push_back(page_index);
      }
      word &= (word - 1U);
    }
  }

  if (!feedback.requested_page_indices.empty()) {
    auto& log_state = feedback_log_states_[readback.view_id.get()];
    if (log_state.last_feedback_count
      != feedback.requested_page_indices.size()) {
      LOG_F(INFO,
        "VirtualShadowRequestPass: view {} submitted {} requested virtual "
        "page(s) from feedback",
        readback.view_id.get(), feedback.requested_page_indices.size());
      log_state.last_feedback_count
        = static_cast<std::uint32_t>(feedback.requested_page_indices.size());
    }
    log_state.had_pending_feedback = true;
    shadow_manager->SubmitVirtualRequestFeedback(
      readback.view_id, std::move(feedback));
  } else {
    auto& log_state = feedback_log_states_[readback.view_id.get()];
    if (log_state.had_pending_feedback || log_state.last_feedback_count != 0U) {
      LOG_F(INFO,
        "VirtualShadowRequestPass: view {} cleared virtual request feedback",
        readback.view_id.get());
      log_state.last_feedback_count = 0U;
      log_state.had_pending_feedback = false;
    }
    shadow_manager->ClearVirtualRequestFeedback(readback.view_id);
  }
  readback.pending_feedback = false;
}

} // namespace oxygen::engine
