//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Renderer/Resources/TextureBinder.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <limits>
#include <span>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Content/ResourceKey.h>
#include <Oxygen/Core/Bindless/Generated.Constants.h>
#include <Oxygen/Core/Detail/FormatUtils.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/TextureResource.h>
#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/Detail/DeferredReclaimer.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Renderer/Upload/Errors.h>
#include <Oxygen/Renderer/Upload/UploadCoordinator.h>

namespace oxygen::renderer::resources {

namespace {

  struct UploadLayout {
    std::vector<oxygen::engine::upload::UploadSubresource> dst_subresources;
    oxygen::engine::upload::UploadTextureSourceView src_view;
    std::size_t trailing_bytes { 0U };
  };

  struct UploadLayoutFailure {
    enum class Reason {
      kDataTooSmall,
      kMipAlignmentOverflow,
    };

    Reason reason { Reason::kDataTooSmall };
    std::uint32_t mip { 0U };
    std::uint32_t layer { 0U };
    std::size_t offset { 0U };
    std::size_t required_bytes { 0U };
    std::size_t total_bytes { 0U };
  };

  [[nodiscard]] auto AlignUpSize(
    const std::size_t value, const std::size_t alignment) -> std::size_t
  {
    if (alignment == 0U) {
      return value;
    }
    const auto mask = alignment - 1U;
    return (value + mask) & ~mask;
  }

  [[nodiscard]] auto BuildTexture2DUploadLayout(
    const graphics::TextureDesc& desc,
    const oxygen::graphics::detail::FormatInfo& format_info,
    const std::span<const std::byte> data_bytes,
    const std::size_t row_pitch_alignment,
    const std::size_t mip_placement_alignment)
    -> std::variant<UploadLayout, UploadLayoutFailure>
  {
    UploadLayout layout;
    const std::uint32_t mip_count = desc.mip_levels;
    const std::uint32_t array_layers = desc.array_size;

    layout.dst_subresources.reserve(
      static_cast<std::size_t>(mip_count) * array_layers);
    layout.src_view.subresources.reserve(
      static_cast<std::size_t>(mip_count) * array_layers);

    std::size_t offset = 0U;
    const std::size_t total_data_size = data_bytes.size();

    for (std::uint32_t mip = 0; mip < mip_count; ++mip) {
      const auto mip_w = (std::max)(desc.width >> mip, 1U);
      const auto mip_h = (std::max)(desc.height >> mip, 1U);
      const auto bytes_per_row = mip_w * format_info.bytes_per_block;
      const auto row_pitch = AlignUpSize(bytes_per_row, row_pitch_alignment);
      const auto slice_pitch = row_pitch * mip_h;

      for (std::uint32_t layer = 0; layer < array_layers; ++layer) {
        if (offset + slice_pitch > total_data_size) {
          return UploadLayoutFailure {
            .reason = UploadLayoutFailure::Reason::kDataTooSmall,
            .mip = mip,
            .layer = layer,
            .offset = offset,
            .required_bytes = slice_pitch,
            .total_bytes = total_data_size,
          };
        }

        layout.dst_subresources.push_back(
          oxygen::engine::upload::UploadSubresource {
            .mip = mip,
            .array_slice = layer,
            .x = 0,
            .y = 0,
            .z = 0,
            .width = mip_w,
            .height = mip_h,
            .depth = 1,
          });

        layout.src_view.subresources.push_back(
          oxygen::engine::upload::UploadTextureSourceSubresource {
            .bytes = std::span<const std::byte>(
              data_bytes.data() + offset, slice_pitch),
            .row_pitch = static_cast<std::uint32_t>(row_pitch),
            .slice_pitch = static_cast<std::uint32_t>(slice_pitch),
          });

        offset += slice_pitch;
      }

      if (mip + 1U < mip_count) {
        const auto aligned_offset
          = AlignUpSize(offset, mip_placement_alignment);
        if (aligned_offset > total_data_size) {
          return UploadLayoutFailure {
            .reason = UploadLayoutFailure::Reason::kMipAlignmentOverflow,
            .mip = mip,
            .layer = 0U,
            .offset = offset,
            .required_bytes = aligned_offset - offset,
            .total_bytes = total_data_size,
          };
        }
        offset = aligned_offset;
      }
    }

    layout.trailing_bytes
      = (offset <= total_data_size) ? (total_data_size - offset) : 0U;
    return layout;
  }

