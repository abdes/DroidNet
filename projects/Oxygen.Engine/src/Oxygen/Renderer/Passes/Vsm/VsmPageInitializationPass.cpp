//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Renderer/Passes/Vsm/VsmPageInitializationPass.h>

#include <optional>
#include <ranges>
#include <stdexcept>
#include <utility>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/Types/Scissors.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/ClearFlags.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmPhysicalPageAddressing.h>

using oxygen::graphics::CommandRecorder;
using oxygen::graphics::DescriptorHandle;
using oxygen::graphics::ResourceStates;
using oxygen::renderer::vsm::TryConvertToCoord;
using oxygen::renderer::vsm::VsmPageInitializationAction;
using oxygen::renderer::vsm::VsmPhysicalPoolSliceRole;

namespace oxygen::engine {

namespace {

  auto FindSliceIndex(const renderer::vsm::VsmPhysicalPoolSnapshot& pool,
    const VsmPhysicalPoolSliceRole role) noexcept
    -> std::optional<std::uint32_t>
  {
    for (std::uint32_t i = 0; i < pool.slice_roles.size(); ++i) {
      if (pool.slice_roles[i] == role) {
        return i;
      }
    }
    return std::nullopt;
  }

} // namespace

struct VsmPageInitializationPass::Impl {
  observer_ptr<Graphics> gfx;
  std::shared_ptr<Config> config;

  std::optional<VsmPageInitializationPassInput> input {};
  bool resources_prepared { false };
  std::optional<std::uint32_t> dynamic_slice_index {};
  std::optional<std::uint32_t> static_slice_index {};

  DescriptorHandle dynamic_slice_dsv_handle {};
  const graphics::Texture* dynamic_slice_owner { nullptr };
  graphics::NativeView dynamic_slice_dsv {};
  std::shared_ptr<graphics::Texture> copy_scratch_texture {};
  std::uint32_t copy_scratch_page_size { 0U };
  Format copy_scratch_format { Format::kUnknown };

  Impl(observer_ptr<Graphics> gfx_, std::shared_ptr<Config> config_)
    : gfx(gfx_)
    , config(std::move(config_))
  {
  }

