//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <utility>

#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/TextureType.h>
#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/DescriptorVisibility.h>
#include <Oxygen/Graphics/Common/Types/ResourceViewType.h>

namespace oxygen::vortex::internal {

//! Shader-visible SRV of every mip and array slice of `texture`.
[[nodiscard]] inline auto WholeTextureSrvDesc(const graphics::Texture& texture)
  -> graphics::TextureViewDescription
{
  return graphics::TextureViewDescription {
    .view_type = graphics::ResourceViewType::kTexture_SRV,
    .visibility = graphics::DescriptorVisibility::kShaderVisible,
    .format = texture.GetDescriptor().format,
    .dimension = texture.GetDescriptor().texture_type,
    .sub_resources = graphics::TextureSubResourceSet::EntireTexture(),
    .is_read_only_dsv = false,
  };
}

//! Shader-visible SRV of mip 0 of one array slice of `texture`, as a
//! one-slice 2D array. Plain 2D textures take slice 0.
[[nodiscard]] inline auto ArraySliceSrvDesc(const graphics::Texture& texture,
  const std::uint32_t array_slice) -> graphics::TextureViewDescription
{
  return graphics::TextureViewDescription {
    .view_type = graphics::ResourceViewType::kTexture_SRV,
    .visibility = graphics::DescriptorVisibility::kShaderVisible,
    .format = texture.GetDescriptor().format,
    .dimension = oxygen::TextureType::kTexture2DArray,
    .sub_resources = {
      .base_mip_level = 0U,
      .num_mip_levels = 1U,
      .base_array_slice = array_slice,
      .num_array_slices = 1U,
    },
    .is_read_only_dsv = false,
  };
}

//! Shader-visible UAV of one mip of a 2D `texture`.
[[nodiscard]] inline auto MipUavDesc(const graphics::Texture& texture,
  const std::uint32_t mip_level) -> graphics::TextureViewDescription
{
  return graphics::TextureViewDescription {
    .view_type = graphics::ResourceViewType::kTexture_UAV,
    .visibility = graphics::DescriptorVisibility::kShaderVisible,
    .format = texture.GetDescriptor().format,
    .dimension = oxygen::TextureType::kTexture2D,
    .sub_resources = {
      .base_mip_level = mip_level,
      .num_mip_levels = 1U,
      .base_array_slice = 0U,
      .num_array_slices = 1U,
    },
    .is_read_only_dsv = false,
  };
}

//! Returns the bindless index of the view `desc` of a registered `texture`,
//! registering the view on first use. Returns an invalid index on failure.
[[nodiscard]] inline auto EnsureTextureView(Graphics& gfx,
  const graphics::Texture& texture,
  const graphics::TextureViewDescription& desc) -> ShaderVisibleIndex
{
  auto& registry = gfx.GetResourceRegistry();
  if (const auto existing = registry.FindShaderVisibleIndex(texture, desc);
    existing.has_value()) {
    return *existing;
  }

  auto& allocator = gfx.GetDescriptorAllocator();
  auto handle = allocator.AllocateRaw(desc.view_type, desc.visibility);
  if (!handle.IsValid()) {
    return kInvalidShaderVisibleIndex;
  }
  const auto index = allocator.GetShaderVisibleIndex(handle);
  const auto view = registry.RegisterView(texture, std::move(handle), desc);
  return view->IsValid() ? index : kInvalidShaderVisibleIndex;
}

} // namespace oxygen::vortex::internal