  struct PreparedTexture2DUpload {
    graphics::TextureDesc desc;
    std::shared_ptr<graphics::Texture> new_texture;
    UploadLayout layout;
  };

  struct PrepareTexture2DUploadFailure {
    enum class Reason {
      kUnsupportedFormat,
      kUnsupportedDepth,
      kBadDataAlignment,
      kCreateTextureException,
      kCreateTextureReturnedNull,
      kLayoutFailure,
    };

    Reason reason { Reason::kCreateTextureReturnedNull };
    std::uint32_t expected_alignment { 0U };
    std::uint32_t actual_alignment { 0U };
    std::optional<UploadLayoutFailure> layout_failure;
  };

  [[nodiscard]] auto PrepareTexture2DUpload(
    Graphics& gfx, const data::TextureResource& tex_res)
    -> std::variant<PreparedTexture2DUpload, PrepareTexture2DUploadFailure>
  {
    // Build GPU texture description.
    graphics::TextureDesc desc;
    desc.texture_type = TextureType::kTexture2D;
    desc.format = tex_res.GetFormat();
    desc.width = tex_res.GetWidth();
    desc.height = tex_res.GetHeight();
    desc.depth = tex_res.GetDepth();
    desc.mip_levels = tex_res.GetMipCount();
    desc.array_size = tex_res.GetArrayLayers();
    desc.is_shader_resource = true;
    desc.debug_name = "TextureBinder.Loaded";

    constexpr std::size_t kRowPitchAlignment = 256U;
    constexpr std::size_t kMipPlacementAlignment = 512U;

    const auto& format_info
      = oxygen::graphics::detail::GetFormatInfo(desc.format);
    if (format_info.block_size != 1U || format_info.bytes_per_block == 0U) {
      return PrepareTexture2DUploadFailure {
        .reason = PrepareTexture2DUploadFailure::Reason::kUnsupportedFormat,
      };
    }

    if (desc.depth != 1U) {
      return PrepareTexture2DUploadFailure {
        .reason = PrepareTexture2DUploadFailure::Reason::kUnsupportedDepth,
      };
    }

    if (tex_res.GetDataAlignment() != kRowPitchAlignment) {
      return PrepareTexture2DUploadFailure {
        .reason = PrepareTexture2DUploadFailure::Reason::kBadDataAlignment,
        .expected_alignment = static_cast<std::uint32_t>(kRowPitchAlignment),
        .actual_alignment = tex_res.GetDataAlignment(),
      };
    }

    std::shared_ptr<graphics::Texture> new_texture;
    try {
      new_texture = gfx.CreateTexture(desc);
    } catch (const std::exception&) {
      return PrepareTexture2DUploadFailure {
        .reason
        = PrepareTexture2DUploadFailure::Reason::kCreateTextureException,
      };
    }

    if (!new_texture) {
      return PrepareTexture2DUploadFailure {
        .reason
        = PrepareTexture2DUploadFailure::Reason::kCreateTextureReturnedNull,
      };
    }

    const auto& data_span = tex_res.GetData();
    const auto data_bytes = std::span<const std::byte>(
      reinterpret_cast<const std::byte*>(data_span.data()), data_span.size());

    const auto layout_result = BuildTexture2DUploadLayout(desc, format_info,
      data_bytes, kRowPitchAlignment, kMipPlacementAlignment);
    if (std::holds_alternative<UploadLayoutFailure>(layout_result)) {
      return PrepareTexture2DUploadFailure {
        .reason = PrepareTexture2DUploadFailure::Reason::kLayoutFailure,
        .layout_failure = std::get<UploadLayoutFailure>(layout_result),
      };
    }

    return PreparedTexture2DUpload {
      .desc = desc,
      .new_texture = std::move(new_texture),
      .layout = std::get<UploadLayout>(layout_result),
    };
  }

  struct MipRange {
    std::uint32_t base_mip_level { 0U };
    std::uint32_t num_mip_levels { 1U };
  };

  struct ArrayRange {
    std::uint32_t base_array_slice { 0U };
    std::uint32_t num_array_slices { 1U };
  };

