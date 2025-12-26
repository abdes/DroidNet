//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Renderer/Resources/TextureBinder.h>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <limits>
#include <span>
#include <tuple>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/Bindless/Generated.Constants.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/TextureResource.h>
#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/Detail/DeferredReclaimer.h>
#include <Oxygen/Core/Detail/FormatUtils.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Renderer/Upload/Errors.h>
#include <Oxygen/Renderer/Upload/UploadCoordinator.h>

namespace oxygen::renderer::resources {

namespace {

  inline auto AlignUp(const std::uint64_t value, const std::uint64_t alignment)
    -> std::uint64_t
  {
    return (value + (alignment - 1ULL)) & ~(alignment - 1ULL);
  }

  inline auto ComputeMipLevels2D(std::uint32_t width, std::uint32_t height)
    -> std::uint32_t
  {
    std::uint32_t levels = 1;
    while (width > 1U || height > 1U) {
      width = (std::max)(width / 2U, 1U);
      height = (std::max)(height / 2U, 1U);
      ++levels;
    }
    return levels;
  }

  auto DownsampleRgba8Box2x2(std::uint32_t src_width, std::uint32_t src_height,
    std::span<const std::byte> src_rgba8) -> std::vector<std::byte>
  {
    const auto dst_width = (std::max)(src_width / 2U, 1U);
    const auto dst_height = (std::max)(src_height / 2U, 1U);

    std::vector<std::byte> dst;
    dst.resize(static_cast<std::size_t>(dst_width) * dst_height * 4U);

    const auto src = src_rgba8;
    auto dst_span = std::span<std::byte> { dst };

    for (std::uint32_t y = 0; y < dst_height; ++y) {
      for (std::uint32_t x = 0; x < dst_width; ++x) {
        const auto sx0 = (std::min)(x * 2U, src_width - 1U);
        const auto sy0 = (std::min)(y * 2U, src_height - 1U);
        const auto sx1 = (std::min)(sx0 + 1U, src_width - 1U);
        const auto sy1 = (std::min)(sy0 + 1U, src_height - 1U);

        auto Sample = [&](std::uint32_t sx, std::uint32_t sy,
                        std::uint32_t channel) -> std::uint32_t {
          const auto idx =
            (static_cast<std::size_t>(sy) * src_width + sx) * 4U + channel;
          return static_cast<std::uint8_t>(src[idx]);
        };

        const std::uint32_t r = (Sample(sx0, sy0, 0U) + Sample(sx1, sy0, 0U)
                                  + Sample(sx0, sy1, 0U) + Sample(sx1, sy1, 0U))
          / 4U;
        const std::uint32_t g = (Sample(sx0, sy0, 1U) + Sample(sx1, sy0, 1U)
                                  + Sample(sx0, sy1, 1U) + Sample(sx1, sy1, 1U))
          / 4U;
        const std::uint32_t b = (Sample(sx0, sy0, 2U) + Sample(sx1, sy0, 2U)
                                  + Sample(sx0, sy1, 2U) + Sample(sx1, sy1, 2U))
          / 4U;
        const std::uint32_t a = (Sample(sx0, sy0, 3U) + Sample(sx1, sy0, 3U)
                                  + Sample(sx0, sy1, 3U) + Sample(sx1, sy1, 3U))
          / 4U;

        const auto out_idx =
          (static_cast<std::size_t>(y) * dst_width + x) * 4U;
        dst_span[out_idx + 0] = std::byte(static_cast<std::uint8_t>(r));
        dst_span[out_idx + 1] = std::byte(static_cast<std::uint8_t>(g));
        dst_span[out_idx + 2] = std::byte(static_cast<std::uint8_t>(b));
        dst_span[out_idx + 3] = std::byte(static_cast<std::uint8_t>(a));
      }
    }

    return dst;
  }

  struct Rgba8MipChain {
    std::vector<std::vector<std::byte>> mips;
  };

