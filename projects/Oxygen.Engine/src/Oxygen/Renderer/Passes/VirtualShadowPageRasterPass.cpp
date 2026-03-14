//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <utility>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/Bindless/Generated.RootSignature.h>
#include <Oxygen/Core/Constants.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Graphics/Common/Types/ClearFlags.h>
#include <Oxygen/Graphics/Common/Types/DescriptorVisibility.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Graphics/Common/Types/ResourceViewType.h>
#include <Oxygen/Renderer/Internal/VirtualShadowRasterCulling.h>
#include <Oxygen/Renderer/Passes/VirtualShadowPageRasterPass.h>
#include <Oxygen/Renderer/RenderContext.h>
#include <Oxygen/Renderer/Renderer.h>
#include <Oxygen/Renderer/ShadowManager.h>
#include <Oxygen/Renderer/Types/DrawMetadata.h>

namespace oxygen::engine {

namespace {

using SteadyClock = std::chrono::steady_clock;

auto ElapsedMicroseconds(const SteadyClock::time_point start) -> std::uint64_t
{
  return static_cast<std::uint64_t>(
    std::chrono::duration_cast<std::chrono::microseconds>(
      SteadyClock::now() - start)
      .count());
}

constexpr float kVirtualShadowRasterDepthBias = 1200.0F;
// Virtual pages are small and heavily PCF-filtered, so leaving raster slope
// bias at zero prints regular self-shadow bands on broad receiver planes.
// Keep the hardware slope term enabled, but clamp it tightly so this does not
// explode into peter-panning on steep geometry.
constexpr float kVirtualShadowRasterSlopeBias = 2.0F;
constexpr float kVirtualShadowRasterDepthBiasClamp = 0.0025F;
constexpr std::uint32_t kPassConstantsStride
  = packing::kConstantBufferAlignment;
constexpr std::uint64_t kIndirectDrawCommandStrideBytes
  = sizeof(std::uint32_t) * 5ULL;

} // namespace

VirtualShadowPageRasterPass::VirtualShadowPageRasterPass(
  std::shared_ptr<Config> config)
  : DepthPrePass(std::move(config))
{
  pass_constants_indices_.fill(kInvalidShaderVisibleIndex);
}

VirtualShadowPageRasterPass::~VirtualShadowPageRasterPass()
{
  if (pass_constants_buffer_ && pass_constants_mapped_ptr_ != nullptr) {
    pass_constants_buffer_->UnMap();
    pass_constants_mapped_ptr_ = nullptr;
  }
}

auto VirtualShadowPageRasterPass::DoPrepareResources(
  graphics::CommandRecorder& recorder) -> co::Co<>
{
  const auto prepare_begin = SteadyClock::now();
  auto& depth_texture = GetDepthTexture();
  if (!recorder.IsResourceTracked(depth_texture)) {
    recorder.BeginTrackingResourceState(
      depth_texture, graphics::ResourceStates::kCommon, true);
  }

  co_await DepthPrePass::DoPrepareResources(recorder);

  const auto shadow_manager = Context().GetRenderer().GetShadowManager();
  if (!shadow_manager) {
    co_return;
  }

  const auto* gpu_inputs = shadow_manager->TryGetVirtualGpuRasterInputs(
    Context().current_view.view_id);
  const auto* metadata = shadow_manager->TryGetVirtualDirectionalMetadata(
    Context().current_view.view_id);
  const auto* introspection = shadow_manager->TryGetVirtualViewIntrospection(
    Context().current_view.view_id);
  const auto& virtual_depth_texture
    = shadow_manager->GetVirtualShadowDepthTexture();
  if (gpu_inputs == nullptr || !HasLiveGpuRasterInputs(*gpu_inputs)
    || metadata == nullptr || metadata->page_size_texels == 0U
    || !virtual_depth_texture) {
    co_return;
  }
  const auto telemetry_page_count = introspection != nullptr
    ? introspection->pending_raster_page_count
    : 0U;

  EnsureClearPipelineState();
  EnsurePassConstantsCapacity();
  UploadRasterPassConstants();

  if (!recorder.IsResourceTracked(*gpu_inputs->schedule_buffer)) {
    recorder.BeginTrackingResourceState(
      *gpu_inputs->schedule_buffer, graphics::ResourceStates::kCommon, true);
  }
  if (!recorder.IsResourceTracked(*gpu_inputs->schedule_count_buffer)) {
    recorder.BeginTrackingResourceState(
      *gpu_inputs->schedule_count_buffer, graphics::ResourceStates::kCommon,
      true);
  }
  if (!recorder.IsResourceTracked(*gpu_inputs->draw_page_ranges_buffer)) {
    recorder.BeginTrackingResourceState(*gpu_inputs->draw_page_ranges_buffer,
      graphics::ResourceStates::kCommon, true);
  }
  if (!recorder.IsResourceTracked(*gpu_inputs->draw_page_indices_buffer)) {
    recorder.BeginTrackingResourceState(*gpu_inputs->draw_page_indices_buffer,
      graphics::ResourceStates::kCommon, true);
  }
  if (!recorder.IsResourceTracked(*gpu_inputs->clear_indirect_args_buffer)) {
    recorder.BeginTrackingResourceState(*gpu_inputs->clear_indirect_args_buffer,
      graphics::ResourceStates::kCommon, true);
  }
  if (!recorder.IsResourceTracked(*gpu_inputs->draw_indirect_args_buffer)) {
    recorder.BeginTrackingResourceState(*gpu_inputs->draw_indirect_args_buffer,
      graphics::ResourceStates::kCommon, true);
  }

  LOG_F(INFO,
    "VirtualShadowPageRasterPass: frame={} view={} prepared live GPU raster "
    "inputs (source_frame={} telemetry_page_mirror={} draw_count={} "
    "schedule_capacity={} cpu_prepare_us={})",
    Context().frame_sequence.get(), Context().current_view.view_id.get(),
    gpu_inputs->source_frame_sequence.get(), telemetry_page_count,
    gpu_inputs->draw_count, gpu_inputs->schedule_capacity,
    ElapsedMicroseconds(prepare_begin));

  co_return;
}

auto VirtualShadowPageRasterPass::UsesFramebufferDepthAttachment() const -> bool
{
  return false;
}

auto VirtualShadowPageRasterPass::BuildRasterizerStateDesc(
  const graphics::CullMode cull_mode) const -> graphics::RasterizerStateDesc
{
  auto desc = DepthPrePass::BuildRasterizerStateDesc(cull_mode);
  desc.depth_bias = kVirtualShadowRasterDepthBias;
  desc.depth_bias_clamp = kVirtualShadowRasterDepthBiasClamp;
  desc.slope_scaled_depth_bias = kVirtualShadowRasterSlopeBias;
  return desc;
}

auto VirtualShadowPageRasterPass::ExtendShaderDefines(
  std::vector<graphics::ShaderDefine>& defines) const -> void
{
  defines.push_back({ "OXYGEN_VIRTUAL_SHADOW_RASTER", "1" });
}

auto VirtualShadowPageRasterPass::DoExecute(graphics::CommandRecorder& recorder)
  -> co::Co<>
{
  const auto shadow_manager = Context().GetRenderer().GetShadowManager();
  if (!shadow_manager) {
    LOG_F(INFO,
      "VirtualShadowPageRasterPass: skipped for view {} (no ShadowManager)",
      Context().current_view.view_id.get());
    Context().RegisterPass(this);
    co_return;
  }

  const auto* gpu_inputs = shadow_manager->TryGetVirtualGpuRasterInputs(
    Context().current_view.view_id);
  const auto* metadata = shadow_manager->TryGetVirtualDirectionalMetadata(
    Context().current_view.view_id);
  const auto* introspection = shadow_manager->TryGetVirtualViewIntrospection(
    Context().current_view.view_id);
  const auto& virtual_depth_texture
    = shadow_manager->GetVirtualShadowDepthTexture();
  if (gpu_inputs == nullptr || !HasLiveGpuRasterInputs(*gpu_inputs)
    || metadata == nullptr || metadata->page_size_texels == 0U
    || !virtual_depth_texture) {
    auto& log_state = view_log_states_[Context().current_view.view_id.get()];
    if (log_state.saw_live_plan_jobs) {
      log_state.saw_live_plan_jobs = false;
      LOG_F(INFO,
        "VirtualShadowPageRasterPass: view {} has no live GPU raster inputs "
        "for frame {} anymore",
        Context().current_view.view_id.get(), Context().frame_sequence.get());
    }
    Context().RegisterPass(this);
    co_return;
  }

  if (Context().frame_slot == frame::kInvalidSlot) {
    throw std::runtime_error(
      "VirtualShadowPageRasterPass: invalid frame slot for raster execution");
  }

  const auto telemetry_resolved_pages = introspection != nullptr
    ? introspection->resolved_raster_pages
    : std::span<const renderer::VirtualShadowResolvedRasterPage> {};
  const auto telemetry_page_count = introspection != nullptr
    ? introspection->pending_raster_page_count
    : static_cast<std::uint32_t>(telemetry_resolved_pages.size());
  const auto atlas_resolution = virtual_depth_texture->GetDescriptor().width;

  auto& log_state = view_log_states_[Context().current_view.view_id.get()];
  if (!log_state.saw_live_plan_jobs) {
    log_state.saw_live_plan_jobs = true;
    LOG_F(INFO,
      "VirtualShadowPageRasterPass: view {} has live GPU raster inputs "
      "(telemetry_page_mirror={} draw_count={} source_frame={})",
      Context().current_view.view_id.get(), telemetry_page_count,
      gpu_inputs->draw_count, gpu_inputs->source_frame_sequence.get());
  }
  LOG_F(INFO,
    "VirtualShadowPageRasterPass: frame={} view={} executing live GPU raster "
    "(telemetry_page_mirror={} draw_count={} page_size={} atlas={}x{} "
    "source_frame={})",
    Context().frame_sequence.get(), Context().current_view.view_id.get(),
    telemetry_page_count, gpu_inputs->draw_count, metadata->page_size_texels,
    atlas_resolution, atlas_resolution,
    gpu_inputs->source_frame_sequence.get());

  const auto psf = Context().current_view.prepared_frame;
  if (!psf || !psf->IsValid() || psf->draw_metadata_bytes.empty()
    || psf->partitions.empty()) {
    LOG_F(ERROR,
      "VirtualShadowPageRasterPass: skipped for view {} "
      "(prepared={} valid={} draw_bytes={} partitions={}) while having {} "
      "live GPU virtual raster page(s)!",
      Context().current_view.view_id.get(), psf != nullptr,
      psf ? psf->IsValid() : false, psf ? psf->draw_metadata_bytes.size() : 0U,
      psf ? psf->partitions.size() : 0U, telemetry_page_count);
    Context().RegisterPass(this);
    co_return;
  }
  if (!log_state.saw_live_prepared_frame) {
    log_state.saw_live_prepared_frame = true;
    LOG_F(INFO,
      "VirtualShadowPageRasterPass: view {} has a live prepared frame "
      "(draw_bytes={} partitions={})",
      Context().current_view.view_id.get(), psf->draw_metadata_bytes.size(),
      psf->partitions.size());
  }

  auto& depth_texture = const_cast<graphics::Texture&>(GetDepthTexture());
  const auto dsv = PrepareFullAtlasDepthStencilView(depth_texture);

  recorder.SetViewport(ViewPort {
    .top_left_x = 0.0F,
    .top_left_y = 0.0F,
    .width = static_cast<float>(atlas_resolution),
    .height = static_cast<float>(atlas_resolution),
    .min_depth = 0.0F,
    .max_depth = 1.0F,
  });
  recorder.SetScissors(Scissors {
    .left = 0,
    .top = 0,
    .right = static_cast<std::int32_t>(atlas_resolution),
    .bottom = static_cast<std::int32_t>(atlas_resolution),
  });
  recorder.SetRenderTargets({}, dsv);

  recorder.RequireResourceState(
    *gpu_inputs->clear_indirect_args_buffer,
    graphics::ResourceStates::kIndirectArgument);
  recorder.RequireResourceState(
    *gpu_inputs->draw_indirect_args_buffer,
    graphics::ResourceStates::kIndirectArgument);
  recorder.RequireResourceState(
    *gpu_inputs->schedule_buffer, graphics::ResourceStates::kShaderResource);
  recorder.RequireResourceState(*gpu_inputs->schedule_count_buffer,
    graphics::ResourceStates::kShaderResource);
  recorder.RequireResourceState(*gpu_inputs->draw_page_ranges_buffer,
    graphics::ResourceStates::kShaderResource);
  recorder.RequireResourceState(*gpu_inputs->draw_page_indices_buffer,
    graphics::ResourceStates::kShaderResource);
  recorder.FlushBarriers();

  if (!clear_pso_.has_value()) {
    throw std::runtime_error(
      "VirtualShadowPageRasterPass: clear pipeline state was not built");
  }
  recorder.SetPipelineState(*clear_pso_);
  RebindCommonRootParameters(recorder);
  recorder.ExecuteIndirect(*gpu_inputs->clear_indirect_args_buffer, 0U);

  const auto* records
    = reinterpret_cast<const DrawMetadata*>(psf->draw_metadata_bytes.data());
  const auto rastered_page_count = telemetry_page_count;
  std::uint64_t emitted_count = 0U;
  std::uint32_t cpu_draw_submission_count = 0U;
  std::uint32_t skipped_invalid = 0U;
  std::uint32_t draw_errors = 0U;

  for (const auto& pr : psf->partitions) {
    if (!pr.pass_mask.IsSet(PassMaskBit::kShadowCaster)) {
      continue;
    }
    if (!pr.pass_mask.IsSet(PassMaskBit::kOpaque)
      && !pr.pass_mask.IsSet(PassMaskBit::kMasked)) {
      continue;
    }

    recorder.SetPipelineState(SelectPipelineStateForPartition(pr.pass_mask));
    RebindCommonRootParameters(recorder);
    EmitIndirectResolvedPageDrawRange(recorder,
      *gpu_inputs->draw_indirect_args_buffer, records, gpu_inputs->draw_count,
      telemetry_resolved_pages, psf->draw_bounding_spheres, pr.begin, pr.end,
      emitted_count,
      cpu_draw_submission_count, skipped_invalid, draw_errors);
  }

  recorder.RequireResourceState(
    depth_texture, graphics::ResourceStates::kShaderResource);
  recorder.FlushBarriers();

  if (emitted_count == 0U) {
    if (!log_state.saw_zero_draw_live_frame) {
      log_state.saw_zero_draw_live_frame = true;
      log_state.saw_nonzero_draw_live_frame = false;
    }
    LOG_F(ERROR,
      "VirtualShadowPageRasterPass: view {} produced no shadow-caster draws "
      "(telemetry_page_mirror={} gpu_schedule=1 rastered_pages={} page_clears={} shadow_draws=0 "
      "cpu_draw_submissions={} skipped_invalid={} errors={})",
      Context().current_view.view_id.get(), telemetry_page_count,
      rastered_page_count, rastered_page_count, cpu_draw_submission_count,
      skipped_invalid, draw_errors);
  } else {
    if (!log_state.saw_nonzero_draw_live_frame) {
      log_state.saw_nonzero_draw_live_frame = true;
      log_state.saw_zero_draw_live_frame = false;
    }
    LOG_F(INFO,
      "VirtualShadowPageRasterPass: frame={} view={} emitted {} "
      "shadow-caster draw(s) for {} resolved virtual page(s) "
      "[gpu_schedule=1] (rastered_pages={} page_clears={} cpu_draw_submissions={} "
      "skipped_invalid={} errors={})",
      Context().frame_sequence.get(), Context().current_view.view_id.get(),
      emitted_count, telemetry_page_count, rastered_page_count,
      rastered_page_count, cpu_draw_submission_count, skipped_invalid,
      draw_errors);
    shadow_manager->MarkVirtualRenderPlanExecuted(
      Context().current_view.view_id);
  }

  Context().RegisterPass(this);
  co_return;
}

auto VirtualShadowPageRasterPass::EmitIndirectResolvedPageDrawRange(
  graphics::CommandRecorder& recorder, const graphics::Buffer& draw_args_buffer,
  const DrawMetadata* records, const std::uint32_t draw_count,
  const std::span<const renderer::VirtualShadowResolvedRasterPage>
    telemetry_resolved_pages,
  const std::span<const glm::vec4> draw_bounding_spheres,
  const std::uint32_t begin, const std::uint32_t end,
  std::uint64_t& emitted_count,
  std::uint32_t& cpu_draw_submission_count, std::uint32_t& skipped_invalid,
  std::uint32_t& draw_errors) const noexcept -> void
{
  const auto clamped_end = std::min(end, draw_count);
  for (std::uint32_t draw_index = begin; draw_index < end; ++draw_index) {
    if (draw_index >= draw_count) {
      ++draw_errors;
      LOG_F(ERROR,
        "VirtualShadowPageRasterPass '{}': partition draw_index={} exceeds "
        "prepared indirect draw count {}; draw dropped.",
        GetName(), draw_index, draw_count);
      continue;
    }

    const auto& md = records[draw_index];
    if ((md.is_indexed && md.index_count == 0U)
      || (!md.is_indexed && md.vertex_count == 0U) || md.instance_count == 0U) {
      ++skipped_invalid;
      continue;
    }

    try {
      std::uint32_t overlapping_page_count
        = static_cast<std::uint32_t>(telemetry_resolved_pages.size());
      if (draw_index < draw_bounding_spheres.size()) {
        overlapping_page_count = 0U;
        const auto& sphere = draw_bounding_spheres[draw_index];
        for (const auto& page : telemetry_resolved_pages) {
          if (renderer::internal::shadow_detail::
                ResolvedVirtualPageOverlapsBoundingSphere(page, sphere)) {
            ++overlapping_page_count;
          }
        }
      }
      emitted_count += static_cast<std::uint64_t>(md.instance_count)
        * static_cast<std::uint64_t>(overlapping_page_count);
    } catch (const std::exception& ex) {
      ++draw_errors;
      LOG_F(ERROR,
        "VirtualShadowPageRasterPass '{}' draw_index={} failed: {}. "
        "Draw dropped.",
        GetName(), draw_index, ex.what());
    } catch (...) {
      ++draw_errors;
      LOG_F(ERROR,
        "VirtualShadowPageRasterPass '{}' draw_index={} failed: unknown "
        "error. Draw dropped.",
        GetName(), draw_index);
    }
  }

  if (begin >= clamped_end) {
    return;
  }

  try {
    recorder.ExecuteIndirect(draw_args_buffer,
      static_cast<std::uint64_t>(begin) * kIndirectDrawCommandStrideBytes,
      clamped_end - begin,
      graphics::CommandRecorder::IndirectCommandLayout::kDrawWithRootConstant);
    ++cpu_draw_submission_count;
  } catch (const std::exception& ex) {
    ++draw_errors;
    LOG_F(ERROR,
      "VirtualShadowPageRasterPass '{}' range=[{}, {}) failed: {}. "
      "Draw range dropped.",
      GetName(), begin, clamped_end, ex.what());
  } catch (...) {
    ++draw_errors;
    LOG_F(ERROR,
      "VirtualShadowPageRasterPass '{}' range=[{}, {}) failed: unknown "
      "error. Draw range dropped.",
      GetName(), begin, clamped_end);
  }
}

auto VirtualShadowPageRasterPass::PrepareFullAtlasDepthStencilView(
  graphics::Texture& depth_texture) const -> graphics::NativeView
{
  auto& graphics = Context().GetGraphics();
  auto& registry = graphics.GetResourceRegistry();
  const graphics::TextureViewDescription dsv_view_desc {
    .view_type = graphics::ResourceViewType::kTexture_DSV,
    .visibility = graphics::DescriptorVisibility::kCpuOnly,
    .format = depth_texture.GetDescriptor().format,
    .dimension = oxygen::TextureType::kTexture2D,
    .sub_resources = {
      .base_mip_level = 0U,
      .num_mip_levels = 1U,
      .base_array_slice = 0U,
      .num_array_slices = 1U,
    },
    .is_read_only_dsv = false,
  };

  if (full_atlas_dsv_->IsValid()) {
    return full_atlas_dsv_;
  }
  if (const auto existing = registry.Find(depth_texture, dsv_view_desc);
    existing->IsValid()) {
    full_atlas_dsv_ = existing;
    return full_atlas_dsv_;
  }

  auto handle = graphics.GetDescriptorAllocator().Allocate(
    graphics::ResourceViewType::kTexture_DSV,
    graphics::DescriptorVisibility::kCpuOnly);
  if (!handle.IsValid()) {
    throw std::runtime_error(
      "VirtualShadowPageRasterPass: failed to allocate full-atlas DSV");
  }

  full_atlas_dsv_
    = registry.RegisterView(depth_texture, std::move(handle), dsv_view_desc);
  if (!full_atlas_dsv_->IsValid()) {
    throw std::runtime_error(
      "VirtualShadowPageRasterPass: failed to register full-atlas DSV");
  }
  return full_atlas_dsv_;
}

auto VirtualShadowPageRasterPass::EnsureClearPipelineState() -> void
{
  if (clear_pso_.has_value()) {
    return;
  }

  using graphics::CompareOp;
  using graphics::CullMode;
  using graphics::DepthStencilStateDesc;
  using graphics::FramebufferLayoutDesc;
  using graphics::PrimitiveType;
  using graphics::ShaderRequest;

  constexpr DepthStencilStateDesc depth_state {
    .depth_test_enable = true,
    .depth_write_enable = true,
    .depth_func = CompareOp::kAlways,
    .stencil_enable = false,
    .stencil_read_mask = 0xFF,
    .stencil_write_mask = 0xFF,
  };
  const FramebufferLayoutDesc framebuffer_layout {
    .color_target_formats = {},
    .depth_stencil_format = GetDepthTexture().GetDescriptor().format,
    .sample_count = GetDepthTexture().GetDescriptor().sample_count,
  };

  auto generated_bindings = BuildRootBindings();
  clear_pso_ = graphics::GraphicsPipelineDesc::Builder()
    .SetVertexShader(ShaderRequest {
      .stage = ShaderType::kVertex,
      .source_path = "Depth/VirtualShadowPageClear.hlsl",
      .entry_point = "VS",
    })
    .SetPixelShader(ShaderRequest {
      .stage = ShaderType::kPixel,
      .source_path = "Depth/VirtualShadowPageClear.hlsl",
      .entry_point = "PS",
    })
    .SetPrimitiveTopology(PrimitiveType::kTriangleList)
    .SetRasterizerState(DepthPrePass::BuildRasterizerStateDesc(CullMode::kNone))
    .SetDepthStencilState(depth_state)
    .SetBlendState({})
    .SetFramebufferLayout(framebuffer_layout)
    .SetRootBindings(std::span<const graphics::RootBindingItem>(
      generated_bindings.data(), generated_bindings.size()))
    .SetDebugName("VirtualShadowPageRasterPass_Clear")
    .Build();
}

auto VirtualShadowPageRasterPass::HasLiveGpuRasterInputs(
  const renderer::VirtualShadowGpuRasterInputs& gpu_inputs) const -> bool
{
  return gpu_inputs.source_frame_sequence == Context().frame_sequence
    && gpu_inputs.schedule_buffer != nullptr
    && gpu_inputs.schedule_count_buffer != nullptr
    && gpu_inputs.draw_page_ranges_buffer != nullptr
    && gpu_inputs.draw_page_indices_buffer != nullptr
    && gpu_inputs.clear_indirect_args_buffer != nullptr
    && gpu_inputs.draw_indirect_args_buffer != nullptr
    && gpu_inputs.schedule_srv.IsValid()
    && gpu_inputs.schedule_count_srv.IsValid()
    && gpu_inputs.draw_page_ranges_srv.IsValid()
    && gpu_inputs.draw_page_indices_srv.IsValid();
}

auto VirtualShadowPageRasterPass::EnsurePassConstantsCapacity() -> void
{
  if (pass_constants_buffer_ && pass_constants_mapped_ptr_ != nullptr
    && pass_constants_indices_[0].IsValid()) {
    return;
  }

  if (pass_constants_buffer_ && pass_constants_mapped_ptr_ != nullptr) {
    pass_constants_buffer_->UnMap();
    pass_constants_mapped_ptr_ = nullptr;
  }
  pass_constants_buffer_.reset();
  pass_constants_cbvs_.fill({});
  pass_constants_indices_.fill(kInvalidShaderVisibleIndex);

  auto& graphics = Context().GetGraphics();
  auto& registry = graphics.GetResourceRegistry();
  auto& allocator = graphics.GetDescriptorAllocator();

  const graphics::BufferDesc desc {
    .size_bytes = kPassConstantsStride * frame::kFramesInFlight.get(),
    .usage = graphics::BufferUsage::kConstant,
    .memory = graphics::BufferMemory::kUpload,
    .debug_name = "VirtualShadowPageRasterPass.Constants",
  };
  pass_constants_buffer_ = graphics.CreateBuffer(desc);
  if (!pass_constants_buffer_) {
    throw std::runtime_error(
      "VirtualShadowPageRasterPass: failed to create pass constants buffer");
  }
  registry.Register(pass_constants_buffer_);
  pass_constants_mapped_ptr_ = pass_constants_buffer_->Map(0U, desc.size_bytes);
  if (pass_constants_mapped_ptr_ == nullptr) {
    pass_constants_buffer_.reset();
    throw std::runtime_error(
      "VirtualShadowPageRasterPass: failed to map pass constants buffer");
  }

  for (std::uint32_t slot = 0U; slot < frame::kFramesInFlight.get(); ++slot) {
    auto cbv_handle = allocator.Allocate(
      graphics::ResourceViewType::kConstantBuffer,
      graphics::DescriptorVisibility::kShaderVisible);
    if (!cbv_handle.IsValid()) {
      throw std::runtime_error(
        "VirtualShadowPageRasterPass: failed to allocate pass constants CBV");
    }

    pass_constants_indices_[slot] = allocator.GetShaderVisibleIndex(cbv_handle);
    graphics::BufferViewDescription cbv_desc;
    cbv_desc.view_type = graphics::ResourceViewType::kConstantBuffer;
    cbv_desc.visibility = graphics::DescriptorVisibility::kShaderVisible;
    cbv_desc.range = { slot * kPassConstantsStride, kPassConstantsStride };
    pass_constants_cbvs_[slot] = registry.RegisterView(
      *pass_constants_buffer_, std::move(cbv_handle), cbv_desc);
  }
}

auto VirtualShadowPageRasterPass::UploadRasterPassConstants() -> void
{
  if (!pass_constants_buffer_ || pass_constants_mapped_ptr_ == nullptr) {
    throw std::runtime_error(
      "VirtualShadowPageRasterPass: pass constants buffer is not initialized");
  }
  if (Context().frame_slot == frame::kInvalidSlot) {
    throw std::runtime_error(
      "VirtualShadowPageRasterPass: invalid frame slot for pass constants");
  }

  const auto shadow_manager = Context().GetRenderer().GetShadowManager();
  if (shadow_manager == nullptr) {
    throw std::runtime_error(
      "VirtualShadowPageRasterPass: ShadowManager is unavailable");
  }
  const auto* metadata = shadow_manager->TryGetVirtualDirectionalMetadata(
    Context().current_view.view_id);
  const auto* gpu_inputs
    = shadow_manager->TryGetVirtualGpuRasterInputs(Context().current_view.view_id);
  const auto& virtual_depth_texture
    = shadow_manager->GetVirtualShadowDepthTexture();
  if (metadata == nullptr || metadata->page_size_texels == 0U
    || gpu_inputs == nullptr || !HasLiveGpuRasterInputs(*gpu_inputs)
    || !virtual_depth_texture) {
    throw std::runtime_error(
      "VirtualShadowPageRasterPass: live GPU raster inputs are unavailable");
  }
  const auto atlas_width = virtual_depth_texture->GetDescriptor().width;
  const auto atlas_tiles_per_axis
    = std::max(1U, atlas_width / metadata->page_size_texels);

  const auto slot = Context().frame_slot.get();
  const RasterPassConstants constants {
    .alpha_cutoff_default = 0.5F,
    .schedule_srv_index = gpu_inputs->schedule_srv.get(),
    .schedule_count_srv_index = gpu_inputs->schedule_count_srv.get(),
    .atlas_tiles_per_axis = atlas_tiles_per_axis,
    .draw_page_ranges_srv_index = gpu_inputs->draw_page_ranges_srv.get(),
    .draw_page_indices_srv_index = gpu_inputs->draw_page_indices_srv.get(),
  };
  auto* dst = static_cast<std::byte*>(pass_constants_mapped_ptr_)
    + static_cast<std::ptrdiff_t>(slot * kPassConstantsStride);
  std::memcpy(dst, &constants, sizeof(constants));
  SetPassConstantsIndex(pass_constants_indices_[slot]);
}

} // namespace oxygen::engine
