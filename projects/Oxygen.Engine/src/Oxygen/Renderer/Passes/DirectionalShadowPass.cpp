//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstddef>
#include <cstring>
#include <stdexcept>
#include <utility>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/Bindless/Generated.RootSignature.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Graphics/Common/Types/DescriptorVisibility.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Graphics/Common/Types/ResourceViewType.h>
#include <Oxygen/Renderer/Passes/DirectionalShadowPass.h>
#include <Oxygen/Renderer/RenderContext.h>
#include <Oxygen/Renderer/Renderer.h>
#include <Oxygen/Renderer/ShadowManager.h>
#include <Oxygen/Renderer/Types/DrawMetadata.h>

namespace oxygen::engine {

namespace {

  constexpr float kDirectionalShadowRasterDepthBias = 1500.0F;
  constexpr float kDirectionalShadowRasterSlopeBias = 2.0F;
  constexpr float kDirectionalShadowRasterDepthBiasClamp = 0.0F;

} // namespace

DirectionalShadowPass::DirectionalShadowPass(std::shared_ptr<Config> config)
  : DepthPrePass(std::move(config))
{
}

DirectionalShadowPass::~DirectionalShadowPass()
{
  if (shadow_view_constants_buffer_ && shadow_view_constants_mapped_ptr_) {
    shadow_view_constants_buffer_->UnMap();
    shadow_view_constants_mapped_ptr_ = nullptr;
  }
  shadow_view_constants_buffer_.reset();
  shadow_view_constants_capacity_ = 0U;
}

auto DirectionalShadowPass::DoPrepareResources(
  graphics::CommandRecorder& recorder) -> co::Co<>
{
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

  const auto* shadow_view
    = shadow_manager->TryGetPublishedViewData(Context().current_view.view_id);
  if (shadow_view == nullptr
    || shadow_view->directional_view_constants.empty()) {
    co_return;
  }

  EnsureShadowViewConstantsCapacity(
    static_cast<std::uint32_t>(shadow_view->directional_view_constants.size()));
  UploadShadowViewConstants(shadow_view->directional_view_constants);

  co_return;
}

auto DirectionalShadowPass::UsesFramebufferDepthAttachment() const -> bool
{
  return false;
}

auto DirectionalShadowPass::BuildRasterizerStateDesc(
  const graphics::CullMode cull_mode) const -> graphics::RasterizerStateDesc
{
  auto desc = DepthPrePass::BuildRasterizerStateDesc(cull_mode);
  desc.depth_bias = kDirectionalShadowRasterDepthBias;
  desc.depth_bias_clamp = kDirectionalShadowRasterDepthBiasClamp;
  desc.slope_scaled_depth_bias = kDirectionalShadowRasterSlopeBias;
  return desc;
}

auto DirectionalShadowPass::DoExecute(graphics::CommandRecorder& recorder)
  -> co::Co<>
{
  const auto shadow_manager = Context().GetRenderer().GetShadowManager();
  if (!shadow_manager) {
    LOG_F(INFO, "DirectionalShadowPass: skipped for view {} (no ShadowManager)",
      Context().current_view.view_id.get());
    Context().RegisterPass(this);
    co_return;
  }

  const auto* shadow_view
    = shadow_manager->TryGetPublishedViewData(Context().current_view.view_id);
  if (shadow_view == nullptr || shadow_view->directional_metadata.empty()
    || shadow_view->directional_view_constants.empty()) {
    LOG_F(INFO,
      "DirectionalShadowPass: skipped for view {} "
      "(published={} directional={} snapshots={})",
      Context().current_view.view_id.get(), shadow_view != nullptr,
      shadow_view ? shadow_view->directional_metadata.size() : 0U,
      shadow_view ? shadow_view->directional_view_constants.size() : 0U);
    Context().RegisterPass(this);
    co_return;
  }

  const auto psf = Context().current_view.prepared_frame;
  if (!psf || !psf->IsValid() || psf->draw_metadata_bytes.empty()
    || psf->partitions.empty()) {
    LOG_F(INFO,
      "DirectionalShadowPass: skipped for view {} "
      "(prepared={} valid={} draw_bytes={} partitions={})",
      Context().current_view.view_id.get(), psf != nullptr,
      psf ? psf->IsValid() : false, psf ? psf->draw_metadata_bytes.size() : 0U,
      psf ? psf->partitions.size() : 0U);
    Context().RegisterPass(this);
    co_return;
  }

  auto& depth_texture = const_cast<graphics::Texture&>(GetDepthTexture());
  SetupViewPortAndScissors(recorder);

  const auto* records
    = reinterpret_cast<const DrawMetadata*>(psf->draw_metadata_bytes.data());
  std::uint32_t emitted_count = 0U;
  std::uint32_t skipped_invalid = 0U;
  std::uint32_t draw_errors = 0U;
  std::uint32_t cascade_snapshot_index = 0U;

  for (const auto& directional_metadata : shadow_view->directional_metadata) {
    for (std::uint32_t cascade_index = 0U;
      cascade_index < directional_metadata.cascade_count;
      ++cascade_index, ++cascade_snapshot_index) {
      const auto layer = directional_metadata.resource_index + cascade_index;
      const auto dsv = PrepareCascadeDepthStencilView(depth_texture, layer);

      recorder.SetRenderTargets({}, dsv);
      ClearDepthStencilView(recorder, dsv);

      for (const auto& pr : psf->partitions) {
        if (!pr.pass_mask.IsSet(PassMaskBit::kShadowCaster)) {
          continue;
        }
        if (!pr.pass_mask.IsSet(PassMaskBit::kOpaque)
          && !pr.pass_mask.IsSet(PassMaskBit::kMasked)) {
          continue;
        }

        recorder.SetPipelineState(
          SelectPipelineStateForPartition(pr.pass_mask));
        RebindCommonRootParameters(recorder);
        BindCascadeViewConstants(recorder, cascade_snapshot_index);
        EmitDrawRange(recorder, records, pr.begin, pr.end, emitted_count,
          skipped_invalid, draw_errors);
      }
    }
  }

  recorder.RequireResourceState(
    depth_texture, graphics::ResourceStates::kShaderResource);
  recorder.FlushBarriers();

  std::uint32_t total_cascades = 0U;
  for (const auto& directional_metadata : shadow_view->directional_metadata) {
    total_cascades += directional_metadata.cascade_count;
  }

  if (emitted_count == 0U) {
    LOG_F(WARNING,
      "DirectionalShadowPass: view {} produced no shadow-caster draws "
      "(directional_products={} cascades={} skipped_invalid={} errors={})",
      Context().current_view.view_id.get(),
      shadow_view->directional_metadata.size(), total_cascades, skipped_invalid,
      draw_errors);
  }

  Context().RegisterPass(this);
  co_return;
}

auto DirectionalShadowPass::EnsureShadowViewConstantsCapacity(
  const std::uint32_t required_snapshots) -> void
{
  if (required_snapshots == 0U
    || required_snapshots <= shadow_view_constants_capacity_) {
    return;
  }

  if (shadow_view_constants_buffer_ && shadow_view_constants_mapped_ptr_) {
    shadow_view_constants_buffer_->UnMap();
    shadow_view_constants_mapped_ptr_ = nullptr;
  }
  shadow_view_constants_buffer_.reset();

  shadow_view_constants_capacity_ = required_snapshots;
  const auto total_bytes
    = static_cast<std::uint64_t>(sizeof(ViewConstants::GpuData))
    * static_cast<std::uint64_t>(frame::kFramesInFlight.get())
    * static_cast<std::uint64_t>(shadow_view_constants_capacity_);

  const graphics::BufferDesc desc {
    .size_bytes = total_bytes,
    .usage = graphics::BufferUsage::kConstant,
    .memory = graphics::BufferMemory::kUpload,
    .debug_name = "DirectionalShadowPass.ViewConstants",
  };

  shadow_view_constants_buffer_ = Context().GetGraphics().CreateBuffer(desc);
  if (!shadow_view_constants_buffer_) {
    shadow_view_constants_capacity_ = 0U;
    throw std::runtime_error(
      "DirectionalShadowPass: failed to create shadow view constants buffer");
  }

  shadow_view_constants_buffer_->SetName(desc.debug_name);
  shadow_view_constants_mapped_ptr_
    = shadow_view_constants_buffer_->Map(0U, desc.size_bytes);
  if (shadow_view_constants_mapped_ptr_ == nullptr) {
    shadow_view_constants_buffer_.reset();
    shadow_view_constants_capacity_ = 0U;
    throw std::runtime_error(
      "DirectionalShadowPass: failed to map shadow view constants buffer");
  }
}

auto DirectionalShadowPass::UploadShadowViewConstants(
  const std::span<const ViewConstants::GpuData> snapshots) -> void
{
  if (snapshots.empty()) {
    return;
  }
  if (!shadow_view_constants_buffer_
    || shadow_view_constants_mapped_ptr_ == nullptr) {
    throw std::runtime_error(
      "DirectionalShadowPass: shadow view constants buffer is not initialized");
  }
  if (Context().frame_slot == frame::kInvalidSlot) {
    throw std::runtime_error("DirectionalShadowPass: invalid frame slot for "
                             "shadow view constants upload");
  }

  const auto base_index = static_cast<std::uint64_t>(Context().frame_slot.get())
    * static_cast<std::uint64_t>(shadow_view_constants_capacity_);
  auto* dst = static_cast<std::byte*>(shadow_view_constants_mapped_ptr_)
    + base_index * sizeof(ViewConstants::GpuData);
  std::memcpy(dst, snapshots.data(), snapshots.size_bytes());
}

auto DirectionalShadowPass::BindCascadeViewConstants(
  graphics::CommandRecorder& recorder, const std::uint32_t cascade_index) const
  -> void
{
  if (!shadow_view_constants_buffer_
    || Context().frame_slot == frame::kInvalidSlot) {
    throw std::runtime_error(
      "DirectionalShadowPass: shadow view constants buffer is not available");
  }
  if (cascade_index >= shadow_view_constants_capacity_) {
    throw std::out_of_range("DirectionalShadowPass: cascade index exceeds "
                            "shadow view constants capacity");
  }

  const auto slot_offset
    = static_cast<std::uint64_t>(Context().frame_slot.get())
      * static_cast<std::uint64_t>(shadow_view_constants_capacity_)
    + cascade_index;
  const auto byte_offset = slot_offset * sizeof(ViewConstants::GpuData);
  recorder.SetGraphicsRootConstantBufferView(
    static_cast<std::uint32_t>(binding::RootParam::kViewConstants),
    shadow_view_constants_buffer_->GetGPUVirtualAddress() + byte_offset);
}

auto DirectionalShadowPass::PrepareCascadeDepthStencilView(
  graphics::Texture& depth_texture, const std::uint32_t array_slice) const
  -> graphics::NativeView
{
  auto& graphics = Context().GetGraphics();
  auto& registry = graphics.GetResourceRegistry();
  auto& allocator = graphics.GetDescriptorAllocator();

  const graphics::TextureViewDescription dsv_view_desc {
    .view_type = graphics::ResourceViewType::kTexture_DSV,
    .visibility = graphics::DescriptorVisibility::kCpuOnly,
    .format = depth_texture.GetDescriptor().format,
    .dimension = oxygen::TextureType::kTexture2DArray,
    .sub_resources = {
      .base_mip_level = 0U,
      .num_mip_levels = 1U,
      .base_array_slice = array_slice,
      .num_array_slices = 1U,
    },
    .is_read_only_dsv = false,
  };

  if (const auto dsv = registry.Find(depth_texture, dsv_view_desc);
    dsv->IsValid()) {
    return dsv;
  }

  auto dsv_desc_handle
    = allocator.Allocate(graphics::ResourceViewType::kTexture_DSV,
      graphics::DescriptorVisibility::kCpuOnly);
  if (!dsv_desc_handle.IsValid()) {
    throw std::runtime_error(
      "DirectionalShadowPass: failed to allocate cascade DSV descriptor");
  }

  const auto dsv = registry.RegisterView(
    depth_texture, std::move(dsv_desc_handle), dsv_view_desc);
  if (!dsv->IsValid()) {
    throw std::runtime_error(
      "DirectionalShadowPass: failed to register cascade DSV view");
  }
  return dsv;
}

} // namespace oxygen::engine