  auto EnsureDynamicSliceDsv(const graphics::Texture& shadow_texture,
    const std::uint32_t dynamic_slice) -> void;
  auto EnsureCopyScratchTexture(
    const renderer::vsm::VsmPhysicalPoolSnapshot& pool) -> bool;
};

auto VsmPageInitializationPass::Impl::EnsureDynamicSliceDsv(
  const graphics::Texture& shadow_texture, const std::uint32_t dynamic_slice)
  -> void
{
  auto& allocator = gfx->GetDescriptorAllocator();
  if (!dynamic_slice_dsv_handle.IsValid()) {
    dynamic_slice_dsv_handle
      = allocator.Allocate(graphics::ResourceViewType::kTexture_DSV,
        graphics::DescriptorVisibility::kCpuOnly);
    CHECK_F(dynamic_slice_dsv_handle.IsValid(),
      "Failed to allocate VSM page-initialization DSV");
  }

  const auto& texture_desc = shadow_texture.GetDescriptor();
  const auto dsv_desc = graphics::TextureViewDescription {
    .view_type = graphics::ResourceViewType::kTexture_DSV,
    .visibility = graphics::DescriptorVisibility::kCpuOnly,
    .format = texture_desc.format,
    .dimension = texture_desc.texture_type,
    .sub_resources = graphics::TextureSubResourceSet {
      .base_mip_level = 0U,
      .num_mip_levels = 1U,
      .base_array_slice = dynamic_slice,
      .num_array_slices = 1U,
    },
    .is_read_only_dsv = false,
  };
  dynamic_slice_dsv
    = shadow_texture.GetNativeView(dynamic_slice_dsv_handle, dsv_desc);
  CHECK_F(dynamic_slice_dsv != graphics::NativeView {},
    "Failed to create VSM page-initialization DSV");
  dynamic_slice_owner = &shadow_texture;
}

auto VsmPageInitializationPass::Impl::EnsureCopyScratchTexture(
  const renderer::vsm::VsmPhysicalPoolSnapshot& pool) -> bool
{
  if (copy_scratch_texture != nullptr
    && copy_scratch_page_size == pool.page_size_texels
    && copy_scratch_format == pool.depth_format) {
    return true;
  }

  auto desc = graphics::TextureDesc {};
  desc.width = pool.page_size_texels;
  desc.height = pool.page_size_texels;
  desc.array_size = 1U;
  desc.mip_levels = 1U;
  desc.format = pool.depth_format;
  desc.texture_type = TextureType::kTexture2D;
  desc.is_render_target = true;
  desc.is_typeless = true;
  desc.initial_state = ResourceStates::kCommon;
  desc.debug_name = config != nullptr && !config->debug_name.empty()
    ? config->debug_name + ".CopyScratch"
    : "VsmPageInitializationPass.CopyScratch";

  copy_scratch_texture = gfx->CreateTexture(desc);
  if (copy_scratch_texture == nullptr) {
    LOG_F(ERROR,
      "VSM page-initialization failed to create copy scratch texture "
      "page_size={} format={}",
      pool.page_size_texels, static_cast<int>(pool.depth_format));
    copy_scratch_page_size = 0U;
    copy_scratch_format = Format::kUnknown;
    return false;
  }

  copy_scratch_page_size = pool.page_size_texels;
  copy_scratch_format = pool.depth_format;
  return true;
}

VsmPageInitializationPass::VsmPageInitializationPass(
  observer_ptr<Graphics> gfx, std::shared_ptr<Config> config)
  : RenderPass(config ? config->debug_name : "VsmPageInitializationPass")
  , impl_(std::make_unique<Impl>(gfx, std::move(config)))
{
}

VsmPageInitializationPass::~VsmPageInitializationPass() = default;

auto VsmPageInitializationPass::SetInput(VsmPageInitializationPassInput input)
  -> void
{
  impl_->input = std::move(input);
}

auto VsmPageInitializationPass::ResetInput() noexcept -> void
{
  impl_->input.reset();
}

auto VsmPageInitializationPass::ValidateConfig() -> void
{
  if (impl_->gfx == nullptr) {
    throw std::runtime_error("VsmPageInitializationPass requires Graphics");
  }
  if (impl_->config == nullptr) {
    throw std::runtime_error("VsmPageInitializationPass requires Config");
  }
}

auto VsmPageInitializationPass::OnPrepareResources(
  CommandRecorder& /*recorder*/) -> void
{
}

auto VsmPageInitializationPass::OnExecute(CommandRecorder& /*recorder*/) -> void
{
}

auto VsmPageInitializationPass::DoPrepareResources(CommandRecorder& recorder)
  -> co::Co<>
{
  impl_->resources_prepared = false;
  impl_->dynamic_slice_index.reset();
  impl_->static_slice_index.reset();

  if (!impl_->input.has_value()) {
    LOG_F(WARNING,
      "VSM page-initialization pass skipped because input is unavailable");
    co_return;
  }

  if (!impl_->input->frame.is_ready) {
    LOG_F(WARNING,
      "VSM page-initialization pass skipped because frame input is not ready");
    co_return;
  }

  if (!impl_->input->physical_pool.is_available
    || impl_->input->physical_pool.shadow_texture == nullptr) {
    LOG_F(WARNING,
      "VSM page-initialization pass skipped because the physical shadow pool "
      "is unavailable");
    co_return;
  }

  impl_->dynamic_slice_index = FindSliceIndex(
    impl_->input->physical_pool, VsmPhysicalPoolSliceRole::kDynamicDepth);
  impl_->static_slice_index = FindSliceIndex(
    impl_->input->physical_pool, VsmPhysicalPoolSliceRole::kStaticDepth);
  CHECK_F(impl_->dynamic_slice_index.has_value(),
    "VSM page-initialization requires a dynamic slice");

  impl_->EnsureDynamicSliceDsv(
    *impl_->input->physical_pool.shadow_texture, *impl_->dynamic_slice_index);
  recorder.BeginTrackingResourceState(
    *std::const_pointer_cast<graphics::Texture>(
      impl_->input->physical_pool.shadow_texture),
    ResourceStates::kCommon, true);

  const auto needs_copy_scratch = std::ranges::any_of(
    impl_->input->frame.plan.initialization_work, [](const auto& work_item) {
      return work_item.action == VsmPageInitializationAction::kCopyStaticSlice;
    });
  if (needs_copy_scratch) {
    CHECK_F(impl_->static_slice_index.has_value(),
      "CopyStaticSlice requested without a static slice in the physical pool");
    if (!impl_->EnsureCopyScratchTexture(impl_->input->physical_pool)) {
      co_return;
    }
    recorder.BeginTrackingResourceState(
      *impl_->copy_scratch_texture, ResourceStates::kCommon, true);
  }

  impl_->resources_prepared = true;
  co_return;
}

auto VsmPageInitializationPass::DoExecute(CommandRecorder& recorder) -> co::Co<>
{
  if (!impl_->resources_prepared || !impl_->input.has_value()) {
    DLOG_F(2, "VSM page-initialization resources were not prepared, skipping");
    co_return;
  }

  if (impl_->input->frame.plan.initialization_work.empty()) {
    DLOG_F(2, "VSM page-initialization pass found no initialization work");
    co_return;
  }

  auto shadow_texture = std::const_pointer_cast<graphics::Texture>(
    impl_->input->physical_pool.shadow_texture);
  CHECK_NOTNULL_F(
    shadow_texture.get(), "VSM page-initialization requires a shadow texture");

  const auto tile_capacity = impl_->input->physical_pool.tile_capacity;
  const auto tiles_per_axis = impl_->input->physical_pool.tiles_per_axis;
  const auto slice_count = impl_->input->physical_pool.slice_count;
  const auto page_size = impl_->input->physical_pool.page_size_texels;
  const auto scratch_slice = graphics::TextureSlice {
    .x = 0U,
    .y = 0U,
    .z = 0U,
    .width = page_size,
    .height = page_size,
    .depth = 1U,
    .mip_level = 0U,
    .array_slice = 0U,
  };
  const auto scratch_subresources = graphics::TextureSubResourceSet {
    .base_mip_level = 0U,
    .num_mip_levels = 1U,
    .base_array_slice = 0U,
    .num_array_slices = 1U,
  };

  auto clear_rects = std::vector<oxygen::Scissors> {};
  bool needs_copy = false;

  for (const auto& work_item : impl_->input->frame.plan.initialization_work) {
    const auto coord = TryConvertToCoord(
      work_item.physical_page, tile_capacity, tiles_per_axis, slice_count);
    CHECK_F(coord.has_value(),
      "Invalid physical page {} in initialization work",
      work_item.physical_page.value);

    const auto rect = oxygen::Scissors {
      .left = static_cast<std::int32_t>(coord->tile_x * page_size),
      .top = static_cast<std::int32_t>(coord->tile_y * page_size),
      .right = static_cast<std::int32_t>((coord->tile_x + 1U) * page_size),
      .bottom = static_cast<std::int32_t>((coord->tile_y + 1U) * page_size),
    };

    if (work_item.action == VsmPageInitializationAction::kClearDepth) {
      clear_rects.push_back(rect);
      continue;
    }

    CHECK_F(work_item.action == VsmPageInitializationAction::kCopyStaticSlice,
      "Unsupported VSM initialization action");
    CHECK_F(impl_->static_slice_index.has_value(),
      "CopyStaticSlice requested without a static slice in the physical pool");
    CHECK_NOTNULL_F(impl_->copy_scratch_texture.get(),
      "VSM page-initialization copy scratch texture is unavailable");
    needs_copy = true;

    recorder.RequireResourceState(*shadow_texture, ResourceStates::kCopySource);
    recorder.RequireResourceState(
      *impl_->copy_scratch_texture, ResourceStates::kCopyDest);
    recorder.FlushBarriers();

    recorder.CopyTexture(*shadow_texture,
      graphics::TextureSlice {
        .x = coord->tile_x * page_size,
        .y = coord->tile_y * page_size,
        .width = page_size,
        .height = page_size,
        .mip_level = 0U,
        .array_slice = *impl_->static_slice_index,
      },
      graphics::TextureSubResourceSet {
        .base_mip_level = 0U,
        .num_mip_levels = 1U,
        .base_array_slice = *impl_->static_slice_index,
        .num_array_slices = 1U,
      },
      *impl_->copy_scratch_texture, scratch_slice, scratch_subresources);

    recorder.RequireResourceState(
      *impl_->copy_scratch_texture, ResourceStates::kCopySource);
    recorder.RequireResourceState(*shadow_texture, ResourceStates::kCopyDest);
    recorder.FlushBarriers();

    recorder.CopyTexture(*impl_->copy_scratch_texture, scratch_slice,
      scratch_subresources, *shadow_texture,
      graphics::TextureSlice {
        .x = coord->tile_x * page_size,
        .y = coord->tile_y * page_size,
        .width = page_size,
        .height = page_size,
        .mip_level = 0U,
        .array_slice = *impl_->dynamic_slice_index,
      },
      graphics::TextureSubResourceSet {
        .base_mip_level = 0U,
        .num_mip_levels = 1U,
        .base_array_slice = *impl_->dynamic_slice_index,
        .num_array_slices = 1U,
      });
  }

  if (!clear_rects.empty()) {
    recorder.RequireResourceState(*shadow_texture, ResourceStates::kDepthWrite);
    recorder.FlushBarriers();
    recorder.ClearDepthStencilView(*shadow_texture, impl_->dynamic_slice_dsv,
      graphics::ClearFlags::kDepth, 1.0F, 0U, clear_rects);
  }

  DLOG_F(2,
    "executed VSM page-initialization pass generation={} work_items={} "
    "copy_static={} clear_depth={}",
    impl_->input->frame.snapshot.frame_generation,
    impl_->input->frame.plan.initialization_work.size(), needs_copy,
    !clear_rects.empty());
  co_return;
}

} // namespace oxygen::engine