  auto BuildRgba8MipChain(const std::uint32_t width, const std::uint32_t height,
    const std::span<const std::byte> base_rgba8) -> Rgba8MipChain
  {
    Rgba8MipChain chain;
    chain.mips.emplace_back(base_rgba8.begin(), base_rgba8.end());

    std::uint32_t cur_w = width;
    std::uint32_t cur_h = height;
    while (cur_w > 1U || cur_h > 1U) {
      const auto& prev = chain.mips.back();
      chain.mips.emplace_back(DownsampleRgba8Box2x2(cur_w, cur_h, prev));
      cur_w = (std::max)(cur_w / 2U, 1U);
      cur_h = (std::max)(cur_h / 2U, 1U);
    }

    return chain;
  }

  //! Generate a magenta/black checkerboard pattern for an error texture.
  auto GenerateErrorTextureData(std::uint32_t width, std::uint32_t height,
    std::uint32_t tile_size_px) -> std::vector<std::uint32_t>
  {
    CHECK_F(width > 0 && height > 0, "Invalid error texture dimensions");
    CHECK_F(tile_size_px > 0, "Invalid error texture tile size");

    std::vector<std::uint32_t> pixels;
    pixels.resize(static_cast<size_t>(width) * static_cast<size_t>(height));

    // Packed RGBA8 in little-endian memory is 0xAABBGGRR. This value produces
    // R=255, G=0, B=255, A=255.
    constexpr std::uint32_t kMagenta = 0xFFFF00FFU;
    constexpr std::uint32_t kBlack = 0xFF000000U;

    for (std::uint32_t y = 0; y < height; ++y) {
      for (std::uint32_t x = 0; x < width; ++x) {
        const bool is_magenta
          = ((x / tile_size_px) + (y / tile_size_px)) % 2 == 0;
        pixels[static_cast<size_t>(y) * width + x]
          = is_magenta ? kMagenta : kBlack;
      }
    }

    return pixels;
  }

} // namespace

//=== TextureBinder Implementation ===========================================//

TextureBinder::TextureBinder(observer_ptr<Graphics> gfx,
  observer_ptr<engine::upload::UploadCoordinator> uploader,
  observer_ptr<engine::upload::StagingProvider> staging_provider,
  observer_ptr<content::AssetLoader> asset_loader)
  : gfx_(std::move(gfx))
  , uploader_(std::move(uploader))
  , staging_provider_(std::move(staging_provider))
  , asset_loader_(std::move(asset_loader))
{
  DCHECK_NOTNULL_F(gfx_, "Graphics cannot be null");
  DCHECK_NOTNULL_F(uploader_, "UploadCoordinator cannot be null");

  error_texture_ = CreateErrorTexture();
  CHECK_NOTNULL_F(error_texture_, "Failed to create error texture");

  auto& registry = gfx_->GetResourceRegistry();
  auto& allocator = gfx_->GetDescriptorAllocator();

  graphics::TextureViewDescription error_view_desc;
  error_view_desc.view_type = graphics::ResourceViewType::kTexture_SRV;
  error_view_desc.visibility = graphics::DescriptorVisibility::kShaderVisible;
  error_view_desc.format = Format::kRGBA8UNorm;
  error_view_desc.sub_resources = {
    .base_mip_level = 0,
    .num_mip_levels = 1,
    .base_array_slice = 0,
    .num_array_slices = 1,
  };

  auto error_handle
    = allocator.Allocate(graphics::ResourceViewType::kTexture_SRV,
      graphics::DescriptorVisibility::kShaderVisible);
  CHECK_F(
    error_handle.IsValid(), "Failed to allocate error texture descriptor");

  error_texture_index_
    = ShaderVisibleIndex(allocator.GetShaderVisibleIndex(error_handle).get());

  registry.Register(error_texture_);
  registry.RegisterView(
    *error_texture_, std::move(error_handle), error_view_desc);

  placeholder_texture_ = CreatePlaceholderTexture();
  if (!placeholder_texture_) {
    LOG_F(ERROR,
      "Failed to create placeholder texture; using error texture instead");
    placeholder_texture_ = error_texture_;
    placeholder_texture_index_ = error_texture_index_;
  } else {
    auto placeholder_handle
      = allocator.Allocate(graphics::ResourceViewType::kTexture_SRV,
        graphics::DescriptorVisibility::kShaderVisible);
    CHECK_F(placeholder_handle.IsValid(),
      "Failed to allocate placeholder texture descriptor");

    placeholder_texture_index_ = ShaderVisibleIndex(
      allocator.GetShaderVisibleIndex(placeholder_handle).get());

    registry.Register(placeholder_texture_);
    registry.RegisterView(
      *placeholder_texture_, std::move(placeholder_handle), error_view_desc);
  }

  LOG_F(INFO, "TextureBinder initialized with error texture at SRV index: {}",
    error_texture_index_.get());
  LOG_F(INFO,
    "TextureBinder initialized with placeholder texture at SRV index: {}",
    placeholder_texture_index_.get());
}

TextureBinder::~TextureBinder()
{
  LOG_SCOPE_F(INFO, "TextureBinder Statistics");
  LOG_F(INFO, "total requests : {}", total_requests_);
  LOG_F(INFO, "cache hits     : {}", cache_hits_);
  LOG_F(INFO, "load failures  : {}", load_failures_);
  LOG_F(INFO, "textures loaded: {}", texture_map_.size());
}

auto TextureBinder::OnFrameStart() -> void { }

auto TextureBinder::OnFrameEnd() -> void { }

auto TextureBinder::GetOrAllocate(data::pak::v1::ResourceIndexT resource_index)
  -> ShaderVisibleIndex
{
  ++total_requests_;

  if (resource_index == data::pak::v1::kFallbackResourceIndex) {
    return placeholder_texture_index_;
  }

  auto it = texture_map_.find(resource_index);
  if (it != texture_map_.end()) {
    ++cache_hits_;
    if (it->second.load_failed) {
      return error_texture_index_;
    }
    return it->second.srv_index;
  }

  TextureEntry entry;
  entry.texture = CreatePlaceholderTexture();
  if (!entry.texture) {
    LOG_F(ERROR,
      "Failed to create per-entry placeholder texture for resource index: {}",
      resource_index);
    ++load_failures_;
    entry.load_failed = true;
    entry.srv_index = error_texture_index_;
    texture_map_.emplace(resource_index, std::move(entry));
    return error_texture_index_;
  }

  auto& registry = gfx_->GetResourceRegistry();
  auto& allocator = gfx_->GetDescriptorAllocator();

  graphics::TextureViewDescription view_desc;
  view_desc.view_type = graphics::ResourceViewType::kTexture_SRV;
  view_desc.visibility = graphics::DescriptorVisibility::kShaderVisible;
  view_desc.format = Format::kRGBA8UNorm;
  view_desc.sub_resources = {
    .base_mip_level = 0,
    .num_mip_levels = 1,
    .base_array_slice = 0,
    .num_array_slices = 1,
  };

  auto handle = allocator.Allocate(graphics::ResourceViewType::kTexture_SRV,
    graphics::DescriptorVisibility::kShaderVisible);
  if (!handle.IsValid()) {
    LOG_F(ERROR, "Failed to allocate descriptor for resource index: {}",
      resource_index);
    ++load_failures_;
    return error_texture_index_;
  }

  entry.descriptor_index = handle.GetBindlessHandle();

  entry.srv_index
    = ShaderVisibleIndex(allocator.GetShaderVisibleIndex(handle).get());

  registry.Register(entry.texture);
  registry.RegisterView(*entry.texture, std::move(handle), view_desc);

  // Allow examples to force the error texture path without depending on the
  // (currently TODO) async loader. Using UINT32_MAX as a clearly-invalid
  // ResourceIndexT keeps the contract explicit and avoids colliding with the
  // valid fallback sentinel (0).
  if (resource_index == (std::numeric_limits<data::pak::v1::ResourceIndexT>::max)()) {
    const bool updated
      = registry.UpdateView(*error_texture_, entry.descriptor_index, view_desc);
    if (!updated) {
      LOG_F(ERROR,
        "Failed to update SRV view to error texture for forced-invalid "
        "resource index: {}",
        resource_index);
    }
    entry.texture = error_texture_;
    entry.is_placeholder = false;
    entry.load_failed = true;
    ++load_failures_;
  } else {
    InitiateAsyncLoad(resource_index, entry);
  }

  DLOG_F(3, "Allocated SRV index {} for resource index {}",
    entry.srv_index.get(), resource_index);

  const auto result_index = entry.srv_index;
  texture_map_.emplace(resource_index, std::move(entry));

  return result_index;
}

auto TextureBinder::GetErrorTextureIndex() const -> ShaderVisibleIndex
{
  return error_texture_index_;
}

auto TextureBinder::OverrideTexture2DRgba8(
  data::pak::v1::ResourceIndexT resource_index, const std::uint32_t width,
  const std::uint32_t height, const std::span<const std::byte> rgba8_bytes,
  const char* debug_name) -> bool
{
  if (resource_index == data::pak::v1::kFallbackResourceIndex) {
    LOG_F(WARNING,
      "Refusing to override fallback (0) texture resource index");
    return false;
  }
  if (resource_index
    == (std::numeric_limits<data::pak::v1::ResourceIndexT>::max)()) {
    LOG_F(WARNING,
      "Refusing to override the forced-error sentinel resource index");
    return false;
  }
  if (width == 0 || height == 0) {
    LOG_F(ERROR, "OverrideTexture2DRgba8: invalid dimensions {}x{}", width,
      height);
    return false;
  }
  if (rgba8_bytes.size() != static_cast<std::size_t>(width)
        * static_cast<std::size_t>(height) * 4U) {
    LOG_F(ERROR,
      "OverrideTexture2DRgba8: unexpected data size {} for {}x{} RGBA8",
      rgba8_bytes.size(), width, height);
    return false;
  }

  // Ensure an entry exists and we own a stable descriptor index.
  (void)GetOrAllocate(resource_index);
  auto it = texture_map_.find(resource_index);
  if (it == texture_map_.end()) {
    LOG_F(ERROR, "OverrideTexture2DRgba8: missing entry for {}", resource_index);
    return false;
  }
  auto& entry = it->second;
  if (entry.descriptor_index == kInvalidBindlessHeapIndex) {
    LOG_F(ERROR, "OverrideTexture2DRgba8: invalid descriptor index for {}",
      resource_index);
    return false;
  }

  graphics::TextureDesc desc;
  desc.texture_type = TextureType::kTexture2D;
  desc.format = Format::kRGBA8UNorm;
  desc.width = width;
  desc.height = height;
  desc.depth = 1;
  const auto mip_levels = ComputeMipLevels2D(width, height);
  desc.mip_levels = mip_levels;
  desc.array_size = 1;
  desc.is_shader_resource = true;
  desc.debug_name = debug_name ? debug_name : "TextureBinder.Override";

  std::shared_ptr<graphics::Texture> new_texture;
  try {
    new_texture = gfx_->CreateTexture(desc);
  } catch (const std::exception& e) {
    LOG_F(ERROR, "OverrideTexture2DRgba8: CreateTexture threw: {}", e.what());
    return false;
  }

  if (!new_texture) {
    LOG_F(ERROR, "OverrideTexture2DRgba8: CreateTexture returned null");
    return false;
  }

  const auto mip_chain = BuildRgba8MipChain(width, height, rgba8_bytes);

  std::vector<oxygen::engine::upload::UploadSubresource> dst_subresources;
  dst_subresources.reserve(mip_chain.mips.size());
  oxygen::engine::upload::UploadTextureSourceView src_view;
  src_view.subresources.reserve(mip_chain.mips.size());

  for (std::uint32_t mip = 0; mip < mip_chain.mips.size(); ++mip) {
    const auto mip_w = (std::max)(width >> mip, 1U);
    const auto mip_h = (std::max)(height >> mip, 1U);
    const auto bytes_per_row = mip_w * 4U;
    const auto slice_pitch = bytes_per_row * mip_h;

    dst_subresources.push_back(oxygen::engine::upload::UploadSubresource {
      .mip = mip,
      .array_slice = 0,
      .x = 0,
      .y = 0,
      .z = 0,
      .width = mip_w,
      .height = mip_h,
      .depth = 1,
    });
    src_view.subresources.push_back(
      oxygen::engine::upload::UploadTextureSourceSubresource {
        .bytes = std::span<const std::byte> { mip_chain.mips[mip] },
        .row_pitch = bytes_per_row,
        .slice_pitch = slice_pitch,
      });
  }

  oxygen::engine::upload::UploadRequest req {
    .kind = oxygen::engine::upload::UploadKind::kTexture2D,
    .debug_name = desc.debug_name,
    .desc = oxygen::engine::upload::UploadTextureDesc {
      .dst = new_texture,
      .width = desc.width,
      .height = desc.height,
      .depth = desc.depth,
      .format = desc.format,
    },
    .subresources = std::move(dst_subresources),
    .data = std::move(src_view),
  };

  const auto upload_result = uploader_->Submit(req, *staging_provider_);
  if (!upload_result) {
    const auto error_code
      = oxygen::engine::upload::make_error_code(upload_result.error());
    LOG_F(ERROR, "TextureBinder upload failed ({}): {}", desc.debug_name,
      error_code.message());
    return false;
  }

  auto& registry = gfx_->GetResourceRegistry();
  registry.Register(new_texture);

  graphics::TextureViewDescription view_desc;
  view_desc.view_type = graphics::ResourceViewType::kTexture_SRV;
  view_desc.visibility = graphics::DescriptorVisibility::kShaderVisible;
  view_desc.format = Format::kRGBA8UNorm;
  view_desc.sub_resources = {
    .base_mip_level = 0,
    .num_mip_levels = desc.mip_levels,
    .base_array_slice = 0,
    .num_array_slices = 1,
  };

  const bool updated
    = registry.UpdateView(*new_texture, entry.descriptor_index, view_desc);
  if (!updated) {
    LOG_F(ERROR,
      "OverrideTexture2DRgba8: failed to UpdateView for resource index {}",
      resource_index);
    return false;
  }

  entry.texture = std::move(new_texture);
  entry.is_placeholder = false;
  entry.load_failed = false;

  return true;
}

//=== Private Implementation =================================================//

/*!
 Creates a 1×1 white placeholder texture for immediate use while actual
 texture loads asynchronously.

 @return Placeholder texture, or nullptr on failure
*/
auto TextureBinder::CreatePlaceholderTexture()
  -> std::shared_ptr<graphics::Texture>
{
  graphics::TextureDesc desc;
  desc.texture_type = TextureType::kTexture2D;
  desc.format = Format::kRGBA8UNorm;
  desc.width = 1;
  desc.height = 1;
  desc.depth = 1;
  desc.mip_levels = 1;
  desc.array_size = 1;
  desc.is_shader_resource = true;
  desc.debug_name = "TexturePlaceholder";

  try {
    auto texture = gfx_->CreateTexture(desc);
    if (!texture) {
      LOG_F(ERROR, "CreateTexture returned null for placeholder");
      return nullptr;
    }

    const std::array<std::byte, sizeof(std::uint32_t)> white_pixel_data {
      std::byte(0xFF), std::byte(0xFF), std::byte(0xFF), std::byte(0xFF)};
    SubmitTextureData(texture, white_pixel_data, "TextureBinder.Placeholder");

    return texture;
  } catch (const std::exception& e) {
    LOG_F(ERROR, "Exception creating placeholder texture: {}", e.what());
    return nullptr;
  }
}

/*!
 Creates a high-contrast magenta/black checkerboard error-indicator texture.

 @return Error texture, or nullptr on failure
*/
auto TextureBinder::CreateErrorTexture() -> std::shared_ptr<graphics::Texture>
{
  graphics::TextureDesc desc;
  desc.texture_type = TextureType::kTexture2D;
  desc.format = Format::kRGBA8UNorm;
  desc.width = 256;
  desc.height = 256;
  desc.depth = 1;
  desc.mip_levels = 1;
  desc.array_size = 1;
  desc.is_shader_resource = true;
  desc.debug_name = "TextureError";

  try {
    auto texture = gfx_->CreateTexture(desc);
    if (!texture) {
      LOG_F(ERROR, "CreateTexture returned null for error texture");
      return nullptr;
    }

    constexpr std::uint32_t kTileSizePx = 32;
    const auto pixels
      = GenerateErrorTextureData(desc.width, desc.height, kTileSizePx);
    const std::span<const std::byte> pixel_bytes {
      reinterpret_cast<const std::byte*>(pixels.data()),
      pixels.size() * sizeof(std::uint32_t)
    };
    SubmitTextureData(texture, pixel_bytes, "TextureBinder.ErrorTexture");

    return texture;
  } catch (const std::exception& e) {
    LOG_F(ERROR, "Exception creating error texture: {}", e.what());
    return nullptr;
  }
}

/*!
 Initiates asynchronous loading of texture resource and schedules replacement
 of placeholder with final texture.

 @param resource_index Resource index to load
 @param entry Texture entry to update when load completes
*/
auto TextureBinder::InitiateAsyncLoad(
  data::pak::v1::ResourceIndexT resource_index,
  [[maybe_unused]] TextureEntry& entry) -> void
{
  DLOG_F(3, "Initiating async load for resource index: {}", resource_index);

  if (resource_index == data::pak::v1::kFallbackResourceIndex) {
    return;
  }

  if (!asset_loader_) {
    DLOG_F(4, "AssetLoader is unavailable; texture {} will keep placeholder",
      resource_index);
    return;
  }

  // TODO: Implement async loading via AssetLoader
  // For now, mark as placeholder and log
  LOG_F(WARNING,
    "Async texture loading not yet implemented for resource index: {}",
    resource_index);
}

auto TextureBinder::SubmitTextureData(
  const std::shared_ptr<graphics::Texture>& texture,
  const std::span<const std::byte> data, const char* debug_name) -> void
{
  if (!texture || !uploader_ || !staging_provider_ || data.empty()) {
    return;
  }

  const auto& desc = texture->GetDescriptor();
  const auto& format_info = oxygen::graphics::detail::GetFormatInfo(desc.format);
  if (format_info.block_size != 1U || format_info.bytes_per_block == 0U) {
    LOG_F(ERROR, "TextureBinder upload only supports non-BC formats");
    return;
  }

  if (desc.depth != 1U) {
    LOG_F(ERROR, "TextureBinder upload only supports Texture2D");
    return;
  }

  const auto bytes_per_row = static_cast<std::uint32_t>(desc.width)
    * static_cast<std::uint32_t>(format_info.bytes_per_block);
  const auto expected_bytes = static_cast<std::size_t>(bytes_per_row)
    * static_cast<std::size_t>(desc.height);
  if (data.size() != expected_bytes) {
    LOG_F(ERROR,
      "TextureBinder upload expected {} bytes for {}x{}, got {}", expected_bytes,
      desc.width, desc.height, data.size());
    return;
  }

  oxygen::engine::upload::UploadTextureSourceView src_view;
  src_view.subresources.push_back({
    .bytes = data,
    .row_pitch = bytes_per_row,
    .slice_pitch = bytes_per_row * desc.height,
  });

  oxygen::engine::upload::UploadRequest req {
    .kind = oxygen::engine::upload::UploadKind::kTexture2D,
    .debug_name = debug_name,
    .desc = oxygen::engine::upload::UploadTextureDesc {
      .dst = texture,
      .width = desc.width,
      .height = desc.height,
      .depth = desc.depth,
      .format = desc.format,
    },
    .subresources = {},
    .data = std::move(src_view),
  };

  const auto result = uploader_->Submit(req, *staging_provider_);
  if (!result) {
    const auto error_code = oxygen::engine::upload::make_error_code(result.error());
    LOG_F(ERROR, "TextureBinder upload failed ({}): {}", debug_name,
      error_code.message());
  }
}

auto TextureBinder::SetAssetLoader(
  observer_ptr<content::AssetLoader> asset_loader) -> void
{
  asset_loader_ = std::move(asset_loader);
  if (asset_loader_) {
    DLOG_F(2, "TextureBinder connected to AssetLoader service");
  }
}

} // namespace oxygen::renderer::resources
