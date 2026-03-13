//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Renderer/Passes/VirtualShadowCoarseMarkPass.h>

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <stdexcept>

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
#include <Oxygen/Renderer/Internal/ShadowBackendCommon.h>
#include <Oxygen/Renderer/Passes/DepthPrePass.h>
#include <Oxygen/Renderer/Passes/VirtualShadowRequestPass.h>
#include <Oxygen/Renderer/RenderContext.h>
#include <Oxygen/Renderer/Renderer.h>
#include <Oxygen/Renderer/ShadowManager.h>
#include <Oxygen/Renderer/Types/VirtualShadowRequestFeedback.h>

namespace oxygen::engine {

using namespace oxygen::renderer::internal::shadow_detail;

namespace {

  using SteadyClock = std::chrono::steady_clock;

  auto ElapsedMicroseconds(const SteadyClock::time_point start)
    -> std::uint64_t
  {
    return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::microseconds>(
        SteadyClock::now() - start)
        .count());
  }

  struct alignas(packing::kShaderDataFieldAlignment)
    VirtualShadowCoarseMarkPassConstants {
    ShaderVisibleIndex depth_texture_index { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex request_words_uav_index { kInvalidShaderVisibleIndex };
    std::uint32_t request_word_count { 0U };
    std::uint32_t coarse_backbone_begin { 0U };
    std::uint32_t coarse_clip_mask { 0U };

    glm::uvec2 screen_dimensions { 0U, 0U };
    std::uint32_t _pad1 { 0U };

    glm::mat4 inv_view_projection_matrix { 1.0F };
  };
  static_assert(sizeof(VirtualShadowCoarseMarkPassConstants)
      % packing::kShaderDataFieldAlignment
    == 0U);

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
  for (auto& slot : slot_readbacks_) {
    if (slot.buffer && slot.mapped_words != nullptr) {
      slot.buffer->UnMap();
      slot.mapped_words = nullptr;
    }
    slot.buffer.reset();
  }
}

auto VirtualShadowCoarseMarkPass::DoPrepareResources(
  graphics::CommandRecorder& recorder) -> co::Co<>
{
  const auto prepare_begin = SteadyClock::now();
  active_dispatch_ = false;
  active_request_word_count_ = 0U;
  active_pages_per_axis_ = 0U;
  active_clip_level_count_ = 0U;
  active_coarse_backbone_begin_ = 0U;
  active_directional_address_space_hash_ = 0U;
  active_clip_grid_origin_x_.fill(0);
  active_clip_grid_origin_y_.fill(0);
  active_view_id_ = {};

  if (Context().frame_slot == frame::kInvalidSlot) {
    co_return;
  }

  ProcessCompletedFeedback(Context().frame_slot);

  const auto* prepared_frame = Context().current_view.prepared_frame.get();
  if (prepared_frame == nullptr || !prepared_frame->IsValid()
    || prepared_frame->draw_metadata_bytes.empty()
    || prepared_frame->partitions.empty()) {
    co_return;
  }

  const auto* depth_pass = Context().GetPass<DepthPrePass>();
  const auto* request_pass = Context().GetPass<VirtualShadowRequestPass>();
  const auto shadow_manager = Context().GetRenderer().GetShadowManager();
  if (depth_pass == nullptr || request_pass == nullptr || shadow_manager == nullptr
    || !request_pass->HasActiveDispatch()
    || !request_pass->GetRequestWordsBuffer()
    || !request_pass->GetRequestWordsUav().IsValid()
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
  EnsureReadbackBuffer(Context().frame_slot);

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
    .request_words_uav_index = request_pass->GetRequestWordsUav(),
    .request_word_count = request_pass->GetActiveRequestWordCount(),
    .coarse_backbone_begin = coarse_backbone_begin,
    .coarse_clip_mask = coarse_clip_mask,
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

  auto& readback = slot_readbacks_[Context().frame_slot.get()];
  if (!recorder.IsResourceTracked(*readback.buffer)) {
    recorder.BeginTrackingResourceState(
      *readback.buffer, graphics::ResourceStates::kCopyDest, false);
  }

  recorder.RequireResourceState(
    depth_texture, graphics::ResourceStates::kShaderResource);
  recorder.RequireResourceState(*request_pass->GetRequestWordsBuffer(),
    graphics::ResourceStates::kUnorderedAccess);
  recorder.FlushBarriers();

  active_dispatch_ = true;
  active_view_id_ = Context().current_view.view_id;
  active_request_word_count_ = request_pass->GetActiveRequestWordCount();
  active_pages_per_axis_ = metadata->pages_per_axis;
  active_clip_level_count_ = metadata->clip_level_count;
  active_coarse_backbone_begin_ = coarse_backbone_begin;
  active_directional_address_space_hash_
    = HashDirectionalVirtualFeedbackAddressSpace(*metadata);
  const auto active_clip_count
    = std::min(metadata->clip_level_count, kMaxSupportedClipLevels);
  for (std::uint32_t clip_index = 0U; clip_index < active_clip_count;
    ++clip_index) {
    active_clip_grid_origin_x_[clip_index]
      = ResolveDirectionalVirtualClipGridOriginX(*metadata, clip_index);
    active_clip_grid_origin_y_[clip_index]
      = ResolveDirectionalVirtualClipGridOriginY(*metadata, clip_index);
  }

  LOG_F(INFO,
    "VirtualShadowCoarseMarkPass: frame={} slot={} view={} prepared dispatch "
    "(words={} pages_per_axis={} clips={} coarse_begin={} coarse_mask=0x{:x} "
    "address_hash=0x{:x} depth={}x{} cpu_prepare_us={})",
    Context().frame_sequence.get(), Context().frame_slot.get(),
    Context().current_view.view_id.get(), active_request_word_count_,
    active_pages_per_axis_, active_clip_level_count_,
    active_coarse_backbone_begin_, coarse_clip_mask,
    active_directional_address_space_hash_,
    pass_constants.screen_dimensions.x, pass_constants.screen_dimensions.y,
    ElapsedMicroseconds(prepare_begin));

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
  const auto group_count_x
    = (width + kDispatchGroupSize - 1U) / kDispatchGroupSize;
  const auto group_count_y
    = (height + kDispatchGroupSize - 1U) / kDispatchGroupSize;
  recorder.Dispatch(group_count_x, group_count_y, 1U);

  auto& readback = slot_readbacks_[Context().frame_slot.get()];
  recorder.RequireResourceState(*request_pass->GetRequestWordsBuffer(),
    graphics::ResourceStates::kCopySource);
  recorder.RequireResourceState(
    *readback.buffer, graphics::ResourceStates::kCopyDest);
  recorder.FlushBarriers();
  recorder.CopyBuffer(*readback.buffer, 0U, *request_pass->GetRequestWordsBuffer(),
    0U, static_cast<std::size_t>(kMaxRequestWordCount * sizeof(std::uint32_t)));

  readback.pending_feedback = true;
  readback.view_id = active_view_id_;
  readback.source_frame_sequence = Context().frame_sequence;
  readback.pages_per_axis = active_pages_per_axis_;
  readback.clip_level_count = active_clip_level_count_;
  readback.directional_address_space_hash = active_directional_address_space_hash_;
  readback.clip_grid_origin_x = active_clip_grid_origin_x_;
  readback.clip_grid_origin_y = active_clip_grid_origin_y_;
  readback.request_word_count = active_request_word_count_;

  LOG_F(INFO,
    "VirtualShadowCoarseMarkPass: frame={} slot={} view={} dispatched coarse "
    "mark pass (groups={}x{} words={} coarse_begin={})",
    Context().frame_sequence.get(), Context().frame_slot.get(),
    active_view_id_.get(), group_count_x, group_count_y,
    active_request_word_count_, active_coarse_backbone_begin_);

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

auto VirtualShadowCoarseMarkPass::EnsureReadbackBuffer(const frame::Slot slot)
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
    .debug_name = "VirtualShadowCoarseMarkPass.Readback",
  };
  readback.buffer = gfx_->CreateBuffer(desc);
  if (!readback.buffer) {
    throw std::runtime_error(
      "VirtualShadowCoarseMarkPass: failed to create readback buffer");
  }
  readback.mapped_words
    = static_cast<std::uint32_t*>(readback.buffer->Map(0U, desc.size_bytes));
  if (readback.mapped_words == nullptr) {
    throw std::runtime_error(
      "VirtualShadowCoarseMarkPass: failed to map readback buffer");
  }
  std::memset(
    readback.mapped_words, 0, static_cast<std::size_t>(desc.size_bytes));
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

auto VirtualShadowCoarseMarkPass::ProcessCompletedFeedback(const frame::Slot slot)
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
  feedback.directional_address_space_hash
    = readback.directional_address_space_hash;
  feedback.kind = renderer::VirtualShadowFeedbackKind::kCoarse;
  const auto pages_per_level
    = readback.pages_per_axis * readback.pages_per_axis;

  const auto max_words = std::min<std::uint32_t>(readback.request_word_count,
    static_cast<std::uint32_t>(kMaxRequestWordCount));
  for (std::uint32_t word_index = 0U; word_index < max_words; ++word_index) {
    auto word = readback.mapped_words[word_index];
    while (word != 0U) {
      const auto bit_index = static_cast<std::uint32_t>(std::countr_zero(word));
      const auto page_index = word_index * 32U + bit_index;
      if (page_index < total_pages) {
        const auto clip_index = page_index / pages_per_level;
        const auto local_page_index = page_index % pages_per_level;
        const auto page_y = local_page_index / readback.pages_per_axis;
        const auto page_x = local_page_index % readback.pages_per_axis;
        const auto resident_key = PackVirtualResidentPageKey(clip_index,
          readback.clip_grid_origin_x[clip_index]
            + static_cast<std::int32_t>(page_x),
          readback.clip_grid_origin_y[clip_index]
            + static_cast<std::int32_t>(page_y));
        feedback.requested_resident_keys.push_back(resident_key);
      }
      word &= (word - 1U);
    }
  }

  auto& log_state = feedback_log_states_[readback.view_id.get()];
  if (!feedback.requested_resident_keys.empty()) {
    LOG_F(INFO,
      "VirtualShadowCoarseMarkPass: frame={} slot={} view={} completed "
      "feedback (source_frame={} requested_pages={} address_hash=0x{:x})",
      Context().frame_sequence.get(), slot.get(), readback.view_id.get(),
      feedback.source_frame_sequence.get(),
      feedback.requested_resident_keys.size(),
      feedback.directional_address_space_hash);
    log_state.last_feedback_count
      = static_cast<std::uint32_t>(feedback.requested_resident_keys.size());
    log_state.had_pending_feedback = true;
    shadow_manager->SubmitVirtualRequestFeedback(
      readback.view_id, std::move(feedback));
  } else {
    LOG_F(INFO,
      "VirtualShadowCoarseMarkPass: frame={} slot={} view={} completed "
      "feedback (source_frame={} requested_pages=0 address_hash=0x{:x})",
      Context().frame_sequence.get(), slot.get(), readback.view_id.get(),
      readback.source_frame_sequence.get(),
      readback.directional_address_space_hash);
    log_state.last_feedback_count = 0U;
    log_state.had_pending_feedback = false;
    shadow_manager->ClearVirtualRequestFeedback(
      readback.view_id, renderer::VirtualShadowFeedbackKind::kCoarse);
  }
  readback.pending_feedback = false;
}

} // namespace oxygen::engine