  [[nodiscard]] auto MakeTextureSrvViewDesc(
    const Format format, const MipRange mips, const ArrayRange slices)
    -> graphics::TextureViewDescription
  {
    graphics::TextureViewDescription view_desc;
    view_desc.view_type = graphics::ResourceViewType::kTexture_SRV;
    view_desc.visibility = graphics::DescriptorVisibility::kShaderVisible;
    view_desc.format = format;
    view_desc.sub_resources = {
      .base_mip_level = mips.base_mip_level,
      .num_mip_levels = mips.num_mip_levels,
      .base_array_slice = slices.base_array_slice,
      .num_array_slices = slices.num_array_slices,
    };
    return view_desc;
  }

  [[nodiscard]] auto IsFallbackSentinel(const content::ResourceKey key) -> bool
  {
    return key == static_cast<content::ResourceKey>(0);
  }

  auto ReleaseTextureNextFrame(graphics::ResourceRegistry& registry,
    graphics::detail::DeferredReclaimer& reclaimer,
    std::shared_ptr<graphics::Texture>&& texture) -> void
  {
    if (!texture) {
      return;
    }
    registry.UnRegisterResource(*texture);
    reclaimer.RegisterDeferredRelease(std::move(texture));
  }

  //! Generate a magenta/black checkerboard pattern for an error texture.
  auto GenerateErrorTextureData(std::uint32_t width, std::uint32_t height,
    std::uint32_t tile_size_px) -> std::vector<std::uint32_t>
  {
    CHECK_F(width > 0 && height > 0, "Invalid error texture dimensions");
    CHECK_F(tile_size_px > 0, "Invalid error texture tile size");

    std::vector<std::uint32_t> pixels;
    pixels.resize(
      static_cast<std::size_t>(width) * static_cast<std::size_t>(height));

    // Packed RGBA8 in little-endian memory is 0xAABBGGRR. This value produces
    // R=255, G=0, B=255, A=255.
    constexpr std::uint32_t kMagenta = 0xFFFF00FFU;
    constexpr std::uint32_t kBlack = 0xFF000000U;

    for (std::uint32_t y = 0; y < height; ++y) {
      for (std::uint32_t x = 0; x < width; ++x) {
        const bool is_magenta
          = ((x / tile_size_px) + (y / tile_size_px)) % 2 == 0;
        pixels[static_cast<std::size_t>(y) * width + x]
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
  CHECK_NOTNULL_F(asset_loader_, "AssetLoader cannot be null");

  error_texture_ = CreateErrorTexture();
  CHECK_NOTNULL_F(error_texture_, "Failed to create error texture");

  auto& registry = gfx_->GetResourceRegistry();
  auto& allocator = gfx_->GetDescriptorAllocator();

  const auto error_view_desc
    = MakeTextureSrvViewDesc(Format::kRGBA8UNorm, {}, {});

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
    error_texture_index_);
  LOG_F(INFO,
    "TextureBinder initialized with placeholder texture at SRV index: {}",
    placeholder_texture_index_);
}

TextureBinder::~TextureBinder()
{
  LOG_SCOPE_F(INFO, "TextureBinder Statistics");
  LOG_F(INFO, "total requests : {}", total_requests_);
  LOG_F(INFO, "cache hits     : {}", cache_hits_);
  LOG_F(INFO, "load failures  : {}", load_failures_);
  LOG_F(INFO, "textures loaded: {}", texture_map_.size());
}

auto TextureBinder::OnFrameStart() -> void
{
  DCHECK_NOTNULL_F(gfx_, "Graphics cannot be null");
  DLOG_SCOPE_F(5, "TextureBinder OnFrameStart");
  DLOG_F(6, "entries: {}", texture_map_.size());
  // Drain completed upload tickets and perform SRV repointing on the render
  // thread. This keeps descriptor updates serialized with other render-thread
  // mutations and relies on UploadCoordinator as the authoritative source of
  // upload completion.
  if (!uploader_) {
    return;
  }

  auto& registry = gfx_->GetResourceRegistry();
  auto& reclaimer = gfx_->GetDeferredReclaimer();

  for (auto& [resource_key, entry] : texture_map_) {
    if (!entry.pending_ticket.has_value()) {
      continue;
    }

    const auto ticket = *entry.pending_ticket;
    const auto maybe_result = uploader_->TryGetResult(ticket);
    if (!maybe_result.has_value()) {
      // Not completed yet
      continue;
    }

    DLOG_SCOPE_F(4, "Upload completion");
    DLOG_F(4, "resource: {}", resource_key);
    DLOG_F(4, "ticket: {}", ticket.id);
    DLOG_F(4, "descriptor_index: {}", entry.descriptor_index);
    DLOG_F(4, "is_placeholder: {}", entry.is_placeholder);
    DLOG_F(4, "load_failed: {}", entry.load_failed);

    LOG_F(INFO, "Upload ticket {} completed for resource key {}", ticket.id,
      resource_key);

    const auto result = *maybe_result;
    DLOG_F(4, "result.success: {}", result.success);
    if (!result.success) {
      // Upload failure: keep the placeholder bound.
      //
      // UploadTracker can report failures for immediate/producer failures or
      // explicit cancellation. In these cases we avoid repointing the
      // descriptor to the error texture and keep the placeholder active.
      LOG_F(WARNING,
        "Texture upload failed for resource entry (ticket={}): keeping "
        "placeholder",
        ticket.id);

      entry.load_failed = true;
      entry.is_placeholder = true;

      // Drop the newly-created destination texture (if any) and keep the
      // original placeholder texture active.
      if (entry.texture && entry.placeholder_texture
        && entry.texture != entry.placeholder_texture) {
        DLOG_F(4, "dropping newly created texture and restoring placeholder");
        ReleaseTextureNextFrame(registry, reclaimer, std::move(entry.texture));
        entry.texture = entry.placeholder_texture;
      }
    } else {
      // Successful upload: repoint the descriptor to the final texture
      entry.is_placeholder = false;
      entry.load_failed = false;

      if (entry.descriptor_index == kInvalidBindlessHeapIndex
        || !entry.pending_view_desc.has_value()) {
        entry.pending_ticket.reset();
        entry.pending_view_desc.reset();
        continue;
      }

      const bool updated = registry.UpdateView(
        *entry.texture, entry.descriptor_index, *entry.pending_view_desc);
      if (!updated) {
        LOG_F(ERROR,
          "Failed to update SRV view after upload completion (ticket={})",
          ticket.id);
        entry.pending_ticket.reset();
        entry.pending_view_desc.reset();
        continue;
      }

      LOG_F(INFO,
        "Repointed descriptor {} to final texture for resource {} (ticket={})",
        entry.descriptor_index, resource_key, ticket.id);

      if (entry.placeholder_texture
        && entry.placeholder_texture != entry.texture
        && entry.placeholder_texture != placeholder_texture_
        && entry.placeholder_texture != error_texture_) {
        DLOG_F(4, "releasing entry placeholder texture");
        ReleaseTextureNextFrame(
          registry, reclaimer, std::move(entry.placeholder_texture));
      }
    }

    // Clear pending ticket and view desc after handling
    entry.pending_ticket.reset();
    entry.pending_view_desc.reset();
  }
}

/*!
 TextureBinder frame-end hook.

 `OnFrameEnd()` is intentionally a no-op.

 TextureBinder drains upload completions and repoints descriptors during
 `OnFrameStart()`. Any GPU-safe destruction is handled by the graphics
 backend's `DeferredReclaimer` on `Graphics::BeginFrame()` when the frame slot
 cycles.
*/
auto TextureBinder::OnFrameEnd() -> void { }

// Index-based allocation has been removed. Use the ResourceKey-only API.

auto TextureBinder::GetOrAllocate(content::ResourceKey resource_key)
  -> ShaderVisibleIndex
{
  DCHECK_NOTNULL_F(gfx_, "Graphics cannot be null");
  ++total_requests_;

  // ResourceKey(0) is treated as a renderer-side fallback sentinel.
  // Never pass it to the AssetLoader (which expects valid, type-encoded keys).
  if (IsFallbackSentinel(resource_key)) {
    // This is an extremely hot path in typical renderer usage.
    // Keep the trace available, but only at very high verbosity.
    DLOG_F(6, "TextureBinder GetOrAllocate: fallback sentinel -> placeholder");
    return placeholder_texture_index_;
  }

  auto it = texture_map_.find(resource_key);
  if (it != texture_map_.end()) {
    ++cache_hits_;
    // Cache hits can be extremely frequent (per-frame, per-material).
    DLOG_F(6, "TextureBinder GetOrAllocate: cache hit -> srv_index {}",
      it->second.srv_index);
    // Preserve per-resource stable indices. On failure, the descriptor is
    // repointed to the error texture, but the shader-visible handle remains
    // the entry's SRV index.
    return it->second.srv_index;
  }

  DLOG_SCOPE_F(4, "TextureBinder GetOrAllocate (allocate)");
  DLOG_F(4, "resource: {}", resource_key);

  TextureEntry entry;
  entry.texture = CreatePlaceholderTexture();
  if (!entry.texture) {
    LOG_F(ERROR,
      "Failed to create per-entry placeholder texture for resource key: {}",
      resource_key);
    ++load_failures_;
    entry.load_failed = true;
    entry.srv_index = error_texture_index_;
    texture_map_.emplace(resource_key, std::move(entry));
    DLOG_F(3, "allocated: per-entry placeholder failed -> error texture");
    return error_texture_index_;
  }

  entry.placeholder_texture = entry.texture;

  // Store the opaque key so we can hand it to AssetLoader when starting
  // the async load. ResourceKey remains opaque outside of this TU.
  entry.resource_key = resource_key;

  auto& registry = gfx_->GetResourceRegistry();
  auto& allocator = gfx_->GetDescriptorAllocator();

  const auto view_desc = MakeTextureSrvViewDesc(Format::kRGBA8UNorm, {}, {});

  auto handle = allocator.Allocate(graphics::ResourceViewType::kTexture_SRV,
    graphics::DescriptorVisibility::kShaderVisible);
  if (!handle.IsValid()) {
    LOG_F(ERROR, "Failed to allocate descriptor for resource key: {}",
      resource_key);
    ++load_failures_;
    DLOG_F(3, "allocated: descriptor allocation failed -> error texture");
    return error_texture_index_;
  }

  entry.descriptor_index = handle.GetBindlessHandle();
  DLOG_F(4, "descriptor_index: {}", entry.descriptor_index);

  entry.srv_index
    = ShaderVisibleIndex(allocator.GetShaderVisibleIndex(handle).get());

  registry.Register(entry.texture);
  registry.RegisterView(*entry.texture, std::move(handle), view_desc);

  // Insert before initiating the load to ensure completion callbacks can
  // always resolve the entry even if the load completes synchronously.
  const auto result_index = entry.srv_index;
  auto [insert_it, inserted]
    = texture_map_.emplace(resource_key, std::move(entry));
  DCHECK_F(inserted);

  // Initiate async load using the opaque ResourceKey.
  InitiateAsyncLoad(resource_key, insert_it->second);

  LOG_F(INFO, "Allocated SRV index {} for resource key {}", result_index,
    resource_key);

  DLOG_F(4, "srv_index: {}", result_index);

  return result_index;
}

auto TextureBinder::GetErrorTextureIndex() const -> ShaderVisibleIndex
{
  return error_texture_index_;
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
  DCHECK_NOTNULL_F(gfx_, "Graphics cannot be null");
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
      std::byte(0xFF), std::byte(0xFF), std::byte(0xFF), std::byte(0xFF)
    };
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
  DCHECK_NOTNULL_F(gfx_, "Graphics cannot be null");
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

 @param resource_key Opaque ResourceKey identifying the resource to load
 @param entry Texture entry to update when load completes
*/
auto TextureBinder::InitiateAsyncLoad(content::ResourceKey resource_key,
  [[maybe_unused]] TextureEntry& entry) -> void
{
  DCHECK_NOTNULL_F(gfx_, "Graphics cannot be null");
  DLOG_SCOPE_F(3, "TextureBinder InitiateAsyncLoad");
  DLOG_F(3, "resource: {}", resource_key);
  LOG_F(INFO, "Initiating async load for resource key: {}", resource_key);

  if (!asset_loader_) {
    LOG_F(INFO, "AssetLoader is unavailable; texture {} will keep placeholder",
      resource_key);
    return;
  }

  // Start only if the caller provided an opaque ResourceKey in the entry
  // (ResourceKey-aware callers route through the ResourceKey overload).
  if (!entry.resource_key.has_value()) {
    LOG_F(INFO, "No opaque ResourceKey available for {}; keeping placeholder",
      resource_key);
    return;
  }

  asset_loader_->StartLoadTexture(*entry.resource_key,
    [this, resource_key](std::shared_ptr<data::TextureResource> tex_res) {
      this->OnTextureResourceLoaded(resource_key, std::move(tex_res));
    });
}

/*!
 Handle completion of an async texture load request.

 This method is invoked on the engine/render thread by `AssetLoader` and is
 allowed to mutate graphics resources and `texture_map_`.

 Preconditions are enforced via `DCHECK_*`:

 - `gfx_` must be valid
 - `uploader_` and `staging_provider_` must be available for upload

 Postconditions:

 - On `tex_res == nullptr`, the entry transitions to the error texture
   (and attempts to repoint the descriptor immediately if one exists).
 - On success, an upload is submitted and the entry is placed in
   "upload pending" state by setting `pending_ticket` and `pending_view_desc`.

 @param resource_key Opaque key for the entry being updated.
 @param tex_res Loaded texture resource, or `nullptr` on load failure.
*/
auto TextureBinder::OnTextureResourceLoaded(content::ResourceKey resource_key,
  std::shared_ptr<data::TextureResource> tex_res) -> void
{
  DCHECK_NOTNULL_F(gfx_, "Graphics cannot be null");
  DCHECK_NOTNULL_F(uploader_, "UploadCoordinator cannot be null");
  DCHECK_NOTNULL_F(staging_provider_, "StagingProvider cannot be null");

  TextureEntry* entry_ptr = this->FindEntryOrLog(resource_key);
  if (!entry_ptr) {
    return;
  }

  DLOG_SCOPE_F(2, "TextureBinder texture load");
  DLOG_F(2, "resource: {}", resource_key);

  auto& entry = *entry_ptr;
  if (!tex_res) {
    DLOG_F(2, "result: tex_res is null");
    LOG_F(WARNING, "Async texture load returned null for resource {}",
      resource_key);
    this->HandleLoadFailure(
      resource_key, entry, FailurePolicy::kBindErrorTexture, nullptr);
    return;
  }

  DLOG_F(2, "format: {}", tex_res->GetFormat());
  DLOG_F(2, "size: {}x{}x{}", tex_res->GetWidth(), tex_res->GetHeight(),
    tex_res->GetDepth());
  DLOG_F(2, "mips: {}", tex_res->GetMipCount());
  DLOG_F(2, "layers: {}", tex_res->GetArrayLayers());
  DLOG_F(2, "data_alignment: {}", tex_res->GetDataAlignment());
  DLOG_F(2, "data_bytes: {}", tex_res->GetData().size());

  const auto prepared_result = PrepareTexture2DUpload(*gfx_, *tex_res);
  if (std::holds_alternative<PrepareTexture2DUploadFailure>(prepared_result)) {
    const auto failure
      = std::get<PrepareTexture2DUploadFailure>(prepared_result);
    switch (failure.reason) {
    case PrepareTexture2DUploadFailure::Reason::kUnsupportedFormat:
      LOG_F(ERROR, "TextureBinder upload only supports non-BC formats");
      break;
    case PrepareTexture2DUploadFailure::Reason::kUnsupportedDepth:
      LOG_F(ERROR, "TextureBinder async upload only supports 2D textures");
      break;
    case PrepareTexture2DUploadFailure::Reason::kBadDataAlignment:
      LOG_F(ERROR,
        "TextureBinder expects cooked texture data alignment {} bytes; got {}",
        failure.expected_alignment, failure.actual_alignment);
      break;
    case PrepareTexture2DUploadFailure::Reason::kCreateTextureException:
      LOG_F(ERROR, "CreateTexture threw during async load");
      break;
    case PrepareTexture2DUploadFailure::Reason::kCreateTextureReturnedNull:
      LOG_F(ERROR, "CreateTexture returned null during async load");
      break;
    case PrepareTexture2DUploadFailure::Reason::kLayoutFailure: {
      DCHECK_F(failure.layout_failure.has_value());
      const auto lf = *failure.layout_failure;

      DLOG_F(2, "failure: upload layout");

      switch (lf.reason) {
      case UploadLayoutFailure::Reason::kDataTooSmall:
        LOG_F(ERROR,
          "TextureResource data too small for expected mip/array layout");
        break;
      case UploadLayoutFailure::Reason::kMipAlignmentOverflow:
        LOG_F(
          ERROR, "TextureResource data too small after mip alignment padding");
        break;
      }
      break;
    }
    }

    this->HandleLoadFailure(
      resource_key, entry, FailurePolicy::kBindErrorTexture, nullptr);
    return;
  }

  auto prepared = std::get<PreparedTexture2DUpload>(prepared_result);
  this->SubmitTextureUpload(resource_key, entry, prepared.desc,
    std::move(prepared.new_texture),
    std::move(prepared.layout.dst_subresources),
    std::move(prepared.layout.src_view), prepared.layout.trailing_bytes);
}

auto TextureBinder::FindEntryOrLog(content::ResourceKey resource_key)
  -> TextureEntry*
{
  auto it = texture_map_.find(resource_key);
  if (it == texture_map_.end()) {
    LOG_F(
      WARNING, "Async load completed but entry missing for {}", resource_key);
    return nullptr;
  }
  return &it->second;
}

auto TextureBinder::SubmitTextureUpload(content::ResourceKey resource_key,
  TextureEntry& entry, const graphics::TextureDesc& desc,
  std::shared_ptr<graphics::Texture>&& new_texture,
  std::vector<oxygen::engine::upload::UploadSubresource>&& dst_subresources,
  oxygen::engine::upload::UploadTextureSourceView&& src_view,
  const std::size_t trailing_bytes) -> void
{
  DCHECK_NOTNULL_F(gfx_, "Graphics cannot be null");
  DCHECK_NOTNULL_F(uploader_, "UploadCoordinator cannot be null");
  DCHECK_NOTNULL_F(staging_provider_, "StagingProvider cannot be null");

  DLOG_SCOPE_F(3, "TextureBinder SubmitTextureUpload");
  DLOG_F(3, "resource: {}", resource_key);
  DLOG_F(3, "debug_name: {}", desc.debug_name);
  DLOG_F(3, "format: {}", desc.format);
  DLOG_F(3, "size: {}x{}x{}", desc.width, desc.height, desc.depth);
  DLOG_F(3, "mips: {}", desc.mip_levels);
  DLOG_F(3, "layers: {}", desc.array_size);
  DLOG_F(3, "subresources: {}", dst_subresources.size());
  DLOG_F(3, "trailing_bytes: {}", trailing_bytes);

  if (trailing_bytes != 0U) {
    LOG_F(INFO, "TextureResource had {} trailing bytes after planned upload",
      trailing_bytes);
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

    // Upload submission failure: keep the placeholder bound.
    this->HandleLoadFailure(resource_key, entry,
      FailurePolicy::kKeepPlaceholderBound, std::move(new_texture));
    return;
  }

  DLOG_F(3, "ticket: {}", upload_result->id);

  // Register the created texture so the ResourceRegistry can manage it and
  // allow us to UpdateView later when upload completes.
  auto& registry = gfx_->GetResourceRegistry();
  registry.Register(new_texture);

  const auto view_desc = MakeTextureSrvViewDesc(desc.format,
    MipRange { .base_mip_level = 0U, .num_mip_levels = desc.mip_levels },
    ArrayRange { .base_array_slice = 0U, .num_array_slices = desc.array_size });

  // Store pending ticket + view desc for OnFrameStart() to observe; also
  // set the entry.texture now so UpdateView can target it when complete.
  entry.pending_ticket = *upload_result;
  entry.pending_view_desc = view_desc;
  entry.texture = std::move(new_texture);
  entry.is_placeholder = true;
  entry.load_failed = false;
  ++total_requests_; // count this as an async load request for metrics

  LOG_F(INFO, "InitiateAsyncLoad: submitted upload ticket {} for resource {}",
    entry.pending_ticket->id, resource_key);
}

/*!
 Apply a load or upload failure policy to an entry.

 This centralizes the two distinct failure policies currently in use:

 - `FailurePolicy::kBindErrorTexture`: set the entry's texture to the shared
   error texture and repoint the descriptor (if present).
 - `FailurePolicy::kKeepPlaceholderBound`: keep the placeholder active; used
   for cases where upload submission failed and no GPU work was recorded.

 @param resource_key Opaque key for the failing entry.
 @param entry Entry to update.
 @param policy Failure policy to apply.
 @param texture_to_release Optional newly-created texture that should be
   released via deferred reclamation.
*/
auto TextureBinder::HandleLoadFailure(content::ResourceKey resource_key,
  TextureEntry& entry, const FailurePolicy policy,
  std::shared_ptr<graphics::Texture>&& texture_to_release) -> void
{
  DCHECK_NOTNULL_F(gfx_, "Graphics cannot be null");

  DLOG_SCOPE_F(3, "TextureBinder HandleLoadFailure");
  DLOG_F(3, "resource: {}", resource_key);
  DLOG_F(3, "policy: {}",
    (policy == FailurePolicy::kBindErrorTexture) ? "BindErrorTexture"
                                                 : "KeepPlaceholderBound");
  DLOG_F(3, "descriptor_index: {}", entry.descriptor_index);
  DLOG_F(3, "is_placeholder: {}", entry.is_placeholder);
  DLOG_F(3, "load_failed: {}", entry.load_failed);
  DLOG_F(3, "releasing_new_texture: {}", static_cast<bool>(texture_to_release));

  ++load_failures_;
  entry.load_failed = true;

  if (texture_to_release) {
    gfx_->GetDeferredReclaimer().RegisterDeferredRelease(
      std::move(texture_to_release));
  }

  if (policy == FailurePolicy::kKeepPlaceholderBound) {
    entry.is_placeholder = true;
    return;
  }

  entry.is_placeholder = false;
  entry.texture = error_texture_;

  // If we already own a descriptor index, repoint it immediately to the error
  // texture so the shader sees the error indicator without requiring further
  // UI interaction.
  if (entry.descriptor_index == kInvalidBindlessHeapIndex) {
    return;
  }

  if (!this->TryRepointEntryToErrorTexture(resource_key, entry)) {
    return;
  }

  this->ReleaseEntryPlaceholderIfOwned(entry);
}

/*!
 Attempt to repoint an existing bindless SRV descriptor to the error texture.

 @param resource_key Opaque key for logging/context.
 @param entry Entry containing the descriptor index to update.
 @return `true` if the view was updated, otherwise `false`.
*/
auto TextureBinder::TryRepointEntryToErrorTexture(
  content::ResourceKey resource_key, TextureEntry& entry) -> bool
{
  DCHECK_NOTNULL_F(gfx_, "Graphics cannot be null");
  CHECK_NOTNULL_F(error_texture_, "Error texture must be initialized");

  DLOG_SCOPE_F(3, "TextureBinder RepointEntryToErrorTexture");
  DLOG_F(3, "resource: {}", resource_key);
  DLOG_F(3, "descriptor_index: {}", entry.descriptor_index);

  auto& registry = gfx_->GetResourceRegistry();
  const auto view_desc = MakeTextureSrvViewDesc(Format::kRGBA8UNorm, {}, {});
  const bool updated
    = registry.UpdateView(*error_texture_, entry.descriptor_index, view_desc);
  if (!updated) {
    LOG_F(ERROR, "Failed to repoint descriptor to error texture for {}",
      resource_key);
    return false;
  }

  LOG_F(INFO, "Repointed descriptor {} to error texture for resource {}",
    entry.descriptor_index, resource_key);
  return true;
}

/*!
 Release a per-entry placeholder texture if it is owned by the entry.

 The global placeholder texture and the shared error texture are never
 released here.

 @param entry Entry whose placeholder may be released.
*/
auto TextureBinder::ReleaseEntryPlaceholderIfOwned(TextureEntry& entry) -> void
{
  if (!entry.placeholder_texture) {
    return;
  }
  if (entry.placeholder_texture == entry.texture) {
    return;
  }
  if (entry.placeholder_texture == placeholder_texture_) {
    return;
  }
  if (entry.placeholder_texture == error_texture_) {
    return;
  }

  auto& registry = gfx_->GetResourceRegistry();
  auto& reclaimer = gfx_->GetDeferredReclaimer();
  ReleaseTextureNextFrame(
    registry, reclaimer, std::move(entry.placeholder_texture));
}

auto TextureBinder::SubmitTextureData(
  const std::shared_ptr<graphics::Texture>& texture,
  const std::span<const std::byte> data, const char* debug_name) -> void
{
  if (!texture || !uploader_ || !staging_provider_ || data.empty()) {
    return;
  }

  const auto& desc = texture->GetDescriptor();
  const auto& format_info
    = oxygen::graphics::detail::GetFormatInfo(desc.format);
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
    LOG_F(ERROR, "TextureBinder upload expected {} bytes for {}x{}, got {}",
      expected_bytes, desc.width, desc.height, data.size());
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
    const auto error_code
      = oxygen::engine::upload::make_error_code(result.error());
    LOG_F(ERROR, "TextureBinder upload failed ({}): {}", debug_name,
      error_code.message());
  }
}

} // namespace oxygen::renderer::resources
